#include "kestrel_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "app_cfg.h"
#include "common.h"
#include "low_power_mode.h"
#include "mem_diag.h"
#include "ota_mode.h"
#include "wifi.h"

#define TAG "Kestrel"
#define NVS_NAMESPACE "KESTREL"

// Timeouts, ms
#define KESTREL_SYNC_TIMEOUT_MS         5000
#define KESTREL_CONNECT_TIMEOUT_MS      60000   // long enough to pick the HUD on the Kestrel the first time
#define KESTREL_SUBSCRIBE_TIMEOUT_MS    10000
#define KESTREL_OPEN_TIMEOUT_MS         10000
#define KESTREL_REPLY_TIMEOUT_MS        8000
#define KESTREL_TARGET_TIMEOUT_MS       10000
#define KESTREL_INDICATE_TIMEOUT_MS     2000
#define KESTREL_DISCONNECT_TIMEOUT_MS   2000
#define KESTREL_PHONE_CONNECT_TIMEOUT_MS 15000  // phone mode: the Kestrel answers at once when it advertises
#define KESTREL_SEARCH_MS               8000
#define KESTREL_GATTC_TIMEOUT_MS        3000

#define KESTREL_CHUNK 20                // ATT MTU 23: the Kestrel does not exchange the MTU (ICD H-4)
#define KESTREL_RX_STREAM_BYTES 2048

HEAPS_CAPS_ATTR kestrel_settings_t kestrel_settings;
static const kestrel_settings_t kestrel_settings_default = {
    .mode = KESTREL_MODE_OFF,
};

static TaskHandle_t service_task = NULL;
static SemaphoreHandle_t status_mutex = NULL;
static kestrel_status_t status;
static uint8_t pull_count = KL_TARGET_COUNT;   // the targets the running pull reads

// Link events from the NimBLE host task to the service task
#define EV_SYNCED       (1 << 0)
#define EV_CONNECTED    (1 << 1)
#define EV_DISCONNECTED (1 << 2)
#define EV_SUBSCRIBED   (1 << 3)
#define EV_CONNECT_FAILED (1 << 4)
#define EV_SEARCH_DONE  (1 << 5)
static EventGroupHandle_t link_events = NULL;
static StreamBufferHandle_t rx_stream = NULL;
static SemaphoreHandle_t indicate_done = NULL;
static volatile int indicate_status = 0;
static volatile uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

// Phone mode: this unit is the central and talks to the Kestrel's own LiNK service as a GATT client
static bool central = false;
static uint16_t peer_svc_end;
static uint16_t peer_out_handle;    // 85920100, indications from the Kestrel
static uint16_t peer_in_handle;     // 85920200, writes to the Kestrel
static uint16_t peer_cccd_handle;
static SemaphoreHandle_t gattc_done = NULL;
static volatile int gattc_status = 0;

// Service requests
#define REQ_PULL   (1 << 0)
#define REQ_SEARCH (1 << 1)

static char hud_serial[8];              // 7 digits
static char hud_name[16];               // "HUD-" + serial

static const char * const phase_names[KESTREL_PHASE_COUNT] = {
    "start", "connect", "subscribe", "open", "auth", "info", "targets", "stop",
};


/*
----------------------------------------------------
Settings
----------------------------------------------------
*/

esp_err_t load_kestrel_settings(void) {
    esp_err_t ret = load_config(NVS_NAMESPACE, &kestrel_settings, &kestrel_settings_default, sizeof(kestrel_settings));
    if (kestrel_settings.mode > KESTREL_MODE_PHONE) {
        kestrel_settings.mode = KESTREL_MODE_OFF;
    }
    kestrel_settings.peer_name[KESTREL_NAME_SIZE - 1] = '\0';
    return ret;
}


void kestrel_settings_reset(void) {
    memcpy(&kestrel_settings, &kestrel_settings_default, sizeof(kestrel_settings));
}


esp_err_t save_kestrel_settings(void) {
    return save_config(NVS_NAMESPACE, &kestrel_settings, sizeof(kestrel_settings));
}


/*
----------------------------------------------------
Status
----------------------------------------------------
*/

const char * kestrel_phase_name(kestrel_phase_t phase) {
    return phase < KESTREL_PHASE_COUNT ? phase_names[phase] : "?";
}


kestrel_status_t kestrel_get_status(void) {
    kestrel_status_t copy;
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    copy = status;
    xSemaphoreGive(status_mutex);
    return copy;
}


static void set_status(kestrel_pull_state_t state, const char * fmt, ...) {
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    status.state = state;
    va_list args;
    va_start(args, fmt);
    vsnprintf(status.message, sizeof(status.message), fmt, args);
    va_end(args);
    status.sequence++;
    xSemaphoreGive(status_mutex);
    ESP_LOGI(TAG, "%s", status.message);
}


void kestrel_hud_name(char * buf, size_t size) {
    snprintf(buf, size, "%s", hud_name);
}


/*
----------------------------------------------------
GATT: a HUD as the Kestrel expects it (ICD §14.3): Device Information, the HUD service and the LiNK service laid
out as the Kestrel's own; the Kestrel writes to 85920200 and takes indications on 85920100
----------------------------------------------------
*/

typedef enum {
    CHR_TABLE,
    CHR_LINK_OUT,
    CHR_LINK_IN,
    CHR_HUD_OUT,
    CHR_HUD_IN,
} chr_kind_t;

static uint16_t link_out_handle;
static uint16_t hud_out_handle;
static uint8_t table_value[204];        // the Kestrel's own table is padded with ff; a HUD's content is unknown

static const char dis_manufacturer[] = "Kestrel by NK";
static const char dis_model[] = "0750L";
static const char dis_hardware[] = "Rev 1";
static const char dis_firmware[] = "0.26";

#define LINK_UUID(b12, b13) BLE_UUID128_INIT(0x03, 0xdb, 0x7a, 0x21, 0x1d, 0xac, 0x4a, 0xae, 0x83, 0x4b, 0x38, 0x03, \
                                             b12, b13, 0x92, 0x85)
#define HUD_UUID(b12, b13) BLE_UUID128_INIT(0x4e, 0x4b, 0x20, 0x48, 0x55, 0x44, 0x20, 0x4d, 0x41, 0x53, 0x53, 0x20, \
                                            b12, b13, 0x54, 0x41)
static const ble_uuid128_t link_svc_uuid = LINK_UUID(0x00, 0x00);
static const ble_uuid128_t link_table_uuid = LINK_UUID(0xff, 0xff);
static const ble_uuid128_t link_out_uuid = LINK_UUID(0x00, 0x01);
static const ble_uuid128_t link_in_uuid = LINK_UUID(0x00, 0x02);
static const ble_uuid128_t hud_svc_uuid = HUD_UUID(0x00, 0x00);
static const ble_uuid128_t hud_table_uuid = HUD_UUID(0xff, 0xff);
static const ble_uuid128_t hud_out_uuid = HUD_UUID(0x00, 0x01);
static const ble_uuid128_t hud_in_uuid = HUD_UUID(0x00, 0x02);


static int dis_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt * ctxt, void * arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const char * text = arg;
    return os_mbuf_append(ctxt->om, text, strlen(text)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}


static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt * ctxt, void * arg) {
    chr_kind_t kind = (chr_kind_t) (intptr_t) arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (kind == CHR_TABLE) {
            return os_mbuf_append(ctxt->om, table_value, sizeof(table_value)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;   // the channels read empty
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t buf[64];
        uint16_t len = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (kind == CHR_LINK_IN) {
            if (xStreamBufferSend(rx_stream, buf, len, 0) != len) {
                ESP_LOGW(TAG, "receive buffer full, %u bytes dropped", len);
            }
        }
        else {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG "/hud", buf, len, ESP_LOG_DEBUG);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}


static const struct ble_gatt_svc_def gatt_services[] = {
    {
        // Device Information first: 16-bit UUIDs before the 128-bit services (ICD §14.3)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180a),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2a29), .access_cb = dis_access, .arg = (void *) dis_manufacturer, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2a24), .access_cb = dis_access, .arg = (void *) dis_model, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2a25), .access_cb = dis_access, .arg = (void *) hud_serial, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2a27), .access_cb = dis_access, .arg = (void *) dis_hardware, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2a26), .access_cb = dis_access, .arg = (void *) dis_firmware, .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &hud_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &hud_table_uuid.u, .access_cb = chr_access, .arg = (void *) CHR_TABLE, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = &hud_out_uuid.u, .access_cb = chr_access, .arg = (void *) CHR_HUD_OUT,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE, .val_handle = &hud_out_handle },
            { .uuid = &hud_in_uuid.u, .access_cb = chr_access, .arg = (void *) CHR_HUD_IN,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { 0 },
        },
    },
    {
        // The LiNK service, required (ICD §14.1 H-5)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &link_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &link_table_uuid.u, .access_cb = chr_access, .arg = (void *) CHR_TABLE, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = &link_out_uuid.u, .access_cb = chr_access, .arg = (void *) CHR_LINK_OUT,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE, .val_handle = &link_out_handle },
            { .uuid = &link_in_uuid.u, .access_cb = chr_access, .arg = (void *) CHR_LINK_IN,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { 0 },
        },
    },
    { 0 },
};


static int gap_event(struct ble_gap_event * event, void * arg);

// Advertising as a HUD the Kestrel lists and offers as available (ICD §14.3 step 1): the name in the
// advertisement; the HUD service and NK's manufacturer data in the scan response
static int start_advertising(void) {
    uint8_t adv[31];
    uint8_t rsp[31];
    size_t a = 0, r = 0;
    size_t name_len = strlen(hud_name);

    adv[a++] = 2; adv[a++] = BLE_HS_ADV_TYPE_FLAGS; adv[a++] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv[a++] = 1 + name_len; adv[a++] = BLE_HS_ADV_TYPE_COMP_NAME;
    memcpy(&adv[a], hud_name, name_len); a += name_len;

    rsp[r++] = 17; rsp[r++] = BLE_HS_ADV_TYPE_COMP_UUIDS128;
    memcpy(&rsp[r], hud_svc_uuid.value, 16); r += 16;
    static const uint8_t nk_manufacturer[] = {0xea, 0x00, 0x0a, 0x00, 0xff, 0xff, 0xff, 0xff};
    rsp[r++] = 1 + sizeof(nk_manufacturer); rsp[r++] = BLE_HS_ADV_TYPE_MFG_DATA;
    memcpy(&rsp[r], nk_manufacturer, sizeof(nk_manufacturer)); r += sizeof(nk_manufacturer);

    int rc = ble_gap_adv_set_data(adv, a);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_data(rsp, r);
    }
    if (rc == 0) {
        struct ble_gap_adv_params params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
        };
        rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising failed: %d", rc);
    }
    return rc;
}


// A search result: Kestrels advertise NK's company id (0x00ea) and a name (ICD L1-1, "ELITE - <SERIAL>"); a
// HUD uses the same company id, so names starting "HUD-" are left out
// The advertisement and the scan response arrive as separate results, and the company id and the name may be in
// different ones, so each device's pieces are gathered by address and judged when the search ends
#define KESTREL_CANDIDATES_MAX 24
typedef struct {
    ble_addr_t addr;
    int8_t rssi;
    bool nk;            // NK's company id seen
    char name[KESTREL_NAME_SIZE];
} search_candidate_t;
static search_candidate_t candidates[KESTREL_CANDIDATES_MAX];
static int candidate_count = 0;


static void search_result(const struct ble_gap_disc_desc * disc) {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return;
    }
    bool nk = fields.mfg_data != NULL && fields.mfg_data_len >= 2 && fields.mfg_data[0] == 0xea &&
              fields.mfg_data[1] == 0x00;

    int slot = -1;
    for (int i = 0; i < candidate_count; i++) {
        if (ble_addr_cmp(&candidates[i].addr, &disc->addr) == 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        if (candidate_count >= KESTREL_CANDIDATES_MAX || (!nk && fields.name == NULL)) {
            return;
        }
        slot = candidate_count++;
        memset(&candidates[slot], 0, sizeof(candidates[slot]));
        candidates[slot].addr = disc->addr;
    }
    candidates[slot].rssi = disc->rssi;
    candidates[slot].nk |= nk;
    if (fields.name != NULL) {
        size_t n = fields.name_len < KESTREL_NAME_SIZE - 1 ? fields.name_len : KESTREL_NAME_SIZE - 1;
        memcpy(candidates[slot].name, fields.name, n);
        candidates[slot].name[n] = '\0';
    }

    // A Kestrel (NK's company id and a name that is not a HUD's) is listed as soon as both are known, so the page
    // fills while the search runs (owner, 2026-11-07); a Kestrel already listed only updates its signal
    const search_candidate_t * c = &candidates[slot];
    if (!c->nk || c->name[0] == '\0' || strncmp(c->name, "HUD-", 4) == 0) {
        return;
    }
    int listed = -1;
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    for (int i = 0; i < status.found_count; i++) {
        if (memcmp(status.found[i].addr, c->addr.val, 6) == 0) {
            listed = i;
        }
    }
    bool added = false;
    if (listed >= 0) {
        status.found[listed].rssi = c->rssi;
    }
    else if (status.found_count < KESTREL_FOUND_MAX) {
        int i = status.found_count++;
        status.found[i].addr_type = c->addr.type;
        memcpy(status.found[i].addr, c->addr.val, 6);
        status.found[i].rssi = c->rssi;
        snprintf(status.found[i].name, sizeof(status.found[i].name), "%s", c->name);
        status.found_id++;
        added = true;
    }
    int found = status.found_count;
    xSemaphoreGive(status_mutex);
    if (added) {
        set_status(KESTREL_PULL_RUNNING, "Searching: %d found", found);
    }
}


static int gap_event(struct ble_gap_event * event, void * arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        search_result(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        xEventGroupSetBits(link_events, EV_SEARCH_DONE);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        // Phone mode: the Kestrel's frames arrive as indications on 85920100 (NimBLE confirms them)
        if (central && event->notify_rx.attr_handle == peer_out_handle) {
            uint8_t buf[64];
            uint16_t len = 0;
            if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len) == 0 &&
                xStreamBufferSend(rx_stream, buf, len, 0) != len) {
                ESP_LOGW(TAG, "receive buffer full, %u bytes dropped", len);
            }
        }
        break;
    case BLE_GAP_EVENT_CONNECT: {
        if (central) {
            // Phone mode: this unit connected out; the status is the connection's
            ESP_LOGI(TAG, "connect: status %d", event->connect.status);
            if (event->connect.status == 0) {
                conn_handle = event->connect.conn_handle;
                xEventGroupClearBits(link_events, EV_DISCONNECTED);
                xEventGroupSetBits(link_events, EV_CONNECTED);
            }
            else {
                xEventGroupSetBits(link_events, EV_CONNECT_FAILED);
            }
            break;
        }
        // As the peripheral, NimBLE reports the connection after asking the central for its features; a Bluetooth
        // 4.0 radio refuses that request from a peripheral (an error status) though the link is up. So the link
        // counts when it exists, whatever the status
        struct ble_gap_conn_desc desc;
        bool found = ble_gap_conn_find(event->connect.conn_handle, &desc) == 0;
        bool linked = event->connect.status == 0 || found;
        ESP_LOGI(TAG, "connect event: status 0x%x, %s", event->connect.status, linked ? "connected" : "no link");
        if (found) {
            ESP_LOGI(TAG, "connection interval %.2f ms, latency %u, timeout %u ms", desc.conn_itvl * 1.25,
                     desc.conn_latency, desc.supervision_timeout * 10);
        }
        if (linked) {
            conn_handle = event->connect.conn_handle;
            xEventGroupClearBits(link_events, EV_DISCONNECTED | EV_SUBSCRIBED);
            xEventGroupSetBits(link_events, EV_CONNECTED);
        }
        else {
            start_advertising();
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason 0x%x", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        xEventGroupClearBits(link_events, EV_CONNECTED | EV_SUBSCRIBED);
        xEventGroupSetBits(link_events, EV_DISCONNECTED);
        xSemaphoreGive(indicate_done);   // a send waiting for a confirmation gives up
        gattc_status = BLE_HS_ENOTCONN;
        xSemaphoreGive(gattc_done);      // and so does a write or a discovery
        break;
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "connection interval now %.2f ms (status %d)", desc.conn_itvl * 1.25, event->conn_update.status);
        }
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == link_out_handle && event->subscribe.cur_indicate) {
            xEventGroupSetBits(link_events, EV_SUBSCRIBED);
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        // An indication ends with its confirmation (BLE_HS_EDONE) or an error
        if (event->notify_tx.indication && event->notify_tx.status != 0) {
            indicate_status = event->notify_tx.status;
            xSemaphoreGive(indicate_done);
        }
        break;
    default:
        break;
    }
    return 0;
}


static void on_sync(void) {
    xEventGroupSetBits(link_events, EV_SYNCED);
}


static void host_task(void * param) {
    nimble_port_run();      // returns after nimble_port_stop()
    nimble_port_freertos_deinit();
}


/*
----------------------------------------------------
The LiNK session over the link (ICD §4, §5, §14.2)
----------------------------------------------------
*/

static kl_deframer_t deframer;
static uint8_t rx_chunk[64];
static size_t rx_len = 0;
static size_t rx_pos = 0;
static bool encrypted = false;
static uint8_t tx_frame[KL_FRAME_MAX];


static uint32_t now_ms(void) {
    return (uint32_t) (esp_timer_get_time() / 1000);
}


/*
Phone mode, GATT client: find the Kestrel's LiNK service (ICD §2.1), its two channels and the CCCD, subscribe to
indications, and write frames in 20-byte chunks with write-with-response (ICD L1-3)
*/

static bool gattc_wait(void) {
    if (xSemaphoreTake(gattc_done, pdMS_TO_TICKS(KESTREL_GATTC_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    return gattc_status == 0;
}


static int svc_found(uint16_t conn, const struct ble_gatt_error * error, const struct ble_gatt_svc * service, void * arg) {
    if (error->status == 0 && service != NULL) {
        peer_svc_end = service->end_handle;
        *(uint16_t *) arg = service->start_handle;
        return 0;
    }
    gattc_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    xSemaphoreGive(gattc_done);
    return 0;
}


static int chr_found(uint16_t conn, const struct ble_gatt_error * error, const struct ble_gatt_chr * chr, void * arg) {
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_cmp(&chr->uuid.u, &link_out_uuid.u) == 0) {
            peer_out_handle = chr->val_handle;
        }
        else if (ble_uuid_cmp(&chr->uuid.u, &link_in_uuid.u) == 0) {
            peer_in_handle = chr->val_handle;
        }
        return 0;
    }
    gattc_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    xSemaphoreGive(gattc_done);
    return 0;
}


static int dsc_found(uint16_t conn, const struct ble_gatt_error * error, uint16_t chr_val_handle,
                     const struct ble_gatt_dsc * dsc, void * arg) {
    if (error->status == 0 && dsc != NULL) {
        if (peer_cccd_handle == 0 && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            peer_cccd_handle = dsc->handle;
        }
        return 0;
    }
    gattc_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    xSemaphoreGive(gattc_done);
    return 0;
}


static int write_done(uint16_t conn, const struct ble_gatt_error * error, struct ble_gatt_attr * attr, void * arg) {
    gattc_status = error->status;
    xSemaphoreGive(gattc_done);
    return 0;
}


static esp_err_t client_subscribe(void) {
    uint16_t svc_start = 0;
    peer_out_handle = peer_in_handle = peer_cccd_handle = 0;

    xSemaphoreTake(gattc_done, 0);
    if (ble_gattc_disc_svc_by_uuid(conn_handle, &link_svc_uuid.u, svc_found, &svc_start) != 0 || !gattc_wait() ||
        svc_start == 0) {
        ESP_LOGW(TAG, "LiNK service not found (%d)", gattc_status);
        return ESP_ERR_NOT_FOUND;
    }
    if (ble_gattc_disc_all_chrs(conn_handle, svc_start, peer_svc_end, chr_found, NULL) != 0 || !gattc_wait() ||
        peer_out_handle == 0 || peer_in_handle == 0) {
        ESP_LOGW(TAG, "LiNK channels not found (%d)", gattc_status);
        return ESP_ERR_NOT_FOUND;
    }
    if (ble_gattc_disc_all_dscs(conn_handle, peer_out_handle, peer_svc_end, dsc_found, NULL) != 0 || !gattc_wait() ||
        peer_cccd_handle == 0) {
        ESP_LOGW(TAG, "LiNK CCCD not found (%d)", gattc_status);
        return ESP_ERR_NOT_FOUND;
    }
    static const uint8_t indicate[] = {0x02, 0x00};
    if (ble_gattc_write_flat(conn_handle, peer_cccd_handle, indicate, sizeof(indicate), write_done, NULL) != 0 ||
        !gattc_wait()) {
        ESP_LOGW(TAG, "subscribe failed (%d)", gattc_status);
        return ESP_FAIL;
    }
    return ESP_OK;
}


static esp_err_t client_send_frame(const uint8_t * frame, size_t len) {
    for (size_t off = 0; off < len; off += KESTREL_CHUNK) {
        size_t n = len - off < KESTREL_CHUNK ? len - off : KESTREL_CHUNK;
        xSemaphoreTake(gattc_done, 0);
        if (ble_gattc_write_flat(conn_handle, peer_in_handle, &frame[off], n, write_done, NULL) != 0 || !gattc_wait()) {
            ESP_LOGW(TAG, "write failed: %d", gattc_status);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}


static esp_err_t send_frame(const uint8_t * frame, size_t len) {
    if (central) {
        return client_send_frame(frame, len);
    }
    for (size_t off = 0; off < len; off += KESTREL_CHUNK) {
        size_t n = len - off < KESTREL_CHUNK ? len - off : KESTREL_CHUNK;
        struct os_mbuf * om = ble_hs_mbuf_from_flat(&frame[off], n);
        if (om == NULL) {
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreTake(indicate_done, 0);
        indicate_status = 0;
        int rc = ble_gatts_indicate_custom(conn_handle, link_out_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "indicate failed: %d", rc);
            return ESP_FAIL;
        }
        if (xSemaphoreTake(indicate_done, pdMS_TO_TICKS(KESTREL_INDICATE_TIMEOUT_MS)) != pdTRUE ||
            indicate_status != BLE_HS_EDONE) {
            ESP_LOGW(TAG, "indication not confirmed: %d", indicate_status);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}


static esp_err_t send_packet(uint16_t type, const uint8_t * payload, uint16_t len) {
    size_t n = kl_build_frame(type, payload, len, encrypted, tx_frame, sizeof(tx_frame));
    if (n == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return send_frame(tx_frame, n);
}


static esp_err_t send_ack(uint16_t op) {
    uint8_t payload[2] = {op & 0xff, op >> 8};
    return send_packet(KL_TYPE_ACK, payload, sizeof(payload));
}


static esp_err_t send_command(uint16_t op, const uint8_t * args, uint16_t len) {
    uint8_t payload[2 + 32];
    if (len > sizeof(payload) - 2) {
        return ESP_ERR_INVALID_SIZE;
    }
    payload[0] = op & 0xff;
    payload[1] = op >> 8;
    if (len > 0) {
        memcpy(&payload[2], args, len);
    }
    return send_packet(KL_TYPE_COMMAND, payload, 2 + len);
}


// The next packet the Kestrel sends; valid until the next call
static esp_err_t recv_packet(uint32_t timeout_ms, kl_packet_t * packet) {
    uint32_t deadline = now_ms() + timeout_ms;
    while (true) {
        while (rx_pos < rx_len) {
            if (kl_deframer_push(&deframer, rx_chunk[rx_pos++])) {
                if (kl_parse_body(deframer.body, deframer.len, encrypted, packet) == ESP_OK) {
                    return ESP_OK;
                }
                ESP_LOGW(TAG, "undecodable frame, %u bytes", (unsigned) deframer.len);
            }
        }
        if (xEventGroupGetBits(link_events) & EV_DISCONNECTED) {
            return ESP_ERR_INVALID_STATE;
        }
        int32_t left = (int32_t) (deadline - now_ms());
        if (left <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        rx_len = xStreamBufferReceive(rx_stream, rx_chunk, sizeof(rx_chunk), pdMS_TO_TICKS(left < 200 ? left : 200));
        rx_pos = 0;
    }
}


// Wait for a packet of this type (and, for COMMAND and ACK, this opcode); others are skipped, as in the
// reference client's handshake
static esp_err_t expect(uint16_t type, int32_t op, uint32_t timeout_ms, kl_packet_t * packet) {
    uint32_t deadline = now_ms() + timeout_ms;
    while (true) {
        int32_t left = (int32_t) (deadline - now_ms());
        if (left <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        esp_err_t ret = recv_packet(left, packet);
        if (ret != ESP_OK) {
            return ret;
        }
        if (packet->type == type && (op < 0 || (packet->len >= 2 && kl_u16(packet->payload) == op))) {
            return ESP_OK;
        }
    }
}


// ICD §5.3 from step 5 (a HUD link has no privacy status, §14.2 H-10)
static esp_err_t authenticate(void) {
    kl_packet_t p;
    uint8_t nonce[16];
    uint8_t proof[36];
    esp_err_t ret;

    static const uint8_t request_auth_args[] = {0x00};
    if ((ret = send_command(KL_OP_REQUEST_AUTH, request_auth_args, sizeof(request_auth_args))) != ESP_OK) return ret;
    if ((ret = expect(KL_TYPE_COMMAND, KL_OP_AUTH_DATA, KESTREL_REPLY_TIMEOUT_MS, &p)) != ESP_OK) return ret;
    if (p.len < 2 + 16) return ESP_ERR_INVALID_RESPONSE;
    memcpy(nonce, &p.payload[2], 16);
    if ((ret = send_ack(KL_OP_AUTH_DATA)) != ESP_OK) return ret;
    if ((ret = kl_auth_response(nonce, proof)) != ESP_OK) return ret;
    if ((ret = send_packet(KL_TYPE_RESPONSE, proof, sizeof(proof))) != ESP_OK) return ret;
    if ((ret = expect(KL_TYPE_ACK, KL_OP_AUTH_DATA, KESTREL_REPLY_TIMEOUT_MS, &p)) != ESP_OK) return ret;

    if ((ret = expect(KL_TYPE_COMMAND, KL_OP_REQUEST_AUTH, KESTREL_REPLY_TIMEOUT_MS, &p)) != ESP_OK) return ret;
    if ((ret = send_ack(KL_OP_REQUEST_AUTH)) != ESP_OK) return ret;
    esp_fill_random(nonce, sizeof(nonce));
    if ((ret = send_command(KL_OP_AUTH_DATA, nonce, sizeof(nonce))) != ESP_OK) return ret;
    while (true) {
        if ((ret = expect(KL_TYPE_RESPONSE, -1, KESTREL_REPLY_TIMEOUT_MS, &p)) != ESP_OK) return ret;
        if (p.len >= 32) break;
    }
    bool verified = kl_auth_verify(p.payload, p.len, nonce);
    if ((ret = send_ack(KL_OP_AUTH_DATA)) != ESP_OK) return ret;
    if (!verified) {
        return ESP_ERR_INVALID_CRC;
    }
    encrypted = true;
    return ESP_OK;
}


// The stop-and-wait exchange for one command (ICD §4.1): RESPONSEs are ACKed with the command's opcode, the
// Kestrel's COMMANDs with their own; ends on the COMMAND `done_op`, whose packet is returned
static esp_err_t exchange(uint16_t op, const uint8_t * args, uint16_t len, uint16_t done_op, uint32_t timeout_ms,
                          kl_packet_t * done, uint8_t * responses, size_t responses_size, size_t * responses_len,
                          uint16_t * nack_reason) {
    esp_err_t ret = send_command(op, args, len);
    if (ret != ESP_OK) {
        return ret;
    }
    if (responses_len != NULL) {
        *responses_len = 0;
    }
    uint32_t deadline = now_ms() + timeout_ms;
    while (true) {
        int32_t left = (int32_t) (deadline - now_ms());
        if (left <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        kl_packet_t p;
        if ((ret = recv_packet(left, &p)) != ESP_OK) {
            return ret;
        }
        if (p.type == KL_TYPE_NACK && p.len >= 2 && kl_u16(p.payload) == op) {
            *nack_reason = p.len >= 4 ? kl_u16(&p.payload[2]) : 0xffff;
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (p.type == KL_TYPE_RESPONSE) {
            if (responses != NULL && *responses_len < responses_size) {
                size_t n = p.len < responses_size - *responses_len ? p.len : responses_size - *responses_len;
                memcpy(&responses[*responses_len], p.payload, n);
                *responses_len += n;
            }
            if ((ret = send_ack(op)) != ESP_OK) return ret;
        }
        else if (p.type == KL_TYPE_COMMAND && p.len >= 2) {
            uint16_t pushed = kl_u16(p.payload);
            if (pushed == done_op) {
                *done = p;      // the caller reads it before the ACK below overwrites nothing it uses
                return ESP_OK;
            }
            if ((ret = send_ack(pushed)) != ESP_OK) return ret;
        }
    }
}


/*
----------------------------------------------------
A pull
----------------------------------------------------
*/

static uint32_t phase_start_ms;
static uint32_t phase_ms[KESTREL_PHASE_COUNT];
static kestrel_phase_t current_phase;   // the phase running now, which a failure ends

static void phase_done(kestrel_phase_t phase) {
    uint32_t now = now_ms();
    phase_ms[phase] = now - phase_start_ms;
    ESP_LOGI(TAG, "phase %s: %" PRIu32 " ms", phase_names[phase], phase_ms[phase]);
    phase_start_ms = now;
    current_phase = phase + 1;
}


// Wait for link events, keeping the unit awake (a pull is the user's action, and the first connection can take
// a while)
static bool wait_link(EventBits_t bits, uint32_t timeout_ms) {
    uint32_t deadline = now_ms() + timeout_ms;
    while (true) {
        int32_t left = (int32_t) (deadline - now_ms());
        if (left <= 0) {
            return false;
        }
        EventBits_t got = xEventGroupWaitBits(link_events, bits | EV_DISCONNECTED, pdFALSE, pdFALSE,
                                              pdMS_TO_TICKS(left < 1000 ? left : 1000));
        if (got & bits) {
            return true;
        }
        if ((got & EV_DISCONNECTED) && !(bits & EV_DISCONNECTED) && (bits != EV_CONNECTED)) {
            return false;
        }
        update_low_power_mode_last_activity_event();
    }
}


static esp_err_t stack_start(void) {
    xEventGroupClearBits(link_events, 0xff);
    xStreamBufferReset(rx_stream);
    conn_handle = BLE_HS_CONN_HANDLE_NONE;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(ret));
        return ret;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(gatt_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(gatt_services);
    }
    if (rc == 0) {
        rc = ble_svc_gap_device_name_set(hud_name);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT setup failed: %d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }
    nimble_port_freertos_init(host_task);

    if (!wait_link(EV_SYNCED, KESTREL_SYNC_TIMEOUT_MS)) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}


static void stack_stop(void) {
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        wait_link(EV_DISCONNECTED, KESTREL_DISCONNECT_TIMEOUT_MS);
    }
    ble_gap_adv_stop();
    if (nimble_port_stop() == 0) {
        nimble_port_deinit();
    }
    else {
        ESP_LOGE(TAG, "nimble_port_stop failed");
    }
}


static void run_pull(int count) {
    kl_target_t targets[KL_TARGET_COUNT];
    bool numbered = false;
    uint32_t target_max_ms = 0;
    const char * failure = NULL;
    esp_err_t ret;
    kl_packet_t p;
    uint16_t nack_reason = 0;
    bool stack_up = false;

    memset(phase_ms, 0, sizeof(phase_ms));
    current_phase = KESTREL_PHASE_START;
    uint32_t start_ms = now_ms();
    phase_start_ms = start_ms;
    encrypted = false;
    kl_deframer_reset(&deframer);
    rx_len = rx_pos = 0;

    central = kestrel_settings.mode == KESTREL_MODE_PHONE;
    set_status(KESTREL_PULL_RUNNING, "Starting Bluetooth");
    mem_diag_log_checkpoint("kestrel: before BLE start");
    kl_self_test();

    if ((ret = stack_start()) != ESP_OK) {
        failure = "Bluetooth did not start";
        goto stop;
    }
    stack_up = true;
    mem_diag_log_checkpoint("kestrel: BLE started");
    phase_done(KESTREL_PHASE_START);

    if (!central) {
        // HUD mode: advertise as the Kestrel's HUD; the Kestrel connects and subscribes
        if (start_advertising() != 0) {
            failure = "Advertising failed";
            goto stop;
        }
        set_status(KESTREL_PULL_RUNNING, "Waiting for the Kestrel (%s)", hud_name);
        if (!wait_link(EV_CONNECTED, KESTREL_CONNECT_TIMEOUT_MS)) {
            failure = "Kestrel not found: on, in HUD Connect, this unit paired?";
            goto stop;
        }
        phase_done(KESTREL_PHASE_CONNECT);

        set_status(KESTREL_PULL_RUNNING, "Connected");
        if (!wait_link(EV_SUBSCRIBED, KESTREL_SUBSCRIBE_TIMEOUT_MS)) {
            failure = "Kestrel connected but did not subscribe";
            goto stop;
        }
        phase_done(KESTREL_PHASE_SUBSCRIBE);

        // The Kestrel opens every HUD connection with FreeToCommunicate (ICD §14.2 H-8)
        if ((ret = expect(KL_TYPE_COMMAND, KL_OP_FREE_TO_COMMUNICATE, KESTREL_OPEN_TIMEOUT_MS, &p)) != ESP_OK ||
            send_ack(KL_OP_FREE_TO_COMMUNICATE) != ESP_OK) {
            failure = "No opening from the Kestrel";
            goto stop;
        }
        phase_done(KESTREL_PHASE_OPEN);
    }
    else {
        // Phone mode: connect to the Kestrel picked on the Kestrel page, as the phone app does
        set_status(KESTREL_PULL_RUNNING, "Connecting to %s", kestrel_settings.peer_name);
        ble_addr_t peer = {.type = kestrel_settings.peer_addr_type};
        memcpy(peer.val, kestrel_settings.peer_addr, sizeof(peer.val));
        int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer, KESTREL_PHONE_CONNECT_TIMEOUT_MS, NULL, gap_event, NULL);
        EventBits_t got = rc == 0 ? xEventGroupWaitBits(link_events, EV_CONNECTED | EV_CONNECT_FAILED, pdFALSE, pdFALSE,
                                                        pdMS_TO_TICKS(KESTREL_PHONE_CONNECT_TIMEOUT_MS + 2000)) : 0;
        if (!(got & EV_CONNECTED)) {
            ESP_LOGW(TAG, "connect: rc %d", rc);
            failure = "Kestrel not found: on, normal Bluetooth mode, phone app closed?";
            goto stop;
        }
        phase_done(KESTREL_PHASE_CONNECT);

        set_status(KESTREL_PULL_RUNNING, "Connected");
        if (client_subscribe() != ESP_OK) {
            failure = "Kestrel's LiNK service not found";
            goto stop;
        }
        phase_done(KESTREL_PHASE_SUBSCRIBE);

        // The phone link starts with GetPrivacyStatus, as the vendor app and the reference client do (ICD §5.3 1-4)
        if (send_command(KL_OP_GET_PRIVACY_STATUS, NULL, 0) != ESP_OK ||
            expect(KL_TYPE_RESPONSE, -1, KESTREL_REPLY_TIMEOUT_MS, &p) != ESP_OK) {
            failure = "No answer from the Kestrel";
            goto stop;
        }
        // 00: privacy off, every read works without the PIN (ICD §5.4); the owner keeps it off, so no PIN is sent
        ESP_LOGI(TAG, "privacy status %02x", p.len > 0 ? p.payload[0] : 0xff);
        if (send_ack(KL_OP_GET_PRIVACY_STATUS) != ESP_OK) {
            failure = "No answer from the Kestrel";
            goto stop;
        }
        phase_done(KESTREL_PHASE_OPEN);
    }

    set_status(KESTREL_PULL_RUNNING, "Authenticating");
    if ((ret = authenticate()) != ESP_OK) {
        failure = ret == ESP_ERR_INVALID_CRC ? "Kestrel failed authentication" : "Authentication did not finish";
        goto stop;
    }
    phase_done(KESTREL_PHASE_AUTH);

    // TgtInfo: its byte 6 is the designator, 0 = A to J, 1 = 1 to 10 (ICD §7.4)
    uint8_t info[64];
    size_t info_len = 0;
    static const uint8_t tgt_info_args[] = {KL_SETTINGS_TGT_INFO};
    ret = exchange(KL_OP_GET_SETTINGS, tgt_info_args, sizeof(tgt_info_args), KL_OP_EOD, KESTREL_REPLY_TIMEOUT_MS,
                   &p, info, sizeof(info), &info_len, &nack_reason);
    if (ret != ESP_OK || send_ack(KL_OP_EOD) != ESP_OK) {
        failure = "Target Card settings not read";
        goto stop;
    }
    numbered = info_len > 6 && info[6] == 1;
    phase_done(KESTREL_PHASE_INFO);

    // Each target: CalcFullSolution with the Target Card arguments only, [t][t][ff][00] (ICD §6.1, SAFE)
    for (int t = 0; t < count; t++) {
        set_status(KESTREL_PULL_RUNNING, "Reading target %d/%d", t + 1, count);
        uint32_t target_start = now_ms();
        const uint8_t args[] = {(uint8_t) t, (uint8_t) t, 0xff, 0x00};
        ret = exchange(KL_OP_CALC_FULL_SOLN, args, sizeof(args), KL_OP_SET_FULL_SOLN, KESTREL_TARGET_TIMEOUT_MS,
                       &p, NULL, 0, NULL, &nack_reason);
        if (ret == ESP_OK) {
            ret = kl_decode_full_solution(&p, &targets[t]);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, p.payload, p.len, ESP_LOG_DEBUG);
            if (ret == ESP_OK) {
                ret = send_ack(KL_OP_SET_FULL_SOLN);
            }
        }
        if (ret != ESP_OK) {
            failure = ret == ESP_ERR_NOT_SUPPORTED ? "Kestrel refused a target" : "Target not read";
            ESP_LOGW(TAG, "target %d: %s (nack 0x%x)", t, esp_err_to_name(ret), nack_reason);
            goto stop;
        }
        uint32_t took = now_ms() - target_start;
        target_max_ms = took > target_max_ms ? took : target_max_ms;
        ESP_LOGI(TAG, "target %d: range %" PRIu32 " cm, elevation %.2f, wind %.2f / %.2f mil, %" PRIu32 " ms", t,
                 targets[t].range_cm, targets[t].elevation_mil, targets[t].wind1_mil, targets[t].wind2_mil, took);
    }
    phase_done(KESTREL_PHASE_TARGETS);

stop:
    if (failure != NULL && current_phase < KESTREL_PHASE_STOP) {
        phase_done(current_phase);      // the time spent in the phase that failed
    }
    if (stack_up || failure == NULL) {
        set_status(KESTREL_PULL_RUNNING, "Disconnecting");
    }
    stack_stop();
    phase_done(KESTREL_PHASE_STOP);
    mem_diag_log_checkpoint("kestrel: BLE stopped");

    uint32_t total = now_ms() - start_ms;
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    memcpy(status.phase_ms, phase_ms, sizeof(phase_ms));
    status.total_ms = total;
    status.target_max_ms = target_max_ms;
    if (failure == NULL) {
        status.have_result = true;
        status.numbered = numbered;
        status.target_count = (uint8_t) count;
        status.result_id++;
        memcpy(status.targets, targets, sizeof(targets));
    }
    xSemaphoreGive(status_mutex);

    if (failure == NULL) {
        set_status(KESTREL_PULL_DONE, "Read %d targets in %.1f s", count, total / 1000.0);
    }
    else {
        set_status(KESTREL_PULL_FAILED, "%s", failure);
    }
}


// Phone mode: list the Kestrels advertising nearby, for the user to pick theirs
static void run_search(void) {
    // A new search starts from an empty list, at once
    candidate_count = 0;
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    status.found_count = 0;
    status.found_id++;
    xSemaphoreGive(status_mutex);

    central = true;
    set_status(KESTREL_PULL_RUNNING, "Searching for Kestrels");
    if (stack_start() != ESP_OK) {
        stack_stop();
        set_status(KESTREL_PULL_FAILED, "Bluetooth did not start");
        return;
    }
    struct ble_gap_disc_params params = {
        .passive = 0,           // the scan response may carry the name
        .filter_duplicates = 1,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, KESTREL_SEARCH_MS, &params, gap_event, NULL);
    if (rc == 0) {
        wait_link(EV_SEARCH_DONE, KESTREL_SEARCH_MS + 2000);
    }
    else {
        ESP_LOGW(TAG, "search failed: %d", rc);
    }
    stack_stop();

    xSemaphoreTake(status_mutex, portMAX_DELAY);
    status.found_id++;      // the list's end state ("none found") shows too
    int found = status.found_count;
    xSemaphoreGive(status_mutex);
    ESP_LOGI(TAG, "search: %d devices heard, %d Kestrel(s)", candidate_count, found);
    if (found == 0) {
        set_status(KESTREL_PULL_FAILED, "No Kestrel found: on, normal Bluetooth mode, phone app closed?");
    }
    else {
        set_status(KESTREL_PULL_IDLE, "Search done, %d found: pick yours", found);
    }
}


static void service_task_fn(void * param) {
    while (true) {
        uint32_t requests = 0;
        xTaskNotifyWait(0, UINT32_MAX, &requests, portMAX_DELAY);
        if (requests & REQ_SEARCH) {
            run_search();
        }
        else if (requests & REQ_PULL) {
            run_pull(pull_count);
        }
    }
}


static esp_err_t radio_allowed(void) {
    // BLE with a phone on the unit's access point is the unstable mix (Espressif); an update needs the radio
    if (wifi_get_status().state == WIRELESS_STATE_PROVISIONING) {
        set_status(KESTREL_PULL_FAILED, "Not while Wi-Fi is being set up");
        return ESP_ERR_INVALID_STATE;
    }
    if (ota_mode_is_active()) {
        set_status(KESTREL_PULL_FAILED, "Not while updating");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}


esp_err_t kestrel_request_search(void) {
    if (kestrel_get_status().state == KESTREL_PULL_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = radio_allowed();
    if (ret != ESP_OK) {
        return ret;
    }
    set_status(KESTREL_PULL_RUNNING, "Starting");
    xTaskNotify(service_task, REQ_SEARCH, eSetBits);
    return ESP_OK;
}


esp_err_t kestrel_choose(int index) {
    kestrel_status_t s = kestrel_get_status();
    if (index < 0 || index >= s.found_count) {
        return ESP_ERR_INVALID_ARG;
    }
    kestrel_settings.peer_addr_type = s.found[index].addr_type;
    memcpy(kestrel_settings.peer_addr, s.found[index].addr, sizeof(kestrel_settings.peer_addr));
    snprintf(kestrel_settings.peer_name, sizeof(kestrel_settings.peer_name), "%s", s.found[index].name);
    ESP_LOGI(TAG, "phone mode: %s picked", kestrel_settings.peer_name);
    return save_kestrel_settings();
}


esp_err_t kestrel_request_pull(int count) {
    if (count < 1 || count > KL_TARGET_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (kestrel_settings.mode == KESTREL_MODE_OFF) {
        set_status(KESTREL_PULL_FAILED, "Kestrel is off in the settings");
        return ESP_ERR_INVALID_STATE;
    }
    if (kestrel_get_status().state == KESTREL_PULL_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (kestrel_settings.mode == KESTREL_MODE_PHONE && kestrel_settings.peer_name[0] == '\0') {
        set_status(KESTREL_PULL_FAILED, "Pick your Kestrel on the Kestrel page");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = radio_allowed();
    if (ret != ESP_OK) {
        return ret;
    }
    pull_count = (uint8_t) count;
    set_status(KESTREL_PULL_RUNNING, "Starting");
    xTaskNotify(service_task, REQ_PULL, eSetBits);
    return ESP_OK;
}


esp_err_t kestrel_service_init(void) {
    status_mutex = xSemaphoreCreateMutex();
    link_events = xEventGroupCreate();
    indicate_done = xSemaphoreCreateBinary();
    gattc_done = xSemaphoreCreateBinary();
    rx_stream = xStreamBufferCreate(KESTREL_RX_STREAM_BYTES, 1);
    if (status_mutex == NULL || link_events == NULL || indicate_done == NULL || gattc_done == NULL || rx_stream == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(table_value, 0xff, sizeof(table_value));

    // The name the Kestrel lists: "HUD-" and 7 digits, from this unit's Bluetooth address so it stays the same
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    uint32_t number = ((uint32_t) mac[3] << 16 | mac[4] << 8 | mac[5]) % 10000000;
    snprintf(hud_serial, sizeof(hud_serial), "%07" PRIu32, number);
    snprintf(hud_name, sizeof(hud_name), "HUD-%s", hud_serial);
    snprintf(status.message, sizeof(status.message), "No pull yet");

    BaseType_t ok = xTaskCreateWithCaps(service_task_fn, "kestrel", KESTREL_SERVICE_TASK_STACK, NULL,
                                        KESTREL_SERVICE_TASK_PRIORITY, &service_task, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

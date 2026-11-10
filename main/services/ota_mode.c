#include "ota_mode.h"

#include <ctype.h>
#include <string.h>

#include "low_power_mode.h"
#include "wifi.h"
#include "main_tileview.h"
#include "app_cfg.h"
#include "common.h"
#include "mem_diag.h"
#include "settings_ui.h"
#include "value_editor.h"
#include "display_shape.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "json_parser.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#define TAG "OTAMode"

#define OTA_UI_LOCK_WAIT_MS 1000        // the OTA task waits this long for the screen to show a step or outcome
#define OTA_GAP_PX 20
#define OTA_BUTTON_PORTRAIT_PX 96
#define OTA_BUTTON_LANDSCAPE_PX 122
#define OTA_BAR_PX 24
#define OTA_SIDE_BUTTON_PX 150         // landscape: the buttons' column on the right
#define OTA_MIDDLE_GAP_PX 12
#define OTA_READ_BUFFER_SIZE 4096
#define OTA_PROGRESS_PERIOD_MS 250

typedef enum {
    OTA_EVENT_MANIFEST_READY = (1 << 0),
    OTA_EVENT_OTA_COMPLETE = (1 << 1),
    OTA_EVENT_USER_CONFIRMED = (1 << 2),
    OTA_EVENT_IS_POWERED_BY_USB = (1 << 3),
} ota_event_e;
static EventGroupHandle_t ota_event_group = NULL;

static lv_obj_t * progress_label;
static lv_obj_t * progress_bar;


static TaskHandle_t ota_poller_task_handle;
static TaskHandle_t ota_update_task_handle;

static lv_obj_t * last_tile = NULL;  // last tile before entering the OTA mode
static lv_obj_t * ota_description_label;
static lv_obj_t * ota_prompt_view;
static lv_obj_t * ota_title_label;
static lv_obj_t * reboot_button;
static lv_obj_t * menu_ota_upgrade_button;
static bool update_found = false;               // a newer firmware was found; set and read with the LVGL lock held

HEAPS_CAPS_ATTR ota_manifest_t ota_manifest;


// The manifest at `url`, or at `host`, `path` and `port`. Without a `url` of its own, a manifest's image is
// downloaded from `host`; a source with no `host` needs manifests with a `url`
typedef struct {
    const char * url;
    const char * host;
    const char * path;
    int port;
} ota_source_t;

// In order: the first manifest read decides
static const ota_source_t ota_sources[] = {
    {.host = OTA_LOCAL_HOST, .path = OTA_LOCAL_MANIFEST_PATH, .port = OTA_MANIFEST_PORT},
    {.url = OTA_GITHUB_MANIFEST_URL},
};

typedef struct {
    char * data;
    int length;
    bool overflow;
} ota_manifest_body_t;


static inline esp_err_t create_ota_event_group() {
    // If not created, then create the event group. This function may be called before the `wifi_init()`. 
    if (ota_event_group == NULL) {
        ota_event_group = xEventGroupCreate();
        if (ota_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create ota_event_group");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    case HTTP_EVENT_ON_STATUS_CODE:
    case HTTP_EVENT_ON_HEADERS_COMPLETE:
        // Added in ESP-IDF v6; not used here
        break;
    }
    return ESP_OK;
}

static void ota_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA started");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
                ESP_LOGI(TAG, "Verifying chip revision of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGD(TAG, "Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA finish");
                break;
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGI(TAG, "OTA abort");
                break;
        }
    }
}

static esp_err_t http_client_init_cb(esp_http_client_handle_t http_client) {
    return ESP_OK;
}


// The final response's body only: a redirect's is skipped
static esp_err_t manifest_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || esp_http_client_get_status_code(evt->client) != 200) {
        return ESP_OK;
    }
    ota_manifest_body_t * body = evt->user_data;
    if (body->length + evt->data_len > OTA_MANIFEST_MAX_SIZE) {
        body->overflow = true;
        return ESP_OK;
    }
    memcpy(&body->data[body->length], evt->data, evt->data_len);
    body->length += evt->data_len;
    return ESP_OK;
}


/*
----------------------------------------------------
The update's screens (ADR-008), in the settings' design: black, large buttons, wrapped text
- The offer: what the update is and its note, Install and Cancel, full screen over every view. Shown from the
  hub's "Update available" row, or at once for a critical update
- The OTA tile: what is happening, a large progress bar and the amount read, then the outcome and Restart
All with the LVGL lock held
----------------------------------------------------
*/

static lv_obj_t * offer_text = NULL;            // the offer's scrolling text area
static lv_obj_t * offer_title_label = NULL;
static lv_obj_t * offer_install_button = NULL;
static lv_obj_t * offer_cancel_button = NULL;
static lv_obj_t * ota_detail_label = NULL;


static bool is_portrait(lv_obj_t * obj) {
    return lv_obj_get_height(obj) > lv_obj_get_width(obj);
}


static void set_ota_prompt_view_visibility(bool is_visible) {
    if (is_visible) {
        lv_obj_set_hidden(ota_prompt_view, false);
        lv_obj_move_foreground(ota_prompt_view);
        lv_obj_scroll_to_y(offer_text, 0, LV_ANIM_OFF);
    } else {
        lv_obj_set_hidden(ota_prompt_view, true);
    }
}


// The manifest found (or a mock): the offer's text, the hub's row, and the offer at once when critical
static void offer_update(void) {
    bool critical = ota_manifest.importance > OTA_IMPORTANCE_NORMAL;
    lv_label_set_text(offer_title_label, critical ? "Important update" : "Update available");
    lv_label_set_text_fmt(ota_description_label, "Firmware %s\n\n%s", ota_manifest.version, ota_manifest.note);
    update_found = true;        // the hub's row shows from its next refresh, the hub laid out again with it
    if (critical) {
        set_ota_prompt_view_visibility(true);
    }
}


// The OTA tile: the step under way, with the amount read when known
static void show_ota_step(const char * step) {
    lv_label_set_text(ota_title_label, step);
}


static void show_ota_progress(int read, int size) {
    if (size > 0) {
        lv_label_set_text_fmt(progress_label, "%d K of %d K", read / 1024, size / 1024);
        lv_bar_set_value(progress_bar, (int32_t) ((int64_t) read * 100 / size), LV_ANIM_OFF);
    }
    else {
        lv_label_set_text_fmt(progress_label, "%d K", read / 1024);    // the server sent no length
    }
}


static lv_obj_t * ota_view = NULL;
static void layout_ota_view(lv_obj_t * view);


// The end, either way: what happened and Restart. A failed update leaves the running firmware in place; the
// error's name goes on a line of its own, so it is not broken inside
static void show_ota_result(bool ok, const char * reason, esp_err_t err) {
    lv_label_set_text(ota_title_label, ok ? "Update installed" : "Update failed");
    lv_obj_set_style_text_color(ota_title_label, ok ? lv_palette_main(LV_PALETTE_ORANGE) :
                                lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    if (ok) {
        lv_label_set_text(ota_detail_label, "Restart to run it.");
        lv_bar_set_value(progress_bar, 100, LV_ANIM_OFF);
    }
    else {
        lv_label_set_text_fmt(ota_detail_label, "%s\n%s\nThe unit keeps its firmware.", reason, esp_err_to_name(err));
    }
    lv_obj_set_hidden(ota_detail_label, false);
    lv_obj_set_hidden(reboot_button, false);
    layout_ota_view(ota_view);
}


// From the OTA task: each update waits for the lock, so no step or outcome is lost on a busy screen
static void ota_ui_step(const char * step) {
    if (lvgl_port_lock(OTA_UI_LOCK_WAIT_MS)) {
        show_ota_step(step);
        lvgl_port_unlock();
    }
}


static void ota_ui_result(bool ok, const char * reason, esp_err_t err) {
    if (lvgl_port_lock(OTA_UI_LOCK_WAIT_MS)) {
        show_ota_result(ok, reason, err);
        lvgl_port_unlock();
    }
}

// One code point as UTF-8, or its plain-text stand-in: the panel's fonts have ASCII, the degree sign and the
// bullet, so typographic quotes, dashes and the ellipsis would show as boxes
static size_t put_code_point(char * out, uint32_t cp) {
    switch (cp) {
    case 0x2018: case 0x2019: out[0] = '\''; return 1;
    case 0x201C: case 0x201D: out[0] = '"'; return 1;
    case 0x2013: case 0x2014: out[0] = '-'; return 1;
    case 0x2026: memcpy(out, "...", 3); return 3;
    default: break;
    }
    if (cp < 0x80) {
        out[0] = (char) cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char) (0xE0 | (cp >> 12));
    out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char) (0x80 | (cp & 0x3F));
    return 3;
}


// A JSON string's escapes decoded in place (the parser hands the text as written: "\n" showed as a backslash and an
// n). The result is never longer than the text
static void json_unescape(char * s) {
    char * out = s;
    for (const char * in = s; *in != '\0'; in++) {
        if (*in != '\\' || in[1] == '\0') {
            *out++ = *in;
            continue;
        }
        in++;
        switch (*in) {
        case 'n': *out++ = '\n'; break;
        case 't': *out++ = ' '; break;
        case 'r': case 'b': case 'f': break;
        case 'u': {
            uint32_t cp = 0;
            int digits = 0;
            while (digits < 4 && isxdigit((unsigned char) in[1])) {
                char c = *++in;
                cp = cp * 16 + (uint32_t) (isdigit((unsigned char) c) ? c - '0' : (tolower((unsigned char) c) - 'a' + 10));
                digits++;
            }
            // A character beyond 0xFFFF (an emoji) comes as a pair of surrogates and has no glyph here: one '?' for
            // the pair (the low half that follows is skipped)
            bool surrogate = digits == 4 && cp >= 0xD800 && cp <= 0xDFFF;
            if (surrogate && cp <= 0xDBFF && in[1] == '\\' && in[2] == 'u' &&
                (in[3] == 'd' || in[3] == 'D') && strchr("cdefCDEF", in[4]) != NULL && in[4] != '\0') {
                in += 6;    // the low half: \uDCxx to \uDFxx
            }
            out += put_code_point(out, (digits == 4 && !surrogate) ? cp : '?');
            break;
        }
        default: *out++ = *in; break;   // \" \\ \/
        }
    }
    *out = '\0';
}


// The release note, as long as the manifest has it, up to the buffer and cut with "..." beyond (inside no
// character); a long note used to reject the whole manifest, so no update was offered
static esp_err_t read_note(jparse_ctx_t * jctx, char * note, size_t size) {
    int len = 0;
    if (json_obj_get_strlen(jctx, "note", &len) != OS_SUCCESS) {
        return ESP_FAIL;
    }
    char * text = heap_caps_malloc(len + 1, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (json_obj_get_string(jctx, "note", text, len + 1) != OS_SUCCESS) {
        heap_caps_free(text);
        return ESP_FAIL;
    }
    json_unescape(text);
    size_t used = strlen(text);
    if (used < size) {
        memcpy(note, text, used + 1);
    }
    else {
        size_t keep = size - 4;
        while (keep > 0 && ((unsigned char) text[keep] & 0xC0) == 0x80) {
            keep--;     // not inside a character's bytes
        }
        memcpy(note, text, keep);
        memcpy(&note[keep], "...", 4);
        ESP_LOGW(TAG, "The note has %u bytes; shown up to %u", (unsigned) used, (unsigned) keep);
    }
    heap_caps_free(text);
    return ESP_OK;
}


esp_err_t fetch_manifest_from_source(const ota_source_t * source) {
    esp_err_t ret;
    char * manifest_json_raw = NULL;
    jparse_ctx_t jctx;
    bool parsing = false;       // the parser's tokens are freed at the end (they were left allocated)
    ota_manifest_body_t body = {0};

    ESP_LOGI(TAG, "Fetching manifest from %s", source->url ? source->url : source->host);

    manifest_json_raw = heap_caps_calloc(1, OTA_MANIFEST_MAX_SIZE + 1, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (!manifest_json_raw) {
        return ESP_ERR_NO_MEM;
    }
    body.data = manifest_json_raw;

    esp_http_client_config_t http_config = {
        .url = source->url,
        .host = source->host,
        .path = source->path,
        .port = source->port,
        .event_handler = manifest_event_handler,
        .user_data = &body,
        .timeout_ms = OTA_HTTP_TIMEOUT,  // longer timeout for slow response servers
        .buffer_size = OTA_HTTP_HEADER_BUFFER_SIZE,
        .buffer_size_tx = OTA_HTTP_HEADER_BUFFER_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client_handle = esp_http_client_init(&http_config);
    if (client_handle == NULL) {
        heap_caps_free(manifest_json_raw);
        return ESP_FAIL;
    }

    // GET, following redirects
    ret = esp_http_client_perform(client_handle);
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to fetch the manifest");

    int status = esp_http_client_get_status_code(client_handle);
    if (status != 200 || body.overflow || body.length == 0) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "No manifest: HTTP %d, %d bytes%s", status, body.length,
                          body.overflow ? ", too long" : "");
    }
    int byte_read = body.length;
    ESP_LOGI(TAG, "Read %d bytes: %s", byte_read, manifest_json_raw);

    // Decode by json parser
    /* Example JSON file: 
        {
            "version": "1.2.3",
            "path": "/1.2.3/firmware.bin"
        }
    */
    if (json_parse_start(&jctx, manifest_json_raw, byte_read) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to decode json string: %s", manifest_json_raw);
    }
    parsing = true;

    // Read manifest_version
    if (json_obj_get_int(&jctx, "manifest_version", &ota_manifest.manifest_version)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract manifest_version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "manifest_version: %d", ota_manifest.manifest_version);
    }

    // Read fw_version
    if (json_obj_get_string(&jctx, "version", ota_manifest.version, sizeof(ota_manifest.version)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "version: %s", ota_manifest.version);
    }

    // Read fw_path
    if (json_obj_get_string(&jctx, "path", ota_manifest.path, sizeof(ota_manifest.path)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract path: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "path: %s", ota_manifest.path);
    }

    // Read the release note
    ret = read_note(&jctx, ota_manifest.note, sizeof(ota_manifest.note));
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract note: %s", manifest_json_raw);
    ESP_LOGI(TAG, "note: %s", ota_manifest.note);

    // The image's own address, when the manifest gives one
    int url_length = 0;
    ota_manifest.url[0] = '\0';
    if (json_obj_get_strlen(&jctx, "url", &url_length) == OS_SUCCESS &&
        (url_length >= (int) sizeof(ota_manifest.url) ||
         json_obj_get_string(&jctx, "url", ota_manifest.url, sizeof(ota_manifest.url)) != OS_SUCCESS)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract url: %s", manifest_json_raw);
    }
    if (ota_manifest.url[0] == '\0' && source->host == NULL) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "This source's manifest needs a url: %s", manifest_json_raw);
    }
    ESP_LOGI(TAG, "url: %s", ota_manifest.url[0] ? ota_manifest.url : "(none)");

    // Read port number
    if (json_obj_get_int(&jctx, "port", &ota_manifest.port)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract port: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "port: %d", ota_manifest.port);
    }

    // Read ignore version flag
    if (json_obj_get_bool(&jctx, "ignore_version", &ota_manifest.ignore_version)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract ignore_version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "ignore_version: %d", ota_manifest.ignore_version);
    }

    // Read type
    if (json_obj_get_int(&jctx, "type", (int *) &ota_manifest.type)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract type: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "type: %d", ota_manifest.type);
    }

    // Read importance
    if (json_obj_get_int(&jctx, "importance", (int *) &ota_manifest.importance)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract importance: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "importance: %d", ota_manifest.importance);
    }

    // Copy other attributes
    ota_manifest.host = source->host;
    ota_manifest.initialized = true;

    // if ignore_version is false then we can compare the version, populate update only if the OTA server has newer version
    if (!ota_manifest.ignore_version) {
        // Check the current version
        const esp_app_desc_t * app_desc = esp_app_get_description();
        ESP_LOGI(TAG, "Running version: %s", app_desc->version);

        // A version that cannot be read is never offered
        version_t self_version;
        ret = parse_git_describe_version(app_desc->version, &self_version);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Cannot read the running version %s", app_desc->version);

        // Read the target version
        version_t other_version;
        ret = parse_git_describe_version(ota_manifest.version, &other_version);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Cannot read the manifest version %s", ota_manifest.version);

        // Compare
        if (compare_version(&self_version, &other_version) >= 0) {
            ESP_LOGI(TAG, "Running version %s is the same as or newer than the manifest's %s: not offered", app_desc->version, ota_manifest.version);
            // don't update
            ret = ESP_OK;
            goto finally;
        }
    }

    // Waits for the lock: a busy screen must not lose the offer
    if (lvgl_port_lock(OTA_UI_LOCK_WAIT_MS)) {
        offer_update();
        lvgl_port_unlock();
    }

    // We're done here
    // The rest will be handled within the OTA mode

    ret = ESP_OK;

finally:
    if (parsing) {
        json_parse_end(&jctx);
    }
    esp_http_client_cleanup(client_handle);

    if (manifest_json_raw) {
        heap_caps_free(manifest_json_raw);
    }

    return ret;
}

void ota_poller_task(void *p) {
    // Create OTA event group
    ESP_ERROR_CHECK(create_ota_event_group());

    // Uninitialize the OTA manifest
    memset(&ota_manifest, 0x0, sizeof(ota_manifest));
    ota_manifest.initialized = false;

    // Wait for wifi to be connected
    while (true) {
        esp_err_t ret = wifi_wait_for_sta_connected(1000);
        if (ret == ESP_OK) {
            // Manifest download ready
            xEventGroupSetBits(ota_event_group, OTA_EVENT_MANIFEST_READY);
            break;
        }
    }

    ESP_LOGI(TAG, "Network connected, will poll OTA source");

    for (size_t idx = 0; idx < sizeof(ota_sources) / sizeof(ota_sources[0]); idx += 1) {
        esp_err_t ret = fetch_manifest_from_source(&ota_sources[idx]);
        if (ret == ESP_OK) {
            break;
        }
    }

    ESP_LOGI(TAG, "Manifest downloaded, waiting for OTA update");
    ESP_LOGI(TAG, "ota_poller stack: %u of %u bytes never used", (unsigned) uxTaskGetStackHighWaterMark(NULL),
             (unsigned) OTA_POLLER_TASK_STACK);

    vTaskDeleteWithCaps(NULL);   // safely remove this task
}


void ota_update_task(void *p) {
    // Create OTA event group
    ESP_ERROR_CHECK(create_ota_event_group());

    // Wait for manifest to be downloaded
    xEventGroupWaitBits(ota_event_group, OTA_EVENT_MANIFEST_READY | OTA_EVENT_USER_CONFIRMED, pdFALSE, pdTRUE, portMAX_DELAY);

    // -------------------------------------
    esp_err_t ret;

    // Register event. This runs once the user confirmed the update, so a failure is shown, not a reboot (ADR-005 C1)
    ret = esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, ota_event_handler, NULL);
    if (ret != ESP_OK) {
        ota_ui_result(false, "The update could not start.", ret);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "esp_event_handler_register failed");
    }

    esp_http_client_config_t http_config = {
        .event_handler = http_client_event_handler,
        .keep_alive_enable = true,
        .timeout_ms = OTA_HTTP_TIMEOUT,
        .buffer_size = OTA_READ_BUFFER_SIZE,    // each read of the image; the default 512 B made it 28 KB/s
        .buffer_size_tx = OTA_HTTP_HEADER_BUFFER_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    if (ota_manifest.url[0] != '\0') {
        http_config.url = ota_manifest.url;
    }
    else {
        http_config.host = ota_manifest.host;
        http_config.path = ota_manifest.path;
        http_config.port = ota_manifest.port;
    }

    // Determine the target partition
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "OTA will be written to partition %s", ota_partition->label);

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = http_client_init_cb,
        .partition = {
            .staging = ota_partition,
            .final = NULL,  // use staging partition
            .finalize_with_copy = false,
        },
        // Erase the partition in one go at the start: erasing each 4 KB sector as the download reached it
        // took half the update's time (42 s, now 22 s, for 2.4 MB). The erase runs at the first read
        .bulk_flash_erase = true,
    };

    ota_ui_step("Connecting");
    esp_https_ota_handle_t https_ota_handle = NULL;
    ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ota_ui_result(false, "The update server did not answer.", ret);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "esp_https_ota_begin failed");
    }

    // Read application size
    int ota_size = esp_https_ota_get_image_size(https_ota_handle);
    ESP_LOGI(TAG, "OTA Size: %d", ota_size);

    // Download the new application header
    esp_app_desc_t app_desc;
    memset(&app_desc, 0x0, sizeof(app_desc));
    ret = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (ret != ESP_OK) {
        ota_ui_result(false, "The firmware's header could not be read.", ret);
        ESP_GOTO_ON_ERROR(ret, abort, TAG, "esp_https_ota_get_img_desc failed");
    }

    // We don't need to verify the target app header,
    ESP_LOGI(TAG, "Declared image version: %s, actual image version: %s", ota_manifest.version, app_desc.version);

    // The partition is erased at the first read (several seconds): "Preparing" until data arrives
    if (lvgl_port_lock(OTA_UI_LOCK_WAIT_MS)) {
        show_ota_step("Preparing");
        show_ota_progress(0, ota_size);
        lvgl_port_unlock();
    }
    bool downloading = false;

    // esp_https_ota_perform returns after every read: ESP_ERR_HTTPS_OTA_IN_PROGRESS while data is
    // still coming, ESP_OK once the whole image is read, any other code on a failure. Reads follow each other
    // with a 1 ms pause (a 20 ms pause after each 512 B read held the download to 28 KB/s), which still lets
    // the lower-priority tasks run (the screen, the idle tasks' watchdog) when data keeps coming. The screen
    // shows the progress a few times a second
    int64_t shown_us = 0;
    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Progress only when the screen is free: a missed step is shown by the next
        int64_t now_us = esp_timer_get_time();
        if (now_us - shown_us >= OTA_PROGRESS_PERIOD_MS * 1000 && lvgl_port_lock(0)) {
            int read = esp_https_ota_get_image_len_read(https_ota_handle);
            if (!downloading && read > 0) {
                show_ota_step("Downloading");
                downloading = true;
            }
            show_ota_progress(read, ota_size);
            lvgl_port_unlock();
            shown_us = now_us;
        }
        vTaskDelay(1);
    }

    if (ret != ESP_OK || !esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ret = (ret == ESP_OK) ? ESP_FAIL : ret;
        ota_ui_result(false, "The download stopped.", ret);
        ESP_GOTO_ON_ERROR(ret, abort, TAG, "esp_https_ota_perform failed");
    }

    // The whole amount read (the last progress may have been skipped, shown only a few times a second)
    if (lvgl_port_lock(OTA_UI_LOCK_WAIT_MS)) {
        show_ota_progress(esp_https_ota_get_image_len_read(https_ota_handle), ota_size);
        show_ota_step("Checking");
        lvgl_port_unlock();
    }

    // Validates the image and frees the handle, whether it succeeds or not
    ret = esp_https_ota_finish(https_ota_handle);
    https_ota_handle = NULL;
    if (ret != ESP_OK) {
        ota_ui_result(false, "The firmware did not pass its check.", ret);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "esp_https_ota_finish failed");
    }

    // Set the next boot partition
    ret = esp_ota_set_boot_partition(ota_partition);
    if (ret != ESP_OK) {
        ota_ui_result(false, "The new firmware could not be selected.", ret);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to set boot partition");
    }
    ota_ui_result(true, NULL, ESP_OK);
    goto finally;

abort:
    // A failure after esp_https_ota_begin: close the connection, free the handle and drop the partial image
    // (ADR-005 B14)
    esp_https_ota_abort(https_ota_handle);

finally:
    ESP_LOGI(TAG, "OTA Complete: %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "ota_updater stack: %u of %u bytes never used", (unsigned) uxTaskGetStackHighWaterMark(NULL),
             (unsigned) OTA_UPDATE_TASK_STACK);
    vTaskDelete(NULL);   // safely remove this task
}

static void on_ota_cancel_button_pressed(lv_event_t *e) {
    ESP_LOGI(TAG, "Cancel OTA");
    set_ota_prompt_view_visibility(false);
}

static void on_ota_accept_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Install pressed");
    set_ota_prompt_view_visibility(false);

    // Shift to OTA view
    last_tile = main_tileview_jump_to(tile_ota_mode_view);

    xEventGroupSetBits(ota_event_group, OTA_EVENT_USER_CONFIRMED);
}

void on_reboot_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}


static lv_obj_t * create_black_screen(lv_obj_t * parent) {
    lv_obj_t * screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, OTA_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(screen, OTA_GAP_PX, LV_PART_MAIN);
    lv_obj_set_scrollable(screen, false);
    return screen;
}


static lv_obj_t * create_wrapped_label(lv_obj_t * parent, const lv_font_t * font) {
    lv_obj_t * label = value_editor_create_label(parent, font);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    return label;
}


// The OTA tile: the step and progress in the middle; Restart, once it ends, along the bottom in portrait and in a
// column on the right in landscape (the screen is too low for both); the middle takes the whole width until then
static void layout_ota_view(lv_obj_t * view) {
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {LV_GRID_FR(1), OTA_BUTTON_PORTRAIT_PX, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(1), OTA_SIDE_BUTTON_PX, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t * middle = lv_obj_get_child(view, 0);
    bool done = !lv_obj_is_hidden(reboot_button);
    if (is_portrait(view)) {
        lv_obj_set_grid_dsc_array(view, portrait_cols, portrait_rows);
        lv_obj_set_grid_cell(middle, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(reboot_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
    else {
        lv_obj_set_grid_dsc_array(view, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(middle, LV_GRID_ALIGN_STRETCH, 0, done ? 1 : 2, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(reboot_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    }
}


static void ota_view_size_changed_cb(lv_event_t * e) {
    layout_ota_view(lv_event_get_target_obj(e));
}


void create_ota_mode_view(lv_obj_t * parent) {
    create_ota_prompt_view(lv_screen_active());

    lv_obj_t * view = create_black_screen(parent);
    ota_view = view;
    lv_obj_add_event_cb(view, ota_view_size_changed_cb, LV_EVENT_SIZE_CHANGED, NULL);

    lv_obj_t * middle = lv_obj_create(view);
    lv_obj_remove_style_all(middle);
    lv_obj_set_height(middle, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(middle, OTA_MIDDLE_GAP_PX, LV_PART_MAIN);
    lv_obj_set_scrollable(middle, false);

    ota_title_label = create_wrapped_label(middle, &lv_font_montserrat_28);
    lv_obj_set_style_text_align(ota_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_title_label, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_label_set_text(ota_title_label, "Update");

    progress_bar = lv_bar_create(middle);
    lv_obj_set_size(progress_bar, lv_pct(100), OTA_BAR_PX);
    lv_obj_set_style_radius(progress_bar, OTA_BAR_PX / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, OTA_BAR_PX / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    lv_bar_set_range(progress_bar, 0, 100);

    progress_label = create_wrapped_label(middle, &lv_font_montserrat_24);
    lv_obj_set_style_text_align(progress_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(progress_label, "");

    ota_detail_label = create_wrapped_label(middle, &lv_font_montserrat_20);
    lv_obj_set_style_text_align(ota_detail_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_hidden(ota_detail_label, true);

    // Restart, once the update ended either way
    reboot_button = value_editor_create_button(view, LV_PALETTE_ORANGE);
    lv_obj_set_style_bg_color(reboot_button, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(reboot_button, &lv_font_montserrat_32), "Restart");
    lv_obj_add_event_cb(reboot_button, on_reboot_button_pressed, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_set_hidden(reboot_button, true);

    layout_ota_view(view);

    // Create the task to poll for OTA update
    BaseType_t rtos_return;
    mem_diag_log_checkpoint("ota_poller_create begin");
    rtos_return = xTaskCreateWithCaps(
        ota_poller_task,
        "ota_poller",
        OTA_POLLER_TASK_STACK,
        NULL,
        OTA_POLLER_TASK_PRIORITY,
        &ota_poller_task_handle,
        HEAPS_CAPS_ALLOC_DEFAULT_FLAGS
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for ota_poller_task");
        mem_diag_log_checkpoint("ota_poller_create failed");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    mem_diag_log_checkpoint("ota_poller_create end");

    // Create the task to perform OTA update
    mem_diag_log_checkpoint("ota_updater_create begin");
    rtos_return = xTaskCreate(
        ota_update_task,
        "ota_updater",
        OTA_UPDATE_TASK_STACK,
        NULL,
        OTA_UPDATE_TASK_PRIORITY,
        &ota_update_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for ota_update_task");
        mem_diag_log_checkpoint("ota_updater_create failed");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    mem_diag_log_checkpoint("ota_updater_create end");
}

static volatile bool ota_mode_active = false;

bool ota_mode_is_active(void) {
    return ota_mode_active;
}

void enter_ota_mode(bool enable) {
    ESP_LOGI(TAG, "OTA Mode %s", enable ? "enabled" : "disabled");
    ota_mode_active = enable;

    if (enable) {
        prevent_idle_mode_enter(true);
    }
    else {
        prevent_idle_mode_enter(false);
    }
}


// The offer: its text scrolls in the space beside the buttons. Portrait: Install over Cancel under the text;
// landscape: Install over Cancel in a column on the right (the screen is too low for text over buttons)
static void layout_offer(void) {
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {LV_GRID_FR(1), OTA_BUTTON_PORTRAIT_PX, OTA_BUTTON_PORTRAIT_PX,
                                            LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(1), OTA_SIDE_BUTTON_PX, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (is_portrait(ota_prompt_view)) {
        lv_obj_set_grid_dsc_array(ota_prompt_view, portrait_cols, portrait_rows);
        lv_obj_set_grid_cell(offer_text, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(offer_install_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(offer_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    }
    else {
        lv_obj_set_grid_dsc_array(ota_prompt_view, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(offer_text, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
        lv_obj_set_grid_cell(offer_install_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(offer_cancel_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
}


static void offer_size_changed_cb(lv_event_t * e) {
    layout_offer();
}


void create_ota_prompt_view(lv_obj_t * parent) {
    ota_prompt_view = create_black_screen(parent);
    lv_obj_set_clickable(ota_prompt_view, true);       // nothing behind it takes a tap
    lv_obj_add_event_cb(ota_prompt_view, offer_size_changed_cb, LV_EVENT_SIZE_CHANGED, NULL);

    // The title, the version and the release note; scrolls when the note is long
    offer_text = lv_obj_create(ota_prompt_view);
    lv_obj_remove_style_all(offer_text);
    lv_obj_set_flex_flow(offer_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(offer_text, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(offer_text, 12, LV_PART_MAIN);
    lv_obj_set_scroll_dir(offer_text, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(offer_text, LV_SCROLLBAR_MODE_AUTO);      // as the settings' lists
    lv_obj_set_style_width(offer_text, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(offer_text, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(offer_text, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(offer_text, LV_OPA_COVER, LV_PART_SCROLLBAR);

    offer_title_label = create_wrapped_label(offer_text, &lv_font_montserrat_28);
    lv_obj_set_style_text_color(offer_title_label, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_label_set_text(offer_title_label, "Update available");

    ota_description_label = create_wrapped_label(offer_text, &lv_font_montserrat_20);
    lv_label_set_text(ota_description_label, "");

    offer_install_button = value_editor_create_button(ota_prompt_view, LV_PALETTE_ORANGE);
    lv_obj_set_style_bg_color(offer_install_button, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(offer_install_button, &lv_font_montserrat_32), "Install");
    lv_obj_add_event_cb(offer_install_button, on_ota_accept_button_pressed, LV_EVENT_SHORT_CLICKED, NULL);

    offer_cancel_button = value_editor_create_button(ota_prompt_view, LV_PALETTE_GREY);
    lv_label_set_text(value_editor_create_label(offer_cancel_button, &lv_font_montserrat_32), "Cancel");
    lv_obj_add_event_cb(offer_cancel_button, on_ota_cancel_button_pressed, LV_EVENT_SHORT_CLICKED, NULL);

    layout_offer();
    set_ota_prompt_view_visibility(false);
}


/*
----------------------------------------------------
Test hooks for the remote control: the offer and the OTA tile's states with mock data, without a server
----------------------------------------------------
*/

void ota_mode_test_mock(bool critical) {
    memset(&ota_manifest, 0, sizeof(ota_manifest));
    ota_manifest.manifest_version = 1;
    snprintf(ota_manifest.version, sizeof(ota_manifest.version), "v0.0.2");
    snprintf(ota_manifest.path, sizeof(ota_manifest.path), "/p1/mock/firmware.bin");
    snprintf(ota_manifest.note, sizeof(ota_manifest.note),
             "Mock update for checking the screens. Settings pages in the new design; the level's dope band "
             "goes round the card; the power button's menu. Installing it fails: there is no such server.");
    ota_manifest.host = "mock.invalid";
    ota_manifest.port = OTA_MANIFEST_PORT;
    ota_manifest.importance = critical ? OTA_IMPORTANCE_CRITICAL : OTA_IMPORTANCE_NORMAL;
    ota_manifest.initialized = true;
    offer_update();
}


void ota_mode_test_offer(bool show) {
    set_ota_prompt_view_visibility(show);
}


// The offer's note scrolled to its end (a long note)
void ota_mode_test_offer_end(void) {
    lv_obj_update_layout(offer_text);
    lv_obj_scroll_to_y(offer_text, lv_obj_get_scroll_y(offer_text) + lv_obj_get_scroll_bottom(offer_text), LV_ANIM_OFF);
}


// The OTA tile in one state: "connecting", "downloading" (mock progress), "done" or "failed". Shows the tile
// without starting the update task (a tap on its Restart would restart the unit)
bool ota_mode_test_view(const char * state) {
    lv_obj_set_style_text_color(ota_title_label, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_hidden(ota_detail_label, true);
    lv_obj_set_hidden(reboot_button, true);
    layout_ota_view(ota_view);
    lv_label_set_text(progress_label, "");
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    if (strcmp(state, "connecting") == 0) {
        show_ota_step("Connecting");
    }
    else if (strcmp(state, "downloading") == 0) {
        show_ota_step("Downloading");
        show_ota_progress(1260 * 1024, 2372 * 1024);
    }
    else if (strcmp(state, "done") == 0) {
        show_ota_progress(2372 * 1024, 2372 * 1024);
        show_ota_result(true, NULL, ESP_OK);
    }
    else if (strcmp(state, "failed") == 0) {
        show_ota_progress(1260 * 1024, 2372 * 1024);
        show_ota_result(false, "The download stopped.", ESP_ERR_HTTP_EAGAIN);
    }
    else {
        return false;
    }
    return true;
}


static void open_update_prompt(void * ctx) {
    set_ota_prompt_view_visibility(true);
}


static bool update_row_shown(void) {
    return update_found;
}


// "Update available" at the top of the settings hub, in orange, shown once a newer firmware is found (ADR-008). No
// version under it: in portrait the label takes two lines of the row (the offer shows the version)
lv_obj_t * create_settings_update_row(settings_page_t * hub) {
    menu_ota_upgrade_button = settings_row_action(hub, LV_SYMBOL_DOWNLOAD, "Update available", NULL, NULL,
                                                  open_update_prompt, NULL, NULL, false);
    settings_row_highlight(hub);
    settings_row_shown_when(hub, update_row_shown);
    return menu_ota_upgrade_button;
}

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "wifi.h"
#include "wifi_provision.h"
#include "common.h"
#include "app_cfg.h"
#include "mem_diag.h"

#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_check.h"
#include "mdns.h"

#define TAG "WiFi"
#define NVS_NAMESPACE "WIFI"

// How long low power waits for the radio to stop or start (it waits so it can sleep right after)
#define WIFI_SUSPEND_TIMEOUT_MS 5000
#define WIFI_REQUEST_QUEUE_LENGTH 16


ESP_EVENT_DEFINE_BASE(WIFI_SERVICE_EVENT);

// Everything that changes Wi-Fi is a request to the service task, so one task owns the state
typedef enum {
    WIFI_REQ_BRING_UP,              // first request: start provisioning or the station
    WIFI_REQ_ENABLE,                // the user's setting changed (flag)
    WIFI_REQ_SUSPEND,               // idle or sleep (flag); the caller waits for wifi_suspend_done
    WIFI_REQ_RESET_PROVISIONING,
    WIFI_REQ_EXPIRE,                // the expiry timer fired (or `wifi expire`)
    WIFI_REQ_DROP,                  // debug: `wifi drop`
    WIFI_REQ_SYS_EVENT,             // an ESP-IDF Wi-Fi, IP or provisioning event, forwarded
} wifi_req_type_e;

typedef struct {
    wifi_req_type_e type;
    bool flag;
    esp_event_base_t base;
    int32_t id;
    uint8_t reason;                 // WIFI_EVENT_STA_DISCONNECTED
    wifi_suspend_e suspend_mode;    // WIFI_REQ_SUSPEND
} wifi_req_t;


EventGroupHandle_t wireless_event_group = NULL;
static QueueHandle_t wifi_request_queue = NULL;
static SemaphoreHandle_t wifi_suspend_done = NULL;
static TimerHandle_t wifi_expiry_timeout_timer = NULL;
static TaskHandle_t wifi_service_task_handle = NULL;

// Written by the service task only; read by others through wifi_get_status()
static portMUX_TYPE wifi_status_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_status_t wifi_status = {
    .state = WIRELESS_STATE_NOT_PROVISIONED,
};

// Service task only
static wifi_suspend_e wifi_suspend_mode = WIFI_SUSPEND_NONE;
static bool wifi_radio_on = false;      // esp_wifi_start() done and not stopped since
static bool wifi_drop_simulated = false;
static bool wifi_prov_mgr_active = false;  // network_prov_mgr_init() done and not de-initialised since
static bool wifi_provisioning_started = false;  // SoftAP provisioning running: started, not yet ended


HEAPS_CAPS_ATTR wifi_user_config_t wifi_user_config;
const wifi_user_config_t default_wifi_user_config = {
    .wifi_enable = true,
    .wifi_expiry_timeout_s = 600,  // 10 minutes
};


static inline esp_err_t create_wireless_event_group() {
    // If not created, then create the event group. This function may be called before the `wifi_init()`.
    if (wireless_event_group == NULL) {
        wireless_event_group = xEventGroupCreate();
        if (wireless_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create wireless_event_group");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}


esp_err_t save_wifi_user_config() {
    return save_config(NVS_NAMESPACE, &wifi_user_config, sizeof(wifi_user_config));
}


esp_err_t load_wifi_user_config() {
    return load_config(NVS_NAMESPACE, &wifi_user_config, &default_wifi_user_config, sizeof(wifi_user_config));
}


const char * wifi_state_name(wireless_state_e state) {
    switch (state) {
        case WIRELESS_STATE_NOT_PROVISIONED:     return "not provisioned";
        case WIRELESS_STATE_PROVISIONED:         return "provisioned";
        case WIRELESS_STATE_PROVISIONING:        return "provisioning";
        case WIRELESS_STATE_PROVISION_FAILED:    return "provision failed";
        case WIRELESS_STATE_PROVISION_EXPIRE:    return "provision expired";
        case WIRELESS_STATE_NOT_CONNECTED:       return "not connected";
        case WIRELESS_STATE_CONNECTING:          return "connecting";
        case WIRELESS_STATE_CONNECTED:           return "connected";
        case WIRELESS_STATE_DISCONNECTED:        return "disconnected";
        case WIRELESS_STATE_NOT_CONNECTED_EXPIRE: return "connect expired";
        default:                                 return "unknown";
    }
}


wifi_status_t wifi_get_status() {
    portENTER_CRITICAL(&wifi_status_lock);
    wifi_status_t status = wifi_status;
    portEXIT_CRITICAL(&wifi_status_lock);
    return status;
}


// ---- Service task only ------------------------------------------------------------------------------------------

static void wifi_publish() {
    // The UI bridge reads wifi_get_status() when it handles this, so the newest status is always shown
    if (esp_event_post(WIFI_SERVICE_EVENT, WIFI_SERVICE_EVENT_STATUS_CHANGED, NULL, 0, pdMS_TO_TICKS(50)) != ESP_OK) {
        ESP_LOGW(TAG, "Status change not posted (event queue full)");
    }
}

static void wifi_set_state(wireless_state_e state) {
    portENTER_CRITICAL(&wifi_status_lock);
    wifi_status.state = state;
    portEXIT_CRITICAL(&wifi_status_lock);

    ESP_LOGI(TAG, "Status: %s", wifi_state_name(state));
    wifi_publish();
}

static void wifi_set_flags(bool enabled, bool expired, bool provisioned) {
    portENTER_CRITICAL(&wifi_status_lock);
    wifi_status.enabled = enabled;
    wifi_status.expired = expired;
    wifi_status.provisioned = provisioned;
    portEXIT_CRITICAL(&wifi_status_lock);

    wifi_publish();
}


// The Wi-Fi window runs only while the station is not connected. Provisioning's last events (credentials accepted,
// provisioning ended) come after the IP address, and restarting the timer there let a connected unit expire 10
// minutes after provisioning
static void wifi_expiry_watchdog_restart() {
    if (wifi_is_sta_connected()) {
        return;
    }
    xTimerReset(wifi_expiry_timeout_timer, 0);
    ESP_LOGW(TAG, "Restart Wifi expiry timer");
}

static void wifi_expiry_watchdog_start() {
    if (xTimerIsTimerActive(wifi_expiry_timeout_timer) == pdFALSE) {
        xTimerReset(wifi_expiry_timeout_timer, 0);
        ESP_LOGW(TAG, "Start Wifi expiry timer as disconnected from WiFi hotspot");
    }
}

static void wifi_expiry_watchdog_stop() {
    xTimerStop(wifi_expiry_timeout_timer, 0);
    ESP_LOGI(TAG, "Wifi expiry timer stopped");
}


static esp_err_t start_mdns_service() {
    mem_diag_log_checkpoint("mdns_restart begin");

    // Make sure the previous session is terminated
    mdns_free();
    mem_diag_log_checkpoint("mdns_free end");

    // Initialize mDNS
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
    }
    mem_diag_log_checkpoint("mdns_init end");

    // TODO: Assign mDNS name and service

    return ret;
}


// The radio runs when the user wants Wi-Fi, the window has not expired and the unit is awake, or idle while it
// waits to be provisioned (idle would cut the SoftAP the phone is talking to). One rule for the switch, idle/wake,
// sleep and the expiry, where each used to call esp_wifi_start/stop on its own terms
static void wifi_apply_radio() {
    bool awake_enough = wifi_suspend_mode == WIFI_SUSPEND_NONE ||
                        (wifi_suspend_mode == WIFI_SUSPEND_IDLE && !wifi_status.provisioned);
    bool want = wifi_user_config.wifi_enable && !wifi_status.expired && awake_enough;
    if (want == wifi_radio_on) {
        return;
    }

    esp_err_t ret = want ? esp_wifi_start() : esp_wifi_stop();
    ESP_LOGI(TAG, "%s the radio: %s", want ? "Starting" : "Stopping", esp_err_to_name(ret));
    if (ret == ESP_OK) {
        wifi_radio_on = want;
    }
}


static void wifi_sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);


// The provisioning manager runs while the unit waits for credentials; a provisioning reset starts it again
static esp_err_t wifi_prov_mgr_start() {
    if (wifi_prov_mgr_active) {
        return ESP_OK;
    }

    network_prov_mgr_config_t config = {
        .network_prov_wifi_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
        .scheme = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    esp_err_t ret = network_prov_mgr_init(config);
    if (ret == ESP_OK) {
        wifi_prov_mgr_active = true;
    }
    return ret;
}

static void wifi_prov_mgr_stop() {
    if (wifi_prov_mgr_active) {
        network_prov_mgr_deinit();
        wifi_prov_mgr_active = false;
        wifi_provisioning_started = false;
    }
}


static void wifi_bring_up() {
    ESP_ERROR_CHECK(wifi_prov_mgr_start());

    bool is_provisioned = false;
    ESP_ERROR_CHECK(network_prov_mgr_is_wifi_provisioned(&is_provisioned));
    wifi_set_flags(wifi_user_config.wifi_enable, false, is_provisioned);

    if (!is_provisioned) {
        wifi_set_state(WIRELESS_STATE_NOT_PROVISIONED);
        ESP_ERROR_CHECK(wifi_provision_init());  // starts the radio for SoftAP provisioning
        wifi_provisioning_started = true;
        wifi_radio_on = true;
    }
    else {
        ESP_LOGI(TAG, "Already provisioned, starting WiFi STA");
        wifi_set_state(WIRELESS_STATE_NOT_CONNECTED);
        xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);

        // Already provisioned, we can start the WiFi directly
        wifi_prov_mgr_stop(); // Deinitialize the manager as we don't need it anymore

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        wifi_radio_on = true;
        wifi_set_state(WIRELESS_STATE_CONNECTING);
    }

    // Disable wifi if configured so
    if (!wifi_user_config.wifi_enable) {
        wifi_set_state(WIRELESS_STATE_DISCONNECTED);
        xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);
        wifi_apply_radio();
    }
}


// The window ran out: the radio stops and the stack stays (owner, 2026-11-07: Wi-Fi's memory stays counted, so new
// features cannot lean on memory that only exists after the expiry). Events from the stopping radio are ignored
// while expired (see the service task), so the stop is not taken for a lost connection (ADR-005 B12)
static void wifi_expire() {
    if (wifi_status.expired) {
        return;
    }

    ESP_LOGW(TAG, "Wifi is not active in time. Stopping the radio until Wi-Fi is switched on again");

    wifi_status_t status = wifi_get_status();
    wifi_set_flags(status.enabled, true, status.provisioned);
    xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);
    xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_EXPIRED);
    wifi_expiry_watchdog_stop();
    wifi_apply_radio();

    // A provisioned unit only failed to connect in time; otherwise the provisioning window ran out
    wifi_set_state(status.provisioned ? WIRELESS_STATE_NOT_CONNECTED_EXPIRE : WIRELESS_STATE_PROVISION_EXPIRE);
}


// A new Wi-Fi window after the expiry: the user switched Wi-Fi on, or reset the provisioning
static void wifi_revive() {
    if (!wifi_status.expired) {
        return;
    }

    ESP_LOGI(TAG, "New Wi-Fi window after the expiry");

    wifi_status_t status = wifi_get_status();
    wifi_set_flags(status.enabled, false, status.provisioned);
    xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_EXPIRED);
    wifi_expiry_watchdog_restart();
    wifi_set_state(status.provisioned ? WIRELESS_STATE_CONNECTING : WIRELESS_STATE_PROVISIONING);
    wifi_apply_radio();
}


// Forget the credentials and start provisioning again, without a reboot: the radio stops, the credentials are
// erased, the provisioning manager (de-initialised once the unit was provisioned) starts again with a new proof of
// possession, and a new Wi-Fi window starts (also after an expiry)
static esp_err_t wifi_reprovision() {
    wifi_status_t status = wifi_get_status();

    // Already waiting for credentials: nothing to erase and provisioning runs; only a new window (also after an
    // expiry). Stopping the radio and starting provisioning again here left the radio off ("already started")
    if (!status.provisioned && wifi_provisioning_started) {
        ESP_LOGI(TAG, "Already waiting for credentials: new Wi-Fi window");
        wifi_set_flags(status.enabled, false, false);
        xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_EXPIRED);
        wifi_expiry_watchdog_restart();
        wifi_set_state(WIRELESS_STATE_PROVISIONING);
        wifi_apply_radio();
        return ESP_OK;
    }

    if (wifi_radio_on) {
        esp_wifi_stop();
        wifi_radio_on = false;
    }

    esp_err_t ret = wifi_provision_reset();
    if (ret != ESP_OK) {
        wifi_apply_radio();
        return ret;
    }
    ESP_LOGI(TAG, "Credentials erased, provisioning again");

    xEventGroupClearBits(wireless_event_group,
                         WIRELESS_STATEFUL_IS_PROVISIONED | WIRELESS_STATEFUL_IS_STA_CONNECTED | WIRELESS_STATEFUL_IS_EXPIRED);
    wifi_set_flags(status.enabled, false, false);
    wifi_set_state(WIRELESS_STATE_NOT_PROVISIONED);
    wifi_expiry_watchdog_restart();

    ret = wifi_prov_mgr_start();
    if (ret == ESP_OK) {
        ret = wifi_provision_init();  // starts the radio for SoftAP provisioning
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning again: %s", esp_err_to_name(ret));
        wifi_apply_radio();
        return ret;
    }
    wifi_provisioning_started = true;
    wifi_radio_on = true;

    // Wi-Fi switched off: provisioning waits until it is switched on
    wifi_apply_radio();
    return ESP_OK;
}


static void wifi_handle_sys_event(const wifi_req_t *req) {
    if (req->base == WIFI_EVENT) {
        switch (req->id) {
            case WIFI_EVENT_STA_START: {
                // Before credentials exist the provisioning manager drives the station (it connects once it
                // receives them); connecting here then only failed with ESP_ERR_WIFI_SSID
                if (!wifi_status.provisioned) {
                    break;
                }
                ESP_LOGI(TAG, "Starting STA connection");
                esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect to AP: %s", esp_err_to_name(ret));
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                uint8_t reason = req->reason;
                if (wifi_drop_simulated) {
                    // Test hook (remote control `wifi drop`): handle this one as a lost connection
                    wifi_drop_simulated = false;
                    reason = WIFI_REASON_BEACON_TIMEOUT;
                }
                ESP_LOGI(TAG, "Disconnected, reason %d%s", req->reason,
                         reason != req->reason ? " (handled as a lost connection: wifi drop)" : "");

                wifi_set_state(WIRELESS_STATE_DISCONNECTED);
                xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);

                // The reason tells the unit's own stops (idle, the switch, a reboot: all "station left") from a
                // lost connection or a failed connect attempt (ADR-005 B8)
                if (reason == WIFI_REASON_ASSOC_LEAVE) {
                    break;
                }

                // While provisioning, a failed attempt is the provisioning manager's to retry (it makes up to five)
                if (!wifi_status.provisioned) {
                    break;
                }

                // Connect again. The expiry timer ends the retries if the AP stays away (ADR-005 B8)
                ESP_LOGI(TAG, "Connection lost. Connecting to the AP again...");
                wifi_expiry_watchdog_start();
                esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect to AP: %s", esp_err_to_name(ret));
                }
                break;
            }

            default:
                break;
        }
    }
    else if (req->base == IP_EVENT && req->id == IP_EVENT_STA_GOT_IP) {
        wifi_set_state(WIRELESS_STATE_CONNECTED);

        // Stop the timer
        wifi_expiry_watchdog_stop();

        // Start mDNS service
        start_mdns_service();

        // Update event to unblock other tasks
        xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);
    }
    else if (req->base == NETWORK_PROV_EVENT) {
        wifi_status_t status = wifi_get_status();

        switch (req->id) {
            case NETWORK_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                wifi_set_state(WIRELESS_STATE_PROVISIONING);
                break;

            case NETWORK_PROV_WIFI_CRED_RECV:
                wifi_expiry_watchdog_restart();
                break;

            case NETWORK_PROV_WIFI_CRED_FAIL:
                wifi_set_state(WIRELESS_STATE_PROVISION_FAILED);
                xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);
                wifi_expiry_watchdog_restart();
                break;

            case NETWORK_PROV_WIFI_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                wifi_expiry_watchdog_restart();
                wifi_set_flags(status.enabled, status.expired, true);
                break;

            case NETWORK_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                wifi_provisioning_started = false;
                // Provisioned is a flag (set at the credentials' success); the state is the link's, and the station
                // is usually connected by now
                if (!wifi_is_sta_connected()) {
                    wifi_set_state(WIRELESS_STATE_PROVISIONED);
                }
                wifi_expiry_watchdog_restart();
                xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);
                break;

            default:
                break;
        }
    }
}


static void wifi_service_task(void *p) {
    wifi_req_t req;

    while (1) {
        if (xQueueReceive(wifi_request_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (req.type) {
            case WIFI_REQ_BRING_UP:
                wifi_bring_up();
                break;

            case WIFI_REQ_ENABLE: {
                // wifi_user_config.wifi_enable is already set by wifi_request_enable()
                ESP_LOGI(TAG, "Wi-Fi setting: %s", req.flag ? "on" : "off");
                wifi_status_t status = wifi_get_status();
                wifi_set_flags(req.flag, status.expired, status.provisioned);
                if (req.flag && status.expired) {
                    wifi_revive();
                }
                else {
                    wifi_apply_radio();
                }
                break;
            }

            case WIFI_REQ_SUSPEND:
                ESP_LOGI(TAG, "Power state %d at state: %s", req.suspend_mode, wifi_state_name(wifi_status.state));
                wifi_suspend_mode = req.suspend_mode;
                wifi_apply_radio();
                xSemaphoreGive(wifi_suspend_done);
                break;

            case WIFI_REQ_RESET_PROVISIONING: {
                esp_err_t ret = wifi_reprovision();
                esp_event_post(WIFI_SERVICE_EVENT, WIFI_SERVICE_EVENT_PROVISION_RESET, &ret, sizeof(ret), pdMS_TO_TICKS(50));
                break;
            }

            case WIFI_REQ_EXPIRE:
                wifi_expire();
                break;

            case WIFI_REQ_DROP:
                if (!wifi_status.expired) {
                    wifi_drop_simulated = true;
                    esp_wifi_disconnect();
                }
                break;

            case WIFI_REQ_SYS_EVENT:
                // While expired the radio is off on purpose: its stopping events change nothing (ADR-005 B12)
                if (!wifi_status.expired) {
                    wifi_handle_sys_event(&req);
                }
                break;
        }
    }
}


// ---- Any task ---------------------------------------------------------------------------------------------------

static bool wifi_send_request(const wifi_req_t *req, TickType_t wait) {
    if (wifi_request_queue == NULL) {
        ESP_LOGW(TAG, "Request %d before wifi_init(), ignored", req->type);
        return false;
    }
    if (xQueueSend(wifi_request_queue, req, wait) != pdTRUE) {
        ESP_LOGE(TAG, "Request queue full, request %d dropped", req->type);
        return false;
    }
    return true;
}


// System event task: log what needs the event data, forward the rest to the service task
static void wifi_sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    wifi_req_t req = {
        .type = WIFI_REQ_SYS_EVENT,
        .base = event_base,
        .id = event_id,
    };

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                req.reason = ((wifi_event_sta_disconnected_t *) event_data)->reason;
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "SoftAP transport: Connected");
                return;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "SoftAP transport: Disconnected");
                return;
            default:
                return;
        }
    }
    else if (event_base == IP_EVENT) {
        if (event_id != IP_EVENT_STA_GOT_IP) {
            return;
        }
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
    }
    else if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
            case NETWORK_PROV_WIFI_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *) event_data;
                // SSID only: the console is readable by anyone with a cable (ADR-005 B11)
                ESP_LOGI(TAG, "Received WiFi credentials for SSID %s", (const char *) wifi_sta_cfg->ssid);
                break;
            }
            case NETWORK_PROV_WIFI_CRED_FAIL: {
                network_prov_wifi_sta_fail_reason_t *reason = (network_prov_wifi_sta_fail_reason_t *) event_data;
                ESP_LOGE(TAG, "Provisioning failed! Reason: %s. Please reset to factory and retry provisioning",
                         (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                break;
            }
            case NETWORK_PROV_START:
            case NETWORK_PROV_WIFI_CRED_SUCCESS:
            case NETWORK_PROV_END:
                break;
            default:
                return;
        }
    }
    else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(TAG, "Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
                break;
            default:
                break;
        }
        return;
    }
    else {
        return;
    }

    wifi_send_request(&req, pdMS_TO_TICKS(100));
}


static void wifi_expiry_timeout_cb(TimerHandle_t timer) {
    // Timer service task: hand the expiry to the service task
    wifi_req_t req = { .type = WIFI_REQ_EXPIRE };
    wifi_send_request(&req, 0);
}


esp_err_t wifi_init() {
    ESP_ERROR_CHECK(create_wireless_event_group());

    // The service owns its settings (the settings page used to load them, which tied Wi-Fi to the UI)
    ESP_ERROR_CHECK(load_wifi_user_config());

    // Generate service name based on MAC address. Read it from eFuse: esp_wifi_get_mac() needs
    // esp_wifi_init(), which runs further down, and left the buffer unset (ADR-005 B9)
    uint8_t eth_mac[6];
    ESP_ERROR_CHECK(esp_read_mac(eth_mac, ESP_MAC_WIFI_STA));
    snprintf(wifi_provision_state.service_name, sizeof(wifi_provision_state.service_name), "OWMC_%02X%02X%02X", eth_mac[3], eth_mac[4], eth_mac[5]);
    ESP_LOGI(TAG, "Service name and hostname: %s (STA MAC " MACSTR ")", wifi_provision_state.service_name, MAC2STR(eth_mac));

    wifi_request_queue = xQueueCreate(WIFI_REQUEST_QUEUE_LENGTH, sizeof(wifi_req_t));
    wifi_suspend_done = xSemaphoreCreateBinary();
    if (wifi_request_queue == NULL || wifi_suspend_done == NULL) {
        ESP_LOGE(TAG, "Failed to create the Wi-Fi request queue");
        return ESP_ERR_NO_MEM;
    }

    // Initialize WiFi stack
    ESP_ERROR_CHECK(esp_netif_init());
    // Register Wifi events
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_sys_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_sys_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sys_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sys_event_handler, NULL));

    // Initialize Wifi including netif with default configuration
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, wifi_provision_state.service_name));

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));


    // Setup a watchdog timer to check provisioning status after the configured time
    wifi_expiry_timeout_timer = xTimerCreate(
        "expiry_timer",
        pdMS_TO_TICKS(wifi_user_config.wifi_expiry_timeout_s * 1000),
        pdFALSE,
        NULL,
        wifi_expiry_timeout_cb);
    if (wifi_expiry_timeout_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi_expiry_timeout_timer");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    ESP_LOGI(TAG, "Expiry timer: %" PRIu32 "s", wifi_user_config.wifi_expiry_timeout_s);

    // Start the timer
    xTimerStart(wifi_expiry_timeout_timer, 0);

    // Internal RAM stack: the task calls esp_wifi_* and the provisioning reset, which write NVS (flash)
    BaseType_t rtos_return = xTaskCreate(
        wifi_service_task,
        "wifi_service",
        WIFI_SERVICE_TASK_STACK,
        NULL,
        WIFI_SERVICE_TASK_PRIORITY,
        &wifi_service_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi_service task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // The first request: provisioning or the station starts in the service task, like every later change
    wifi_req_t req = { .type = WIFI_REQ_BRING_UP };
    wifi_send_request(&req, portMAX_DELAY);

    return ESP_OK;
}


void wifi_request_enable(bool enable) {
    // The setting at once (so a save right after keeps it); the radio follows in the service task
    wifi_user_config.wifi_enable = enable;

    wifi_req_t req = { .type = WIFI_REQ_ENABLE, .flag = enable };
    wifi_send_request(&req, 0);
}


esp_err_t wifi_request_suspend(wifi_suspend_e mode) {
    if (wifi_request_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // One caller (the low power task), so one completion semaphore; drop a late completion of a timed-out call
    xSemaphoreTake(wifi_suspend_done, 0);

    wifi_req_t req = { .type = WIFI_REQ_SUSPEND, .suspend_mode = mode };
    if (!wifi_send_request(&req, pdMS_TO_TICKS(WIFI_SUSPEND_TIMEOUT_MS))) {
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(wifi_suspend_done, pdMS_TO_TICKS(WIFI_SUSPEND_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Power state %d not applied within %d ms", mode, WIFI_SUSPEND_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}


void wifi_request_reset_provisioning() {
    wifi_req_t req = { .type = WIFI_REQ_RESET_PROVISIONING };
    wifi_send_request(&req, 0);
}


void wifi_drop_now() {
    ESP_LOGW(TAG, "Wifi drop forced");
    wifi_req_t req = { .type = WIFI_REQ_DROP };
    wifi_send_request(&req, 0);
}


void wifi_expire_now() {
    ESP_LOGW(TAG, "Wifi expiry forced");
    wifi_req_t req = { .type = WIFI_REQ_EXPIRE };
    wifi_send_request(&req, 0);
}


esp_err_t wifi_wait_for_provision(uint32_t block_wait_ms) {
    create_wireless_event_group();

    // Wait for provision event
    EventBits_t asserted_bits = xEventGroupWaitBits(
        wireless_event_group,
        WIRELESS_STATEFUL_IS_PROVISIONED,
        pdFALSE,        // don't clear on assert -> Calling the same function when provisioned will return immediately
        pdTRUE,         // wait for all bits to assert
        pdMS_TO_TICKS(block_wait_ms)
    );

    if (asserted_bits & WIRELESS_STATEFUL_IS_PROVISIONED) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}


bool wifi_is_provisioned() {
    create_wireless_event_group();
    EventBits_t asserted_bits = xEventGroupGetBits(wireless_event_group);

    return asserted_bits & WIRELESS_STATEFUL_IS_PROVISIONED;
}


esp_err_t wifi_wait_for_sta_connected(uint32_t block_wait_ms) {
    create_wireless_event_group();

    // Wait for connect event
    EventBits_t asserted_bits = xEventGroupWaitBits(
        wireless_event_group,
        WIRELESS_STATEFUL_IS_STA_CONNECTED,
        pdFALSE,        // don't clear on assert
        pdTRUE,         // wait for all bits to assert
        pdMS_TO_TICKS(block_wait_ms)
    );

    if (asserted_bits & WIRELESS_STATEFUL_IS_STA_CONNECTED) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

bool wifi_is_sta_connected() {
    create_wireless_event_group();
    EventBits_t asserted_bits = xEventGroupGetBits(wireless_event_group);

    return asserted_bits & WIRELESS_STATEFUL_IS_STA_CONNECTED;
}


esp_err_t wifi_wait_for_expire(uint32_t block_wait_ms) {
    create_wireless_event_group();

    // Wait for expire event
    EventBits_t asserted_bits = xEventGroupWaitBits(
        wireless_event_group,
        WIRELESS_STATEFUL_IS_EXPIRED,
        pdFALSE,        // don't clear on assert
        pdTRUE,         // wait for all bits to assert
        pdMS_TO_TICKS(block_wait_ms)
    );

    if (asserted_bits & WIRELESS_STATEFUL_IS_EXPIRED) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}


bool wifi_is_expired() {
    create_wireless_event_group();
    EventBits_t asserted_bits = xEventGroupGetBits(wireless_event_group);

    return asserted_bits & WIRELESS_STATEFUL_IS_EXPIRED;
}

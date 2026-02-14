#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "wifi.h"
#include "wifi_provision.h"
#include "common.h"
#include "wifi_config.h"
#include "app_cfg.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_check.h"
#include "esp_task_wdt.h"
#include "mdns.h"


#include "config_view.h"

#define TAG "WiFi"


extern wireless_provision_state_t wifi_provision_state;
EventGroupHandle_t wireless_event_group = NULL;
static TaskHandle_t wifi_deinit_task_handle;
wireless_state_e wireless_state;
TimerHandle_t wifi_expiry_timeout_timer;


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


static esp_err_t start_mdns_service() {
    // Make sure the previous session is terminated
    mdns_free();

    // Initialize mDNS
    esp_err_t ret = mdns_init();

    // TODO: Assign mDNS name and service

    return ret;
}


void wifi_deinit_task(void *p) {
    // Create event if not created already
    ESP_ERROR_CHECK(create_wireless_event_group());

    // // Disable task watchdog
    // esp_task_wdt_delete(NULL);

    // Block waiting for wifi to be expired
    while (wifi_wait_for_expire(2000) != ESP_OK)
    {
        continue;
    }

    wireless_state = WIRELESS_STATE_PROVISION_EXPIRE;
    status_bar_update_wireless_state(wireless_state);

    // Update the provision qr
    wifi_config_disable_provision_interface(WIRELESS_STATE_PROVISION_EXPIRE);

    // De-initialize wifi 
    ESP_LOGW(TAG, "Wifi is not active in time. Will disable WiFI completely");

    // Stop provisioning manager
    wifi_prov_mgr_deinit();

    // Stop wifi and free resource
    esp_wifi_stop();
    esp_wifi_deinit();

    // Deinitialize network stack
    esp_netif_deinit();

    ESP_LOGI(TAG, "Wi-Fi stack deinitialized, stopping wifi_state_poller_task");

    // Update GUI
    wifi_config_disable_enable_interface();

    vTaskDelete(NULL);   // safely remove this task
}


void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        wifi_provision_event_handler(arg, event_id, event_data);
    }
    else if (event_base == WIFI_EVENT) {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
            {
                ESP_LOGI(TAG, "Starting STA connection");

                esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to connect to AP: %s", esp_err_to_name(ret));
                }
                break;
            }
                

            case WIFI_EVENT_STA_DISCONNECTED:
            {
                ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
                wireless_state = WIRELESS_STATE_DISCONNECTED;
                status_bar_update_wireless_state(wireless_state);

                // Update event too
                xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);

                // Start the expiry timer after disconnected from Wifi (exclude the intentional state change)
                if (wifi_user_config.wifi_enable) {
                    wifi_expiry_watchdog_start();
                    
                    esp_err_t ret = esp_wifi_start();
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
                    }
                }

                break;
            }
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "SoftAP transport: Connected");
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "SoftAP transport: Disconnected");
                break;

            default:
                break;
        }
    }
    else if (event_base == IP_EVENT) {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
                wireless_state = WIRELESS_STATE_CONNECTED;
                status_bar_update_wireless_state(wireless_state);

                // Stop the timer
                wifi_expiry_watchdog_stop();

                // Start mDNS service
                start_mdns_service();

                // Update event to unblock other tasks
                xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);

                break;
            }
            default:
                break;
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
    }
}


static void wifi_provision_timeout_cb(TimerHandle_t timer) {
    // Trigger expiry event
    xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_EXPIRED);
}


esp_err_t wifi_init() {
    // Create event group if not previously created
    ESP_ERROR_CHECK(create_wireless_event_group());
    // Set initial state
    wireless_state = WIRELESS_STATE_NOT_PROVISIONED;
    status_bar_update_wireless_state(wireless_state);

    // Generate service name based on MAC address
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(wifi_provision_state.service_name, sizeof(wifi_provision_state.service_name), "OWMC_%02X%02X%02X", eth_mac[3], eth_mac[4], eth_mac[5]);

    // Initialize WiFi stack
    ESP_ERROR_CHECK(esp_netif_init());
    // Register Wifi events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Initialize Wifi including netif with default configuration
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, wifi_provision_state.service_name));

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure the provision manager
    wifi_prov_mgr_config_t config = {
        .wifi_prov_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));


    // Setup a watchdog timer to check provisioning status after the configured time
    wifi_expiry_timeout_timer = xTimerCreate(
        "expiry_timer", 
        pdMS_TO_TICKS(wifi_user_config.wifi_expiry_timeout_s * 1000),
        pdFALSE, 
        NULL,
        wifi_provision_timeout_cb);
    if (wifi_expiry_timeout_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi_provision_timeout_timer");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    ESP_LOGI(TAG, "Expiry timer: %ds", wifi_user_config.wifi_expiry_timeout_s);

    // Start the timer
    xTimerStart(wifi_expiry_timeout_timer, 0);

    // Create a task to handle the wifi deinit event
    BaseType_t rtos_return;
    rtos_return = xTaskCreate(
        wifi_deinit_task, 
        "wifi_deinit",
        WIFI_DEINIT_TASK_STACK,
        NULL, 
        WIFI_DEINIT_TASK_PRIORITY,
        &wifi_deinit_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for wifi_state_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Check if the system is already provisioned
    bool is_provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&is_provisioned));

    if (!is_provisioned) {
        wifi_provision_init();

    } else {
        ESP_LOGI(TAG, "Already provisioned, starting WiFi STA");
        wifi_config_disable_provision_interface(WIRELESS_STATE_PROVISIONED);

        wireless_state = WIRELESS_STATE_NOT_CONNECTED;
        status_bar_update_wireless_state(wireless_state);

        xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);

        // Already provisioned, we can start the WiFi directly
        wifi_prov_mgr_deinit(); // Deinitialize the manager as we don't need it anymore

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());

        wireless_state = WIRELESS_STATE_CONNECTING;
        status_bar_update_wireless_state(wireless_state);
    }

    // Disable wifi if configured so
    if (wireless_state != WIRELESS_STATE_PROVISION_EXPIRE || wireless_state != WIRELESS_STATE_NOT_CONNECTED_EXPIRE) {
        if (!wifi_user_config.wifi_enable) {
            wireless_state = WIRELESS_STATE_DISCONNECTED;
            status_bar_update_wireless_state(wireless_state);
            xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);

            esp_wifi_stop();
        }
    }

    return ESP_OK;
}

esp_err_t wifi_request_start() {
    ESP_LOGI(TAG, "Starting WiFi control at state: %d", wireless_state);
    if (!wifi_is_expired() && wifi_user_config.wifi_enable) {
        ESP_LOGI(TAG, "Requesting esp_wifi_start");
        return esp_wifi_start();
    }
    
    ESP_LOGI(TAG, "WiFi is disabled or expired. Start request ignored.");
    return ESP_ERR_INVALID_STATE;
}


esp_err_t wifi_request_stop() {
    ESP_LOGI(TAG, "Stopping WiFi control at state: %d", wireless_state);
    if (!wifi_is_expired() && wifi_user_config.wifi_enable) {
        ESP_LOGI(TAG, "Requesting esp_wifi_stop");
        return esp_wifi_stop();
    }

    ESP_LOGI(TAG, "WiFi is disabled or expired. Stop request ignored.");
    return ESP_ERR_INVALID_STATE;
}


void wifi_expiry_watchdog_restart() {
    xTimerReset(wifi_expiry_timeout_timer, 0);
    ESP_LOGW(TAG, "Restart Wifi expiry timer as disconnected from WiFi hotspot");
}

void wifi_expiry_watchdog_start() {
    if (xTimerIsTimerActive(wifi_expiry_timeout_timer) == pdFALSE) {
        xTimerReset(wifi_expiry_timeout_timer, 0);
        ESP_LOGW(TAG, "Start Wifi expiry timer as disconnected from WiFi hotspot");
    }
}

void wifi_expiry_watchdog_stop() {
    xTimerStop(wifi_expiry_timeout_timer, 0);
    ESP_LOGI(TAG, "Wifi expiry timer stopped");
}


wireless_state_e get_wireless_state() {
    return wireless_state;
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
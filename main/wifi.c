#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "wifi.h"
#include "wifi_provision.h"
#include "common.h"
#include "wifi_config.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_random.h"
#include "nvs.h"
#include "esp_crc.h"
#include "esp_check.h"
#include "esp_task_wdt.h"


#include "config_view.h"

#define TAG "WiFi"
#define NVS_NAMESPACE "WIFI"


extern wireless_provision_state_t wifi_provision_state;
EventGroupHandle_t wireless_event_group = NULL;
static TaskHandle_t wifi_deinit_task_handle;
wireless_state_e wireless_state;
TimerHandle_t wifi_expiry_timeout_timer;


wifi_user_config_t wifi_user_config;
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


void wifi_deinit_task(void *p) {
    // Create event if not created already
    ESP_ERROR_CHECK(create_wireless_event_group());

    // Disable task watchdog
    esp_task_wdt_delete(NULL);

    // Wait for expiry event
    xEventGroupWaitBits(
        wireless_event_group, 
        WIRELESS_STATEFUL_IS_EXPIRED, 
        pdTRUE,         // clear on assert
        pdTRUE,         // wait for all bits to assert
        portMAX_DELAY
    );

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
                ESP_LOGI(TAG, "Starting STA connection");

                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
                wireless_state = WIRELESS_STATE_DISCONNECTED;
                status_bar_update_wireless_state(wireless_state);

                // Update event too
                xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);

                // Restart the expiry timer after disconnected from Wifi
                wifi_expiry_watchdog_restart();

                esp_wifi_connect();
                break;

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

                // Update event too
                xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_STA_CONNECTED);

                // Stop the timer
                wifi_expiry_watchdog_stop();

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


esp_err_t save_wifi_user_config() {
    esp_err_t ret;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);

    // Calculate CRC
    wifi_user_config.crc32 = crc32_wrapper(&wifi_user_config, sizeof(wifi_user_config), sizeof(wifi_user_config.crc32));

    // Write to NVS
    ret = nvs_set_blob(handle, "cfg", &wifi_user_config, sizeof(wifi_user_config));
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to write NVS blob");

    ret = nvs_commit(handle);
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to commit NVS changes");

finally:
    nvs_close(handle);

    return ret;
}



esp_err_t load_wifi_user_config() {
    esp_err_t ret;

    // Read configuration from NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);

    size_t required_size = sizeof(wifi_user_config);
    ret = nvs_get_blob(handle, "cfg", &wifi_user_config, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Initialize wifi_user_config with default values");

        // Copy default values
        memcpy(&wifi_user_config, &default_wifi_user_config, sizeof(wifi_user_config));
        wifi_user_config.crc32 = crc32_wrapper(&wifi_user_config, sizeof(wifi_user_config), sizeof(wifi_user_config.crc32));

        // Write to NVS
        ret = nvs_set_blob(handle, "cfg", &wifi_user_config, sizeof(wifi_user_config));
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to write NVS blob");
        ret = nvs_commit(handle);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to commit NVS changes");
    }
    else {
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to read NVS blob");
    }

    // Verify CRC32
    uint32_t crc32 = crc32_wrapper(&wifi_user_config, sizeof(wifi_user_config), sizeof(wifi_user_config.crc32));

    if (crc32 != wifi_user_config.crc32) {
        ESP_LOGW(TAG, "CRC32 mismatch, will use default settings. Expected %p, got %p", wifi_user_config.crc32, crc32);
        memcpy(&wifi_user_config, &default_wifi_user_config, sizeof(wifi_user_config));

        ESP_ERROR_CHECK(save_wifi_user_config());
    }
    else {
        ESP_LOGI(TAG, "wifi_user_config loaded successfully");
    }

finally:
    nvs_close(handle);

    return ret;
}


static void wifi_provision_timeout_cb(TimerHandle_t timer) {
    // Trigger expiry event
    xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_EXPIRED);
}


esp_err_t wifi_init() {
    // Load configuration
    ESP_ERROR_CHECK(load_wifi_user_config());
    
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

        xEventGroupSetBits(wireless_event_group, WIRELESS_STATE_PROVISIONED);

        // Already provisioned, we can start the WiFi directly
        wifi_prov_mgr_deinit(); // Deinitialize the manager as we don't need it anymore

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    return ESP_OK;
}


void wifi_expiry_watchdog_restart() {
    xTimerReset(wifi_expiry_timeout_timer, 0);
    ESP_LOGW(TAG, "Restart Wifi expiry timer as disconnected from WiFi hotspot");
}

void wifi_expiry_watchdog_stop() {
    xTimerStop(wifi_expiry_timeout_timer, 0);
    ESP_LOGI(TAG, "Wifi expiry timer stopped");
}


wireless_state_e get_wireless_state() {
    return wireless_state;
}
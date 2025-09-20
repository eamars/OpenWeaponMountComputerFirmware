#include "freertos/FreeRTOS.h"
// #include "freertos/event_groups.h"

#include "wifi.h"
#include "wifi_provision.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_random.h"

#define TAG "WiFiEvent"


wifi_provision_state_t wifi_provision_state;
static EventGroupHandle_t wifi_event_group = NULL;


void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        wifi_provision_event_handler(arg, event_id, event_data);
    }
    else if (event_base == WIFI_EVENT) {
        switch (event_id)
        {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
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


esp_err_t wifi_init() {
    // Initialize WiFi stack
    ESP_ERROR_CHECK(esp_netif_init());
    // Register Wifi events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Initialize Wifi including netif with default configuration
    esp_netif_create_default_wifi_sta();
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

    // Check if the system is already provisioned
    wifi_provision_state.is_provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&wifi_provision_state.is_provisioned));

    if (!wifi_provision_state.is_provisioned) {
        ESP_LOGI(TAG, "Starting WiFi SoftAP provisioning");

        // Generate service name based on MAC address
        uint8_t eth_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
        snprintf(wifi_provision_state.service_name, sizeof(wifi_provision_state.service_name), "PROV_%02X%02X%02X", eth_mac[3], eth_mac[4], eth_mac[5]);

        // Generate POP randomly
        for (int i = 0; i < sizeof(wifi_provision_state.pop) - 1; i++) {
            wifi_provision_state.pop[i] = '0' + (esp_random() % 10);
        }
        wifi_provision_state.pop[sizeof(wifi_provision_state.pop) - 1] = '\0';

        ESP_LOGI(TAG, "SoftAP SSID: %s, POP: %s", wifi_provision_state.service_name, wifi_provision_state.pop);

        // Start provisioning
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1, 
            (const void *) wifi_provision_state.pop, 
            wifi_provision_state.service_name, 
            NULL));

    } else {
        ESP_LOGI(TAG, "Already provisioned, starting WiFi STA");
        // Already provisioned, we can start the WiFi directly
        wifi_prov_mgr_deinit(); // Deinitialize the manager as we don't need it anymore

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }


    return ESP_OK;
}
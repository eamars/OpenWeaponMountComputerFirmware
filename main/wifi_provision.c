#include "wifi_provision.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"

#include "esp_random.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_check.h"

#include "config_view.h"
#include "wifi.h"
#include "wifi_config.h"


#define TAG "WiFiProvision"

wireless_provision_state_t wifi_provision_state;
extern EventGroupHandle_t wireless_event_group;
extern wifi_user_config_t wifi_user_config;
extern wireless_state_e wireless_state;


void wifi_provision_event_handler(void* arg, int32_t event_id, void* event_data) {
    switch (event_id)
    {
    case NETWORK_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        
        wireless_state = WIRELESS_STATE_PROVISIONING;
        status_bar_update_wireless_state(wireless_state);

        break;
    case NETWORK_PROV_WIFI_CRED_RECV: {
        wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *) event_data;
        // SSID only: the console is readable by anyone with a cable (ADR-005 B11)
        ESP_LOGI(TAG, "Received WiFi credentials for SSID %s", (const char *) wifi_sta_cfg->ssid);

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        break;
    }
    case NETWORK_PROV_WIFI_CRED_FAIL: {
        network_prov_wifi_sta_fail_reason_t *reason = (network_prov_wifi_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                    "\n\tPlease reset to factory and retry provisioning",
                    (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) ?
                    "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
        
        wireless_state = WIRELESS_STATE_PROVISION_FAILED;
        status_bar_update_wireless_state(wireless_state);

        // Update event too
        xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        break;
    }
    case NETWORK_PROV_WIFI_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning successful");

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        wifi_config_disable_provision_interface(WIRELESS_STATE_PROVISIONED);

        break;

    case NETWORK_PROV_END:
        ESP_LOGI(TAG, "Provisioning ended");

        wireless_state = WIRELESS_STATE_PROVISIONED;
        status_bar_update_wireless_state(wireless_state);

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        // Update event too
        xEventGroupSetBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);

        break;

    default:
        break;
    }
}


esp_err_t wifi_provision_reset() {
    esp_err_t ret = network_prov_mgr_reset_wifi_provisioning();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset provisioning: %s", esp_err_to_name(ret));
    }
    return ret;
}


void wifi_provision_init() {
    // Generate POP randomly
    for (int i = 0; i < sizeof(wifi_provision_state.pop) - 1; i++) {
        wifi_provision_state.pop[i] = '0' + (esp_random() % 10);
    }
    wifi_provision_state.pop[sizeof(wifi_provision_state.pop) - 1] = '\0';

    // Start provisioning
    ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(
        NETWORK_PROV_SECURITY_1, 
        (const void *) wifi_provision_state.pop, 
        wifi_provision_state.service_name, 
        NULL));
}



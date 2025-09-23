#include "wifi_provision.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

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
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        
        wireless_state = WIRELESS_STATE_PROVISIONING;
        status_bar_update_wireless_state(wireless_state);

        break;
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *) event_data;
        ESP_LOGI(TAG, "Received WiFi credentials"
                     "\n\tSSID     : %s"
                     "\n\tPassword : %s",
                     (const char *) wifi_sta_cfg->ssid,
                     (const char *) wifi_sta_cfg->password);

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                    "\n\tPlease reset to factory and retry provisioning",
                    (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                    "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
        
        wireless_state = WIRELESS_STATE_PROVISION_FAILED;
        status_bar_update_wireless_state(wireless_state);

        // Update event too
        xEventGroupClearBits(wireless_event_group, WIRELESS_STATEFUL_IS_PROVISIONED);

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning successful");

        // Restart the watchdog timer
        wifi_expiry_watchdog_restart();

        wifi_config_disable_provision_interface(WIRELESS_STATE_PROVISIONED);

        break;

    case WIFI_PROV_END:
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


void wifi_provision_reset() {
    ESP_ERROR_CHECK(wifi_prov_mgr_reset_provisioning());
}


void wifi_provision_init() {
    // Generate POP randomly
    for (int i = 0; i < sizeof(wifi_provision_state.pop) - 1; i++) {
        wifi_provision_state.pop[i] = '0' + (esp_random() % 10);
    }
    wifi_provision_state.pop[sizeof(wifi_provision_state.pop) - 1] = '\0';

    // Start provisioning
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1, 
        (const void *) wifi_provision_state.pop, 
        wifi_provision_state.service_name, 
        NULL));
}



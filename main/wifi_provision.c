#include "wifi_provision.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "esp_log.h"
#include "esp_err.h"

#define TAG "WiFiProvision"

void wifi_provision_event_handler(void* arg, int32_t event_id, void* event_data) {
    switch (event_id)
    {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        break;
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *) event_data;
        ESP_LOGI(TAG, "Received WiFi credentials"
                     "\n\tSSID     : %s"
                     "\n\tPassword : %s",
                     (const char *) wifi_sta_cfg->ssid,
                     (const char *) wifi_sta_cfg->password);
        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                    "\n\tPlease reset to factory and retry provisioning",
                    (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                    "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning successful");
        break;

    case WIFI_PROV_END:
        ESP_LOGI(TAG, "Provisioning ended");
        break;

    default:
        break;
    }
}


void wifi_provision_reset() {
    ESP_ERROR_CHECK(wifi_prov_mgr_reset_provisioning());
}

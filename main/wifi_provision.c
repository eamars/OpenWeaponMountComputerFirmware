#include "wifi_provision.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"

#include <string.h>

#include "esp_random.h"
#include "esp_log.h"
#include "esp_err.h"


#define TAG "WiFiProvision"

wireless_provision_state_t wifi_provision_state;


esp_err_t wifi_provision_reset() {
    esp_err_t ret = network_prov_mgr_reset_wifi_provisioning();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset provisioning: %s", esp_err_to_name(ret));
    }
    return ret;
}


esp_err_t wifi_provision_init() {
    // Generate POP randomly. The QR code shows wifi_provision_state.pop, so it changes only once provisioning has
    // started with the new one (the manager keeps its own copy); a failed start used to leave the two different
    char pop[sizeof(wifi_provision_state.pop)];
    for (int i = 0; i < sizeof(pop) - 1; i++) {
        pop[i] = '0' + (esp_random() % 10);
    }
    pop[sizeof(pop) - 1] = '\0';

    // Start provisioning
    esp_err_t ret = network_prov_mgr_start_provisioning(
        NETWORK_PROV_SECURITY_1,
        (const void *) pop,
        wifi_provision_state.service_name,
        NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(ret));
        return ret;
    }

    memcpy(wifi_provision_state.pop, pop, sizeof(pop));
    return ESP_OK;
}

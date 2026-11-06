#ifndef WIFI_PROVISION_H_
#define WIFI_PROVISION_H_

#include <stdint.h>
#include "esp_err.h"
#include "wifi.h"

// Provisioning helpers for the Wi-Fi service (wifi.c); called from its task only.

// Service name and proof of possession, shown in the provisioning QR code
extern wireless_provision_state_t wifi_provision_state;

/**
 * @brief Forget the saved Wi-Fi credentials. Returns the error instead of aborting the unit; it fails once the
 * Wi-Fi expiry has de-initialised Wi-Fi (ADR-005 B10).
 */
esp_err_t wifi_provision_reset();

/** @brief Pick a random proof of possession and start SoftAP provisioning (the provisioning manager must be up). */
esp_err_t wifi_provision_init();

#endif  // WIFI_PROVISION_H_

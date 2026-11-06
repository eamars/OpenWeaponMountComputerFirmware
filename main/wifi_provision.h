#ifndef WIFI_PROVISION_H_
#define WIFI_PROVISION_H_

#include <stdint.h>
#include "esp_err.h"

void wifi_provision_event_handler(void* arg, int32_t event_id, void* event_data);

/**
 * @brief Forget the saved Wi-Fi credentials. Returns the error instead of aborting the unit; it fails once the
 * Wi-Fi expiry has de-initialised Wi-Fi (ADR-005 B10).
 */
esp_err_t wifi_provision_reset();
void wifi_provision_init();

#endif  // WIFI_PROVISION_H_
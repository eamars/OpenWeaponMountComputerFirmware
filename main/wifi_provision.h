#ifndef WIFI_PROVISION_H_
#define WIFI_PROVISION_H_

#include <stdint.h>

void wifi_provision_event_handler(void* arg, int32_t event_id, void* event_data);

void wifi_provision_reset();

#endif  // WIFI_PROVISION_H_
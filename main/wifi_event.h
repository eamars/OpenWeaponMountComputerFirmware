#ifndef WIFI_EVENT_H_
#define WIFI_EVENT_H_

#include <stdint.h>
#include "esp_event.h"

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data);


#endif  // WIFI_EVENT_H_
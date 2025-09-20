#ifndef WIFI_H_
#define WIFI_H_

#include <stdint.h>
#include "esp_event.h"
#include "esp_err.h"


typedef struct {
    bool is_provisioned;
    char service_name[16];
    char pop[9];
} wifi_provision_state_t;



void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data);

esp_err_t wifi_init();

#endif  // WIFI_H_
#ifndef WIFI_H_
#define WIFI_H_

#include <stdint.h>
#include "esp_event.h"
#include "esp_err.h"

#define WIFI_DEINIT_TASK_STACK 4096
#define WIFI_DEINIT_TASK_PRIORITY 3


typedef struct {
    char service_name[16];
    char pop[9];
} wireless_provision_state_t;


// To be used with `wireless_event_group` for the other part of the application to block waiting for wifi
// to be connnected
// NOT to be confused with `status_bar_wireless_state_t`. 
typedef enum {
    WIRELESS_STATE_NOT_PROVISIONED,
    WIRELESS_STATE_PROVISIONED,
    WIRELESS_STATE_PROVISIONING,
    WIRELESS_STATE_PROVISION_FAILED,
    WIRELESS_STATE_PROVISION_EXPIRE,

    WIRELESS_STATE_NOT_CONNECTED,
    WIRELESS_STATE_CONNECTING,
    WIRELESS_STATE_CONNECTED,
    WIRELESS_STATE_DISCONNECTED,
    WIRELESS_STATE_NOT_CONNECTED_EXPIRE,
} wireless_state_e;


typedef enum {
    WIRELESS_STATEFUL_IS_PROVISIONED = (1 << 0),
    WIRELESS_STATEFUL_IS_STA_CONNECTED = (1 << 1),
    WIRELESS_STATEFUL_IS_EXPIRED = (1 << 2),
} wireless_stateful_event_e;


typedef struct {
    uint32_t crc32;
    bool wifi_enable;
    uint32_t wifi_expiry_timeout_s;
} wifi_user_config_t;


void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data);

esp_err_t wifi_init();

/**
 * @brief Block waiting for system to be provisioned. The function will return immediately if already provisioned. 
 */
esp_err_t wifi_wait_for_provision(uint32_t block_wait_ms);
bool wifi_is_provisioned();

/**
 * @brief Block waiting for system to be connected to wifi in STA mode. The function will return immediately if already connected
 */
esp_err_t wifi_wait_for_sta_connected(uint32_t block_wait_ms);


void wifi_expiry_watchdog_restart();
void wifi_expiry_watchdog_stop();


#endif  // WIFI_H_
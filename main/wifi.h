#ifndef WIFI_H_
#define WIFI_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_event.h"
#include "esp_err.h"

// The Wi-Fi service (ADR-005 step 3). It owns the Wi-Fi state, its settings and one task; every change goes
// through that task, which never calls LVGL. State changes are posted as WIFI_SERVICE_EVENT on the default event
// loop; the UI bridge (ui_bridge.c) shows them. It needs no UI, so it can start before or after the display.

#define WIFI_SERVICE_TASK_STACK 4096
#define WIFI_SERVICE_TASK_PRIORITY 3


typedef struct {
    char service_name[16];
    char pop[9];
} wireless_provision_state_t;


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


// For other tasks to block waiting for Wi-Fi (`wifi_wait_for_*`)
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


// What the service shows to others; read it with wifi_get_status()
typedef struct {
    wireless_state_e state;
    bool enabled;       // the user's Wi-Fi setting
    bool expired;       // the Wi-Fi window ran out: the radio is off until Wi-Fi is switched on again
    bool provisioned;   // credentials are known (stored, or just received)
} wifi_status_t;


ESP_EVENT_DECLARE_BASE(WIFI_SERVICE_EVENT);
typedef enum {
    WIFI_SERVICE_EVENT_STATUS_CHANGED,      // no data: read wifi_get_status()
    WIFI_SERVICE_EVENT_PROVISION_RESET,     // data: esp_err_t, the result of wifi_request_reset_provisioning()
} wifi_service_event_e;


/**
 * @brief Load the Wi-Fi settings, bring up the stack and start the service task. Needs the default event loop and
 * NVS; does not need the UI.
 */
esp_err_t wifi_init();

/** @brief A copy of the current status; callable from any task. Before wifi_init() it reads "not provisioned". */
wifi_status_t wifi_get_status();

/** @brief Short name of a state, as the settings page and the log show it ("connected", ...). */
const char * wifi_state_name(wireless_state_e state);

/**
 * @brief The user's Wi-Fi setting (switch, remote control). The setting changes at once (so a save right after
 * keeps it); the radio follows in the service task. On after the window expired starts a new window. Returns at once.
 */
void wifi_request_enable(bool enable);

typedef enum {
    WIFI_SUSPEND_NONE,      // awake
    WIFI_SUSPEND_IDLE,      // screen off: the radio stops, except while waiting to be provisioned (the phone is
                            // talking to the unit's SoftAP)
    WIFI_SUSPEND_SLEEP,     // light sleep: the radio always stops
} wifi_suspend_e;

/**
 * @brief Tell the service the unit's power state. Returns once the service task has started or stopped the radio
 * for it, so the caller may sleep right after; times out after a few seconds. Call it without the LVGL lock.
 */
esp_err_t wifi_request_suspend(wifi_suspend_e mode);

/**
 * @brief Forget the stored credentials and start provisioning again, with a new proof of possession and a new
 * Wi-Fi window; no reboot needed. The result comes as WIFI_SERVICE_EVENT_PROVISION_RESET. Returns at once.
 */
void wifi_request_reset_provisioning();

/**
 * @brief Debug (remote control `wifi expire`): end the Wi-Fi window now, as the expiry timer would. The radio
 * stops and the stack stays; switching Wi-Fi on starts a new window.
 */
void wifi_expire_now();

/** @brief Debug (remote control `wifi drop`): disconnect, and handle that one disconnect as a lost connection. */
void wifi_drop_now();

/**
 * @brief Block waiting for system to be provisioned. The function will return immediately if already provisioned.
 */
esp_err_t wifi_wait_for_provision(uint32_t block_wait_ms);
bool wifi_is_provisioned();

/**
 * @brief Block waiting for system to be connected to wifi in STA mode. The function will return immediately if already connected
 */
esp_err_t wifi_wait_for_sta_connected(uint32_t block_wait_ms);
bool wifi_is_sta_connected();

/**
 * @brief Block waiting for wifi to expire.
 */
esp_err_t wifi_wait_for_expire(uint32_t block_wait_ms);
bool wifi_is_expired();

esp_err_t save_wifi_user_config();
esp_err_t load_wifi_user_config();

// Shared state, defined in wifi.c
extern wifi_user_config_t wifi_user_config;
extern const wifi_user_config_t default_wifi_user_config;

#endif  // WIFI_H_

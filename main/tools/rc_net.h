#ifndef RC_NET_H_
#define RC_NET_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// The remote control over Wi-Fi (ADR-013): the same command lines, replies and snap lines as the serial remote
// control, plus the unit's log, on one TCP port while the unit has an address and the switch is on.
#define RC_NET_PORT 3333
#define RC_NET_MDNS_SERVICE "_owmc-rc"
#define RC_NET_MDNS_PROTO "_tcp"

#define RC_NET_TASK_STACK 5120     // 840 B left at 4096 after snaps and `stats` (ADR-013); in PSRAM
#define RC_NET_TASK_PRIORITY 3


/**
 * @brief Create the task and hook the log. Call once at start-up, after remote_control_init(); nothing listens until
 * rc_net_set_enabled(true).
 */
esp_err_t rc_net_init(void);

/**
 * @brief Listen while the unit has an address (on), or close the port and the connection (off). Returns at once;
 * the task follows within a poll period. Safe from LVGL callbacks.
 */
void rc_net_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif  // RC_NET_H_

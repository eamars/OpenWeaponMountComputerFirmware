#ifndef LOW_POWER_MODE_H
#define LOW_POWER_MODE_H

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus


void create_low_power_mode_view(lv_obj_t *parent);

void update_low_power_mode_last_activity_event();

bool is_idle_mode_activated();
bool is_sleep_mode_activated();

void prevent_idle_mode_enter(bool prevent);
void prevent_sleep_mode_enter(bool prevent);

void wake_from_idle_mode();

/**
 * @brief Enter idle mode the way the inactivity timeout does. Does nothing when already idle.
 * @note Creates an LVGL timer: call with the LVGL lock held, or from the low power monitor task as before.
 */
void low_power_mode_request_idle();

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif // LOW_POWER_MODE_H
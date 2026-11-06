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

/**
 * @brief Leave idle mode. Does nothing when not idle.
 * @note Returns at once; the low power monitor task runs the wake outside any LVGL pass. Callable from any task,
 *       with or without the LVGL lock.
 */
void wake_from_idle_mode();

/**
 * @brief For input read callbacks: report what the hardware just read, get back whether LVGL must not see it.
 * A press counts as activity. A press that starts while idle wakes the unit, and that whole press, until it is
 * released, is the wake gesture: LVGL must see the input as released meanwhile, so the wake never triggers a
 * widget the user cannot see (ADR-005 B3).
 * @param pressed The state the hardware reported in this read.
 * @param wake_press Per input device state, false at start; owned by the caller.
 * @return true if the caller must report LV_INDEV_STATE_RELEASED to LVGL for this read.
 */
bool low_power_mode_swallow_wake_press(bool pressed, bool *wake_press);

/**
 * @brief Enter idle mode the way the inactivity timeout does. Does nothing when already idle.
 * @note Returns at once; the low power monitor task runs the entry outside any LVGL pass. Callable from any task,
 *       with or without the LVGL lock.
 */
void low_power_mode_request_idle();

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif // LOW_POWER_MODE_H
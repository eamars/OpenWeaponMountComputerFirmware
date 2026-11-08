#ifndef LOW_POWER_MODE_H
#define LOW_POWER_MODE_H

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus


// Low power (ADR-005 step 3): its state and tasks no longer start inside its view.

/** @brief Create the low-power state. Call early at boot, before anything that asks whether the unit is idle. */
esp_err_t low_power_mode_init();

/**
 * @brief Install the touch wake wrapper and start the low-power tasks (idle and sleep timeouts, the stability
 * detector). Call once the display and its touch input exist, without the LVGL lock held.
 */
esp_err_t low_power_mode_start();

/** @brief The black tile shown while idle. LVGL lock held. */
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

/** @brief Wake profiling (ADR-009): name the cause of the next wake (a string literal); no-op without profiling */
void low_power_mode_note_wake_cause(const char *cause);

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

/** @brief Wake lines counted by the light-sleep statistics: touch, BNO085, button, PMIC (those built in) */
#define LOW_POWER_SLEEP_STATS_LINES 4

/** @brief Automatic light sleep while idle (ADR-009), counted since boot */
typedef struct {
    int64_t idle_battery_us;      // time spent idle without USB power (light sleep allowed), up to now
    uint32_t idle_periods;        // times idle was entered
    int64_t slept_us;             // time spent in light sleep
    uint32_t sleeps;              // light sleeps (each one ends in one wake)
    uint32_t wake_gpio;           // woken by a wake line
    uint32_t wake_gpio_no_line;   // ... with no line low any more on the way out (a touch pulse; treated as touch)
    uint32_t wake_line[LOW_POWER_SLEEP_STATS_LINES];  // ... with that line low on the way out
    uint32_t wake_timer;          // woken by a task's or timer's own deadline
    uint32_t wake_other;          // any other cause (a refused sleep counts here)
    bool usb_power;               // USB power present now: no light sleep
} low_power_sleep_stats_t;

/** @brief The PMIC's USB power reading: no light sleep while present (ADR-009). PMIC monitor task only. */
void low_power_mode_set_usb_power(bool present);

/** @brief Copy the light-sleep statistics. Any task. */
void low_power_mode_get_sleep_stats(low_power_sleep_stats_t *out);

// Shared state, defined in low_power_mode.c
extern uint32_t wakeup_cause;

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif // LOW_POWER_MODE_H
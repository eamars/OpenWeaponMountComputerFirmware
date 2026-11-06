#ifndef UI_BRIDGE_H_
#define UI_BRIDGE_H_

#include "lvgl.h"

// The UI bridge (ADR-005 step 3): the one place that turns service events into UI updates under the LVGL lock.
// It holds the services' state as LVGL subjects; widgets bind to them with lv_subject_add_observer_obj() when
// they are created, and are updated in LVGL context from then on. Services never call LVGL.

// Wi-Fi service (wifi.h), all int subjects
extern lv_subject_t ui_wifi_state;          // wireless_state_e
extern lv_subject_t ui_wifi_enabled;        // 0 / 1: the user's Wi-Fi setting
extern lv_subject_t ui_wifi_expired;        // 0 / 1: Wi-Fi is off until the next boot
extern lv_subject_t ui_wifi_provisioned;    // 0 / 1

/**
 * @brief Set up the subjects from the services' current state and subscribe to their events. Call with the LVGL
 * lock held, before the widgets that bind to the subjects are created. The services may start before or after.
 */
void ui_bridge_init();

#endif  // UI_BRIDGE_H_

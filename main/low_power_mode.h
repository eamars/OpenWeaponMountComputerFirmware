#ifndef LOW_POWER_MODE_H
#define LOW_POWER_MODE_H

#include "lvgl.h"


void create_low_power_mode_view(lv_obj_t * parent);
void enable_low_power_mode(bool enable);
void update_low_power_mode_last_activity_event();
bool is_low_power_mode_activated();

void prevent_low_power_mode_enter(bool prevent);

#endif // LOW_POWER_MODE_H
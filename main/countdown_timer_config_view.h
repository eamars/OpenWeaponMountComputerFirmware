#ifndef COUNTDOWN_TIMER_CONFIG_VIEW_H_
#define COUNTDOWN_TIMER_CONFIG_VIEW_H_

#include <lvgl.h>

void create_countdown_timer_config_view(lv_obj_t * parent);

void countdown_timer_rotation_event_callback(lv_event_t * e);

#endif  // COUNTDOWN_TIMER_CONFIG_VIEW_H_
#ifndef SEND_IT_VIEW_H_
#define SEND_IT_VIEW_H_

#include <lvgl.h>

void create_send_it_view(lv_obj_t *parent);
void enable_send_it_view(bool enable);

void send_it_view_rotation_event_callback(lv_event_t * e);

#endif  // SEND_IT_VIEW_H_
#ifndef DOPE_CONFIG_VIEW_H_
#define DOPE_CONFIG_VIEW_H_

#include <lvgl.h>
 
#define DOPE_CONFIG_MAX_DOPE_ITEM 12

void create_dope_config_view(lv_obj_t * parent);
lv_obj_t * create_dope_card_list_widget(lv_obj_t * parent);

void set_rotation_dope_card_list(lv_display_rotation_t rotation);

void dope_config_view_rotation_event_callback(lv_event_t * e);

#endif // DOPE_CONFIG_VIEW_H_
#ifndef SYSTEM_CONFIG_VIEW_H_
#define SYSTEM_CONFIG_VIEW_H_


#include <lvgl.h>

#define MAX_NUM_COLOURS 20  // 19 preset colours from lv_palette_t
#define LV_PALETTE_BLACK LV_PALETTE_LAST

void create_system_config_view(lv_obj_t *parent);

lv_obj_t * create_menu_container_with_text(lv_obj_t * parent, const char * icon, const char * text);

lv_obj_t * create_spin_box(lv_obj_t * container, 
                           int32_t range_min, int32_t range_max, int32_t digit_count, int32_t sep_pos, int32_t default_value,
                           lv_event_cb_t event_cb, void *event_cb_args);

lv_obj_t * create_colour_picker(lv_obj_t * container, lv_palette_t default_colour, lv_event_cb_t event_cb, void *event_cb_args);


#endif  // SYSTEM_CONFIG_VIEW_H_
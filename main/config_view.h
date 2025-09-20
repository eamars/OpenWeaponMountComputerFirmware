#ifndef CONFIG_VIEW_H_
#define CONFIG_VIEW_H_


#include <lvgl.h>

#define MAX_NUM_COLOURS 20  // 19 preset colours from lv_palette_t
#define LV_PALETTE_BLACK LV_PALETTE_LAST

void create_config_view(lv_obj_t *parent);

lv_obj_t * create_menu_container_with_text(lv_obj_t * parent, const char * icon, const char * text);

lv_obj_t * create_spin_box(lv_obj_t * container, 
                           int32_t range_min, int32_t range_max, uint32_t step_size, int32_t digit_count, int32_t sep_pos, int32_t default_value,
                           lv_event_cb_t event_cb, void *event_cb_args);
lv_obj_t * create_colour_picker(lv_obj_t * container, lv_palette_t * colour, lv_event_cb_t event_cb);
lv_obj_t * create_dropdown_list(lv_obj_t * container, const char * options, int32_t current_selection, lv_event_cb_t event_cb, void * event_cb_args);
lv_obj_t * create_save_reload_reset_buttons(lv_obj_t * container, lv_event_cb_t save_event_cb, lv_event_cb_t reload_event_cb, lv_event_cb_t reset_event_cb);
lv_obj_t * create_switch(lv_obj_t * container, bool * state, lv_event_cb_t event_cb);
lv_obj_t * create_config_label_static(lv_obj_t * parent, char * text);
lv_obj_t * create_single_button(lv_obj_t * container, const char * icon, lv_event_cb_t event_cb);

void update_info_msg_box(const char * text);

#endif  // CONFIG_VIEW_H_
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


typedef enum {
    STATUS_BAR_WIRELESS_STATE_UNKNOWN = 0,
    STATUS_BAR_WIRELESS_STATE_NOT_PROVISIONED,
    STATUS_BAR_WIRELESS_STATE_PROVISIONING,
    STATUS_BAR_WIRELESS_STATE_STA_CONNECTING,
    STATUS_BAR_WIRELESS_STATE_STA_CONNECTED,
    STATUS_BAR_WIRELESS_STATE_STA_DISCONNECTED,
} status_bar_wireless_state_t;

// Status bar update
void update_status_bar_wireless_state(status_bar_wireless_state_t state);

/** 
 * @brief Update the battery level icon in the status bar
 * @param level_percentage Battery level percentage (0-100). < 100 is powered by USB, -1 is unknown>
 */
void update_status_bar_battery_level(int level_percentage);

void update_info_msg_box(const char * text);

#endif  // CONFIG_VIEW_H_
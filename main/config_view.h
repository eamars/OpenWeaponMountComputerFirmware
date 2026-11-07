#ifndef CONFIG_VIEW_H_
#define CONFIG_VIEW_H_


#include <lvgl.h>
#include "esp_lvgl_port.h"

#include "wifi.h"

#define MAX_NUM_COLOURS 20  // 19 preset colours from lv_palette_t
#define LV_PALETTE_BLACK LV_PALETTE_LAST


typedef void (*lvgl_additional_init_func)(lv_obj_t *);


#ifdef __cplusplus
extern "C" {
#endif


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
lv_obj_t * create_info_msg_box(lv_obj_t *parent, lvgl_additional_init_func callback_f);

void update_info_msg_box(lv_obj_t * msg_box, const char * text);

// Builds a settings page's widgets from its settings
typedef void (*config_page_fill_func_t)(lv_obj_t * page);

/**
 * @brief Create a settings sub page of the menu and fill it
 * @param fill Builds the widgets; run again by config_page_rebuild()
 */
lv_obj_t * create_config_page(lv_obj_t * menu, config_page_fill_func_t fill);

/**
 * @brief Redraw a settings page from its settings, after Reload or Reset
 * Runs once the current event has finished (the pressed button is on the page) and keeps the scroll position
 */
void config_page_rebuild(lv_obj_t * page);

/**
 * @brief Show the outcome of a settings page's Save or Reload in its message box
 * @param ret The action's result; the cause of a failure is logged where it happened
 * @param done_text Shown on ESP_OK
 * @param failed_text Shown with the error name otherwise
 */
void update_info_msg_box_result(lv_obj_t * msg_box, esp_err_t ret, const char * done_text, const char * failed_text);

// Shared state, defined in config_view.c
extern lv_obj_t * msg_box;  // the settings pages' shared message box

#ifdef __cplusplus
}
#endif

#endif  // CONFIG_VIEW_H_
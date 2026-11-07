#include "config_view.h"

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "app_cfg.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "sensor_config.h"
#include "about_config.h"
#include "wifi_config.h"
#include "ui_bridge.h"
#include "ota_mode.h"
#include "pmic_axp2101.h"
#include "pmic_config.h"
#include "countdown_timer_config_view.h"
#include "dope_config_view.h"
#include "kestrel_config_view.h"
#include "settings_ui.h"

#define TAG "ConfigView"


// The old settings' shared message box: no settings page uses it since ADR-008 (it stays NULL; the UI bridge
// checks); the helpers below go in ADR-008 step 3, once the power button's menu is rebuilt
lv_obj_t * msg_box;



lv_obj_t * create_menu_container_with_text(lv_obj_t * parent, const char * icon, const char * text) {
    lv_obj_t * container = lv_menu_cont_create(parent);

    lv_obj_t *img = NULL;
    lv_obj_t *label = NULL;

    if (icon) {
        img = lv_image_create(container);
        lv_image_set_src(img, icon);
    }

    if (text) {
        label = lv_label_create(container);
        lv_label_set_text(label, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);

    }

    // Set colour
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(container, lv_color_white(), 0);
    // lv_obj_set_style_bg_color(container, lv_palette_main(LV_PALETTE_YELLOW), 0);

    // Other styling
    lv_obj_set_width(container, lv_pct(100));  // extend to full width
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    return container;
}

lv_obj_t * create_config_label_static(lv_obj_t * parent, char * text) {
    lv_obj_t * container = lv_menu_cont_create(parent);
    // Expand the parent object with the size of content
    lv_obj_set_height(container, LV_SIZE_CONTENT);

    lv_obj_t * label = lv_label_create(container);
    lv_label_set_text_static(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(label, 1);

    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_width(container, lv_pct(100));  // extend to full width
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    return label;
}


static void lv_spinbox_increment_event_cb(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_user_data(e);
    lv_spinbox_increment(spinbox);
}

static void lv_spinbox_decrement_event_cb(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_user_data(e);
    lv_spinbox_decrement(spinbox);
}

lv_obj_t * create_spin_box(lv_obj_t * container, 
                           int32_t range_min, int32_t range_max, uint32_t step_size, int32_t digit_count, int32_t sep_pos, int32_t default_value,
                           lv_event_cb_t event_cb, void *event_cb_args) {
    // Add buttons
    lv_obj_t * minus_button = lv_button_create(container);
    lv_obj_t * spinbox = lv_spinbox_create(container);
    lv_obj_t * plus_button = lv_button_create(container);

    lv_spinbox_set_value(spinbox, default_value);

    // configure spinbox    
    lv_spinbox_set_range(spinbox, range_min, range_max);
    lv_spinbox_set_digit_format(spinbox, digit_count, sep_pos);
    lv_spinbox_set_step(spinbox, step_size);
    lv_obj_set_flex_grow(spinbox, 1);
    lv_obj_set_style_text_font(spinbox, &lv_font_montserrat_20, LV_PART_MAIN);

    lv_obj_set_style_bg_image_src(minus_button, LV_SYMBOL_MINUS, 0);
    lv_obj_add_event_cb(minus_button, lv_spinbox_decrement_event_cb, LV_EVENT_SHORT_CLICKED, (void *) spinbox);

    lv_obj_set_style_bg_image_src(plus_button, LV_SYMBOL_PLUS, 0);
    lv_obj_add_event_cb(plus_button, lv_spinbox_increment_event_cb, LV_EVENT_SHORT_CLICKED, (void *) spinbox);
    
    lv_obj_add_event_cb(spinbox, event_cb, LV_EVENT_VALUE_CHANGED, event_cb_args);

    // Configure layout
    lv_obj_add_flag(minus_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_height(minus_button, lv_obj_get_height(spinbox));
    lv_obj_set_width(minus_button, lv_pct(30));
    lv_obj_set_height(plus_button, lv_obj_get_height(spinbox));
    lv_obj_set_width(plus_button, lv_pct(30));

    return spinbox;
}


static void lv_colour_picker_left_event_cb(lv_event_t *e) {
    lv_obj_t * colour_indicator = lv_event_get_user_data(e);
    lv_palette_t * colour_idx = lv_obj_get_user_data(colour_indicator);

    // Take the previous colour
    if (*colour_idx > 0) {
        *colour_idx -= 1;
    }
    // ESP_LOGI(TAG, "Current colour %d, current colour addr %p", *colour_idx, colour_idx);


    // Apply the colour
    lv_led_set_color(colour_indicator, lv_palette_main(*colour_idx));

    // Apply callback
    lv_obj_send_event(colour_indicator, LV_EVENT_VALUE_CHANGED, NULL);
}

static void lv_colour_picker_right_event_cb(lv_event_t *e) {
    lv_obj_t * colour_indicator = lv_event_get_user_data(e);
    lv_palette_t * colour_idx = lv_obj_get_user_data(colour_indicator);

    // Take the previous colour
    if (*colour_idx < (MAX_NUM_COLOURS - 1)) {
        *colour_idx += 1;
    }
    // ESP_LOGI(TAG, "Current colour %d, current colour addr %p", *colour_idx, colour_idx);

    // Apply the colour
    lv_led_set_color(colour_indicator, lv_palette_main(*colour_idx));

    // Apply callback
    lv_obj_send_event(colour_indicator, LV_EVENT_VALUE_CHANGED, NULL);
}


static void update_lv_palette_item(lv_event_t * e) {
    lv_obj_t * colour_indicator = lv_event_get_target_obj(e);
    lv_palette_t * colour_idx = lv_obj_get_user_data(colour_indicator);
    lv_palette_t * target_colour_idx = lv_event_get_user_data(e);
    *target_colour_idx = *colour_idx;
    ESP_LOGI(TAG, "Target colour updated to %d", *target_colour_idx);
}


lv_obj_t * create_colour_picker(lv_obj_t * container, lv_palette_t * colour, lv_event_cb_t event_cb) {
    lv_obj_t * left_button = lv_button_create(container);
    lv_obj_t * colour_indicator = lv_led_create(container);
    lv_obj_t * right_button = lv_button_create(container);

    lv_obj_set_user_data(colour_indicator, colour);  // There is no way to get colour from led thus we have to retrieve it via user data
    lv_led_set_color(colour_indicator, lv_palette_main(*colour));  // set default colour

    // FIXME: The LED object won't emit value change event therefore the callback needed to be called manually
    if (event_cb == NULL) {
        lv_obj_add_event_cb(colour_indicator, update_lv_palette_item, LV_EVENT_VALUE_CHANGED, (void *) colour);
    }
    else {
        lv_obj_add_event_cb(colour_indicator, event_cb, LV_EVENT_VALUE_CHANGED, (void *) colour);
    }
    

    lv_obj_set_style_bg_image_src(left_button, LV_SYMBOL_LEFT, 0);
    lv_obj_set_height(left_button, 60);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(left_button, lv_pct(30));
    lv_obj_add_event_cb(left_button, lv_colour_picker_left_event_cb, LV_EVENT_SHORT_CLICKED, (void *) colour_indicator);

    lv_obj_set_style_bg_image_src(right_button, LV_SYMBOL_RIGHT, 0);
    lv_obj_set_height(right_button, 60);
    lv_obj_set_width(right_button, lv_pct(30));
    lv_obj_add_event_cb(right_button, lv_colour_picker_right_event_cb, LV_EVENT_SHORT_CLICKED, (void *) colour_indicator);

    // set style
    lv_obj_add_flag(left_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);    // add a new line before the widget
    lv_obj_align(colour_indicator, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(right_button, LV_ALIGN_RIGHT_MID, 0, 0);

    return colour_indicator;
}


lv_obj_t * create_dropdown_list(lv_obj_t * container, const char * options, int32_t current_selection, lv_event_cb_t event_cb, void * event_cb_args) {
    lv_obj_t * dropdown_list_option = lv_dropdown_create(container);
    lv_obj_set_size(dropdown_list_option, lv_pct(100), 36);
    lv_obj_add_flag(dropdown_list_option, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_dropdown_set_options(dropdown_list_option, options);
    lv_dropdown_set_selected(dropdown_list_option, current_selection);
    
    lv_obj_add_event_cb(dropdown_list_option, event_cb, LV_EVENT_VALUE_CHANGED, event_cb_args);
    
    return dropdown_list_option;
}


lv_obj_t * create_save_reload_reset_buttons(lv_obj_t * container, lv_event_cb_t save_event_cb, lv_event_cb_t reload_event_cb, lv_event_cb_t reset_event_cb) {
    lv_obj_t * save_button = lv_btn_create(container);
    lv_obj_t * reload_button = lv_btn_create(container);
    lv_obj_t * reset_button = lv_btn_create(container);

    // Save/reload Styling
    lv_obj_add_flag(save_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_style_bg_image_src(save_button, LV_SYMBOL_SAVE, 0);
    lv_obj_set_height(save_button, 60);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(save_button, lv_pct(30));
    lv_obj_add_event_cb(save_button, save_event_cb, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_set_style_bg_image_src(reload_button, LV_SYMBOL_UPLOAD, 0);
    lv_obj_set_height(reload_button, 60);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(reload_button, lv_pct(30));
    lv_obj_add_event_cb(reload_button, reload_event_cb, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_set_style_bg_image_src(reset_button, LV_SYMBOL_WARNING, 0);
    lv_obj_set_height(reset_button, 60);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(reset_button, lv_pct(30));
    lv_obj_add_event_cb(reset_button, reset_event_cb, LV_EVENT_SINGLE_CLICKED, NULL);

    return container;
}


lv_obj_t * create_single_button(lv_obj_t * container, const char * icon, lv_event_cb_t event_cb) {
    lv_obj_t * reset_provision_button = lv_btn_create(container);

    // Save/reload Styling
    // lv_obj_add_flag(reset_provision_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_style_bg_image_src(reset_provision_button, icon, 0);
    lv_obj_set_width(reset_provision_button, lv_pct(40));
    lv_obj_set_height(reset_provision_button, 48);  // TODO: Find a better way to read the height from other widgets
    lv_obj_add_event_cb(reset_provision_button, event_cb, LV_EVENT_SINGLE_CLICKED, NULL);

    return reset_provision_button;
}


static void on_switch_value_changed(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    bool *state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);
}


lv_obj_t * create_switch(lv_obj_t * container, bool * state, lv_event_cb_t event_cb) {
    lv_obj_t * enable_switch = lv_switch_create(container);
    lv_obj_set_width(enable_switch, lv_pct(40));
    lv_switch_set_orientation(enable_switch, LV_SWITCH_ORIENTATION_HORIZONTAL);

    // Set initial state
    if (*state) {
        lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
    }
    
    if (event_cb == NULL) {
        lv_obj_add_event_cb(enable_switch, on_switch_value_changed, LV_EVENT_VALUE_CHANGED, (void *) state);
    } else {
        lv_obj_add_event_cb(enable_switch, event_cb, LV_EVENT_VALUE_CHANGED, (void *) state);
    }
    
    return enable_switch;
}


static void on_msg_box_ok_button_clicked(lv_event_t *e) {
    // hide
    lv_obj_t * msg_box = lv_event_get_user_data(e);
    lv_obj_add_flag(msg_box, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t * create_info_msg_box(lv_obj_t *parent, lvgl_additional_init_func callback_f) {
    // Create a message box to be called by its content
    lv_obj_t * msg_box = lv_obj_create(parent);

    // Styling
    lv_obj_set_style_bg_opa(msg_box, LV_OPA_90, LV_PART_MAIN);  // semi transparent background
    lv_obj_set_size(msg_box, lv_pct(90), lv_pct(90));
    lv_obj_center(msg_box);
    // lv_obj_set_style_blur_backdrop(msg_box, true, LV_PART_MAIN);  // blur the background of the msg box
    // lv_obj_set_style_blur_radius(msg_box, 10, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(msg_box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(msg_box, 0, 0);

    lv_obj_set_flex_flow(msg_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(msg_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(msg_box, LV_DIR_NONE);  // no scroll

    if (callback_f) {
        callback_f(msg_box);
    }

    // Add a button (not a footer button)
    lv_obj_t * ok_btn = lv_button_create(msg_box);
    lv_obj_set_size(ok_btn, lv_pct(50), 60);
    lv_obj_add_event_cb(ok_btn, on_msg_box_ok_button_clicked, LV_EVENT_CLICKED, msg_box);

    lv_obj_t *ok_label = lv_label_create(ok_btn);
    lv_label_set_text(ok_label, "OK");
    lv_obj_set_style_text_font(ok_label, &lv_font_montserrat_28, 0);
    lv_obj_center(ok_label);

    // Set hidden
    lv_obj_add_flag(msg_box, LV_OBJ_FLAG_HIDDEN);

    return msg_box;
}

void update_info_msg_box(lv_obj_t * msg_box, const char * text) {
    // Retrieve the user data
    lv_obj_t * msg_box_label = lv_obj_get_user_data(msg_box);

    lv_label_set_text(msg_box_label, text);

    lv_obj_clear_flag(msg_box, LV_OBJ_FLAG_HIDDEN);
}

void update_info_msg_box_result(lv_obj_t * msg_box, esp_err_t ret, const char * done_text, const char * failed_text) {
    if (ret == ESP_OK) {
        update_info_msg_box(msg_box, done_text);
        return;
    }

    char text[64];
    snprintf(text, sizeof(text), "%s: %s", failed_text, esp_err_to_name(ret));
    update_info_msg_box(msg_box, text);
}

lv_obj_t * create_config_page(lv_obj_t * menu, config_page_fill_func_t fill) {
    lv_obj_t * page = lv_menu_page_create(menu, NULL);
    lv_obj_set_user_data(page, (void *) fill);
    fill(page);

    return page;
}


// LVGL task (lv_async_call), after the Reload or Reset event has returned
static void config_page_rebuild_async(void * p) {
    lv_obj_t * page = (lv_obj_t *) p;
    config_page_fill_func_t fill = (config_page_fill_func_t) lv_obj_get_user_data(page);
    int32_t scroll_y = lv_obj_get_scroll_y(page);

    lv_obj_clean(page);  // observers bound to the old widgets go with them
    fill(page);

    lv_obj_update_layout(page);
    lv_obj_scroll_to_y(page, scroll_y, LV_ANIM_OFF);
}


void config_page_rebuild(lv_obj_t * page) {
    if (page) {
        lv_async_call(config_page_rebuild_async, page);
    }
}


/*
----------------------------------------------------
The settings tile (ADR-008): the hub, a page a row, each row saying what its page holds
----------------------------------------------------
*/

static void advanced_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Log, sensor, charging");
}


// Advanced: what a shooter does not change at a match. Its rows come from the modules that own the settings
static void reset_advanced(void * ctx) {
    system_settings_reset_advanced();
#if USE_BNO085
    sensor_settings_reset_advanced();
#endif  // USE_BNO085
#if USE_PMIC
    pmic_settings_reset_advanced();
#endif  // USE_PMIC
}


static settings_page_t * create_advanced_settings_page(void) {
    settings_page_t * page = settings_page_create("Advanced");
    system_settings_add_advanced_rows(page);
#if USE_BNO085
    sensor_settings_add_advanced_rows(page);
#endif  // USE_BNO085
#if USE_PMIC
    pmic_settings_add_advanced_rows(page);
#endif  // USE_PMIC
    settings_row_reset(page, "Reset Advanced", "Reset the Advanced settings? Remote control turns back on.",
                       reset_advanced, NULL);
    return page;
}


void create_config_view(lv_obj_t * tile) {
    settings_page_t * hub = settings_ui_create(tile);

    create_settings_update_row(hub);    // hidden until a newer firmware is found
#if USE_BNO085
    settings_row_link(hub, LV_SYMBOL_BELL, "Timer", countdown_timer_settings_summary, NULL,
                      create_countdown_timer_settings_page());
    settings_row_link(hub, LV_SYMBOL_LIST, "Dope", dope_settings_summary, NULL, create_dope_settings_page());
    settings_row_link(hub, LV_SYMBOL_BLUETOOTH, "Kestrel", kestrel_settings_summary, NULL,
                      create_kestrel_settings_page());
    settings_row_link(hub, LV_SYMBOL_GPS, "Level", digital_level_settings_summary, NULL,
                      create_digital_level_settings_page());
#endif  // USE_BNO085
    settings_row_link(hub, LV_SYMBOL_IMAGE, "Display", system_display_settings_summary, NULL,
                      create_display_settings_page());
    settings_row_link(hub, LV_SYMBOL_WIFI, "Wi-Fi", wifi_settings_summary, NULL, create_wifi_settings_page());
#if USE_PMIC
    settings_row_link(hub, LV_SYMBOL_BATTERY_FULL, "Power", pmic_settings_summary, NULL, create_power_settings_page());
#endif  // USE_PMIC
    settings_row_link(hub, LV_SYMBOL_SETTINGS, "Advanced", advanced_summary, NULL, create_advanced_settings_page());
    settings_row_link(hub, LV_SYMBOL_FILE, "About", about_settings_summary, NULL, create_about_settings_page());

#if USE_PMIC
    create_power_menu_overlay();
#endif  // USE_PMIC

    settings_ui_start();
}

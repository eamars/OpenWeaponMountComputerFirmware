#include "config_view.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "app_cfg.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "sensor_config.h"
#include "about_config.h"
#include "wifi_config.h"
#include "ota_mode.h"
#include "pmic_axp2101.h"

#define TAG "ConfigView"


lv_obj_t * config_menu;
lv_obj_t * msg_box;
static lv_obj_t * parent_container;
static lv_obj_t * status_bar;
static lv_obj_t * status_bar_wireless_state;
static lv_obj_t * status_bar_battery_state;

HEAPS_CAPS_ATTR static char status_bar_wireless_state_str[4] = {0};
HEAPS_CAPS_ATTR static char status_bar_battery_state_str[4] = {0};

extern system_config_t system_config;


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

static void create_msg_label(lv_obj_t * parent) {
    lv_obj_t * msg_box_label = lv_label_create(parent);
    lv_obj_set_style_text_font(msg_box_label, &lv_font_montserrat_28, 0);
    lv_obj_set_size(msg_box_label, LV_PCT(100), LV_PCT(100));
    lv_label_set_long_mode(msg_box_label, LV_LABEL_LONG_MODE_WRAP);

    // Add msgbox into the user data of the msgbox
    lv_obj_set_user_data(parent, msg_box_label);
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

void set_rotation_config_view(lv_display_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        // Parent container layout
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_COLUMN);

        // Set order of status bar and menu
        lv_obj_move_to_index(status_bar, 0);

        // Set size
        lv_obj_set_width(status_bar, lv_pct(100));
        lv_obj_set_height(status_bar, LV_SIZE_CONTENT);

        // Status bar layout
        lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(status_bar,
            LV_FLEX_ALIGN_END,  // main axis (row) center
            LV_FLEX_ALIGN_CENTER,  // cross axis center
            LV_FLEX_ALIGN_CENTER); // track cross axis center

        // Put back button to the bottom of the menu
        lv_menu_set_mode_header(config_menu, LV_MENU_HEADER_BOTTOM_FIXED);

    } else {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_ROW);

        lv_obj_move_to_index(config_menu, 0);

        lv_obj_set_height(status_bar, lv_pct(100));
        lv_obj_set_width(status_bar, LV_SIZE_CONTENT);

        lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(status_bar,
            LV_FLEX_ALIGN_START,  // main axis (row) center
            LV_FLEX_ALIGN_CENTER,  // cross axis center
            LV_FLEX_ALIGN_CENTER); // track cross axis center

        // Put back button to the top of the menu
        lv_menu_set_mode_header(config_menu, LV_MENU_HEADER_TOP_FIXED);
    }
}


void config_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_config_view(system_config.rotation);
}

void create_config_view(lv_obj_t *parent) {
    // Create a parent container
    parent_container = lv_obj_create(parent);
    lv_obj_set_size(parent_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(parent_container, 0, 0);
    lv_obj_set_style_bg_opa(parent_container, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(parent_container, LV_DIR_NONE);  // no scroll

    // Create status bar
    status_bar = lv_obj_create(parent_container);
    lv_obj_set_style_pad_all(status_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_bar, lv_color_darken(lv_color_white(), 20), LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(status_bar, 0, LV_PART_MAIN);

    // Set container attribute for status bar
    lv_obj_set_scroll_dir(status_bar, LV_DIR_NONE);  // no scroll

    // WiFi icon
    status_bar_wireless_state = lv_label_create(status_bar);
    memset(status_bar_wireless_state_str, 0, sizeof(status_bar_wireless_state_str));
    lv_label_set_text_static(status_bar_wireless_state, status_bar_wireless_state_str);
    status_bar_update_wireless_state(WIRELESS_STATE_NOT_PROVISIONED);

    // Battery icon
    status_bar_battery_state = lv_label_create(status_bar);
    memset(status_bar_battery_state_str, 0, sizeof(status_bar_battery_state_str));
    lv_label_set_text_static(status_bar_battery_state, status_bar_battery_state_str);
    status_bar_update_battery_level(101);

    // Create menu item
    config_menu = lv_menu_create(parent_container);
    lv_obj_set_style_pad_all(config_menu, 0, 0);
    lv_obj_set_flex_grow(config_menu, 1);
    lv_obj_set_size(config_menu, lv_pct(100), lv_pct(100));
    lv_obj_center(config_menu);
    lv_obj_set_style_bg_color(config_menu, lv_color_darken(lv_color_white(), 20), 0);

    lv_obj_t * back_button = lv_menu_get_main_header_back_button(config_menu);
    // Set back button background colour to blue
    lv_obj_set_style_bg_color(back_button, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    // set no transparency for back button
    lv_obj_set_style_bg_opa(back_button, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t * back_button_label = lv_label_create(back_button);
    lv_label_set_text(back_button_label, "  Back  ");
    lv_obj_set_style_text_font(back_button_label, &lv_font_montserrat_28, 0);
    lv_obj_center(back_button_label);

    // Set overall layout based on rotation
    set_rotation_config_view(system_config.rotation);

    // Create a messagebox for displaying information
    msg_box = create_info_msg_box(parent, create_msg_label);

    /*Create a main page*/
    lv_obj_t * main_page = lv_menu_page_create(config_menu, NULL);
    lv_obj_set_scroll_dir(main_page, LV_DIR_VER);  // only horizontal scroll

    // All top level menu items
    create_system_config_view_config(config_menu, main_page);
#if USE_BNO085
    create_digital_level_view_config(config_menu, main_page);
    create_sensor_config_view_config(config_menu, main_page);
#endif  // USE_BNO085
    create_wifi_config_view_config(config_menu, main_page);
#if USE_PMIC
    create_power_management_view_config(config_menu, main_page);
#endif // USE_PMIC
    create_about_config_view_config(config_menu, main_page);
    create_menu_ota_upgrade_button(main_page);

    // House keeping
    lv_menu_set_page(config_menu, main_page);

    // Setup event callback handlers
    lv_obj_add_event_cb(parent, config_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);
}


void status_bar_update_wireless_state(wireless_state_e state) {
    switch (state)
    {
        case WIRELESS_STATE_NOT_PROVISIONED:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_LOOP, sizeof(LV_SYMBOL_LOOP));
            wifi_config_update_status("not provisioned");
            break;
        case WIRELESS_STATE_PROVISIONING:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_SHUFFLE, sizeof(LV_SYMBOL_SHUFFLE));
            wifi_config_update_status("provisioning");
            break;
        case WIRELESS_STATE_PROVISION_FAILED:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_WARNING, sizeof(LV_SYMBOL_WARNING));
            wifi_config_update_status("provision failed");
            break;
        case WIRELESS_STATE_PROVISION_EXPIRE:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_CLOSE, sizeof(LV_SYMBOL_CLOSE));
            wifi_config_update_status("provision expired");
            break;
        case WIRELESS_STATE_NOT_CONNECTED:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_LOOP, sizeof(LV_SYMBOL_LOOP));
            wifi_config_update_status("not connected");
            break;
        case WIRELESS_STATE_CONNECTING:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_LOOP, sizeof(LV_SYMBOL_LOOP));
            wifi_config_update_status("connecting");
            break;
        case WIRELESS_STATE_CONNECTED:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_WIFI, sizeof(LV_SYMBOL_WIFI));
            wifi_config_update_status("connected");
            break;
        case WIRELESS_STATE_DISCONNECTED:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_LOOP, sizeof(LV_SYMBOL_LOOP));
            wifi_config_update_status("disconnected");
            break;
        case WIRELESS_STATE_NOT_CONNECTED_EXPIRE:
            memcpy(status_bar_wireless_state_str, LV_SYMBOL_CLOSE, sizeof(LV_SYMBOL_CLOSE));
            wifi_config_update_status("connect expired");
            break;
        default:
            break;
    }
}

void status_bar_update_battery_level(int level_percentage) {
    if (level_percentage < 0) {
        // Unknown state
        memcpy(status_bar_battery_state_str, LV_SYMBOL_WARNING, sizeof(LV_SYMBOL_WARNING));
    } else if (level_percentage < 10) {
        memcpy(status_bar_battery_state_str, LV_SYMBOL_BATTERY_EMPTY, sizeof(LV_SYMBOL_BATTERY_EMPTY));
    } else if (level_percentage < 40) {
        memcpy(status_bar_battery_state_str, LV_SYMBOL_BATTERY_1, sizeof(LV_SYMBOL_BATTERY_1));
    } else if (level_percentage < 70) {
        memcpy(status_bar_battery_state_str, LV_SYMBOL_BATTERY_2, sizeof(LV_SYMBOL_BATTERY_2));
    } else if (level_percentage < 90) {
        memcpy(status_bar_battery_state_str, LV_SYMBOL_BATTERY_3, sizeof(LV_SYMBOL_BATTERY_3));
    } else if (level_percentage <= 100) {
        memcpy(status_bar_battery_state_str, LV_SYMBOL_BATTERY_FULL, sizeof(LV_SYMBOL_BATTERY_FULL));
    } else {
        memcpy(status_bar_battery_state_str, LV_SYMBOL_USB, sizeof(LV_SYMBOL_USB));
    }
}
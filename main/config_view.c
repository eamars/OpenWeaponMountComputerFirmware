#include "config_view.h"

#include "esp_err.h"
#include "esp_log.h"

#include "digital_level_view.h"
#include "system_config.h"


#define TAG "ConfigView"

lv_obj_t * config_menu;


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


static void lv_spinbox_increment_event_cb(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_user_data(e);
    lv_spinbox_increment(spinbox);
}

static void lv_spinbox_decrement_event_cb(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_user_data(e);
    lv_spinbox_decrement(spinbox);
}

lv_obj_t * create_spin_box(lv_obj_t * container, 
                           int32_t range_min, int32_t range_max, int32_t digit_count, int32_t sep_pos, int32_t default_value,
                           lv_event_cb_t event_cb, void *event_cb_args) {
    // Add buttons
    lv_obj_t * minus_button = lv_button_create(container);
    lv_obj_t * spinbox = lv_spinbox_create(container);
    lv_obj_t * plus_button = lv_button_create(container);

    lv_spinbox_set_value(spinbox, default_value);

    // configure spinbox    
    lv_spinbox_set_range(spinbox, range_min, range_max);
    lv_spinbox_set_digit_format(spinbox, digit_count, sep_pos);
    lv_spinbox_set_step(spinbox, 1);
    lv_obj_set_flex_grow(spinbox, 1);

    lv_obj_set_style_bg_image_src(minus_button, LV_SYMBOL_MINUS, 0);
    lv_obj_add_event_cb(minus_button, lv_spinbox_decrement_event_cb, LV_EVENT_SINGLE_CLICKED, (void *) spinbox);

    lv_obj_set_style_bg_image_src(plus_button, LV_SYMBOL_PLUS, 0);
    lv_obj_add_event_cb(plus_button, lv_spinbox_increment_event_cb, LV_EVENT_SINGLE_CLICKED, (void *) spinbox);
    
    lv_obj_add_event_cb(spinbox, event_cb, LV_EVENT_VALUE_CHANGED, event_cb_args);

    // Configure layout
    lv_obj_add_flag(minus_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_height(minus_button, lv_obj_get_height(spinbox));
    lv_obj_set_width(minus_button, lv_pct(30));
    lv_obj_set_height(plus_button, lv_obj_get_height(spinbox));
    lv_obj_set_width(plus_button, lv_pct(30));

    return container;
}


static void lv_colour_picker_left_event_cb(lv_event_t *e) {
    lv_obj_t * colour_indicator = lv_event_get_user_data(e);
    lv_palette_t * colour_idx = lv_obj_get_user_data(colour_indicator);

    // Take the previous colour
    if (*colour_idx > 0) {
        *colour_idx -= 1;
    }

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

    // Apply the colour
    lv_led_set_color(colour_indicator, lv_palette_main(*colour_idx));

    // Apply callback
    lv_obj_send_event(colour_indicator, LV_EVENT_VALUE_CHANGED, NULL);
    ESP_LOGI(TAG, "Current colour %d", *colour_idx);
}


lv_obj_t * create_colour_picker(lv_obj_t * container, lv_palette_t default_colour, lv_event_cb_t event_cb, void *event_cb_args) {
    lv_obj_t * left_button = lv_button_create(container);
    lv_obj_t * colour_indicator = lv_led_create(container);
    lv_obj_t * right_button = lv_button_create(container);

    lv_palette_t * colour_idx = malloc(sizeof(lv_palette_t)); 
    *colour_idx = default_colour;

    lv_obj_set_user_data(colour_indicator, colour_idx);
    lv_led_set_color(colour_indicator, lv_palette_main(*colour_idx));  // set default colour

    // FIXME: The LED object won't emit value change event therefore the callback needed to be called manually
    lv_obj_add_event_cb(colour_indicator, event_cb, LV_EVENT_VALUE_CHANGED, event_cb_args);

    lv_obj_set_style_bg_image_src(left_button, LV_SYMBOL_LEFT, 0);
    lv_obj_set_height(left_button, 36);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(left_button, lv_pct(30));
    lv_obj_add_event_cb(left_button, lv_colour_picker_left_event_cb, LV_EVENT_SINGLE_CLICKED, (void *) colour_indicator);

    lv_obj_set_style_bg_image_src(right_button, LV_SYMBOL_RIGHT, 0);
    lv_obj_set_height(right_button, 36);
    lv_obj_set_width(right_button, lv_pct(30));
    lv_obj_add_event_cb(right_button, lv_colour_picker_right_event_cb, LV_EVENT_SINGLE_CLICKED, (void *) colour_indicator);


    // set style
    lv_obj_add_flag(left_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);    // add a new line before the widget
    lv_obj_align(colour_indicator, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(right_button, LV_ALIGN_RIGHT_MID, 0, 0);

    return container;
}


lv_obj_t * create_dropdown_list(lv_obj_t * container, const char * options, lv_event_cb_t event_cb, void * event_cb_args) {
    lv_obj_t * dropdown_list_option = lv_dropdown_create(container);
    lv_obj_set_size(dropdown_list_option, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(dropdown_list_option, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_dropdown_set_options(dropdown_list_option, options);
    lv_obj_add_event_cb(dropdown_list_option, event_cb, LV_EVENT_VALUE_CHANGED, event_cb_args);
    return dropdown_list_option;
}



void create_config_view(lv_obj_t *parent) {
    config_menu = lv_menu_create(parent);
    lv_obj_set_size(config_menu, lv_pct(100), lv_pct(100));
    lv_obj_center(config_menu);
    lv_obj_set_style_bg_color(config_menu, lv_color_darken(lv_color_white(), 20), 0);

    lv_obj_t * back_button = lv_menu_get_main_header_back_button(config_menu);
    lv_obj_t * back_button_label = lv_label_create(back_button);
    lv_label_set_text(back_button_label, "Back");


    /*Create a main page*/
    lv_obj_t * main_page = lv_menu_page_create(config_menu, "Settings");

    create_system_config_view_config(config_menu, main_page);
    create_digital_level_view_config(config_menu, main_page);

    lv_menu_set_page(config_menu, main_page);
}



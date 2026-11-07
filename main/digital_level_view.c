#include <limits.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs.h"
#include "esp_crc.h"
#include "esp_check.h"

#include "digital_level_view.h"
#include "app_cfg.h"
#include "bno085.h"
#include "esp_lvgl_port.h"
#include "countdown_timer.h"
#include "config_view.h"
#include "dope_config_view.h"
#include "level_band.h"
#include "display_shape.h"
#include "common.h"
#include "system_config.h"
#include "digital_level_view_controller.h"
#include "digital_level_view_type_1.h"
#include "digital_level_view_type_2.h"


#define TAG "DigitalLevelView"
#define NVS_NAMESPACE "DLV"



HEAPS_CAPS_ATTR digital_level_view_config_t digital_level_view_config;
const digital_level_view_config_t digital_level_view_config_default = {
    .roll_display_gain = 1.0f,
    .pitch_display_gain = 0.1f, // Default gain of 1/10
    .delta_level_threshold = 1.0f, // Default level threshold
    .user_roll_rad_offset = 0.0,       // Default to no offset
    .colour_left_tilt_indicator = LV_PALETTE_LIGHT_BLUE,
    .colour_right_tilt_indicator = LV_PALETTE_AMBER,
    .colour_horizontal_level_indicator = LV_PALETTE_LIGHT_GREEN,
    .colour_foreground = LV_PALETTE_BLACK,
    .level_style = DIGITAL_LEVEL_VIEW_TYPE_1,
};

static lv_obj_t * tilt_angle_label = NULL;
static lv_obj_t * tilt_angle_button = NULL;
static lv_obj_t * parent_container = NULL;
static lv_obj_t * overlay_container = NULL;

HEAPS_CAPS_ATTR countdown_timer_t countdown_timer;
digital_level_view_t * current_digital_level_view;


// NOT threadsafe copy of roll and pitch to be shared within the module


// Forward declaration
esp_err_t load_digital_level_view_config();
esp_err_t save_digital_level_view_config();


// A short message over the level ("Hold to set zero"), gone after a moment
#define HINT_SHOW_MS 1500
static lv_obj_t * hint_label = NULL;
static lv_timer_t * hint_timer = NULL;

static void hint_timeout_cb(lv_timer_t * timer) {
    lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

static void show_hint(const char * text) {
    lv_label_set_text(hint_label, text);
    lv_obj_remove_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(hint_label);
    lv_timer_reset(hint_timer);
    lv_timer_resume(hint_timer);
}

static void create_hint(lv_obj_t * parent) {
    hint_label = lv_label_create(parent);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(hint_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hint_label, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hint_label, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hint_label, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(hint_label, 12, LV_PART_MAIN);
    lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);

    hint_timer = lv_timer_create(hint_timeout_cb, HINT_SHOW_MS, NULL);
    lv_timer_set_auto_delete(hint_timer, false);
    lv_timer_set_repeat_count(hint_timer, -1);
    lv_timer_pause(hint_timer);
}


// The tilt box (ADR-007): a tap says how to zero; holding it sets the zero. A touch on the clock cannot move it
void tilt_angle_button_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
        show_hint("Hold to set zero");
        return;
    }

    // Take a snapshot of current roll and use that as offset (take account of the screen rotation). The zero is a
    // calibration, so it is saved, as after a tare
    digital_level_view_config.user_roll_rad_offset = -1 * (sensor_roll_thread_unsafe - system_config.rotation * M_PI_2);
    ESP_LOGI(TAG, "user_roll_rad_offset := %f", digital_level_view_config.user_roll_rad_offset);

    esp_err_t ret = save_digital_level_view_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save the level zero: %s", esp_err_to_name(ret));
    }
    show_hint(ret == ESP_OK ? "Zero set" : "Zero set, not saved");
}


static void overlay_clicked_cb(lv_event_t * e) {
    if (get_countdown_timer_widget_enabled() && get_countdown_timer_state(&countdown_timer) == COUNTDOWN_TIMER_PAUSE) {
        countdown_timer_continue(&countdown_timer);
    }
}

void digital_level_view_zero_after_tare() {
    // After a tare the sensor reads roll 0 in the tare pose, but the level subtracts rotation x 90 deg, which
    // assumes the untared sensor; so at rotation 2 the tare pose read 180. The offset cancels that term, so the
    // tare pose reads 0 at the current rotation. Saved with the level's settings, as the tare is saved in the
    // sensor, so the two stay together across reboots (ADR-005 B25)
    digital_level_view_config.user_roll_rad_offset = system_config.rotation * M_PI_2;
    ESP_LOGI(TAG, "user_roll_rad_offset := %f (after tare)", digital_level_view_config.user_roll_rad_offset);

    esp_err_t ret = save_digital_level_view_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save the level zero after tare: %s", esp_err_to_name(ret));
    }
}


// Degrees the label shows; INT_MIN when the label is new and still shows "--"
static int tilt_angle_label_deg = INT_MIN;
static lv_obj_t * tilt_sign_label = NULL;

void update_roll_deg_indicator(float roll_rad) {
    // Setting the text redraws the label, so set it only when the shown number changes. The digits stay centred
    // and the minus sign hangs to their left, so the number does not jump when it turns negative (ADR-007)
    const int deg = (int) roundf(RAD_TO_DEG(roll_rad));
    if (deg != tilt_angle_label_deg) {
        lv_label_set_text_fmt(tilt_angle_label, "%d", deg < 0 ? -deg : deg);
        lv_label_set_text(tilt_sign_label, deg < 0 ? "-" : "");
        lv_obj_update_layout(tilt_angle_label);
        lv_obj_align_to(tilt_sign_label, tilt_angle_label, LV_ALIGN_OUT_LEFT_MID, -2, 0);
        tilt_angle_label_deg = deg;
    }
}


void set_rotation_roll_deg_indicator(lv_display_rotation_t display_rotation){
    // Centred at the top in both rotations (ADR-007)
    lv_obj_align(tilt_angle_button, LV_ALIGN_TOP_MID, 0, DISPLAY_MARGIN_PX);
}

void update_digital_level_view(float roll_rad, float pitch_rad)
 {
    // Update the tilt angle label
    update_roll_deg_indicator(roll_rad);;

    // Update background widget
    if (current_digital_level_view != NULL) {
        current_digital_level_view->update_callback(roll_rad, pitch_rad);
    }
}


void create_roll_deg_indicator(lv_obj_t * parent) {
    // Create button
    static lv_style_t btn_style;  // NOTE: "static" is required to hold the style within the memory
    lv_style_init(&btn_style);
    lv_style_set_bg_opa(&btn_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&btn_style, 2);
    lv_style_set_border_color(&btn_style, lv_color_white());
    lv_style_set_shadow_width(&btn_style, 0);

    tilt_angle_button = lv_btn_create(parent);
    lv_obj_add_style(tilt_angle_button, &btn_style, LV_PART_MAIN);
    lv_obj_set_width(tilt_angle_button, 80);
    lv_obj_add_event_cb(tilt_angle_button, tilt_angle_button_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(tilt_angle_button, tilt_angle_button_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    set_rotation_roll_deg_indicator(system_config.rotation);

    // Create label on the button
    tilt_angle_label = lv_label_create(tilt_angle_button);
    lv_label_set_text(tilt_angle_label, "--");
    tilt_angle_label_deg = INT_MIN;
    lv_obj_set_style_text_color(tilt_angle_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(tilt_angle_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(tilt_angle_label, LV_ALIGN_CENTER, 0, 0);

    tilt_sign_label = lv_label_create(tilt_angle_button);
    lv_label_set_text(tilt_sign_label, "");
    lv_obj_set_style_text_color(tilt_sign_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(tilt_sign_label, &lv_font_montserrat_32, LV_PART_MAIN);
}


void create_digital_level_overlay(lv_obj_t *parent)
{
    overlay_container = lv_obj_create(parent);
    lv_obj_set_style_border_width(overlay_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay_container, 0, 0);
    lv_obj_set_size(overlay_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(overlay_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_center(overlay_container);

    create_roll_deg_indicator(overlay_container);
    countdown_timer_setup(&countdown_timer);
    level_band_create(overlay_container, &countdown_timer);
    create_hint(overlay_container);

    // While the timer is armed or paused, a tap anywhere on the level (not the band or the tilt box) starts it:
    // the largest target on the screen for the moment of the beep (ADR-007)
    lv_obj_add_event_cb(overlay_container, overlay_clicked_cb, LV_EVENT_SHORT_CLICKED, NULL);
}


digital_level_view_t * select_digital_level_view(digital_level_view_type_t level_style) {
    digital_level_view_t * selected = NULL;
    // Load digital level view widget
    switch(level_style) {
        case DIGITAL_LEVEL_VIEW_TYPE_1:
            selected = &digital_level_view_type_1_context;
            break;
        case DIGITAL_LEVEL_VIEW_TYPE_2:
            selected = &digital_level_view_type_2_context;
            break;
        default:
            break;
    }
    return selected;
}

void create_digital_level_view(lv_obj_t *parent)
{
    parent_container = parent;

    // The settings are read at start-up (app_main)

    // Load digital level view widget
    current_digital_level_view = select_digital_level_view(digital_level_view_config.level_style);

    // Create widget
    current_digital_level_view->constructor(parent);

    // Create overlays
    create_digital_level_overlay(parent);

    // Initialize the digital level view controller tasks
    ESP_ERROR_CHECK(digital_level_view_controller_init());

    // Handle rotation change event
    lv_obj_add_event_cb(parent, digital_level_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);
}


/* 
----------------------------------------------------
Configuration Menu Items
----------------------------------------------------
*/

static void update_float_item_gain_10(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_target_obj(e);
    float * target_ptr = lv_event_get_user_data(e);
    int32_t value = lv_spinbox_get_value(spinbox);
    *target_ptr = value / 10.0;
}


static void update_roll_offset(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_target_obj(e);
    float * target_ptr = lv_event_get_user_data(e);
    int32_t value = lv_spinbox_get_value(spinbox);
    
    ESP_LOGI(TAG, "Roll offset updated to %d", value);
}


static lv_obj_t * digital_level_config_page = NULL;


// Show the style the settings ask for (the style dropdown, Reload, Reset); the level reads its other settings live
static void apply_digital_level_view_style() {
    digital_level_view_t * wanted = select_digital_level_view(digital_level_view_config.level_style);
    if (wanted == NULL || wanted == current_digital_level_view) {
        return;
    }

    // LVGL task (an LVGL event), so the swap needs no lock
    current_digital_level_view->destructor(current_digital_level_view->container);
    current_digital_level_view = wanted;
    current_digital_level_view->constructor(parent_container);

    // Important! Move overlay to foreground
    lv_obj_move_foreground(overlay_container);
}

static void on_save_button_pressed(lv_event_t * e) {
    update_info_msg_box_result(msg_box, save_digital_level_view_config(), "Configuration Saved", "Save failed");
}

static void on_reload_button_pressed(lv_event_t * e) {
    esp_err_t ret = load_digital_level_view_config();
    apply_digital_level_view_style();
    config_page_rebuild(digital_level_config_page);

    update_info_msg_box_result(msg_box, ret, "Previous Configuration Reloaded", "Reload failed");
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values. Reset restores the preferences, not the calibration: the zero (the mounting
    // offset, and after a tare the rotation it was taken at) stays, as the tare stays in the sensor (owner, ADR-005)
    float zero_rad = digital_level_view_config.user_roll_rad_offset;
    memcpy(&digital_level_view_config, &digital_level_view_config_default, sizeof(digital_level_view_config));
    digital_level_view_config.user_roll_rad_offset = zero_rad;
    apply_digital_level_view_style();
    config_page_rebuild(digital_level_config_page);

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");
}


void digital_level_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_roll_deg_indicator(system_config.rotation);
    level_band_set_rotation(system_config.rotation);
}


static void update_level_style(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    digital_level_view_config.level_style = (digital_level_view_type_t) lv_dropdown_get_selected(dropdown);
    ESP_LOGI(TAG, "Level style set to %d", digital_level_view_config.level_style);

    apply_digital_level_view_style();
}


// The page's widgets, built from the settings; config_page_rebuild() runs it again after Reload and Reset
static void fill_digital_level_config_page(lv_obj_t * sub_page_config_view) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    // Type
    const char level_style_options[] = "Level\nBlock";
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Level Style");
    config_item = create_dropdown_list(container, level_style_options, digital_level_view_config.level_style, update_level_style, NULL);

    // Roll display gain
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Roll Display Gain");
    config_item = create_spin_box(container, 10, 20, 1, 2, 1, (int32_t) (digital_level_view_config.roll_display_gain * 10), update_float_item_gain_10, &digital_level_view_config.roll_display_gain);

    // Pitch display gain
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Pitch Display Gain");
    config_item = create_spin_box(container, 1, 10, 1, 2, 1, (int32_t) (digital_level_view_config.pitch_display_gain * 10), update_float_item_gain_10, &digital_level_view_config.pitch_display_gain);

    // delta level threshold
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Level Threshold (deg)");
    config_item = create_spin_box(container, 5, 20, 1, 2, 1, (int32_t) (digital_level_view_config.delta_level_threshold * 10), update_float_item_gain_10, &digital_level_view_config.delta_level_threshold);

    // Left tilt indicator colour
    container = create_menu_container_with_text(sub_page_config_view, LV_SYMBOL_EYE_OPEN, "Left Tilt Colour");
    config_item = create_colour_picker(container, &digital_level_view_config.colour_left_tilt_indicator, NULL);

    // Right tilt indicator colour
    container = create_menu_container_with_text(sub_page_config_view, LV_SYMBOL_EYE_OPEN, "Right Tilt Colour");
    config_item = create_colour_picker(container, &digital_level_view_config.colour_right_tilt_indicator, NULL);

    // Horizontal tilt indicator colour
    container = create_menu_container_with_text(sub_page_config_view, LV_SYMBOL_EYE_OPEN, "Leveled Colour");
    config_item = create_colour_picker(container, &digital_level_view_config.colour_horizontal_level_indicator, NULL);

    // Horizontal tilt indicator colour
    container = create_menu_container_with_text(sub_page_config_view, LV_SYMBOL_EYE_OPEN, "Foreground Colour");
    config_item = create_colour_picker(container, &digital_level_view_config.colour_foreground, NULL);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    config_item = create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);
}


lv_obj_t * create_digital_level_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * sub_page_config_view = create_config_page(parent, fill_digital_level_config_page);
    digital_level_config_page = sub_page_config_view;

    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "Digital Level");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);

    return sub_page_config_view;
}


esp_err_t load_digital_level_view_config() {
    return load_config(NVS_NAMESPACE, &digital_level_view_config, &digital_level_view_config_default, sizeof(digital_level_view_config));
}

esp_err_t save_digital_level_view_config() {
    return save_config(NVS_NAMESPACE, &digital_level_view_config, sizeof(digital_level_view_config));
}

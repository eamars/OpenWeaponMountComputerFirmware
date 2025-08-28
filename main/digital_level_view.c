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
#include "common.h"
#include "system_config.h"
#include "digital_level_view_controller.h"
#include "digital_level_view_type_1.h"
#include "digital_level_view_type_2.h"


#define TAG "DigitalLevelView"
#define DIGITAL_LEVEL_VIEW_NAMESPACE "DLV"



digital_level_view_config_t digital_level_view_config;
const digital_level_view_config_t digital_level_view_config_default = {
    .roll_display_gain = 1.0f,
    .pitch_display_gain = 0.1f, // Default gain of 1/10
    .delta_level_threshold = 1.0f, // Default level threshold
    .user_roll_rad_offset = 0.0,       // Default to no offset
    .colour_left_tilt_indicator = LV_PALETTE_LIGHT_BLUE,
    .colour_right_tilt_indicator = LV_PALETTE_AMBER,
    .colour_horizontal_level_indicator = LV_PALETTE_LIGHT_GREEN,
    .colour_foreground = LV_PALETTE_BLACK,
    .auto_start_countdown_timer_on_recoil = true,
    .auto_move_dope_card_on_recoil = true,
    .level_style = DIGITAL_LEVEL_VIEW_TYPE_1,
};

static lv_obj_t * tilt_angle_label = NULL;
static lv_obj_t * tilt_angle_button = NULL;
static lv_obj_t * parent_container = NULL;
static lv_obj_t * overlay_container = NULL;

extern system_config_t system_config;
countdown_timer_t countdown_timer;
digital_level_view_t * current_digital_level_view;

extern digital_level_view_t digital_level_view_type_1_context;
extern digital_level_view_t digital_level_view_type_2_context;

// NOT threadsafe copy of roll and pitch to be shared within the module
extern float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;
extern float sensor_x_acceleration_thread_unsafe;


// Forward declaration
esp_err_t load_digital_level_view_config();
esp_err_t save_digital_level_view_config();


void tilt_angle_button_short_press_cb(lv_event_t * e) {
    // Take a snapshot of current roll and use that as offset (take account of the screen rotation)
    digital_level_view_config.user_roll_rad_offset = -1 * (sensor_roll_thread_unsafe - system_config.rotation * M_PI_2);

    ESP_LOGI(TAG, "user_roll_rad_offset := %f", digital_level_view_config.user_roll_rad_offset);
}

 void update_roll_deg_indicator(float roll_rad) {
    lv_label_set_text_fmt(tilt_angle_label, "%d", (int) roundf(RAD_TO_DEG(roll_rad)));
}


void set_rotation_roll_deg_indicator(lv_display_rotation_t display_rotation){
    if (display_rotation == LV_DISPLAY_ROTATION_0 || display_rotation == LV_DISPLAY_ROTATION_180) {
        lv_obj_align(tilt_angle_button, LV_ALIGN_TOP_MID, 0, 20);
    }
    else {
        lv_obj_align(tilt_angle_button, LV_ALIGN_RIGHT_MID, -20, -20);
    }
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
    lv_obj_add_event_cb(tilt_angle_button, tilt_angle_button_short_press_cb, LV_EVENT_SINGLE_CLICKED, NULL);

    set_rotation_roll_deg_indicator(system_config.rotation);

    // Create label on the button
    tilt_angle_label = lv_label_create(tilt_angle_button);
    lv_label_set_text(tilt_angle_label, "--");
    lv_obj_set_style_text_color(tilt_angle_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(tilt_angle_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(tilt_angle_label, LV_ALIGN_CENTER, 0, 0);
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
    create_countdown_timer_widget(overlay_container, &countdown_timer);
    create_dope_card_list_widget(overlay_container);
}


digital_level_view_t * select_digital_level_view(digital_level_view_type_t level_style) {
    digital_level_view_t * selected = NULL;
    // Load digital level view widget
    switch(digital_level_view_config.level_style) {
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

    // Read configuration from NVS
    ESP_ERROR_CHECK(load_digital_level_view_config());

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


static void on_save_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(save_digital_level_view_config());

    update_info_msg_box("Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_digital_level_view_config());

    update_info_msg_box("Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&digital_level_view_config, &digital_level_view_config_default, sizeof(digital_level_view_config));

    update_info_msg_box("Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


void digital_level_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_roll_deg_indicator(system_config.rotation);
    set_rotation_countdown_timer_widget(system_config.rotation);
}


static void update_level_style(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected != digital_level_view_config.level_style) {
        digital_level_view_config.level_style = (digital_level_view_type_t) selected;
        ESP_LOGI(TAG, "Level style updated to %d", digital_level_view_config.level_style);

        // Deinitialize the previous style
        // TODO: Add lock
        current_digital_level_view->destructor(current_digital_level_view->container);
        current_digital_level_view = NULL;

        // Load digital level view widget
        current_digital_level_view = select_digital_level_view(digital_level_view_config.level_style);

        // Create widget
        current_digital_level_view->constructor(parent_container);

        // Important! Move overlay to foreground
         lv_obj_move_foreground(overlay_container);
        
    }
    else {
        ESP_LOGI(TAG, "Level style unchanged");
    }
}


lv_obj_t * create_digital_level_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    // Create a sub page for digital_level_view
    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

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

    // auto_start_countdown_timer_on_recoil
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Auto Start Countdown Timer on Recoil");
    config_item = create_switch(container, &digital_level_view_config.auto_start_countdown_timer_on_recoil, NULL);

    // auto_move_dope_card_on_recoil
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Auto Move Dope Card on Recoil");
    config_item = create_switch(container, &digital_level_view_config.auto_move_dope_card_on_recoil, NULL);

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
    esp_err_t err;

    // Read configuration from NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(DIGITAL_LEVEL_VIEW_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", DIGITAL_LEVEL_VIEW_NAMESPACE);

    size_t required_size = sizeof(digital_level_view_config);
    err = nvs_get_blob(handle, "cfg", &digital_level_view_config, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Initialize digital_level_view_config with default values");

        // Initialize with default values
        memcpy(&digital_level_view_config, &digital_level_view_config_default, sizeof(digital_level_view_config));
        // Calculate CRC
        digital_level_view_config.crc32 = crc32_wrapper(&digital_level_view_config, sizeof(digital_level_view_config), sizeof(digital_level_view_config.crc32));

        // Write to NVS
        ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &digital_level_view_config, required_size), TAG, "Failed to write NVS blob");
        ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");
    } else {
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to read NVS blob");
    }

    // Verify CRC
    uint32_t crc32 = crc32_wrapper(&digital_level_view_config, sizeof(digital_level_view_config), sizeof(digital_level_view_config.crc32));

    if (crc32 != digital_level_view_config.crc32) {
        ESP_LOGW(TAG, "CRC32 mismatch, will use default settings. Expected %p, got %p", digital_level_view_config.crc32, crc32);
        memcpy(&digital_level_view_config, &digital_level_view_config_default, sizeof(digital_level_view_config));

        ESP_ERROR_CHECK(save_digital_level_view_config());
    }
    else {
        ESP_LOGI(TAG, "Digital level view configuration loaded successfully");
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t save_digital_level_view_config() {
    size_t required_size = sizeof(digital_level_view_config);
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(DIGITAL_LEVEL_VIEW_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", DIGITAL_LEVEL_VIEW_NAMESPACE);

    // Calculate CRC
    digital_level_view_config.crc32 = crc32_wrapper(&digital_level_view_config, sizeof(digital_level_view_config), sizeof(digital_level_view_config.crc32));

    // Write to NVS
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &digital_level_view_config, required_size), TAG, "Failed to write NVS blob");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");

    ESP_LOGI(TAG, "Digital level view configuration saved successfully");

    nvs_close(handle);

    return ESP_OK;
}

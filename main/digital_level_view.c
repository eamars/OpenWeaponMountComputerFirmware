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
    .auto_move_dope_card_on_recoil = true
};

lv_obj_t * tilt_angle_label = NULL;
lv_obj_t * horizontal_indicator_line_left = NULL;
lv_obj_t * horizontal_indicator_line_right = NULL;
lv_obj_t * digital_level_bg_canvas = NULL;
lv_obj_t * tilt_angle_button = NULL;

extern system_config_t system_config;
countdown_timer_t countdown_timer;
uint8_t *lv_canvas_draw_buffer;

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


void update_tilt_canvas_draw(float roll_rad, float pitch_rad){
    int32_t disp_width, disp_height;
    if (system_config.rotation == LV_DISPLAY_ROTATION_0 || system_config.rotation == LV_DISPLAY_ROTATION_180) {
        disp_width = DISP_H_RES_PIXEL;
        disp_height = DISP_V_RES_PIXEL;
    }
    else {
        disp_width = DISP_V_RES_PIXEL;
        disp_height = DISP_H_RES_PIXEL;
    }

    float max_delta_vertical_shift = disp_height/ 4.0;  // maximum vertical shift for the indicator lines
    float max_delta_vertical_vertex = disp_height / 2.0f - 20;  // Maximum vertical verticies for the triangle

    // -------------------------
    // Update line location based on pitch
    // -------------------------
    float delta_vertical_shift = -tanf(pitch_rad) * (disp_height / 2);
    
    // Apply gain to the pitch
    delta_vertical_shift *= digital_level_view_config.pitch_display_gain;

    // Limit the vertical shift to a maximum value
    if (delta_vertical_shift > max_delta_vertical_shift) delta_vertical_shift = max_delta_vertical_shift;
    if (delta_vertical_shift < -max_delta_vertical_shift) delta_vertical_shift = -max_delta_vertical_shift;

    // Update line location
    lv_obj_set_pos(horizontal_indicator_line_left, 0, delta_vertical_shift);
    lv_obj_set_pos(horizontal_indicator_line_right, 0, delta_vertical_shift);


    // Calculate vertical location for the polygon (drawn as triangle)
    float vertical_base_position = disp_height / 2 + delta_vertical_shift;

    // -------------------------
    // Update background canvas
    // -------------------------
    // Based on input roll, fill the background canvas with different colors
    float threshold_rad = DEG_TO_RAD(digital_level_view_config.delta_level_threshold);
    if (roll_rad < -threshold_rad) {
        lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_OPA_COVER);
    }
    else if (roll_rad > threshold_rad) {
        lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_OPA_COVER);
    }
    else {
        lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator), LV_OPA_COVER);
    }

    // Draw new layer on the canvas
    lv_layer_t layer;
    lv_canvas_init_layer(digital_level_bg_canvas, &layer);

    // Special case: If below than threshold, draw rectangle instead
    if (fabsf(roll_rad) < threshold_rad) {
        // Draw rectangle
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator);
        lv_area_t coords = {0, disp_height / 2, disp_width, disp_height};
        lv_draw_rect(&layer, &rect_dsc, &coords);
    }
    else {
        // Calculate verticies for the triangle
        float dy = fabsf(tanf(roll_rad) * (disp_width / 2));

        // Apply gain
        dy *= digital_level_view_config.roll_display_gain;

        // Limit the vertical shift to a maximum value
        if (dy > max_delta_vertical_vertex) dy = max_delta_vertical_vertex;

        // Draw triangle
        lv_draw_triangle_dsc_t tri_dsc;
        lv_draw_triangle_dsc_init(&tri_dsc);
        tri_dsc.color = lv_palette_main(digital_level_view_config.colour_foreground);

        // Depending on the roll direction, set the points of the triangle
        if (roll_rad < 0) {
            tri_dsc.p[0].x = 0;
            tri_dsc.p[0].y = vertical_base_position - dy;
        }
        else {
            tri_dsc.p[0].x = disp_width;
            tri_dsc.p[0].y = vertical_base_position - dy;
        }
        tri_dsc.p[1].x = disp_width;
        tri_dsc.p[1].y = vertical_base_position + dy;
        tri_dsc.p[2].x = 0;
        tri_dsc.p[2].y = vertical_base_position + dy;
        lv_draw_triangle(&layer, &tri_dsc);

        // Draw rectangle
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_foreground);
        lv_area_t coords = {0, vertical_base_position + dy, disp_width, disp_height};
        lv_draw_rect(&layer, &rect_dsc, &coords);
    }
    lv_canvas_finish_layer(digital_level_bg_canvas, &layer);
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
    update_roll_deg_indicator(roll_rad);

    // Update the canvas drawing
    update_tilt_canvas_draw(roll_rad, pitch_rad);
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

    // TODO: Place in a function
    set_rotation_roll_deg_indicator(system_config.rotation);

    // Create label on the button
    tilt_angle_label = lv_label_create(tilt_angle_button);
    lv_label_set_text(tilt_angle_label, "--");
    lv_obj_set_style_text_color(tilt_angle_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(tilt_angle_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(tilt_angle_label, LV_ALIGN_CENTER, 0, 0);
}


void create_digital_level_layout(lv_obj_t *parent)
{
    create_roll_deg_indicator(parent);
    create_countdown_timer_widget(parent, &countdown_timer);
    create_dope_card_list_widget(parent);

    // Create two lines on both side of the screen to indicate the horizontal level
    static lv_style_t line_style;
    lv_style_init(&line_style);
    lv_style_set_line_width(&line_style, 8);
    lv_style_set_line_color(&line_style, lv_color_white());

    horizontal_indicator_line_left = lv_line_create(parent);
    horizontal_indicator_line_right = lv_line_create(parent);

    static lv_point_precise_t line_points[] = { { 0, 0 }, { 10, 0 } };
    lv_line_set_points(horizontal_indicator_line_left, line_points, 2); /*Set the points*/
    lv_obj_add_style(horizontal_indicator_line_left, &line_style, LV_PART_MAIN);
    lv_obj_align(horizontal_indicator_line_left, LV_ALIGN_LEFT_MID, 5, 0);

    lv_line_set_points(horizontal_indicator_line_right, line_points, 2); /*Set the points*/
    lv_obj_add_style(horizontal_indicator_line_right, &line_style, LV_PART_MAIN);
    lv_obj_align(horizontal_indicator_line_right, LV_ALIGN_RIGHT_MID, -5, 0);
}


void set_rotation_canvas(lv_display_rotation_t rotation)
{
    int32_t width, height;
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        width = DISP_H_RES_PIXEL;
        height = DISP_V_RES_PIXEL;
    }
    else {
        width = DISP_V_RES_PIXEL;
        height = DISP_H_RES_PIXEL;
    }

    lv_canvas_set_buffer(digital_level_bg_canvas, (void *) lv_canvas_draw_buffer, width, height, LV_COLOR_FORMAT_RGB565);
}

void create_digital_level_view(lv_obj_t *parent)
{
    // Read configuration from NVS
    ESP_ERROR_CHECK(load_digital_level_view_config());

    // Create a canvas and initialize its palette
    digital_level_bg_canvas = lv_canvas_create(parent);
    // Allocate memory for buffer
    uint32_t bpp = lv_color_format_get_bpp(LV_COLOR_FORMAT_RGB565); // = 16
    size_t buf_size = LV_CANVAS_BUF_SIZE(DISP_H_RES_PIXEL, DISP_V_RES_PIXEL, bpp, LV_DRAW_BUF_STRIDE_ALIGN);
    ESP_LOGI(TAG, "Canvas buffer size: %d bytes", buf_size);

    // lv_canvas_draw_buffer = malloc(buf_size);
    lv_canvas_draw_buffer = heap_caps_calloc(1, buf_size, MALLOC_CAP_DEFAULT);

    if (lv_canvas_draw_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for canvas draw buffer");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    set_rotation_canvas(system_config.rotation);
    lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator), LV_OPA_COVER);
    lv_obj_center(digital_level_bg_canvas);

    // Create overlays
    create_digital_level_layout(parent);

    // Set initial value
    update_digital_level_view(DEG_TO_RAD(0), DEG_TO_RAD(0));

    // Initialize the digital level view controller tasks
    ESP_ERROR_CHECK(digital_level_view_controller_init());
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
    set_rotation_canvas(system_config.rotation);
    set_rotation_countdown_timer_widget(system_config.rotation);
}


lv_obj_t * create_digital_level_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    // Create a sub page for digital_level_view
    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

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

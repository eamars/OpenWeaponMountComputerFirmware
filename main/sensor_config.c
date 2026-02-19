#include "sensor_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_check.h"
#include "lvgl.h"
#include "bno085.h"
#include "app_cfg.h"
#include "config_view.h"
#include "common.h"

#define TAG "SensorConfig"
#define NVS_NAMESPACE "SEC"

HEAPS_CAPS_ATTR sensor_config_t sensor_config;
const sensor_config_t sensor_config_default = {
    .recoil_acceleration_trigger_level = 10,
    .trigger_edge = TRIGGER_RISING_EDGE,
    .enable_game_rotation_vector_report = true,
    .enable_linear_acceleration_report = true,
    .enable_rotation_vector_report = true,
};


extern bno085_ctx_t * bno085_dev;

lv_obj_t * tare_calibration_button = NULL;
lv_obj_t * sensor_calibration_button = NULL;


extern lv_obj_t * tile_sensor_calibration_view;  // from main_tileview.c
extern lv_obj_t * main_tileview;                 // from main_tileview.c
extern lv_obj_t * last_tile_before_enter_calibration;  // from sensor_calibration_view.c
extern lv_obj_t * msg_box;  // from config_view.c

esp_err_t save_sensor_config() {
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);

    // Calculate CRC
    sensor_config.crc32 = crc32_wrapper(&sensor_config, sizeof(sensor_config), sizeof(sensor_config.crc32));

    // Write to NVS
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &sensor_config, sizeof(sensor_config)), TAG, "Failed to write NVS blob");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");

    ESP_LOGI(TAG, "Sensor configuration saved successfully");

    nvs_close(handle);
    return ESP_OK;
}


esp_err_t load_sensor_config() {
    esp_err_t ret;

    // Read configuration from NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);
    size_t required_size = sizeof(sensor_config_t);
    ret = nvs_get_blob(handle, "cfg", &sensor_config, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND || ret == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGI(TAG, "Initialize sensor_config with default values");

        // Initialize with default values
        memcpy(&sensor_config, &sensor_config_default, sizeof(sensor_config));
        // Calculate CRC
        sensor_config.crc32 = crc32_wrapper(&sensor_config, sizeof(sensor_config), sizeof(sensor_config.crc32));

        // Write to NVS
        ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &sensor_config, required_size), TAG, "Failed to write NVS blob");
        ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");
    } else {
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to read NVS blob");
    }

    // Verify CRC
    uint32_t crc32 = crc32_wrapper(&sensor_config, sizeof(sensor_config), sizeof(sensor_config.crc32));

    if (crc32 != sensor_config.crc32) {
        ESP_LOGW(TAG, "CRC32 mismatch, will use default settings. Expected %p, got %p", sensor_config.crc32, crc32);
        memcpy(&sensor_config, &sensor_config_default, sizeof(sensor_config));

        ESP_ERROR_CHECK(save_sensor_config());
    }
    else {
        ESP_LOGI(TAG, "Sensor configuration loaded successfully");
    }

    nvs_close(handle);
    return ESP_OK;
}


static void on_save_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(save_sensor_config());

    update_info_msg_box(msg_box, "Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_sensor_config());

    update_info_msg_box(msg_box, "Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&sensor_config, &sensor_config_default, sizeof(sensor_config));

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


static void update_uint32_item(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_target_obj(e);
    int32_t * target_ptr = lv_event_get_user_data(e);
    int32_t value = lv_spinbox_get_value(spinbox);
    *target_ptr = value;

    ESP_LOGI(TAG, "Value updated to %ld", *target_ptr);
}


static void toggle_linear_acceleration_report(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (*state == false) {
        ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, 0));
    }
}

static void toggle_game_rotation_vector_report(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (*state == false) {
        ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
    }
}

static void toggle_rotation_vector_report(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (*state == false) {
        ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));
    }
}



static void on_tare_calibration_button_pressed(lv_event_t * e) {
    // Store current tile
    last_tile_before_enter_calibration = lv_tileview_get_tile_active(main_tileview);

    // Move to tare calibration view
    lv_tileview_set_tile(main_tileview, tile_sensor_calibration_view, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
}

static void on_sensor_calibration_button_pressed(lv_event_t * e) {

}


lv_obj_t * create_sensor_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Recoil acceleration trigger level
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Recoil Trigger Level (m/s^2)");
    config_item = create_spin_box(container, 10, 100, 10, 3, 0, sensor_config.recoil_acceleration_trigger_level, update_uint32_item, &sensor_config.recoil_acceleration_trigger_level);

    // Game rotation vector (for level view)
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable Game Rotation Vector Report");
    config_item = create_switch(container, &sensor_config.enable_game_rotation_vector_report, toggle_game_rotation_vector_report);

    // Acceleration (for level view and acceleration view)
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable Linear Accel Report");
    config_item = create_switch(container, &sensor_config.enable_linear_acceleration_report, toggle_linear_acceleration_report);

    // Rotation vector (for POI view)
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable Rotation Vector Report");
    config_item = create_switch(container, &sensor_config.enable_rotation_vector_report, toggle_rotation_vector_report);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);

    // Tare calibration
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Tare Calibration");
    tare_calibration_button = create_single_button(container, LV_SYMBOL_WARNING, on_tare_calibration_button_pressed);

    // Sensor calibration
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Sensor Calibration");
    sensor_calibration_button = create_single_button(container, LV_SYMBOL_WARNING, on_sensor_calibration_button_pressed);

    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "Sensor Config");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);

    return sub_page_config_view;
}
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

sensor_config_t sensor_config;
const sensor_config_t sensor_config_default = {
    .recoil_acceleration_trigger_level = 20,
    .trigger_edge = TRIGGER_RISING_EDGE,
    .enable_game_rotation_vector_report = true,
    .enable_linear_acceleration_report = true
};


extern bno085_ctx_t bno085_dev;

esp_err_t save_sensor_config() {
    esp_err_t err;
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
    esp_err_t err;

    // Read configuration from NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);
    size_t required_size = sizeof(sensor_config_t);
    err = nvs_get_blob(handle, "cfg", &sensor_config, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Initialize sensor_config with default values");

        // Initialize with default values
        memcpy(&sensor_config, &sensor_config_default, sizeof(sensor_config));
        // Calculate CRC
        sensor_config.crc32 = crc32_wrapper(&sensor_config, sizeof(sensor_config), sizeof(sensor_config.crc32));

        // Write to NVS
        ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &sensor_config, required_size), TAG, "Failed to write NVS blob");
        ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");
    } else {
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to read NVS blob");
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

    update_info_msg_box("Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_sensor_config());

    update_info_msg_box("Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&sensor_config, &sensor_config_default, sizeof(sensor_config));

    update_info_msg_box("Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


static void update_uint32_item(lv_event_t *e) {
    lv_obj_t * spinbox = lv_event_get_target_obj(e);
    int32_t * target_ptr = lv_event_get_user_data(e);
    int32_t value = lv_spinbox_get_value(spinbox);
    *target_ptr = value;

    ESP_LOGI(TAG, "Value updated to %ld", *target_ptr);
    ESP_LOGI(TAG, "Sensor config updated: recoil_acceleration_trigger_level = %ld", sensor_config.recoil_acceleration_trigger_level);
}


static void toggle_linear_acceleration_report(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ESP_LOGI(TAG, "Linear Acceleration Report toggled: %s", *state ? "Enabled" : "Disabled");

    if (*state) {
        ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(&bno085_dev, SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS));
    }
    else {
        ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(&bno085_dev, 0));
    }
}


lv_obj_t * create_sensor_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Recoil acceleration trigger level
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Recoil Trigger Level (m/s^2)");
    config_item = create_spin_box(container, 10, 5000, 10, 4, 0, sensor_config.recoil_acceleration_trigger_level, update_uint32_item, &sensor_config.recoil_acceleration_trigger_level);

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable Linear Accel Report");
    config_item = create_switch(container, &sensor_config.enable_linear_acceleration_report, toggle_linear_acceleration_report);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);

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
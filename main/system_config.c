#include "system_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "config_view.h"

#include "common.h"

#define TAG "SystemConfig"
#define NVS_NAMESPACE "SC"


system_config_t system_config;
const system_config_t system_config_default = {
    .rotation = LV_DISPLAY_ROTATION_0,  // LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270
};


const char rotation_options[] = "0: 0°\n1: 90°\n2: 180°\n3: 270°";


esp_err_t load_system_config() {
    esp_err_t err;

    // Read configuration fron NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);
    size_t required_size = sizeof(system_config_t);
    err = nvs_get_blob(handle, "cfg", &system_config, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Initialize system_config with default values");

        // Initialize with default values
        memcpy(&system_config, &system_config_default, sizeof(system_config));
        // Calculate CRC
        system_config.crc32 = crc32_wrapper(&system_config, sizeof(system_config), sizeof(system_config.crc32));

        // Write to NVS
        ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &system_config, required_size), TAG, "Failed to write NVS blob");
        ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");
    } else {
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to read NVS blob");
    }

    // Verify CRC
    uint32_t crc32 = crc32_wrapper(&system_config, sizeof(system_config), sizeof(system_config.crc32));

    if (crc32 != system_config.crc32) {
        ESP_LOGW(TAG, "CRC32 mismatch, will use default settings. Expected %p, got %p", system_config.crc32, crc32);
        memcpy(&system_config, &system_config_default, sizeof(system_config));

        ESP_ERROR_CHECK(save_system_config());
    }
    else {
        ESP_LOGI(TAG, "Digital level view configuration loaded successfully");
    }

    nvs_close(handle);

    return ESP_OK;
}


esp_err_t save_system_config() {
    size_t required_size = sizeof(system_config);
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);

    // Calculate CRC
    system_config.crc32 = crc32_wrapper(&system_config, sizeof(system_config), sizeof(system_config.crc32));

    // Write to NVS
    ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &system_config, required_size), TAG, "Failed to write NVS blob");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");

    ESP_LOGI(TAG, "System configuration saved successfully");

    nvs_close(handle);

    return ESP_OK;
}


void update_rotation_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);
    
    if (selected != system_config.rotation) {
        system_config.rotation = (lv_display_rotation_t) selected;
        ESP_LOGI(TAG, "Rotation updated to %d", system_config.rotation);
        lv_display_set_rotation(lv_display_get_default(), system_config.rotation);
    }
    else {
        ESP_LOGI(TAG, "Rotation not changed");
    }
}


static void on_save_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(save_system_config());

    update_info_msg_box("Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_system_config());

    update_info_msg_box("Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&system_config, &system_config_default, sizeof(system_config));

    update_info_msg_box("Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


lv_obj_t * create_system_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Rotation
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Screen Rotation");
    config_item = create_dropdown_list(container, rotation_options, system_config.rotation, update_rotation_event_cb, NULL);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    lv_obj_t * save_button = lv_btn_create(container);
    lv_obj_t * reload_button = lv_btn_create(container);
    lv_obj_t * reset_button = lv_btn_create(container);

    // Save/reload Styling
    lv_obj_add_flag(save_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_style_bg_image_src(save_button, LV_SYMBOL_SAVE, 0);
    lv_obj_set_height(save_button, 36);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(save_button, lv_pct(30));
    lv_obj_add_event_cb(save_button, on_save_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_set_style_bg_image_src(reload_button, LV_SYMBOL_UPLOAD, 0);
    lv_obj_set_height(reload_button, 36);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(reload_button, lv_pct(30));
    lv_obj_add_event_cb(reload_button, on_reload_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_set_style_bg_image_src(reset_button, LV_SYMBOL_WARNING, 0);
    lv_obj_set_height(reset_button, 36);  // TODO: Find a better way to read the height from other widgets
    lv_obj_set_width(reset_button, lv_pct(30));
    lv_obj_add_event_cb(reset_button, on_reset_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "System Config");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);

    return sub_page_config_view;
}

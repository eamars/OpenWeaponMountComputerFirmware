#include "system_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "config_view.h"
#include "app_cfg.h"
#include "pmic_axp2101.h"
#include "low_power_mode.h"
#include "esp_lcd_types.h"
#include "common.h"
#include "bsp.h"

#define TAG "SystemConfig"
#define NVS_NAMESPACE "SC"


HEAPS_CAPS_ATTR system_config_t system_config;
const system_config_t system_config_default = {
    .rotation = LV_DISPLAY_ROTATION_0,      // LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270
    .idle_timeout = IDLE_TIMEOUT_5_MIN,
    .power_off_timeout = POWER_OFF_TIMEOUT_1_HRS,
    .screen_brightness_normal_pct = SCREEN_BRIGHTNESS_100_PCT,
    .screen_brightness_idle_pct = SCREEN_BRIGHTNESS_10_PCT,
};

extern esp_lcd_panel_io_handle_t io_handle;


const char rotation_options[] = "0°\n90°\n180°\n270°";
const char idle_timeout_options[] = "Never\n1 min\n5 min\n10 min\n10 sec";
const char power_off_timeout_options[] = "Never\n1 hrs\n2 hrs\n5 hrs\n 1 min";
const char screen_brightness_pct_options[] = "5%\n10%\n20%\n30%\n40%\n50%\n60%\n70%\n80%\n90%\n100%";


screen_brightness_pct_t screen_brightness_pct_option_to_pct(int32_t selected) {
    if (selected == 0) {
        return SCREEN_BRIGHTNESS_5_PCT;
    }
    else {
        return selected * 10;
    }
}


int32_t pct_to_screen_brightness_pct_option(screen_brightness_pct_t pct) {
    if (pct == SCREEN_BRIGHTNESS_5_PCT) {
        return 0;
    }
    else {
        return pct / 10;
    }
}


uint32_t idle_timeout_to_secs(idle_timeout_t timeout) {
    switch (timeout) {
        case IDLE_TIMEOUT_NEVER:
            return 0;
        case IDLE_TIMEOUT_1_MIN:
            return 60;
        case IDLE_TIMEOUT_5_MIN:
            return 300;
        case IDLE_TIMEOUT_10_MIN:
            return 600;
        case IDLE_TIMEOUT_10_SEC:
            return 10;
        default:
            return 0;
    }

    return 0;
}


uint32_t power_off_timeout_to_secs(power_off_timeout_t timeout) {
    switch (timeout) {
        case POWER_OFF_TIMEOUT_NEVER:
            return 0;
        case POWER_OFF_TIMEOUT_1_HRS:
            return 60 * 60;
        case POWER_OFF_TIMEOUT_2_HRS:
            return 60 * 60 * 2;
        case POWER_OFF_TIMEOUT_5_HRS:
            return 60 * 60 * 5;
        case POWER_OFF_TIMEOUT_1_MIN:
            return 60;
        default:
            return 0;
    }

    return 0;
}


esp_err_t load_system_config() {
    esp_err_t ret;

    // Read configuration fron NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);
    size_t required_size = sizeof(system_config_t);
    ret = nvs_get_blob(handle, "cfg", &system_config, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND || ret == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGI(TAG, "Initialize system_config with default values");

        // Initialize with default values
        memcpy(&system_config, &system_config_default, sizeof(system_config));
        // Calculate CRC
        system_config.crc32 = crc32_wrapper(&system_config, sizeof(system_config), sizeof(system_config.crc32));

        // Write to NVS
        ESP_RETURN_ON_ERROR(nvs_set_blob(handle, "cfg", &system_config, required_size), TAG, "Failed to write NVS blob");
        ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");
    } else {
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to read NVS blob");
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

void update_idle_timeout_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    system_config.idle_timeout = (idle_timeout_t) selected;
    ESP_LOGI(TAG, "Idle timeout updated to %d", system_config.idle_timeout);
}


void update_power_off_timeout_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    system_config.power_off_timeout = (idle_timeout_t) selected;
    ESP_LOGI(TAG, "Power off timeout updated to %d", system_config.power_off_timeout);
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


void update_normal_screen_brightness(lv_event_t * e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    system_config.screen_brightness_normal_pct = screen_brightness_pct_option_to_pct(selected);

    // Check if the system is in normal mode
    if (!is_low_power_mode_activated()) {
        set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
    }
}


void update_idle_screen_brightness(lv_event_t * e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    system_config.screen_brightness_idle_pct = screen_brightness_pct_option_to_pct(selected);

    // Check if the system is in normal mode
    if (is_low_power_mode_activated()) {
        set_display_brightness(&io_handle, system_config.screen_brightness_idle_pct);
    }
}


lv_obj_t * create_system_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Rotation
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Screen Rotation");
    config_item = create_dropdown_list(container, rotation_options, system_config.rotation, update_rotation_event_cb, NULL);

    // Idle timeout
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Idle Timeout");
    config_item = create_dropdown_list(container, idle_timeout_options, system_config.idle_timeout, update_idle_timeout_event_cb, NULL);

    // Shutdown timeout
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Power Off Timeout");
    config_item = create_dropdown_list(container, power_off_timeout_options, system_config.power_off_timeout, update_power_off_timeout_event_cb, NULL);


    // Screene brightness (normal)
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Screen Brightness");
    config_item = create_dropdown_list(container, screen_brightness_pct_options, pct_to_screen_brightness_pct_option(system_config.screen_brightness_normal_pct), update_normal_screen_brightness, NULL);

    // Screene brightness (idle)
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Idle Screen Brightness");
    config_item = create_dropdown_list(container, screen_brightness_pct_options, pct_to_screen_brightness_pct_option(system_config.screen_brightness_idle_pct), update_idle_screen_brightness, NULL);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);
    
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

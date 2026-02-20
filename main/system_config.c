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
    .rotation = LV_DISPLAY_ROTATION_180,      // LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270
    .screen_brightness_normal_pct = SCREEN_BRIGHTNESS_100_PCT,
    .global_log_level = ESP_LOG_INFO,
};

extern esp_lcd_panel_io_handle_t io_handle;
extern lv_obj_t * msg_box;

const char rotation_options[] = "0°\n90°\n180°\n270°";
const char screen_brightness_pct_options[] = "50%\n60%\n70%\n80%\n90%\n100%";
const char log_level_options[] = "None\nError\nWarn\nInfo\nDebug\nVerbose";


screen_brightness_pct_t screen_brightness_pct_option_to_pct(int32_t selected) {
    return (selected + 5) * 10;
}


int32_t pct_to_screen_brightness_pct_option(screen_brightness_pct_t pct) {
     return pct / 10;
}

esp_err_t load_system_config() {
    return load_config(NVS_NAMESPACE, &system_config, &system_config_default, sizeof(system_config));
}


esp_err_t save_system_config() {
    return save_config(NVS_NAMESPACE, &system_config, sizeof(system_config));
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

    update_info_msg_box(msg_box, "Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_system_config());

    update_info_msg_box(msg_box, "Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&system_config, &system_config_default, sizeof(system_config));

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


void update_normal_screen_brightness(lv_event_t * e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    system_config.screen_brightness_normal_pct = screen_brightness_pct_option_to_pct(selected);

    // Apply the changes directly
    if (!is_idle_mode_activated() && !is_sleep_mode_activated()) {
        set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
    }
}


void update_global_log_level(lv_event_t * e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    system_config.global_log_level = (esp_log_level_t) selected;

    esp_log_level_set("*", system_config.global_log_level);
    ESP_LOGE(TAG, "Global log level updated to %d", system_config.global_log_level);
}

lv_obj_t * create_system_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Rotation
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Screen Rotation");
    config_item = create_dropdown_list(container, rotation_options, system_config.rotation, update_rotation_event_cb, NULL);

    // Global log level
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Global Log Level");
    config_item = create_dropdown_list(container, log_level_options, system_config.global_log_level, update_global_log_level, NULL);

    // Screene brightness (normal)
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Screen Brightness");
    config_item = create_dropdown_list(container, screen_brightness_pct_options, pct_to_screen_brightness_pct_option(system_config.screen_brightness_normal_pct), update_normal_screen_brightness, NULL);

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

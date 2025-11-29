#include "pmic_axp2101.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "common.h"
#include "app_cfg.h"
#include "config_view.h"

#include "lvgl.h"

#define TAG "AXP2101"
#define NVS_NAMESPACE "PMG"


extern power_management_config_t power_management_config;

const char vbus_current_limit_options[] = "100mA\n500mA\n900mA\n1000mA\n1500mA\n2000mA";
const char battery_charge_current_options[] = "100mA\n200mA\n300mA\n400mA\n500mA\n600mA\n700mA\n800mA\n900mA\n1000mA";
const char battery_charge_voltage_options[] = "4.0V\n4.1V\n4.2V\n4.35V\n4.4V";


HEAPS_CAPS_ATTR power_management_config_t power_management_config;
const power_management_config_t default_power_management_config_t = {
    .crc32 = 0,
    .vbus_current_limit = PMIC_VBUS_CURRENT_LIMIT_500MA,
    .battery_charge_current = PMIC_BATTERY_CHARGE_CURRENT_500MA,
    .battery_charge_voltage = PMIC_BATTERY_CHARGE_VOLTAGE_4V2,
};

// from app.c
extern axp2101_ctx_t * axp2101_dev;


HEAPS_CAPS_ATTR static char status_str_l1[64] = {0};
HEAPS_CAPS_ATTR static char status_str_l2[64] = {0};


esp_err_t save_pmic_config() {
    esp_err_t ret;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);

    // Calculate CRC
    power_management_config.crc32 = crc32_wrapper(&power_management_config, sizeof(power_management_config), sizeof(power_management_config.crc32));

    // Write to NVS
    ret = nvs_set_blob(handle, "cfg", &power_management_config, sizeof(power_management_config));
    ESP_GOTO_ON_ERROR(ret, finally,  TAG, "Failed to write NVS blob");

    ret = nvs_commit(handle);
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to commit NVS changes");

finally:
    nvs_close(handle);

    return ret;
}


esp_err_t load_pmic_config() {
    esp_err_t ret;
    uint32_t crc32;

    // Read configuration from NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", NVS_NAMESPACE);

    size_t required_size = sizeof(power_management_config);
    ret = nvs_get_blob(handle, "cfg", &power_management_config, &required_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Initialize wifi_user_config with default values");

        // Copy default values
        memcpy(&power_management_config, &default_power_management_config_t, sizeof(power_management_config));
        power_management_config.crc32 = crc32_wrapper(&power_management_config, sizeof(power_management_config), sizeof(power_management_config.crc32));

        // Write to NVS
        ret = nvs_set_blob(handle, "cfg", &power_management_config, sizeof(power_management_config));
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to write NVS blob");
        ret = nvs_commit(handle);
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to commit NVS changes");
    }
    else {
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to read NVS blob");
    }

    // Verify CRC32
    crc32 = crc32_wrapper(&power_management_config, sizeof(power_management_config), sizeof(power_management_config.crc32));

    if (crc32 != power_management_config.crc32) {
        ESP_LOGW(TAG, "CRC32 mismatch, will use default settings. Expected %p, got %p", power_management_config.crc32, crc32);
        memcpy(&power_management_config, &default_power_management_config_t, sizeof(power_management_config));
        ESP_ERROR_CHECK(save_pmic_config());
    }
    else {
        ESP_LOGI(TAG, "pmic_config loaded successfully");
    }

finally:
    nvs_close(handle);

    return ret;
}


static void update_vbus_current_limit_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected != power_management_config.vbus_current_limit) {
        power_management_config.vbus_current_limit = (pmic_vbus_current_limit_e) selected;
        ESP_LOGI(TAG, "VBUS Current Limit updated to %d", power_management_config.vbus_current_limit);
    }
    else {
        ESP_LOGI(TAG, "VBUS Current Limit not changed");
    }
}


static void update_battery_charge_current_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected != power_management_config.battery_charge_current) {
        power_management_config.battery_charge_current = (pmic_battery_charge_current_e) selected;
        ESP_LOGI(TAG, "Battery Charge Current updated to %d", power_management_config.battery_charge_current);
    }
    else {
        ESP_LOGI(TAG, "Battery Charge Current not changed");
    }
}


static void update_battery_charge_voltage_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    if (selected != power_management_config.battery_charge_voltage) {
        power_management_config.battery_charge_voltage = (pmic_battery_charge_voltage_e) selected;
        ESP_LOGI(TAG, "Battery Charge Voltage updated to %d", power_management_config.battery_charge_voltage);
    }
    else {
        ESP_LOGI(TAG, "Battery Charge Voltage not changed");
    }
}


static void on_save_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(save_pmic_config());

    update_info_msg_box("Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_pmic_config());

    update_info_msg_box("Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&power_management_config, &default_power_management_config_t, sizeof(power_management_config));

    update_info_msg_box("Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}



lv_obj_t * create_power_management_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // FIXME: Update status once PMIC is working
    snprintf(status_str_l1, sizeof(status_str_l1), "SoC:%d,VBAT:%dmV", axp2101_dev->status.battery_percentage, axp2101_dev->status.vbatt_voltage_mv);
    snprintf(status_str_l2, sizeof(status_str_l2), "VBUS:%dmV,VSYS:%dmV", axp2101_dev->status.vbus_voltage_mv, axp2101_dev->status.vsys_voltage_mv);

    // Battery status label
    lv_obj_t * status_label_1 = create_config_label_static(sub_page_config_view, status_str_l1);
    lv_obj_t * status_label_2 = create_config_label_static(sub_page_config_view, status_str_l2);

    // VBUS current limit
    container = create_menu_container_with_text(sub_page_config_view, NULL, "VBUS Current Limit");
    config_item = create_dropdown_list(container, vbus_current_limit_options, (int32_t) power_management_config.vbus_current_limit, update_vbus_current_limit_event_cb, NULL);

    // Battery charge current
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Battery Charge Current");
    config_item = create_dropdown_list(container, battery_charge_current_options, (int32_t) power_management_config.battery_charge_current, update_battery_charge_current_event_cb, NULL);
    // Battery charge voltage
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Battery Charge Voltage");
    config_item = create_dropdown_list(container, battery_charge_voltage_options, (int32_t) power_management_config.battery_charge_voltage, update_battery_charge_voltage_event_cb, NULL);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);
    
    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "Power Management");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);

    return sub_page_config_view;
}

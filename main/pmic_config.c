#include "pmic_axp2101.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "common.h"
#include "app_cfg.h"
#include "config_view.h"
#include "ota_mode.h"

#include "lvgl.h"

#define TAG "AXP2101"
#define NVS_NAMESPACE "PMG"

static lv_obj_t * power_menu = NULL;
extern power_management_config_t power_management_config;

const char vbus_current_limit_options[] = "100mA\n500mA\n900mA\n1000mA\n1500mA\n2000mA";
const char battery_charge_current_options[] = "100mA\n200mA\n300mA\n400mA\n500mA\n600mA\n700mA\n800mA\n900mA\n1000mA";
const char battery_charge_voltage_options[] = "4.0V\n4.1V\n4.2V\n4.35V\n4.4V";
const char idle_timeout_options[] = "Never\n1 min\n5 min\n10 min\n10 sec";
const char sleep_timeout_options[] = "Never\n1 hrs\n2 hrs\n5 hrs\n 1 min";


HEAPS_CAPS_ATTR power_management_config_t power_management_config;
const power_management_config_t default_power_management_config = {
    .vbus_current_limit = PMIC_VBUS_CURRENT_LIMIT_500MA,
    .battery_charge_current = PMIC_BATTERY_CHARGE_CURRENT_500MA,
    .battery_charge_voltage = PMIC_BATTERY_CHARGE_VOLTAGE_4V2,
    .idle_timeout = IDLE_TIMEOUT_5_MIN,
    .sleep_timeout = SLEEP_TIMEOUT_1_HRS,
};

// from app.c
extern axp2101_ctx_t * axp2101_dev;
extern lv_obj_t * msg_box;

HEAPS_CAPS_ATTR static char status_str_l1[64] = {0};
HEAPS_CAPS_ATTR static char status_str_l2[64] = {0};


esp_err_t save_pmic_config() {
    return save_config(NVS_NAMESPACE, &power_management_config, sizeof(power_management_config));
}


esp_err_t load_pmic_config() {
    return load_config(NVS_NAMESPACE, &power_management_config, &default_power_management_config, sizeof(power_management_config));
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

uint32_t sleep_timeout_to_secs(sleep_timeout_t timeout) {
    switch (timeout) {
        case SLEEP_TIMEOUT_NEVER:
            return 0;
        case SLEEP_TIMEOUT_1_HRS:
            return 60 * 60;
        case SLEEP_TIMEOUT_2_HRS:
            return 60 * 60 * 2;
        case SLEEP_TIMEOUT_5_HRS:
            return 60 * 60 * 5;
        case SLEEP_TIMEOUT_1_MIN:
            return 60;
        default:
            return 0;
    }

    return 0;
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

    update_info_msg_box(msg_box, "Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_pmic_config());

    update_info_msg_box(msg_box, "Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&power_management_config, &default_power_management_config, sizeof(power_management_config));

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


void update_idle_timeout_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    power_management_config.idle_timeout = (idle_timeout_t) selected;
    ESP_LOGI(TAG, "Idle timeout updated to %d", power_management_config.idle_timeout);
}


void update_sleep_timeout_event_cb(lv_event_t *e) {
    lv_obj_t * dropdown = lv_event_get_target(e);
    int32_t selected = lv_dropdown_get_selected(dropdown);

    power_management_config.sleep_timeout = (sleep_timeout_t) selected;
    ESP_LOGI(TAG, "Sleep timeout updated to %d", power_management_config.sleep_timeout);
}



// FIXME: Make it generic (this is imported from ota_mode.c)
void on_reboot_button_pressed(lv_event_t * e);

void on_power_off_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Shutting down");

    pmic_power_off();
 }


void power_management_view_update_status(axp2101_ctx_t *ctx) {
    snprintf(status_str_l1, sizeof(status_str_l1), "SoC:%d,VBAT:%dmV", ctx->status.battery_percentage, ctx->status.vbatt_voltage_mv);
    snprintf(status_str_l2, sizeof(status_str_l2), "VBUS:%dmV,VSYS:%dmV", ctx->status.vbus_voltage_mv, ctx->status.vsys_voltage_mv);
}


static void create_power_menu(lv_obj_t * parent) {
    lv_obj_t * container = lv_obj_create(parent);

    lv_obj_remove_style_all(container);  // nuclear option
    lv_obj_set_style_pad_row(container, 20, 0);  // vertical spacing

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);  // semi transparent background


    lv_obj_t * reboot_button = lv_button_create(container);
    lv_obj_set_style_bg_color(reboot_button, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(reboot_button, 200, 60);
    lv_obj_add_event_cb(reboot_button, on_reboot_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * reboot_button_label = lv_label_create(reboot_button);
    lv_label_set_text(reboot_button_label, "Reboot");
    lv_obj_center(reboot_button_label);
    lv_obj_set_style_text_font(reboot_button_label, &lv_font_montserrat_28, LV_PART_MAIN);

#if USE_PMIC
    lv_obj_t * power_off_button = lv_button_create(container);
    lv_obj_set_style_bg_color(power_off_button, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(power_off_button, 200, 60);
    lv_obj_add_event_cb(power_off_button, on_power_off_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * power_off_button_label = lv_label_create(power_off_button);
    lv_label_set_text(power_off_button_label, "Power Off");
    lv_obj_center(power_off_button_label);
    lv_obj_set_style_text_font(power_off_button_label, &lv_font_montserrat_28, LV_PART_MAIN);
#endif  // USE_PMIC
}


void power_menu_make_visible() {
    lv_obj_clear_flag(power_menu, LV_OBJ_FLAG_HIDDEN);
}


lv_obj_t * create_power_management_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Set initial status
    power_management_view_update_status(axp2101_dev);

    // Battery status label
    lv_obj_t * status_label_1 = create_config_label_static(sub_page_config_view, status_str_l1);
    lv_obj_t * status_label_2 = create_config_label_static(sub_page_config_view, status_str_l2);

    // Idle timeout
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Idle Timeout");
    config_item = create_dropdown_list(container, idle_timeout_options, power_management_config.idle_timeout, update_idle_timeout_event_cb, NULL);

    // Sleep timeout
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Sleep Timeout");
    config_item = create_dropdown_list(container, sleep_timeout_options, power_management_config.sleep_timeout, update_sleep_timeout_event_cb, NULL);


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


    // Create an view that is binded to power button.
    power_menu = create_info_msg_box(lv_screen_active(), create_power_menu);
    // lv_obj_clear_flag(power_menu, LV_OBJ_FLAG_HIDDEN);

    return sub_page_config_view;
}

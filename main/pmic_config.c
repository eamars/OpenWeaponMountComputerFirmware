#include "pmic_axp2101.h"
#include "pmic_config.h"

#include <stdio.h>

#include "ui_bridge.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "common.h"
#include "app_cfg.h"
#include "config_view.h"
#include "settings_ui.h"
#include "ota_mode.h"

#include "lvgl.h"
#include "app.h"

#define TAG "AXP2101"
#define NVS_NAMESPACE "PMG"

static lv_obj_t * power_menu = NULL;


HEAPS_CAPS_ATTR power_management_config_t power_management_config;
const power_management_config_t default_power_management_config = {
    .vbus_current_limit = PMIC_VBUS_CURRENT_LIMIT_500MA,
    .battery_charge_current = PMIC_BATTERY_CHARGE_CURRENT_500MA,
    .battery_charge_voltage = PMIC_BATTERY_CHARGE_VOLTAGE_4V2,
    .idle_timeout = IDLE_TIMEOUT_5_MIN,
    .sleep_timeout = SLEEP_TIMEOUT_1_HRS,
};



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

// FIXME: Make it generic (this is imported from ota_mode.c)
void on_reboot_button_pressed(lv_event_t * e);

void on_power_off_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Shutting down");

    // The PMIC service's task does the shutdown; it is the only code that talks to the chip (ADR-005 step 3)
    ESP_ERROR_CHECK_WITHOUT_ABORT(pmic_request_power_off());
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
    if (power_menu != NULL) {
        lv_obj_clear_flag(power_menu, LV_OBJ_FLAG_HIDDEN);
    }
}


/*
----------------------------------------------------
Settings: the Power page and the charging rows of Advanced (ADR-008: each change stored when made)
----------------------------------------------------
*/

static const char * const idle_timeout_names[] = {"Never", "1 min", "5 min", "10 min", "10 s"};
static const char * const sleep_timeout_names[] = {"Never", "1 h", "2 h", "5 h", "1 min"};
static const char * const vbus_current_limit_names[] = {"100 mA", "500 mA", "900 mA", "1000 mA", "1500 mA", "2000 mA"};
static const char * const battery_charge_current_names[] = {"100 mA", "200 mA", "300 mA", "400 mA", "500 mA",
                                                            "600 mA", "700 mA", "800 mA", "900 mA", "1000 mA"};
static const char * const battery_charge_voltage_names[] = {"4.0 V", "4.1 V", "4.2 V", "4.35 V", "4.4 V"};


static void save_power(void) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_pmic_config());
}


// The charge settings reach the chip at once, as Save did (ADR-005 B15)
static void save_charging(void) {
    esp_err_t ret = save_pmic_config();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    if (ret == ESP_OK) {
        axp2101_request_apply_charge_settings(axp2101_dev);
    }
}


static int get_idle_timeout(void * ctx) {
    return power_management_config.idle_timeout;
}


static void set_idle_timeout(int index, void * ctx) {
    power_management_config.idle_timeout = (idle_timeout_t) index;
    save_power();
}


static int get_sleep_timeout(void * ctx) {
    return power_management_config.sleep_timeout;
}


static void set_sleep_timeout(int index, void * ctx) {
    power_management_config.sleep_timeout = (sleep_timeout_t) index;
    save_power();
}


static void power_status_text(char * buf, size_t size, void * ctx) {
    const axp2101_status_t * status = (const axp2101_status_t *) lv_subject_get_pointer(&ui_pmic_status);
    if (status == NULL) {
        snprintf(buf, size, "Battery: no reading yet");
        return;
    }
    snprintf(buf, size, "Battery %d%%, %d mV\nUSB %d mV, system %d mV", status->battery_percentage,
             status->vbatt_voltage_mv, status->vbus_voltage_mv, status->vsys_voltage_mv);
}


static void reset_power(void * ctx) {
    power_management_config.idle_timeout = default_power_management_config.idle_timeout;
    power_management_config.sleep_timeout = default_power_management_config.sleep_timeout;
    save_power();
}


void pmic_settings_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Idle %s, sleep %s", idle_timeout_names[power_management_config.idle_timeout % 5],
             sleep_timeout_names[power_management_config.sleep_timeout % 5]);
}


settings_page_t * create_power_settings_page(void) {
    settings_page_t * page = settings_page_create("Power");
    settings_row_status(page, power_status_text, NULL);
    settings_row_choice(page, "Idle timeout", idle_timeout_names, 5, get_idle_timeout, set_idle_timeout, NULL);
    settings_row_choice(page, "Sleep timeout", sleep_timeout_names, 5, get_sleep_timeout, set_sleep_timeout, NULL);
    settings_row_reset(page, "Reset Power", "Reset the idle and sleep timeouts?", reset_power, NULL);
    return page;
}


static int get_vbus_limit(void * ctx) {
    return power_management_config.vbus_current_limit;
}


static void set_vbus_limit(int index, void * ctx) {
    power_management_config.vbus_current_limit = (pmic_vbus_current_limit_e) index;
    save_charging();
}


static int get_charge_current(void * ctx) {
    return power_management_config.battery_charge_current;
}


static void set_charge_current(int index, void * ctx) {
    power_management_config.battery_charge_current = (pmic_battery_charge_current_e) index;
    save_charging();
}


static int get_charge_voltage(void * ctx) {
    return power_management_config.battery_charge_voltage;
}


static void set_charge_voltage(int index, void * ctx) {
    power_management_config.battery_charge_voltage = (pmic_battery_charge_voltage_e) index;
    save_charging();
}


void pmic_settings_add_advanced_rows(settings_page_t * page) {
    settings_row_choice(page, "USB current limit", vbus_current_limit_names, 6, get_vbus_limit, set_vbus_limit, NULL);
    settings_row_choice(page, "Charge current", battery_charge_current_names, 10, get_charge_current,
                        set_charge_current, NULL);
    settings_row_choice(page, "Charge voltage", battery_charge_voltage_names, 5, get_charge_voltage,
                        set_charge_voltage, NULL);
}


void pmic_settings_reset_advanced(void) {
    power_management_config.vbus_current_limit = default_power_management_config.vbus_current_limit;
    power_management_config.battery_charge_current = default_power_management_config.battery_charge_current;
    power_management_config.battery_charge_voltage = default_power_management_config.battery_charge_voltage;
    save_charging();
}


// The power button's menu (Reboot, Power off), on the screen over every view
void create_power_menu_overlay(void) {
    power_menu = create_info_msg_box(lv_screen_active(), create_power_menu);
}

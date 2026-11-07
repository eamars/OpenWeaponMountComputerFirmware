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
#include "settings_ui.h"
#include "value_editor.h"
#include "display_shape.h"
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


/*
----------------------------------------------------
The power button's menu (ADR-008): a full screen over every view, as the settings' confirmations: what the
held button can do, each a large button, and Cancel. Portrait: stacked; landscape: side by side under the title
----------------------------------------------------
*/

#define POWER_MENU_GAP_PX 20

static lv_obj_t * power_menu_title = NULL;
static lv_obj_t * reboot_button = NULL;
static lv_obj_t * power_off_button = NULL;
static lv_obj_t * power_cancel_button = NULL;


static void layout_power_menu(void) {
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                            LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    // Three buttons side by side in landscape are 133 px wide: "Power off" fits at 24 px, not at 32
    bool portrait = lv_obj_get_height(power_menu) > lv_obj_get_width(power_menu);
    const lv_font_t * font = portrait ? &lv_font_montserrat_32 : &lv_font_montserrat_24;
    lv_obj_t * buttons[] = {reboot_button, power_off_button, power_cancel_button};
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        lv_obj_set_style_text_font(lv_obj_get_child(buttons[i], 0), font, LV_PART_MAIN);
    }

    if (portrait) {
        lv_obj_set_grid_dsc_array(power_menu, portrait_cols, portrait_rows);
        lv_obj_set_grid_cell(power_menu_title, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(reboot_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(power_off_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
        lv_obj_set_grid_cell(power_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 3, 1);
    }
    else {
        // Cancel on the left, as the settings' confirmations
        lv_obj_set_grid_dsc_array(power_menu, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(power_menu_title, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(power_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(reboot_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(power_off_button, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
}


static void power_menu_size_changed_cb(lv_event_t * e) {
    layout_power_menu();
}


static void power_cancel_cb(lv_event_t * e) {
    power_menu_show(false);
}


static lv_obj_t * create_power_menu_button(const char * text, lv_color_t colour, lv_event_cb_t cb) {
    lv_obj_t * button = value_editor_create_button(power_menu, LV_PALETTE_GREY);
    lv_obj_set_style_bg_color(button, colour, LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(button, &lv_font_montserrat_32), text);
    lv_obj_add_event_cb(button, cb, LV_EVENT_SHORT_CLICKED, NULL);
    return button;
}


void power_menu_show(bool show) {
    if (power_menu == NULL) {
        return;
    }
    if (show) {
        layout_power_menu();
        lv_obj_remove_flag(power_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(power_menu);
    }
    else {
        lv_obj_add_flag(power_menu, LV_OBJ_FLAG_HIDDEN);
    }
}


bool power_menu_shown(void) {
    return power_menu != NULL && !lv_obj_has_flag(power_menu, LV_OBJ_FLAG_HIDDEN);
}


// On the screen, over the tiles (brought to the front when shown), so it covers whichever view is showing; the
// remote control's capture reads the screen
void create_power_menu_overlay(void) {
    power_menu = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(power_menu);
    lv_obj_set_size(power_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(power_menu, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(power_menu, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(power_menu, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(power_menu, POWER_MENU_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(power_menu, POWER_MENU_GAP_PX, LV_PART_MAIN);
    lv_obj_remove_flag(power_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(power_menu, LV_OBJ_FLAG_CLICKABLE);       // nothing behind it takes a tap
    lv_obj_add_event_cb(power_menu, power_menu_size_changed_cb, LV_EVENT_SIZE_CHANGED, NULL);

    power_menu_title = value_editor_create_label(power_menu, &lv_font_montserrat_24);
    lv_obj_set_style_text_align(power_menu_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(power_menu_title, "Power");

    reboot_button = create_power_menu_button("Reboot", lv_palette_darken(LV_PALETTE_ORANGE, 2),
                                             on_reboot_button_pressed);
    power_off_button = create_power_menu_button("Power off", lv_palette_darken(LV_PALETTE_RED, 2),
                                                on_power_off_button_pressed);
    power_cancel_button = create_power_menu_button("Cancel", lv_palette_darken(LV_PALETTE_GREY, 3), power_cancel_cb);

    layout_power_menu();
    lv_obj_add_flag(power_menu, LV_OBJ_FLAG_HIDDEN);
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


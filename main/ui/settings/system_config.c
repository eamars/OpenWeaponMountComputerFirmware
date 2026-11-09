#include "system_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "app_cfg.h"
#include "pmic_axp2101.h"
#include "low_power_mode.h"
#include "esp_lcd_types.h"
#include "common.h"
#include "bsp.h"
#include "remote_control.h"
#include "rc_serial.h"
#include "rc_net.h"
#include "lvgl_display.h"
#include "settings_ui.h"

#define TAG "SystemConfig"
#define NVS_NAMESPACE "SC"


HEAPS_CAPS_ATTR system_config_t system_config;
const system_config_t system_config_default = {
    .rotation = LV_DISPLAY_ROTATION_180,      // LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270
    .screen_brightness_normal_pct = SCREEN_BRIGHTNESS_100_PCT,
    .global_log_level = ESP_LOG_INFO,
};


esp_err_t load_system_config() {
    return load_config(NVS_NAMESPACE, &system_config, &system_config_default, sizeof(system_config));
}


esp_err_t save_system_config() {
    return save_config(NVS_NAMESPACE, &system_config, sizeof(system_config));
}


void system_config_set_rotation(lv_display_rotation_t rotation) {
    if (rotation != system_config.rotation) {
        system_config.rotation = rotation;
        ESP_LOGI(TAG, "Rotation updated to %d", system_config.rotation);
        lv_display_set_rotation(lv_display_get_default(), system_config.rotation);
    }
    else {
        ESP_LOGI(TAG, "Rotation not changed");
    }
}


// Start or stop the remote control to match its two switches, USB and Wi-Fi, each on its own
static void apply_remote_control_config() {
    if (remote_control_config.serial_enable) {
        rc_serial_start();
    }
    else {
        rc_serial_stop();
    }
    rc_net_set_enabled(remote_control_config.wifi_enable);
}


static void apply_brightness(void) {
    if (!is_idle_mode_activated()) {
        set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
    }
}


/*
----------------------------------------------------
Settings: the Display page and the system rows of Advanced (ADR-008: each change stored when made)
----------------------------------------------------
*/

static const char * const rotation_names[] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};
static const char * const brightness_names[] = {"50%", "60%", "70%", "80%", "90%", "100%"};
static const char * const log_level_names[] = {"None", "Error", "Warn", "Info", "Debug", "Verbose"};

#define BRIGHTNESS_MIN_PCT 50


static void save_system(void) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_system_config());
}


static int get_rotation(void * ctx) {
    return system_config.rotation;
}


static void set_rotation(int index, void * ctx) {
    system_config_set_rotation((lv_display_rotation_t) index);
    save_system();
}


static int get_brightness(void * ctx) {
    return (system_config.screen_brightness_normal_pct - BRIGHTNESS_MIN_PCT) / 10;
}


static void set_brightness(int index, void * ctx) {
    system_config.screen_brightness_normal_pct = (screen_brightness_pct_t) (BRIGHTNESS_MIN_PCT + 10 * index);
    apply_brightness();
    save_system();
}


static void reset_display(void * ctx) {
    system_config_set_rotation(system_config_default.rotation);
    system_config.screen_brightness_normal_pct = system_config_default.screen_brightness_normal_pct;
    apply_brightness();
    save_system();
}


void system_display_settings_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Rotation %s, brightness %d%%", rotation_names[system_config.rotation & 3],
             (int) system_config.screen_brightness_normal_pct);
}


settings_page_t * create_display_settings_page(void) {
    settings_page_t * page = settings_page_create("Display");
    settings_row_choice(page, "Screen rotation", rotation_names, 4, get_rotation, set_rotation, NULL);
    settings_row_choice(page, "Screen brightness", brightness_names, 6, get_brightness, set_brightness, NULL);
    settings_row_reset(page, "Reset Display", "Reset the rotation and brightness?", reset_display, NULL);
    return page;
}


static int get_log_level(void * ctx) {
    return system_config.global_log_level;
}


static void set_log_level(int index, void * ctx) {
    system_config.global_log_level = (esp_log_level_t) index;
    esp_log_level_set("*", system_config.global_log_level);
    save_system();
}


static bool get_remote_control(void * ctx) {
    return remote_control_config.serial_enable;
}


static void set_remote_control(bool on, void * ctx) {
    remote_control_config.serial_enable = on;
    apply_remote_control_config();
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_remote_control_config());
}


static bool get_remote_control_wifi(void * ctx) {
    return remote_control_config.wifi_enable;
}


static void set_remote_control_wifi(bool on, void * ctx) {
    remote_control_config.wifi_enable = on;
    apply_remote_control_config();
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_remote_control_config());
}


void system_settings_add_advanced_rows(settings_page_t * page) {
    settings_row_choice(page, "Log level", log_level_names, 6, get_log_level, set_log_level, NULL);
    settings_row_toggle(page, "Remote control over USB", get_remote_control, set_remote_control, NULL);
    settings_row_toggle(page, "Remote control over Wi-Fi", get_remote_control_wifi, set_remote_control_wifi, NULL);
}


void system_settings_reset_advanced(void) {
    system_config.global_log_level = system_config_default.global_log_level;
    esp_log_level_set("*", system_config.global_log_level);
    save_system();
    memcpy(&remote_control_config, &remote_control_config_default, sizeof(remote_control_config));
    apply_remote_control_config();
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_remote_control_config());
}

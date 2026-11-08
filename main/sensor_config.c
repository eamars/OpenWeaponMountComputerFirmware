#include "sensor_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"
#include "bno085.h"
#include "app_cfg.h"
#include "settings_ui.h"
#include "common.h"
#include "app.h"
#include "main_tileview.h"
#include "sensor_calibration_view.h"

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



esp_err_t save_sensor_config() {
    return save_config(NVS_NAMESPACE, &sensor_config, sizeof(sensor_config));
}


esp_err_t load_sensor_config() {
    return load_config(NVS_NAMESPACE, &sensor_config, &sensor_config_default, sizeof(sensor_config));
}


// Apply the settings to the running unit, as the page's switches do (Reload, Reset): a report switched off is
// stopped; a report switched on starts when the view that uses it is enabled
static void apply_sensor_config() {
    if (!sensor_config.enable_game_rotation_vector_report) {
        bno085_enable_game_rotation_vector_report(bno085_dev, 0);  // a failure is logged by the driver
    }
    if (!sensor_config.enable_linear_acceleration_report) {
        bno085_enable_linear_acceleration_report(bno085_dev, 0);
    }
    if (!sensor_config.enable_rotation_vector_report) {
        bno085_enable_rotation_vector_report(bno085_dev, 0);
    }
}

/*
----------------------------------------------------
Settings: the sensor rows of Advanced (ADR-008: each change stored when made)
----------------------------------------------------
*/

static void save_sensor(void) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_sensor_config());
}


static int32_t get_recoil_level(void * ctx) {
    return (int32_t) sensor_config.recoil_acceleration_trigger_level;
}


static void set_recoil_level(int32_t value, void * ctx) {
    sensor_config.recoil_acceleration_trigger_level = value;
    save_sensor();
}


static void format_recoil_level(char * buf, size_t size, int32_t value) {
    snprintf(buf, size, "%ld m/s^2", (long) value);
}


// A report switched off is stopped now; a report switched on starts when the view that uses it is enabled
static bool get_report(void * ctx) {
    return *(bool *) ctx;
}


static void set_report(bool on, void * ctx) {
    *(bool *) ctx = on;
    apply_sensor_config();
    save_sensor();
}


// Recoil is detected from the linear acceleration report: with it off, the level does nothing
static bool recoil_level_shown(void) {
    return sensor_config.enable_linear_acceleration_report;
}


static void start_sensor_calibration(void * ctx) {
    last_tile_before_enter_calibration = lv_tileview_get_tile_active(main_tileview);
    lv_tileview_set_tile(main_tileview, tile_sensor_calibration_view, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
}


void sensor_settings_add_advanced_rows(settings_page_t * page) {
    // Steps of 1 (owner: steps of 10 were too coarse); from 1, for the light recoil of a .22LR
    static const settings_number_t recoil_level = {.min = 1, .max = 100, .step = 1, .fast_steps = 5,
                                                   .format_value = format_recoil_level};
    settings_row_number(page, "Recoil trigger level", &recoil_level, get_recoil_level, set_recoil_level, NULL);
    settings_row_shown_when(page, recoil_level_shown);
    settings_row_toggle(page, "Game rotation", get_report, set_report,
                        &sensor_config.enable_game_rotation_vector_report);
    settings_row_toggle(page, "Linear accel", get_report, set_report,
                        &sensor_config.enable_linear_acceleration_report);
    settings_row_toggle(page, "Rotation vector", get_report, set_report,
                        &sensor_config.enable_rotation_vector_report);
    // The factory calibration routine (ADR-011); it asks nothing here: its first screen has Start and Cancel
    settings_row_action(page, NULL, "Sensor calibration", NULL, NULL, start_sensor_calibration, NULL, NULL, false);
}


void sensor_settings_reset_advanced(void) {
    memcpy(&sensor_config, &sensor_config_default, sizeof(sensor_config));
    apply_sensor_config();
    save_sensor();
}

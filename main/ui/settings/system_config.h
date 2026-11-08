#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "settings_ui.h"


typedef enum {
    SCREEN_BRIGHTNESS_50_PCT    = 50,
    SCREEN_BRIGHTNESS_60_PCT    = 60,
    SCREEN_BRIGHTNESS_70_PCT    = 70,
    SCREEN_BRIGHTNESS_80_PCT    = 80,
    SCREEN_BRIGHTNESS_90_PCT    = 90,
    SCREEN_BRIGHTNESS_100_PCT   = 100,
} screen_brightness_pct_t;


typedef struct {
    lv_display_rotation_t rotation;
    screen_brightness_pct_t screen_brightness_normal_pct;
    esp_log_level_t global_log_level;
} system_config_t;


esp_err_t load_system_config();
esp_err_t save_system_config();

/**
 * @brief Rotate the display (in memory; the Display page stores it). Call with the LVGL lock held.
 */
void system_config_set_rotation(lv_display_rotation_t rotation);

/** @brief The Display page of the settings, and its line on the hub (ADR-008) */
settings_page_t * create_display_settings_page(void);
void system_display_settings_summary(char * buf, size_t size, void * ctx);

/** @brief Advanced: log level and remote control; their part of Advanced's Reset */
void system_settings_add_advanced_rows(settings_page_t * page);
void system_settings_reset_advanced(void);

// Shared state, defined in system_config.c
extern system_config_t system_config;

#endif  // SYSTEM_CONFIG_H_
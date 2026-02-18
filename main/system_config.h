#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"


typedef enum {
    IDLE_TIMEOUT_NEVER = 0,
    IDLE_TIMEOUT_1_MIN,
    IDLE_TIMEOUT_5_MIN,
    IDLE_TIMEOUT_10_MIN, 
    IDLE_TIMEOUT_10_SEC,  // debug
} idle_timeout_t;

typedef enum {
    SLEEP_TIMEOUT_NEVER = 0,
    SLEEP_TIMEOUT_1_HRS,
    SLEEP_TIMEOUT_2_HRS,
    SLEEP_TIMEOUT_5_HRS,
    SLEEP_TIMEOUT_1_MIN,  // debug
} sleep_timeout_t;


typedef enum {
    SCREEN_BRIGHTNESS_50_PCT    = 50,
    SCREEN_BRIGHTNESS_60_PCT    = 60,
    SCREEN_BRIGHTNESS_70_PCT    = 70,
    SCREEN_BRIGHTNESS_80_PCT    = 80,
    SCREEN_BRIGHTNESS_90_PCT    = 90,
    SCREEN_BRIGHTNESS_100_PCT   = 100,
} screen_brightness_pct_t;


typedef struct {
    uint32_t crc32;
    lv_display_rotation_t rotation;
    idle_timeout_t idle_timeout;
    sleep_timeout_t sleep_timeout;
    screen_brightness_pct_t screen_brightness_normal_pct;
    esp_log_level_t global_log_level;
} system_config_t;


esp_err_t load_system_config();
esp_err_t save_system_config();

lv_obj_t * create_system_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page);


uint32_t idle_timeout_to_secs(idle_timeout_t timeout);
uint32_t sleep_timeout_to_secs(sleep_timeout_t timeout);

#endif  // SYSTEM_CONFIG_H_
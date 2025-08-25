#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

#include <stdint.h>
#include "esp_err.h"

#include "lvgl.h"


typedef enum {
    IDLE_TIMEOUT_NEVER = 0,
    IDLE_TIMEOUT_1_MIN,
    IDLE_TIMEOUT_5_MIN,
    IDLE_TIMEOUT_10_MIN,
    IDLE_TIMEOUT_10_SEC,  // debug
} idle_timeout_t;


typedef struct {
    uint32_t crc32;
    lv_display_rotation_t rotation;
    idle_timeout_t idle_timeout;

} system_config_t;


esp_err_t load_system_config();
esp_err_t save_system_config();

lv_obj_t * create_system_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page);


uint32_t idle_timeout_to_secs(idle_timeout_t timeout);


#endif  // SYSTEM_CONFIG_H_
#ifndef SYSTEM_CONFIG_H_
#define SYSTEM_CONFIG_H_

#include <stdint.h>
#include "esp_err.h"

#include "lvgl.h"

typedef enum {
    ROTATION_0 = 0,
    ROTATION_90,
    ROTATION_180,
    ROTATION_270
} rotation_t;

typedef struct {
    uint32_t crc32;

    rotation_t rotation;

} system_config_t;


esp_err_t load_system_config();
esp_err_t save_system_config();

lv_obj_t * create_system_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page);


#endif  // SYSTEM_CONFIG_H_
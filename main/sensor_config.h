#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include <stdint.h>
#include "lvgl.h"
#include "esp_err.h"

typedef enum {
    TRIGGER_RISING_EDGE,
    TRIGGER_FALLING_EDGE,
} trigger_edge_t;


typedef struct {
    uint32_t crc32;
    uint32_t recoil_acceleration_trigger_level;
    trigger_edge_t trigger_edge;
    bool enable_game_rotation_vector_report;
    bool enable_linear_acceleration_report;
    bool enable_rotation_vector_report;
} sensor_config_t;


lv_obj_t * create_sensor_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page);
esp_err_t load_sensor_config();
esp_err_t save_sensor_config();


#endif // SENSOR_CONFIG_H
#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include <stdint.h>
#include "esp_err.h"
#include "settings_ui.h"

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


/** @brief Advanced: the recoil trigger level, the sensor reports, the tare calibration; their part of
 * Advanced's Reset (ADR-008) */
void sensor_settings_add_advanced_rows(settings_page_t * page);
void sensor_settings_reset_advanced(void);
esp_err_t load_sensor_config();
esp_err_t save_sensor_config();

// Shared state, defined in sensor_config.c
extern sensor_config_t sensor_config;

#endif // SENSOR_CONFIG_H
#ifndef SENSOR_CALIBRATION_VIEW_H
#define SENSOR_CALIBRATION_VIEW_H

#include "lvgl.h"

// The factory calibration routine (ADR-011), on the tile the Tare page used

void create_sensor_calibration_view(lv_obj_t * parent);
void enter_sensor_calibration_mode(bool enable);

// Shared state, defined in sensor_calibration_view.c
extern lv_obj_t * last_tile_before_enter_calibration;

#endif  // SENSOR_CALIBRATION_VIEW_H

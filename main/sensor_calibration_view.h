#ifndef SENSOR_CALIBRATION_VIEW_H
#define SENSOR_CALIBRATION_VIEW_H

#include "lvgl.h"


#ifndef SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_STACK
    #define SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_STACK 4096
#endif  // SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_STACK


#ifndef SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_PRIORITY
    #define SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_PRIORITY 4
#endif  // SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_PRIORITY


void create_sensor_calibration_view(lv_obj_t * parent);
void enter_sensor_calibration_mode(bool enable);

// Shared state, defined in sensor_calibration_view.c
extern lv_obj_t * last_tile_before_enter_calibration;

#endif  // SENSOR_CALIBRATION_VIEW_H
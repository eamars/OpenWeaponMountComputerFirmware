#ifndef DIGITAL_LEVEL_VIEW_CONTROLLER_H
#define DIGITAL_LEVEL_VIEW_CONTROLLER_H

#include <lvgl.h>
#include <stdint.h>
#include "esp_err.h"

typedef lv_obj_t * (*view_constructor_t)(lv_obj_t * parent);
typedef void (*view_destructor_t)(lv_obj_t * container);
typedef void (*view_update_callback_t)(float roll_rad, float pitch_rad);

typedef struct {
    view_constructor_t constructor;
    view_destructor_t destructor;
    view_update_callback_t update_callback;
    lv_obj_t * container;
} digital_level_view_t;


typedef enum {
    DIGITAL_LEVEL_VIEW_TYPE_1 = 0,
    DIGITAL_LEVEL_VIEW_TYPE_2,
    DIGITAL_LEVEL_VIEW_TYPE_3,  // chevrons (ADR-010)
} digital_level_view_type_t;

esp_err_t digital_level_view_controller_init();

void enable_digital_level_view_controller(bool enable);
float get_relative_roll_angle_rad_thread_unsafe();

// Shared state, defined in digital_level_view_controller.c
extern float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;

// After each wake the shown cant and pitch are corrected toward the accelerometer's for a few seconds (ADR-014). On by
// default; the remote control's `bno levelfix off` (RAM only) turns it off to compare
extern bool level_wake_fix_enabled;
// For the remote control's trace: the fusion's own cant and the correction applied to it (radians)
extern float level_grv_cant_rad, level_fix_rad;
extern float sensor_x_acceleration_thread_unsafe;

#endif // DIGITAL_LEVEL_VIEW_CONTROLLER_H
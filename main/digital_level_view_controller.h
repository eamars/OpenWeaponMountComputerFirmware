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
} digital_level_view_type_t;

esp_err_t digital_level_view_controller_init();

void enable_digital_level_view_controller(bool enable);
float get_relative_roll_angle_rad_thread_unsafe();


#endif // DIGITAL_LEVEL_VIEW_CONTROLLER_H
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


// The level tile's background (ADR-017). The values are stored (the level's settings): new ones go at the end
typedef enum {
    DIGITAL_LEVEL_VIEW_TYPE_1 = 0,
    DIGITAL_LEVEL_VIEW_TYPE_2,
    DIGITAL_LEVEL_VIEW_TYPE_3,  // chevrons (ADR-010)
    DIGITAL_LEVEL_VIEW_OFF,     // no background: a black screen
    DIGITAL_LEVEL_VIEW_TYPE_COUNT,
} digital_level_view_type_t;

// The level tile's foreground (ADR-017): what is shown over the background, beside the tilt box. Stored too
typedef enum {
    LEVEL_FOREGROUND_MATCH = 0,  // the band: the timer bar and the dope (level_band.c)
    LEVEL_FOREGROUND_OFF,
    LEVEL_FOREGROUND_DOPE,       // the card as rows, no timer (level_card.c, ADR-018)
    LEVEL_FOREGROUND_COUNT,
} level_foreground_type_t;

esp_err_t digital_level_view_controller_init();

void enable_digital_level_view_controller(bool enable);
float get_relative_roll_angle_rad_thread_unsafe();

// Shared state, defined in digital_level_view_controller.c
extern float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;
extern float sensor_x_acceleration_thread_unsafe;

#endif // DIGITAL_LEVEL_VIEW_CONTROLLER_H
#ifndef DIGITAL_LEVEL_VIEW_TYPE_1_H
#define DIGITAL_LEVEL_VIEW_TYPE_1_H

#include "lvgl.h"
#include "digital_level_view_controller.h"


lv_obj_t * create_digital_level_view_type_1(lv_obj_t *parent);
void delete_digital_level_view_type_1(lv_obj_t *container);
void update_digital_level_view_type_1(float roll_rad, float pitch_rad);

// Shared state, defined in digital_level_view_type_1.c
extern digital_level_view_t digital_level_view_type_1_context;

#endif // DIGITAL_LEVEL_VIEW_TYPE_1_H
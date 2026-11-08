#ifndef DIGITAL_LEVEL_VIEW_TYPE_3_H
#define DIGITAL_LEVEL_VIEW_TYPE_3_H

#include <lvgl.h>

#include "digital_level_view_controller.h"

// Level style 3, "Chevrons" (ADR-010): on black, chevrons in the leaning side's colour move the way to turn the
// rifle, faster with more cant; within the level threshold two still bars in the levelled colour
lv_obj_t * create_digital_level_view_type_3(lv_obj_t *parent);
void delete_digital_level_view_type_3(lv_obj_t *container);
void update_digital_level_view_type_3(float roll_rad, float pitch_rad);

// Shared state, defined in digital_level_view_type_3.c
extern digital_level_view_t digital_level_view_type_3_context;

#endif // DIGITAL_LEVEL_VIEW_TYPE_3_H

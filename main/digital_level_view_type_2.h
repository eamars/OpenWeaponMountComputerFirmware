#ifndef DIGITAL_LEVEL_VIEW_TYPE_2_H
#define DIGITAL_LEVEL_VIEW_TYPE_2_H

#include <lvgl.h>
#include <stdint.h>
#include "esp_err.h"

#include "digital_level_view_controller.h"

lv_obj_t * create_digital_level_view_type_2(lv_obj_t *parent);
void delete_digital_level_view_type_2(lv_obj_t *container);
void update_digital_level_view_type_2(float roll_rad, float pitch_rad);

#endif // DIGITAL_LEVEL_VIEW_TYPE_2_H
#ifndef DIGITAL_LEVEL_VIEW_H
#define DIGITAL_LEVEL_VIEW_H


#include <lvgl.h>

typedef struct {
    // Record the gain between the physical tilt angle and the displayed angle
    float roll_display_gain;
    float pitch_display_gain;

    // Threshold that determines the device is leveled
    float delta_level_threshold;

    // User defined roll offset to compensate for installation errors
    float user_roll_rad_offset;

    // Display colours for tilt conditions
    lv_color_t colour_left_tilt_indicator;
    lv_color_t colour_right_tilt_indicator;
    lv_color_t colour_horizontal_level_indicator;
    lv_color_t colour_foreground;
} digital_level_view_config_t;


void create_digital_level_view(lv_obj_t *parent);

void enable_digital_level_view(bool enable);

#endif // DIGITAL_LEVEL_VIEW_H
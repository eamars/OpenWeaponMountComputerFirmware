#include "digital_level_view_type_2.h"

#include <math.h>

#include "esp_macros.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "bno085.h"


#define TAG "DigitalLevelViewType2"





static lv_obj_t * left_tilt_led = NULL;
static lv_obj_t * center_tilt_led = NULL;
static lv_obj_t * right_tilt_led = NULL;



digital_level_view_t digital_level_view_type_2_context = {
    .constructor = create_digital_level_view_type_2,
    .destructor = delete_digital_level_view_type_2,
    .update_callback = update_digital_level_view_type_2,
    // .container = NULL  // container is created dynamically from the constructor
};

lv_obj_t * create_digital_level_view_type_2(lv_obj_t *parent) {
    lv_obj_t * container = lv_obj_create(parent);
    digital_level_view_type_2_context.container = container;

    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(container);

    left_tilt_led = lv_obj_create(container);
    lv_obj_set_style_border_width(left_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(left_tilt_led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(left_tilt_led, 0, LV_PART_MAIN);


    right_tilt_led = lv_obj_create(container);
    lv_obj_set_style_border_width(right_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(right_tilt_led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(right_tilt_led, 0, LV_PART_MAIN);


    center_tilt_led = lv_obj_create(container);
    lv_obj_set_style_border_width(center_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(center_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_tilt_led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(center_tilt_led, 0, LV_PART_MAIN);

    lv_obj_set_size(left_tilt_led, lv_pct(33), lv_pct(100));
    lv_obj_align(left_tilt_led, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_set_size(center_tilt_led, lv_pct(33), lv_pct(100));
    lv_obj_align(center_tilt_led, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_size(right_tilt_led, lv_pct(33), lv_pct(100));
    lv_obj_align(right_tilt_led, LV_ALIGN_RIGHT_MID, 0, 0);

    // Set initial state
    update_digital_level_view_type_2(0, 0);
    

    return container;
}

void delete_digital_level_view_type_2(lv_obj_t *container) {
    lv_obj_del(container);
}

void update_digital_level_view_type_2(float roll_rad, float pitch_rad) {
    ESP_UNUSED(pitch_rad);
    const float roll_deg = RAD_TO_DEG(roll_rad);
    const float threshold_deg = digital_level_view_config.delta_level_threshold;
    lv_obj_set_style_bg_color(digital_level_view_type_2_context.container, lv_palette_main(digital_level_view_config.colour_foreground), 0);

    // Every angle sets all three blocks. Level within the threshold, as the other styles; beyond it the centre and
    // the side block show the tilt colour, and past three thresholds the side block alone. The same on both sides
    lv_palette_t left = digital_level_view_config.colour_foreground;
    lv_palette_t center = digital_level_view_config.colour_foreground;
    lv_palette_t right = digital_level_view_config.colour_foreground;

    if (fabsf(roll_deg) <= threshold_deg) {
        center = digital_level_view_config.colour_horizontal_level_indicator;
    }
    else {
        const bool left_tilt = roll_deg < 0;
        const lv_palette_t tilt = left_tilt ? digital_level_view_config.colour_left_tilt_indicator :
                                              digital_level_view_config.colour_right_tilt_indicator;
        if (left_tilt) {
            left = tilt;
        }
        else {
            right = tilt;
        }
        if (fabsf(roll_deg) <= 3 * threshold_deg) {
            center = tilt;
        }
    }

    lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(left), LV_PART_MAIN);
    lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(center), LV_PART_MAIN);
    lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(right), LV_PART_MAIN);
}

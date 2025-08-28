#include "digital_level_view_type_2.h"
#include "esp_macros.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "bno085.h"


#define TAG "DigitalLevelViewType2"



extern digital_level_view_config_t digital_level_view_config;
extern system_config_t system_config;


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
    float roll_deg = RAD_TO_DEG(roll_rad);
    lv_obj_set_style_bg_color(digital_level_view_type_2_context.container, lv_palette_main(digital_level_view_config.colour_foreground), 0);

    if (roll_deg < -3 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
    }
    else if (roll_deg < -2 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
    }
    else if (roll_deg > -1 * digital_level_view_config.delta_level_threshold && roll_deg < 1 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);

    }
    else if (roll_deg > 5 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_PART_MAIN);

    }
    else if (roll_deg > 2 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_PART_MAIN);
    }
}

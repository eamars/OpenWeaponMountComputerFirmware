#include <math.h>

#include "digital_level_view_type_1.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "app_cfg.h"
#include "bno085.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "DigitalLevelViewType1"


extern digital_level_view_config_t digital_level_view_config;
extern system_config_t system_config;

static lv_obj_t * horizontal_indicator_line_left = NULL;
static lv_obj_t * horizontal_indicator_line_right = NULL;
static lv_obj_t * digital_level_bg_canvas = NULL;

static float roll_rad_local = 0;
static float pitch_rad_local = 0;
static lv_obj_t * digital_level = NULL;

digital_level_view_t digital_level_view_type_1_context = {
    .constructor = create_digital_level_view_type_1,
    .destructor = delete_digital_level_view_type_1,
    .update_callback = update_digital_level_view_type_1
};


static void on_rotation_change_event(lv_event_t * e) {

}


static void digital_level_view_draw_event_cb(lv_event_t * e) {
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_obj_t * obj = lv_event_get_target(e);

    // Get object area and coordinate
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int32_t disp_width = lv_area_get_width(&coords);
    int32_t disp_height = lv_area_get_height(&coords);
    float max_delta_vertical_shift = disp_height/ 4.0;  // maximum vertical shift for the indicator lines
    float max_delta_vertical_vertex = disp_height / 2.0f - 20;  // Maximum vertical verticies for the triangle
    float delta_vertical_shift = -tanf(pitch_rad_local) * (disp_height / 2);
    float vertical_base_position = disp_height / 2 + delta_vertical_shift;

    float threshold_rad = DEG_TO_RAD(digital_level_view_config.delta_level_threshold);

    // Set background colour of the widget
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_opa = LV_OPA_COVER;
    coords.x1 = 0;
    coords.y1 = 0;
    coords.x2 = disp_width;
    coords.y2 = disp_height;
    // Set background colour based on the left/right tilt
    if (roll_rad_local < -threshold_rad) {
        bg_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_left_tilt_indicator);
    }
    else if (roll_rad_local > threshold_rad) {
        bg_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_right_tilt_indicator);
    }
    else {
        bg_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator);
    }
    lv_draw_rect(layer, &bg_dsc, &coords);

    // With special case that the roll is less than the threshold, fill the background with rectangle for everything
    if (fabsf(roll_rad_local) < threshold_rad) {
        // Draw rectangle
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator);
        coords.x1 = 0;
        coords.y1 = disp_height / 2;
        coords.x2 = disp_width;
        coords.y2 = disp_height;
        lv_draw_rect(layer, &rect_dsc, &coords);
    }
    else {
        // Calculate verticies for the triangle
        float dy = fabsf(tanf(roll_rad_local) * (disp_width / 2));

        // Apply gain
        dy *= digital_level_view_config.roll_display_gain;

        // Limit the vertical shift to a maximum value
        if (dy > max_delta_vertical_vertex) dy = max_delta_vertical_vertex;
        
        // Draw triangle
        lv_draw_triangle_dsc_t tri_dsc;
        lv_draw_triangle_dsc_init(&tri_dsc);
        tri_dsc.color = lv_palette_main(digital_level_view_config.colour_foreground);

        // Depending on the roll direction, set the points of the triangle
        if (roll_rad_local < 0) {
            tri_dsc.p[0].x = 0;
            tri_dsc.p[0].y = vertical_base_position - dy;
        }
        else {
            tri_dsc.p[0].x = disp_width;
            tri_dsc.p[0].y = vertical_base_position - dy;
        }
        tri_dsc.p[1].x = disp_width;
        tri_dsc.p[1].y = vertical_base_position + dy;
        tri_dsc.p[2].x = 0;
        tri_dsc.p[2].y = vertical_base_position + dy;
        lv_draw_triangle(layer, &tri_dsc);

        // Draw rectangle
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_foreground);
        lv_area_t coords = {0, vertical_base_position + dy, disp_width, disp_height};
        lv_draw_rect(layer, &rect_dsc, &coords);
    }

    // Draw horizontal level indicator lines
    lv_draw_line_dsc_t left_line_dsc;
    lv_draw_line_dsc_init(&left_line_dsc);
    left_line_dsc.color = lv_color_white();
    left_line_dsc.width = 8;
    left_line_dsc.p1.x = 0;
    left_line_dsc.p1.y = disp_height / 2 + delta_vertical_shift;
    left_line_dsc.p2.x = 20;
    left_line_dsc.p2.y = disp_height / 2 + delta_vertical_shift;
    lv_draw_line(layer, &left_line_dsc);

    lv_draw_line_dsc_t right_line_dsc;
    lv_draw_line_dsc_init(&right_line_dsc);
    right_line_dsc.color = lv_color_white();
    right_line_dsc.width = 8;
    right_line_dsc.p1.x = disp_width;
    right_line_dsc.p1.y = disp_height / 2 + delta_vertical_shift;
    right_line_dsc.p2.x = disp_width - 20;
    right_line_dsc.p2.y = disp_height / 2 + delta_vertical_shift;
    lv_draw_line(layer, &right_line_dsc);

}

lv_obj_t * create_digital_level_view_type_1(lv_obj_t *parent) {
    // lv_obj_t * container = lv_obj_create(parent);
    // digital_level_view_type_1_context.container = container;

    // lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    // lv_obj_set_style_pad_all(container, 0, 0);
    // lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    // lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    // lv_obj_center(container);

    // Create drawing object
    digital_level = lv_obj_create(parent);
    digital_level_view_type_1_context.container = digital_level;
    lv_obj_set_size(digital_level, lv_pct(100), lv_pct(100));
    lv_obj_center(digital_level);

    // Remove styling
    lv_obj_set_style_border_width(digital_level, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(digital_level, 0, LV_PART_MAIN);

    // Callback to draw the digital level
    lv_obj_add_event_cb(digital_level, digital_level_view_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);

    return digital_level;
}

void delete_digital_level_view_type_1(lv_obj_t *container) {
    lv_obj_delete(container);
}

void update_digital_level_view_type_1(float roll_rad, float pitch_rad) {
    // Transfer to local variable
    roll_rad_local = roll_rad;
    pitch_rad_local = pitch_rad;

    lv_obj_invalidate(digital_level);
}
#include <math.h>
#include <string.h>

#include "digital_level_view_type_1.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "app_cfg.h"
#include "bno085.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "DigitalLevelViewType1"



static float roll_rad_local = 0;
static float pitch_rad_local = 0;
static lv_obj_t * digital_level = NULL;

// Half the level line width (8 px) and an allowance for anti-aliased edges, in px
#define LEVEL_LINE_HALF_WIDTH 4
#define LEVEL_EDGE_MARGIN 2

// The state of the last update; the next update redraws only the rows that differ from it. Reset with the view.
static struct {
    bool valid;
    float roll_rad;
    float pitch_rad;
    digital_level_view_config_t config;
} shown;

digital_level_view_t digital_level_view_type_1_context = {
    .constructor = create_digital_level_view_type_1,
    .destructor = delete_digital_level_view_type_1,
    .update_callback = update_digital_level_view_type_1
};


static void digital_level_view_draw_event_cb(lv_event_t * e) {
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_obj_t * obj = lv_event_get_target(e);

    // Get object area and coordinate
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int32_t disp_width = lv_area_get_width(&coords);
    int32_t disp_height = lv_area_get_height(&coords);
    float max_delta_vertical_vertex = disp_height / 2.0f - 20;  // Maximum vertical verticies for the triangle
    float delta_vertical_shift = -tanf(pitch_rad_local) * (disp_height / 2) * digital_level_view_config.pitch_display_gain;
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

    // A new view is drawn whole; the first update cannot compare against anything
    shown.valid = false;

    return digital_level;
}

void delete_digital_level_view_type_1(lv_obj_t *container) {
    lv_obj_delete(container);
}

// Background colour class as the draw callback picks it: -1 left tilt, 0 level, 1 right tilt
static int level_colour_class(float roll_rad, float threshold_rad) {
    if (roll_rad < -threshold_rad) {
        return -1;
    }
    if (roll_rad > threshold_rad) {
        return 1;
    }
    return 0;
}

// Rows (relative to the view's top, inclusive) that depend on the angles, with the same arithmetic as the draw
// callback: the triangle band [base - dy, base + dy] and the level lines at base. Above the band is the background
// colour, below it the foreground; neither depends on the angles while the colour class stays the same.
static void level_angle_rows(float roll_rad, float pitch_rad, int32_t width, int32_t height,
                             int32_t *top, int32_t *bottom) {
    const float threshold_rad = DEG_TO_RAD(digital_level_view_config.delta_level_threshold);
    const float delta_vertical_shift = -tanf(pitch_rad) * (height / 2) * digital_level_view_config.pitch_display_gain;
    const float base = height / 2 + delta_vertical_shift;

    float half = LEVEL_LINE_HALF_WIDTH;
    if (!(fabsf(roll_rad) < threshold_rad)) {
        float dy = fabsf(tanf(roll_rad) * (width / 2)) * digital_level_view_config.roll_display_gain;
        const float max_delta_vertical_vertex = height / 2.0f - 20;
        if (dy > max_delta_vertical_vertex) dy = max_delta_vertical_vertex;
        if (dy > half) half = dy;
    }

    *top = (int32_t) floorf(base - half) - LEVEL_EDGE_MARGIN;
    *bottom = (int32_t) ceilf(base + half) + LEVEL_EDGE_MARGIN;
}

void update_digital_level_view_type_1(float roll_rad, float pitch_rad) {
    // Transfer to local variable
    roll_rad_local = roll_rad;
    pitch_rad_local = pitch_rad;

    const float threshold_rad = DEG_TO_RAD(digital_level_view_config.delta_level_threshold);
    const bool whole = !shown.valid ||
        memcmp(&shown.config, &digital_level_view_config, sizeof(shown.config)) != 0 ||
        level_colour_class(shown.roll_rad, threshold_rad) != level_colour_class(roll_rad, threshold_rad) ||
        (fabsf(shown.roll_rad) < threshold_rad) != (fabsf(roll_rad) < threshold_rad);

    if (whole) {
        lv_obj_invalidate(digital_level);
    }
    else if (shown.roll_rad != roll_rad || shown.pitch_rad != pitch_rad) {
        // Only the rows between the old and the new picture's angle-dependent rows change
        lv_area_t coords;
        lv_obj_get_coords(digital_level, &coords);
        const int32_t width = lv_area_get_width(&coords);
        const int32_t height = lv_area_get_height(&coords);

        int32_t old_top, old_bottom, new_top, new_bottom;
        level_angle_rows(shown.roll_rad, shown.pitch_rad, width, height, &old_top, &old_bottom);
        level_angle_rows(roll_rad, pitch_rad, width, height, &new_top, &new_bottom);

        const lv_area_t rows = {
            .x1 = coords.x1,
            .y1 = coords.y1 + LV_MIN(old_top, new_top),
            .x2 = coords.x2,
            .y2 = coords.y1 + LV_MAX(old_bottom, new_bottom),
        };
        lv_obj_invalidate_area(digital_level, &rows);  // clipped to the view
    }

    shown.valid = true;
    shown.roll_rad = roll_rad;
    shown.pitch_rad = pitch_rad;
    shown.config = digital_level_view_config;
}
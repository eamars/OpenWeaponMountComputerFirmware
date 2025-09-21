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

uint8_t *lv_canvas_draw_buffer;

digital_level_view_t digital_level_view_type_1_context = {
    .constructor = create_digital_level_view_type_1,
    .destructor = delete_digital_level_view_type_1,
    .update_callback = update_digital_level_view_type_1
};


static void set_rotation_canvas(lv_display_rotation_t rotation)
{
    int32_t width, height;
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        width = DISP_H_RES_PIXEL;
        height = DISP_V_RES_PIXEL;
    }
    else {
        width = DISP_V_RES_PIXEL;
        height = DISP_H_RES_PIXEL;
    }

    lv_canvas_set_buffer(digital_level_bg_canvas, (void *) lv_canvas_draw_buffer, width, height, LV_COLOR_FORMAT_RGB565);
}


static void on_rotation_change_event(lv_event_t * e) {
    set_rotation_canvas(system_config.rotation);
}

lv_obj_t * create_digital_level_view_type_1(lv_obj_t *parent) {
    lv_obj_t * container = lv_obj_create(parent);
    digital_level_view_type_1_context.container = container;

    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_center(container);

    // Create canvas
    digital_level_bg_canvas = lv_canvas_create(container);

    // Allocate memory for buffer
    uint32_t bpp = lv_color_format_get_bpp(LV_COLOR_FORMAT_RGB565); // = 16
    size_t buf_size = LV_CANVAS_BUF_SIZE(DISP_H_RES_PIXEL, DISP_V_RES_PIXEL, bpp, LV_DRAW_BUF_STRIDE_ALIGN);
    ESP_LOGI(TAG, "Canvas buffer size: %d bytes", buf_size);

    lv_canvas_draw_buffer = heap_caps_calloc(1, buf_size, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (lv_canvas_draw_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for canvas draw buffer");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    set_rotation_canvas(system_config.rotation);
    lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator), LV_OPA_COVER);
    lv_obj_set_size(digital_level_bg_canvas, lv_pct(100), lv_pct(100));
    lv_obj_center(digital_level_bg_canvas);

    // Create horizontal indicator lines
    static lv_style_t line_style;
    lv_style_init(&line_style);
    lv_style_set_line_width(&line_style, 8);
    lv_style_set_line_color(&line_style, lv_color_white());

    static lv_point_precise_t line_points[] = { { 0, 0 }, { 10, 0 } };

    horizontal_indicator_line_left = lv_line_create(container);
    lv_line_set_points(horizontal_indicator_line_left, line_points, 2); /*Set the points*/
    lv_obj_add_style(horizontal_indicator_line_left, &line_style, LV_PART_MAIN);
    lv_obj_align(horizontal_indicator_line_left, LV_ALIGN_LEFT_MID, 5, 0);

    horizontal_indicator_line_right = lv_line_create(container);
    lv_line_set_points(horizontal_indicator_line_right, line_points, 2); /*Set the points*/
    lv_obj_add_style(horizontal_indicator_line_right, &line_style, LV_PART_MAIN);
    lv_obj_align(horizontal_indicator_line_right, LV_ALIGN_RIGHT_MID, -5, 0);

    // Set initial state
    update_digital_level_view_type_1(0, 0);

    // Create a event callback to handle rotation change
    lv_obj_add_event_cb(container, on_rotation_change_event, LV_EVENT_SIZE_CHANGED, NULL);

    return container;
}

void delete_digital_level_view_type_1(lv_obj_t *container) {
    lv_obj_delete(container);

    // Free RAM
    if (lv_canvas_draw_buffer != NULL) {
        ESP_LOGI(TAG, "Canvas MEM freed");
        heap_caps_free(lv_canvas_draw_buffer);
        lv_canvas_draw_buffer = NULL;
    }
}

void update_digital_level_view_type_1(float roll_rad, float pitch_rad) {
    int32_t disp_width, disp_height;
    if (system_config.rotation == LV_DISPLAY_ROTATION_0 || system_config.rotation == LV_DISPLAY_ROTATION_180) {
        disp_width = DISP_H_RES_PIXEL;
        disp_height = DISP_V_RES_PIXEL;
    }
    else {
        disp_width = DISP_V_RES_PIXEL;
        disp_height = DISP_H_RES_PIXEL;
    }

    float max_delta_vertical_shift = disp_height/ 4.0;  // maximum vertical shift for the indicator lines
    float max_delta_vertical_vertex = disp_height / 2.0f - 20;  // Maximum vertical verticies for the triangle

    // -------------------------
    // Update line location based on pitch
    // -------------------------
    float delta_vertical_shift = -tanf(pitch_rad) * (disp_height / 2);
    
    // Apply gain to the pitch
    delta_vertical_shift *= digital_level_view_config.pitch_display_gain;

    // Limit the vertical shift to a maximum value
    if (delta_vertical_shift > max_delta_vertical_shift) delta_vertical_shift = max_delta_vertical_shift;
    if (delta_vertical_shift < -max_delta_vertical_shift) delta_vertical_shift = -max_delta_vertical_shift;

    // Update line location
    lv_obj_set_pos(horizontal_indicator_line_left, 0, delta_vertical_shift);
    lv_obj_set_pos(horizontal_indicator_line_right, 0, delta_vertical_shift);


    // Calculate vertical location for the polygon (drawn as triangle)
    float vertical_base_position = disp_height / 2 + delta_vertical_shift;

    // -------------------------
    // Update background canvas
    // -------------------------
    // Based on input roll, fill the background canvas with different colors
    float threshold_rad = DEG_TO_RAD(digital_level_view_config.delta_level_threshold);
    if (roll_rad < -threshold_rad) {
        lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_OPA_COVER);
    }
    else if (roll_rad > threshold_rad) {
        lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_OPA_COVER);
    }
    else {
        lv_canvas_fill_bg(digital_level_bg_canvas, lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator), LV_OPA_COVER);
    }

    // Draw new layer on the canvas
    lv_layer_t layer;
    lv_canvas_init_layer(digital_level_bg_canvas, &layer);

    // Special case: If below than threshold, draw rectangle instead
    if (fabsf(roll_rad) < threshold_rad) {
        // Draw rectangle
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator);
        lv_area_t coords = {0, disp_height / 2, disp_width, disp_height};
        lv_draw_rect(&layer, &rect_dsc, &coords);
    }
    else {
        // Calculate verticies for the triangle
        float dy = fabsf(tanf(roll_rad) * (disp_width / 2));

        // Apply gain
        dy *= digital_level_view_config.roll_display_gain;

        // Limit the vertical shift to a maximum value
        if (dy > max_delta_vertical_vertex) dy = max_delta_vertical_vertex;

        // Draw triangle
        lv_draw_triangle_dsc_t tri_dsc;
        lv_draw_triangle_dsc_init(&tri_dsc);
        tri_dsc.color = lv_palette_main(digital_level_view_config.colour_foreground);

        // Depending on the roll direction, set the points of the triangle
        if (roll_rad < 0) {
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
        lv_draw_triangle(&layer, &tri_dsc);

        // Draw rectangle
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_palette_main(digital_level_view_config.colour_foreground);
        lv_area_t coords = {0, vertical_base_position + dy, disp_width, disp_height};
        lv_draw_rect(&layer, &rect_dsc, &coords);
    }
    lv_canvas_finish_layer(digital_level_bg_canvas, &layer);
}
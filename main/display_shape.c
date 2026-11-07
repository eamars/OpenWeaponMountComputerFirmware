#include "display_shape.h"

#include <math.h>

#include "lvgl.h"


static lv_obj_t * display_outline = NULL;


int32_t display_concentric_radius(int32_t margin) {
    int32_t radius = DISP_CORNER_RADIUS_PX - margin;
    return radius > 0 ? radius : 0;
}


int32_t display_corner_clearance(int32_t depth) {
    const float r = DISP_CORNER_RADIUS_PX;
    if (depth >= DISP_CORNER_RADIUS_PX) {
        return 0;
    }
    if (depth < 0) {
        depth = 0;
    }

    // The corner arc's centre is r in from both edges; at `depth` from one edge, the arc is this far from the other
    float dy = r - (float) depth;
    return (int32_t) ceilf(r - sqrtf(r * r - dy * dy));
}


void display_shape_show_outline(bool show) {
    if (!show) {
        if (display_outline) {
            lv_obj_delete(display_outline);
            display_outline = NULL;
        }
        return;
    }

    if (display_outline) {
        return;
    }

    display_outline = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(display_outline);
    lv_obj_set_size(display_outline, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(display_outline, DISP_CORNER_RADIUS_PX, LV_PART_MAIN);
    lv_obj_set_style_border_width(display_outline, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(display_outline, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_border_opa(display_outline, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(display_outline, LV_OBJ_FLAG_CLICKABLE);
}

#ifndef DISPLAY_SHAPE_H_
#define DISPLAY_SHAPE_H_

#include <stdbool.h>
#include <stdint.h>

#include "app_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

// The panel's rounded corners, one rule for every view (ADR-006). The corners are the panel's, so they are the
// same at every rotation.
#ifndef DISP_CORNER_RADIUS_PX
    #define DISP_CORNER_RADIUS_PX (0)  // a panel with square corners
#endif

// Views keep this far from the screen edge
#define DISPLAY_MARGIN_PX (8)

/**
 * @brief Corner radius for an object `margin` px in from two screen edges, so its outline runs parallel to the
 * screen's corner (concentric corners) and nothing of it is cut off.
 */
int32_t display_concentric_radius(int32_t margin);

/**
 * @brief How far from the other edge an object must stay, near a corner, when one of its edges lies `depth` px
 * from a screen edge. 0 at a depth of the corner radius or more.
 */
int32_t display_corner_clearance(int32_t depth);

/**
 * @brief Row height for a scrolling list that shows whole rows only (ADR-008, the owner: no half row at the end of
 * a scroll): as many rows of at least `min_row` px as fit in `viewport` with `gap` between them, stretched to fill
 * it exactly. Used with LV_SCROLL_SNAP_START, every scroll then stops on a row edge.
 */
int32_t display_locked_row_height(int32_t viewport, int32_t min_row, int32_t gap);

/**
 * @brief Debug: draw the panel's outline on the top layer (remote control `outline on|off`). LVGL lock held.
 */
void display_shape_show_outline(bool show);

#ifdef __cplusplus
}
#endif

#endif  // DISPLAY_SHAPE_H_

#include "level_card.h"

#include <stdio.h>
#include <string.h>

#include "app_cfg.h"
#include "dope_view.h"
#include "display_shape.h"
#include "system_config.h"

// Layout (ADR-018): the targets start 20 px under the tilt box (8 px margin, 58 px tall) and end 8 px from the
// bottom edge. A target's outline keeps 6 px from the sides and a row's text another 8 px. No background: white
// text on the level's colour, as the band (ADR-007).
// The same fonts in both rotations. Widths checked against the font: "1000 m", "99.9" and "L1.2" side by side fit
// the 252 px inside a portrait row; stacked, a target fits a quarter of the landscape screen (111 px: "99.9" is 101)
#define CARD_TOP_PX 86
#define CARD_BOTTOM_PX DISPLAY_MARGIN_PX
#define CARD_CELL_MARGIN_PX 6
#define CARD_ROW_PAD_PX 8
#define CARD_ROWS_PORTRAIT 5
#define CARD_COLUMNS_LANDSCAPE 4
#define CARD_CELLS_MAX 5
#define CARD_ROW_WIND_WIDTH_PX 60       // a row keeps this for the wind, right of the elevation
#define CARD_OUTLINE_PX 2               // the current target's outline, as the tilt box's
#define CARD_MARK_PX 3                  // where the shown targets are in the card: a line at the right or the bottom
#define CARD_MARK_GAP_PX 3              // landscape: between the targets and the mark under them
#define CARD_MARK_CORNER_CLEAR_PX DISP_CORNER_RADIUS_PX  // the mark stays out of the panel's corners
#define CARD_TAP_SLOP_PX 20             // a tap ends this near to where it began (1.6 mm); further is a drag

#define CARD_NAME_FONT (&lv_font_montserrat_20)
#define CARD_RANGE_FONT (&lv_font_montserrat_28)
#define CARD_ELEVATION_FONT (&lv_font_montserrat_48)
#define CARD_WIND_FONT (&lv_font_montserrat_28)
#define CARD_GUST_FONT (&lv_font_montserrat_20)

typedef struct {
    lv_obj_t * cell;
    lv_obj_t * name;
    lv_obj_t * range_line;      // the range and its "m", hidden for a target with no range
    lv_obj_t * range;
    lv_obj_t * elevation;
    lv_obj_t * wind;
    lv_obj_t * gust;
} card_target_t;

static lv_obj_t * card = NULL;
static lv_obj_t * window_mark = NULL;
HEAPS_CAPS_ATTR static card_target_t card_targets[CARD_CELLS_MAX];    // PSRAM: internal RAM is kept for the services
static bool side_by_side = false;   // landscape: the targets as columns; portrait: as rows
static int shown = 0;               // targets on screen
static int32_t card_width = 0;
static int32_t card_height = 0;
static int window_start = 0;        // the first shown target, counted in set targets
static lv_point_t press_point;      // where the touch on the card began


static lv_obj_t * create_text(lv_obj_t * parent, const lv_font_t * font, lv_color_t colour) {
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
    lv_label_set_text(label, "");
    return label;
}


// A group of labels sized by its content, laid out by `flow`, its children's far edges in line (text on one
// baseline in a row, right edges in a column)
static lv_obj_t * create_group(lv_obj_t * parent, lv_flex_flow_t flow) {
    lv_obj_t * group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, flow);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_scrollable(group, false);
    lv_obj_set_clickable(group, false);
    return group;
}


// The card's listener (dope_set_card_listener): the card, the names or the current target changed
static void refresh_targets(void) {
    if (card == NULL) {
        return;
    }

    // The set targets, in the card's order
    int set_targets[DOPE_TARGET_MAX];
    int count = 0;
    for (int i = dope_find_set_target(0, 1); i >= 0; i = dope_find_set_target(i + 1, 1)) {
        set_targets[count++] = i;
    }
    const int current_target = dope_current_target();
    int current_position = 0;
    for (int i = 0; i < count; i++) {
        if (set_targets[i] == current_target) {
            current_position = i;
        }
    }

    // The shown targets move just far enough to keep the current one among them
    if (current_position < window_start) {
        window_start = current_position;
    }
    if (current_position >= window_start + shown) {
        window_start = current_position - shown + 1;
    }
    if (window_start > count - shown) {
        window_start = count - shown;
    }
    if (window_start < 0) {
        window_start = 0;
    }

    for (int c = 0; c < shown; c++) {
        card_target_t * w = &card_targets[c];
        if (window_start + c >= count) {
            lv_obj_set_hidden(w->cell, true);
            continue;
        }
        const int index = set_targets[window_start + c];
        const dope_target_t * target = &dope_card.target[index];
        char text[24];
        lv_obj_set_hidden(w->cell, false);
        lv_obj_set_style_border_opa(w->cell, index == current_target ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);

        // A target with a range (from the Kestrel) leads with it, its name small; one without leads with its name
        dope_target_name(text, sizeof(text), index);
        lv_label_set_text(w->name, text);
        const bool has_range = target->range_m > 0;
        lv_obj_set_hidden(w->range_line, !has_range);
        if (has_range) {
            lv_label_set_text_fmt(w->range, "%u", (unsigned) target->range_m);
        }
        lv_obj_set_style_text_font(w->name, has_range ? CARD_NAME_FONT : CARD_RANGE_FONT, LV_PART_MAIN);

        // Up is plain and down has a minus, as on the dope tile
        dope_format_elevation(text, sizeof(text), target->elevation_x10);
        lv_label_set_text(w->elevation, text);

        dope_format_target_wind(text, sizeof(text), target);
        lv_label_set_text(w->wind, text);
        if (target->gust_x10 != DOPE_HOLD_NONE) {
            dope_format_target_gust(text, sizeof(text), target);
            lv_label_set_text_fmt(w->gust, "/ %s", text);
            lv_obj_set_hidden(w->gust, false);
        }
        else {
            lv_obj_set_hidden(w->gust, true);
        }
    }

    // Where the shown targets are in the card, only when the card has more than fit: down the right edge beside
    // rows, along the bottom under columns
    if (count <= shown) {
        lv_obj_set_hidden(window_mark, true);
        return;
    }
    lv_obj_set_hidden(window_mark, false);
    if (side_by_side) {
        const int32_t track = card_width - 2 * CARD_MARK_CORNER_CLEAR_PX;
        lv_obj_set_size(window_mark, track * shown / count, CARD_MARK_PX);
        lv_obj_set_pos(window_mark, CARD_MARK_CORNER_CLEAR_PX + track * window_start / count,
                       card_height - CARD_MARK_PX);
    }
    else {
        const int32_t track = card_height - CARD_MARK_CORNER_CLEAR_PX;
        lv_obj_set_size(window_mark, CARD_MARK_PX, track * shown / count);
        lv_obj_set_pos(window_mark, card_width - CARD_MARK_PX - 1, track * window_start / count);
    }
}


// Build the targets' cells for the rotation in use; the card's children are deleted first
static void build_targets(void) {
    lv_obj_clean(card);
    memset(card_targets, 0, sizeof(card_targets));

    const lv_color_t light_grey = lv_palette_lighten(LV_PALETTE_GREY, 3);
    const lv_color_t orange = lv_palette_main(LV_PALETTE_ORANGE);
    const int32_t cells_width = card_width - 2 * CARD_CELL_MARGIN_PX;
    const int32_t cell_width = side_by_side ? cells_width / shown : cells_width;
    const int32_t cell_height = side_by_side ? card_height - CARD_MARK_PX - CARD_MARK_GAP_PX : card_height / shown;

    for (int c = 0; c < shown; c++) {
        card_target_t * w = &card_targets[c];

        w->cell = lv_obj_create(card);
        lv_obj_remove_style_all(w->cell);
        lv_obj_set_pos(w->cell, CARD_CELL_MARGIN_PX + (side_by_side ? c * cell_width : 0),
                       side_by_side ? 0 : c * cell_height);
        lv_obj_set_size(w->cell, cell_width, cell_height);
        lv_obj_set_style_pad_hor(w->cell, side_by_side ? 0 : CARD_ROW_PAD_PX, LV_PART_MAIN);
        lv_obj_set_style_border_width(w->cell, CARD_OUTLINE_PX, LV_PART_MAIN);
        lv_obj_set_style_border_color(w->cell, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_opa(w->cell, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_radius(w->cell, display_concentric_radius(CARD_CELL_MARGIN_PX), LV_PART_MAIN);
        lv_obj_set_scrollable(w->cell, false);
        lv_obj_set_clickable(w->cell, false);   // a touch goes to the card under it

        // A column: everything in one centred stack. A row: the range at the left with the name over it, the
        // elevation, and the wind at the right with the gust under it
        lv_obj_t * holds = w->cell;
        lv_obj_t * lead;
        if (side_by_side) {
            holds = create_group(w->cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(holds, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_center(holds);
            lead = holds;
        }
        else {
            lead = create_group(w->cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(lead, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_align(lead, LV_ALIGN_LEFT_MID, 0, 0);
        }
        w->name = create_text(lead, CARD_NAME_FONT, orange);
        w->range_line = create_group(lead, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(w->range_line, 3, LV_PART_MAIN);
        w->range = create_text(w->range_line, CARD_RANGE_FONT, lv_color_white());
        lv_obj_t * unit = create_text(w->range_line, CARD_NAME_FONT, light_grey);
        lv_label_set_text(unit, "m");

        w->elevation = create_text(holds, CARD_ELEVATION_FONT, lv_color_white());
        lv_obj_t * wind_group = holds;
        if (!side_by_side) {
            // The elevation's right edge is fixed, so the decimal points line up down the card
            lv_obj_align(w->elevation, LV_ALIGN_RIGHT_MID, -CARD_ROW_WIND_WIDTH_PX, 0);
            wind_group = create_group(w->cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_align(wind_group, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        w->wind = create_text(wind_group, CARD_WIND_FONT, lv_color_white());
        w->gust = create_text(wind_group, CARD_GUST_FONT, light_grey);
    }

    window_mark = lv_obj_create(card);
    lv_obj_remove_style_all(window_mark);
    lv_obj_set_style_bg_color(window_mark, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(window_mark, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(window_mark, CARD_MARK_PX / 2, LV_PART_MAIN);
    lv_obj_set_clickable(window_mark, false);
    lv_obj_set_hidden(window_mark, true);
}


// The card's tap and long press: round the card, as the band's (owner, 2026-11-13). A swipe toward a tile this
// foreground does not offer scrolls nothing, and LVGL ends it as a click on the card: only a touch that stays where
// it began is a tap or a hold
static void card_event_cb(lv_event_t * e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);
    if (code == LV_EVENT_PRESSED) {
        press_point = point;
        return;
    }
    if (LV_ABS(point.x - press_point.x) > CARD_TAP_SLOP_PX || LV_ABS(point.y - press_point.y) > CARD_TAP_SLOP_PX) {
        return;
    }
    dope_step_target(code == LV_EVENT_SHORT_CLICKED ? 1 : -1, true);
}


void level_card_create(lv_obj_t * overlay) {
    card = lv_obj_create(overlay);
    lv_obj_remove_style_all(card);  // transparent
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(card, card_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    window_start = 0;
    level_card_set_rotation(system_config.rotation);
    dope_set_card_listener(refresh_targets);
}


void level_card_delete(void) {
    if (card == NULL) {
        return;
    }
    dope_set_card_listener(NULL);
    lv_obj_delete(card);    // the targets with it
    card = NULL;
    window_mark = NULL;
    memset(card_targets, 0, sizeof(card_targets));
}


void level_card_set_rotation(lv_display_rotation_t rotation) {
    if (card == NULL) {
        return;
    }
    const bool portrait = rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180;
    const int32_t height = portrait ? 456 : 280;

    side_by_side = !portrait;
    shown = portrait ? CARD_ROWS_PORTRAIT : CARD_COLUMNS_LANDSCAPE;
    card_width = portrait ? 280 : 456;
    card_height = height - CARD_TOP_PX - CARD_BOTTOM_PX;
    lv_obj_set_pos(card, 0, CARD_TOP_PX);
    lv_obj_set_size(card, card_width, card_height);
    build_targets();
    refresh_targets();
}

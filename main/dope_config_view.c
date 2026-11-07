#include "dope_config_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "system_config.h"
#include "common.h"
#include "app_cfg.h"
#include "display_shape.h"
#include "value_editor.h"
#include "config_view.h"
#include "level_band.h"

#define TAG "DopeConfigView"
#define NVS_NAMESPACE "DOPE"
#define NVS_NAMESPACE_SETTINGS "DOPECFG"

// The tile (ADR-007): an 8 px margin, 20 px between targets, cells at least 122 px (9.5 mm) on the short side
#define DOPE_TILE_GAP_PX 20
#define DOPE_CELL_PX 122
#define DOPE_HEADER_LANDSCAPE_PX 64     // landscape: the column with the letter and the range
#define DOPE_HEADER_INSET_PX 12         // portrait: the letter clears the panel's top corner
#define DOPE_CELL_INACTIVE_COLOUR 0x1a1a1a

// The editor's ranges, mil x 10
#define DOPE_ELEVATION_MIN_X10 (-50)
#define DOPE_ELEVATION_MAX_X10 400
#define DOPE_WIND_MAX_X10 100
#define DOPE_EDITOR_FAST_STEPS 5        // holding: 0.5 mil per repeat after a second


HEAPS_CAPS_ATTR dope_card_t dope_card;
static const dope_card_t dope_card_default = {0};   // every target empty

HEAPS_CAPS_ATTR dope_settings_t dope_settings;
static const dope_settings_t dope_settings_default = {
    .target_names = DOPE_TARGET_NAMES_LETTERS,
    .next_target_on_recoil = false,
};
static const char * target_names_options = "A, B, C\n1, 2, 3";


typedef enum {
    DOPE_FIELD_ELEVATION,
    DOPE_FIELD_WIND,
} dope_field_t;

typedef struct {
    lv_obj_t * row;
    lv_obj_t * header;
    lv_obj_t * letter_label;
    lv_obj_t * range_label;
    lv_obj_t * cell[2];             // by dope_field_t
    lv_obj_t * cell_caption[2];
    lv_obj_t * cell_value[2];

} dope_target_widgets_t;

static dope_target_widgets_t target_widgets[DOPE_TARGET_COUNT];
static lv_obj_t * target_list = NULL;
static lv_obj_t * clear_all_button = NULL;
static value_editor_t * editor = NULL;
static char editor_title[24];

// The confirmation for clearing a target (index) or the card (-1)
static lv_obj_t * confirm_screen = NULL;
static lv_obj_t * confirm_label = NULL;
static lv_obj_t * confirm_clear_button = NULL;
static lv_obj_t * confirm_cancel_button = NULL;
static int confirm_target = -1;



/*
----------------------------------------------------
The data
----------------------------------------------------
*/

esp_err_t load_dope_config() {
    return load_config(NVS_NAMESPACE, &dope_card, &dope_card_default, sizeof(dope_card));
}


esp_err_t save_dope_config() {
    return save_config(NVS_NAMESPACE, &dope_card, sizeof(dope_card));
}


void dope_format_elevation(char * buf, size_t size, int32_t elevation_x10) {
    snprintf(buf, size, "%s%ld.%ld", elevation_x10 < 0 ? "-" : "", labs(elevation_x10) / 10, labs(elevation_x10) % 10);
}


void dope_format_wind(char * buf, size_t size, int32_t wind_x10) {
    const char * side = wind_x10 > 0 ? "R" : wind_x10 < 0 ? "L" : "";
    snprintf(buf, size, "%s%ld.%ld", side, labs(wind_x10) / 10, labs(wind_x10) % 10);
}


esp_err_t load_dope_settings() {
    esp_err_t ret = load_config(NVS_NAMESPACE_SETTINGS, &dope_settings, &dope_settings_default, sizeof(dope_settings));
    if (dope_settings.target_names > DOPE_TARGET_NAMES_NUMBERS) {
        dope_settings.target_names = dope_settings_default.target_names;
    }
    return ret;
}


esp_err_t save_dope_settings() {
    return save_config(NVS_NAMESPACE_SETTINGS, &dope_settings, sizeof(dope_settings));
}


void dope_target_name(char * buf, size_t size, int index) {
    if (dope_settings.target_names == DOPE_TARGET_NAMES_NUMBERS) {
        snprintf(buf, size, "%d", index + 1);
    }
    else {
        snprintf(buf, size, "%c", 'A' + index);
    }
}


/*
----------------------------------------------------
Showing the card
----------------------------------------------------
*/

// LVGL lock held. The tile's row for one target
static void refresh_target(int index) {
    const dope_target_t * target = &dope_card.target[index];
    dope_target_widgets_t * w = &target_widgets[index];
    bool set = dope_target_is_set(target);
    char text[24];
    char name[DOPE_TARGET_NAME_SIZE];
    dope_target_name(name, sizeof(name), index);

    if (w->row != NULL) {
        lv_label_set_text(w->letter_label, name);
        if (set && target->range_m > 0) {
            lv_label_set_text_fmt(w->range_label, "%u m", (unsigned) target->range_m);
        }
        else {
            lv_label_set_text(w->range_label, "");
        }

        if (set) {
            dope_format_elevation(text, sizeof(text), target->elevation_x10);
            lv_label_set_text(w->cell_value[DOPE_FIELD_ELEVATION], text);
            dope_format_wind(text, sizeof(text), target->wind_x10);
            lv_label_set_text(w->cell_value[DOPE_FIELD_WIND], text);
            lv_obj_set_style_bg_color(w->cell[DOPE_FIELD_WIND], lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
        }
        else {
            lv_label_set_text(w->cell_value[DOPE_FIELD_ELEVATION], "-");
            lv_label_set_text(w->cell_value[DOPE_FIELD_WIND], "-");
            // The elevation makes a target: its wind waits until it has one, shown darker (not the theme's
            // disabled state, which lightens it)
            lv_obj_set_style_bg_color(w->cell[DOPE_FIELD_WIND], lv_color_hex(DOPE_CELL_INACTIVE_COLOUR), LV_PART_MAIN);
        }

        if (set && target->gust_x10 != DOPE_HOLD_NONE) {
            dope_format_wind(text, sizeof(text), target->gust_x10);
            lv_label_set_text_fmt(w->cell_caption[DOPE_FIELD_WIND], "Wind, gust %s", text);
        }
        else {
            lv_label_set_text(w->cell_caption[DOPE_FIELD_WIND], "Wind");
        }
    }
}


// The tile and the level's band, after the card or the names changed
static void refresh_card() {
    for (int i = 0; i < DOPE_TARGET_COUNT; i++) {
        refresh_target(i);
    }
    level_band_refresh_dope();
}


static void save_card() {
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_config());
}


/*
----------------------------------------------------
Editing a target (value_editor)
----------------------------------------------------
*/

static void format_editor_elevation(char * buf, size_t size, int32_t value) {
    char number[12];
    dope_format_elevation(number, sizeof(number), labs(value));
    snprintf(buf, size, "%s%s", number, value > 0 ? " U" : value < 0 ? " D" : "");
}


static void format_editor_elevation_step(char * buf, size_t size, int direction, int32_t step) {
    snprintf(buf, size, "%c0.%ld", direction < 0 ? '-' : '+', step);
}


static void format_editor_wind_step(char * buf, size_t size, int direction, int32_t step) {
    snprintf(buf, size, "%c 0.%ld", direction < 0 ? 'L' : 'R', step);
}


// The nearest set target before this one, whose holds a new target starts from (targets mostly go further out)
static const dope_target_t * previous_set_target(int index) {
    for (int i = index - 1; i >= 0; i--) {
        if (dope_target_is_set(&dope_card.target[i])) {
            return &dope_card.target[i];
        }
    }
    return NULL;
}


// Done, and leaving the tile, keep the edit. An empty target becomes set on Done; otherwise saves only a change
static void elevation_editor_closed(int32_t value, void * ctx) {
    int index = (int) (intptr_t) ctx;
    dope_target_t * target = &dope_card.target[index];

    if (dope_target_is_set(target) && target->elevation_x10 == value) {
        return;
    }

    if (!dope_target_is_set(target)) {
        const dope_target_t * previous = previous_set_target(index);
        memset(target, 0, sizeof(*target));
        target->wind_x10 = previous != NULL ? previous->wind_x10 : 0;
        target->gust_x10 = DOPE_HOLD_NONE;
        target->flags = DOPE_TARGET_SET;
    }
    target->elevation_x10 = (int16_t) value;
    target->flags &= ~DOPE_TARGET_FROM_KESTREL;

    save_card();
    refresh_card();
}


static void wind_editor_closed(int32_t value, void * ctx) {
    dope_target_t * target = &dope_card.target[(int) (intptr_t) ctx];

    if (target->wind_x10 == value) {
        return;
    }

    // The gust was the Kestrel's bracket; a hand edit replaces the hold, so the bracket no longer applies
    target->wind_x10 = (int16_t) value;
    target->gust_x10 = DOPE_HOLD_NONE;
    target->flags &= ~DOPE_TARGET_FROM_KESTREL;

    save_card();
    refresh_card();
}


static void open_editor(int index, dope_field_t field) {
    const dope_target_t * target = &dope_card.target[index];
    const dope_target_t * previous = previous_set_target(index);
    value_editor_params_t params = {
        .step = 1,
        .fast_steps = DOPE_EDITOR_FAST_STEPS,
        .ctx = (void *) (intptr_t) index,
    };
    char name[DOPE_TARGET_NAME_SIZE];
    dope_target_name(name, sizeof(name), index);

    if (field == DOPE_FIELD_ELEVATION) {
        snprintf(editor_title, sizeof(editor_title), "%s  Elevation", name);
        params.value = dope_target_is_set(target) ? target->elevation_x10
                     : previous != NULL ? previous->elevation_x10 : 0;
        params.min = DOPE_ELEVATION_MIN_X10;
        params.max = DOPE_ELEVATION_MAX_X10;
        params.format_value = format_editor_elevation;
        params.format_step = format_editor_elevation_step;
        params.closed = elevation_editor_closed;
    }
    else {
        snprintf(editor_title, sizeof(editor_title), "%s  Wind", name);
        params.value = target->wind_x10;
        params.min = -DOPE_WIND_MAX_X10;
        params.max = DOPE_WIND_MAX_X10;
        params.format_value = dope_format_wind;
        params.format_step = format_editor_wind_step;
        params.closed = wind_editor_closed;
    }
    params.title = editor_title;

    value_editor_open(editor, &params);
}


static void cell_clicked_cb(lv_event_t * e) {
    lv_obj_t * cell = lv_event_get_current_target_obj(e);
    int index = (int) (intptr_t) lv_obj_get_user_data(cell);
    dope_field_t field = (dope_field_t) (intptr_t) lv_event_get_user_data(e);
    if (field == DOPE_FIELD_WIND && !dope_target_is_set(&dope_card.target[index])) {
        return;     // an empty target's wind waits for its elevation
    }
    open_editor(index, field);
}


/*
----------------------------------------------------
Clearing a target or the card, after a confirmation
----------------------------------------------------
*/

static void open_confirm(int target) {
    confirm_target = target;
    if (target >= 0) {
        char name[DOPE_TARGET_NAME_SIZE];
        dope_target_name(name, sizeof(name), target);
        lv_label_set_text_fmt(confirm_label, "Clear target %s?", name);
    }
    else {
        lv_label_set_text(confirm_label, "Clear all targets?");
    }
    lv_obj_remove_flag(confirm_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(confirm_screen);
}


static void close_confirm() {
    lv_obj_add_flag(confirm_screen, LV_OBJ_FLAG_HIDDEN);
}


static void confirm_clear_cb(lv_event_t * e) {
    close_confirm();

    bool changed = false;
    for (int i = 0; i < DOPE_TARGET_COUNT; i++) {
        if ((confirm_target < 0 || confirm_target == i) && dope_target_is_set(&dope_card.target[i])) {
            memset(&dope_card.target[i], 0, sizeof(dope_target_t));
            changed = true;
        }
    }

    if (changed) {
        save_card();
        refresh_card();
    }
}


static void confirm_cancel_cb(lv_event_t * e) {
    close_confirm();
}


static void target_long_pressed_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_current_target_obj(e);
    int index = (int) (intptr_t) lv_obj_get_user_data(obj);
    if (dope_target_is_set(&dope_card.target[index])) {
        open_confirm(index);
    }
}


static void clear_all_clicked_cb(lv_event_t * e) {
    open_confirm(-1);
}


static lv_obj_t * create_screen(lv_obj_t * parent) {
    lv_obj_t * screen = lv_obj_create(parent);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, DOPE_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(screen, DOPE_TILE_GAP_PX, LV_PART_MAIN);
    return screen;
}


static void create_confirm(lv_obj_t * parent) {
    confirm_screen = create_screen(parent);
    lv_obj_remove_flag(confirm_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(confirm_screen, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(confirm_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(confirm_screen, LV_OBJ_FLAG_HIDDEN);

    confirm_label = value_editor_create_label(confirm_screen, &lv_font_montserrat_32);
    lv_label_set_long_mode(confirm_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(confirm_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    confirm_clear_button = value_editor_create_button(confirm_screen, LV_PALETTE_RED);
    lv_obj_set_style_bg_color(confirm_clear_button, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(confirm_clear_button, &lv_font_montserrat_32), "Clear");
    lv_obj_add_event_cb(confirm_clear_button, confirm_clear_cb, LV_EVENT_SHORT_CLICKED, NULL);

    confirm_cancel_button = value_editor_create_button(confirm_screen, LV_PALETTE_GREY);
    lv_label_set_text(value_editor_create_label(confirm_cancel_button, &lv_font_montserrat_32), "Cancel");
    lv_obj_add_event_cb(confirm_cancel_button, confirm_cancel_cb, LV_EVENT_SHORT_CLICKED, NULL);
}


/*
----------------------------------------------------
The tile: one row per target
----------------------------------------------------
*/

static lv_obj_t * create_cell(lv_obj_t * parent, int index, dope_field_t field, const char * caption,
                              lv_obj_t ** caption_label, lv_obj_t ** value_label) {
    lv_obj_t * cell = value_editor_create_button(parent, LV_PALETTE_ORANGE);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 4, LV_PART_MAIN);
    lv_obj_set_user_data(cell, (void *) (intptr_t) index);
    lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) field);
    lv_obj_add_event_cb(cell, target_long_pressed_cb, LV_EVENT_LONG_PRESSED, NULL);

    *caption_label = value_editor_create_label(cell, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(*caption_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_label_set_text(*caption_label, caption);

    *value_label = value_editor_create_label(cell, &lv_font_montserrat_40);
    return cell;
}


static void create_target_row(lv_obj_t * parent, int index) {
    dope_target_widgets_t * w = &target_widgets[index];

    w->row = lv_obj_create(parent);
    lv_obj_remove_style_all(w->row);
    lv_obj_set_size(w->row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(w->row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(w->row, DOPE_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_remove_flag(w->row, LV_OBJ_FLAG_SCROLLABLE);

    // The letter and the range; a long press here clears the target, as on the cells
    w->header = lv_obj_create(w->row);
    lv_obj_remove_style_all(w->header);
    lv_obj_set_style_pad_column(w->header, 12, LV_PART_MAIN);
    lv_obj_remove_flag(w->header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(w->header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(w->header, (void *) (intptr_t) index);
    lv_obj_add_event_cb(w->header, target_long_pressed_cb, LV_EVENT_LONG_PRESSED, NULL);

    w->letter_label = value_editor_create_label(w->header, &lv_font_montserrat_32);
    lv_obj_set_style_text_color(w->letter_label, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);

    w->range_label = value_editor_create_label(w->header, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(w->range_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);

    w->cell[DOPE_FIELD_ELEVATION] = create_cell(w->row, index, DOPE_FIELD_ELEVATION, "Elevation",
                                                &w->cell_caption[DOPE_FIELD_ELEVATION],
                                                &w->cell_value[DOPE_FIELD_ELEVATION]);
    w->cell[DOPE_FIELD_WIND] = create_cell(w->row, index, DOPE_FIELD_WIND, "Wind",
                                           &w->cell_caption[DOPE_FIELD_WIND], &w->cell_value[DOPE_FIELD_WIND]);
}


void set_rotation_dope_card_config_view(lv_display_rotation_t rotation) {
    bool portrait = rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180;

    // Portrait: the letter and range over the two cells; landscape: the letter and range in a column on the left
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {LV_GRID_CONTENT, DOPE_CELL_PX, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {DOPE_HEADER_LANDSCAPE_PX, LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {DOPE_CELL_PX, LV_GRID_TEMPLATE_LAST};

    for (int i = 0; i < DOPE_TARGET_COUNT; i++) {
        dope_target_widgets_t * w = &target_widgets[i];
        if (portrait) {
            lv_obj_set_grid_dsc_array(w->row, portrait_cols, portrait_rows);
            lv_obj_set_grid_cell(w->header, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
            lv_obj_set_grid_cell(w->cell[DOPE_FIELD_ELEVATION], LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
            lv_obj_set_grid_cell(w->cell[DOPE_FIELD_WIND], LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
            lv_obj_set_flex_flow(w->header, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(w->header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
            lv_obj_set_height(w->header, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_left(w->header, DOPE_HEADER_INSET_PX, LV_PART_MAIN);
        }
        else {
            lv_obj_set_grid_dsc_array(w->row, landscape_cols, landscape_rows);
            lv_obj_set_grid_cell(w->header, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
            lv_obj_set_grid_cell(w->cell[DOPE_FIELD_ELEVATION], LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
            lv_obj_set_grid_cell(w->cell[DOPE_FIELD_WIND], LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
            lv_obj_set_flex_flow(w->header, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(w->header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_left(w->header, 0, LV_PART_MAIN);
        }
    }

    // The confirmation: portrait, the question over Clear over Cancel; landscape, the question over Cancel and
    // Clear side by side
    static const int32_t confirm_portrait_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t confirm_portrait_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t confirm_landscape_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t confirm_landscape_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (portrait) {
        lv_obj_set_grid_dsc_array(confirm_screen, confirm_portrait_cols, confirm_portrait_rows);
        lv_obj_set_grid_cell(confirm_label, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(confirm_clear_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(confirm_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    }
    else {
        lv_obj_set_grid_dsc_array(confirm_screen, confirm_landscape_cols, confirm_landscape_rows);
        lv_obj_set_grid_cell(confirm_label, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(confirm_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(confirm_clear_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }

    value_editor_set_rotation(editor, rotation);
}


// The card is read at start-up (app_main), so the level's band and the tile show the same targets
void create_dope_config_view(lv_obj_t * parent) {
    target_list = create_screen(parent);
    lv_obj_set_flex_flow(target_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(target_list, LV_DIR_VER);

    for (int i = 0; i < DOPE_TARGET_COUNT; i++) {
        create_target_row(target_list, i);
    }

    clear_all_button = value_editor_create_button(target_list, LV_PALETTE_RED);
    lv_obj_set_size(clear_all_button, lv_pct(100), DOPE_CELL_PX);
    lv_obj_set_style_bg_color(clear_all_button, lv_palette_darken(LV_PALETTE_RED, 3), LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(clear_all_button, &lv_font_montserrat_32), "Clear all");
    lv_obj_add_event_cb(clear_all_button, clear_all_clicked_cb, LV_EVENT_SHORT_CLICKED, NULL);

    editor = value_editor_create(parent);
    ESP_ERROR_CHECK(editor == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    create_confirm(parent);

    set_rotation_dope_card_config_view(system_config.rotation);
    refresh_card();
}


void dope_config_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_dope_card_config_view(system_config.rotation);
}


void enable_dope_config_view(bool enable) {
    if (!enable) {
        // Leaving the tile keeps an edit in progress, as Done does, and drops an unanswered confirmation. Nothing
        // else is saved here: every change saves when it is made (ADR-007)
        value_editor_close(editor);
        close_confirm();
    }
}


/*
----------------------------------------------------
Settings page: Dope
----------------------------------------------------
*/

static lv_obj_t * dope_config_page = NULL;


// The names show at once on the tile and the level; Save keeps them
static void target_names_changed_cb(lv_event_t * e) {
    uint32_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if (selected <= DOPE_TARGET_NAMES_NUMBERS) {
        dope_settings.target_names = (uint8_t) selected;
        refresh_card();
    }
}


static void on_save_button_pressed(lv_event_t * e) {
    update_info_msg_box_result(msg_box, save_dope_settings(), "Configuration Saved", "Save failed");
}


static void on_reload_button_pressed(lv_event_t * e) {
    esp_err_t ret = load_dope_settings();
    refresh_card();
    config_page_rebuild(dope_config_page);

    update_info_msg_box_result(msg_box, ret, "Previous Configuration Reloaded", "Reload failed");
}


static void on_reset_button_pressed(lv_event_t * e) {
    memcpy(&dope_settings, &dope_settings_default, sizeof(dope_settings));
    refresh_card();
    config_page_rebuild(dope_config_page);

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");
}


// The page's widgets, built from the settings; config_page_rebuild() runs it again after Reload and Reset
static void fill_dope_config_page(lv_obj_t * sub_page_config_view) {
    lv_obj_t * container;

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Target Names");
    create_dropdown_list(container, target_names_options, dope_settings.target_names, target_names_changed_cb, NULL);

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Next Target on Recoil");
    create_switch(container, &dope_settings.next_target_on_recoil, NULL);

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);
}


lv_obj_t * create_dope_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    dope_config_page = create_config_page(parent, fill_dope_config_page);

    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "Dope");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, dope_config_page);

    return dope_config_page;
}


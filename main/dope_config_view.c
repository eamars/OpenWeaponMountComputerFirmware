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
#include "settings_ui.h"
#include "level_band.h"
#include "kestrel_service.h"

#include <math.h>
#include <stddef.h>

#define TAG "DopeConfigView"
#define NVS_NAMESPACE "DOPE"
#define NVS_NAMESPACE_SETTINGS "DOPECFG"

// The tile (ADR-007): an 8 px margin, 20 px between targets, cells at least 122 px (9.5 mm) on the short side
#define DOPE_TILE_GAP_PX 20
#define DOPE_CELL_PX 122
#define DOPE_HEADER_LANDSCAPE_PX 64     // landscape: the column with the letter and the range
#define DOPE_HEADER_INSET_PX 12         // portrait: the letter clears the panel's top corner
#define DOPE_HEADER_PORTRAIT_PX 40      // portrait: the letter and range line over the cells
#define DOPE_ROW_INNER_GAP_PX 8         // between a row's header (or status) and its cells
#define DOPE_CELL_INACTIVE_COLOUR 0x1a1a1a

// The editor's ranges, mil x 10
#define DOPE_ELEVATION_MIN_X10 (-50)
#define DOPE_ELEVATION_MAX_X10 999      // 99.9 mil: a .22LR at 700 m already needs 62 (owner's Kestrel)
#define DOPE_WIND_MAX_X10 100
#define DOPE_EDITOR_FAST_STEPS 5        // holding: 0.5 mil per repeat after a second


HEAPS_CAPS_ATTR dope_card_t dope_card;
static const dope_card_t dope_card_default = {0};   // every target empty

HEAPS_CAPS_ATTR dope_settings_t dope_settings;
static const dope_settings_t dope_settings_default = {
    .target_names = DOPE_TARGET_NAMES_LETTERS,
    .next_target_on_recoil = false,
    .pull_count = 5,
    .target_count = 12,
};
static const uint8_t target_count_values[] = {10, 12, 15};


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

HEAPS_CAPS_ATTR static dope_target_widgets_t target_widgets[DOPE_TARGET_MAX];
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

// The Kestrel row at the top of the tile (ADR-007 step 4)
#define DOPE_KESTREL_REFRESH_MS 300
#define DOPE_PULL_BUTTON_LANDSCAPE_PX 150
static lv_obj_t * kestrel_row = NULL;
static lv_obj_t * kestrel_label = NULL;
static lv_obj_t * pull_button = NULL;
static bool pull_editor_open = false;       // the editor asks how many targets to pull
static uint32_t kestrel_shown_sequence = UINT32_MAX;
static uint32_t applied_result_id = 0;      // the last pull result put on the card



/*
----------------------------------------------------
The data
----------------------------------------------------
*/

esp_err_t load_dope_config() {
    // A card saved with 10 targets loads into the first 10
    return load_config_grown(NVS_NAMESPACE, &dope_card, &dope_card_default, sizeof(dope_card),
                             DOPE_TARGET_COUNT_OLD * sizeof(dope_target_t));
}


esp_err_t save_dope_config() {
    return save_config(NVS_NAMESPACE, &dope_card, sizeof(dope_card));
}


void dope_format_elevation(char * buf, size_t size, int32_t elevation_x10) {
    snprintf(buf, size, "%s%ld.%ld", elevation_x10 < 0 ? "-" : "", labs(elevation_x10) / 10, labs(elevation_x10) % 10);
}


// A hold, "R0.4", with its side letter when side is non-zero (-1 left, +1 right) and shown
static void format_hold(char * buf, size_t size, int32_t x10, int side, bool show_side) {
    const char * letter = !show_side ? "" : side > 0 ? "R" : side < 0 ? "L" : "";
    snprintf(buf, size, "%s%ld.%ld", letter, labs(x10) / 10, labs(x10) % 10);
}


// The side of a hold: its sign, or for a hold that rounded to 0.0, the side the Kestrel gave it
static int hold_side(int32_t x10, uint8_t flags, uint8_t left_flag, uint8_t right_flag) {
    if (x10 != 0) {
        return x10 > 0 ? 1 : -1;
    }
    return (flags & left_flag) ? -1 : (flags & right_flag) ? 1 : 0;
}


void dope_format_wind(char * buf, size_t size, int32_t wind_x10) {
    format_hold(buf, size, wind_x10, wind_x10 > 0 ? 1 : wind_x10 < 0 ? -1 : 0, true);
}


void dope_format_target_wind(char * buf, size_t size, const dope_target_t * target) {
    format_hold(buf, size, target->wind_x10,
                hold_side(target->wind_x10, target->flags, DOPE_TARGET_WIND_LEFT, DOPE_TARGET_WIND_RIGHT), true);
}


void dope_format_target_gust(char * buf, size_t size, const dope_target_t * target) {
    int wind_side = hold_side(target->wind_x10, target->flags, DOPE_TARGET_WIND_LEFT, DOPE_TARGET_WIND_RIGHT);
    int gust_side = hold_side(target->gust_x10, target->flags, DOPE_TARGET_GUST_LEFT, DOPE_TARGET_GUST_RIGHT);
    // The gust's letter only when it differs from the wind's (a wind with no side shows the gust's own)
    format_hold(buf, size, target->gust_x10, gust_side, gust_side != wind_side);
}


esp_err_t load_dope_settings() {
    // Settings saved before the Card Size setting load with the default size
    esp_err_t ret = load_config_grown(NVS_NAMESPACE_SETTINGS, &dope_settings, &dope_settings_default,
                                      sizeof(dope_settings), offsetof(dope_settings_t, target_count));
    if (dope_settings.target_names > DOPE_TARGET_NAMES_NUMBERS) {
        dope_settings.target_names = dope_settings_default.target_names;
    }
    if (dope_settings.pull_count < 1 || dope_settings.pull_count > KL_TARGET_COUNT) {
        dope_settings.pull_count = dope_settings_default.pull_count;
    }
    if (memchr(target_count_values, dope_settings.target_count, sizeof(target_count_values)) == NULL) {
        dope_settings.target_count = dope_settings_default.target_count;
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
        if (index < dope_target_count()) {
            lv_obj_set_hidden(w->row, false);
        }
        else {
            lv_obj_set_hidden(w->row, true);
        }
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
            dope_format_target_wind(text, sizeof(text), target);
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
            dope_format_target_gust(text, sizeof(text), target);
            lv_label_set_text_fmt(w->cell_caption[DOPE_FIELD_WIND], "Wind / %s", text);
        }
        else {
            lv_label_set_text(w->cell_caption[DOPE_FIELD_WIND], "Wind");
        }
    }
}


// The tile and the level's band, after the card, the names or the size changed
static int32_t locked_rows_per_screen = 2;     // set by lock_rows_to_list()

static void refresh_card() {
    for (int i = 0; i < DOPE_TARGET_MAX; i++) {
        refresh_target(i);
    }
    // The card size shows or hides rows: the last screen's rows move
    if (target_list != NULL) {
        display_locked_list_snap(target_list, locked_rows_per_screen);
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
    target->flags &= ~(DOPE_TARGET_FROM_KESTREL | DOPE_TARGET_WIND_LEFT | DOPE_TARGET_WIND_RIGHT |
                       DOPE_TARGET_GUST_LEFT | DOPE_TARGET_GUST_RIGHT);

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
    lv_obj_set_hidden(confirm_screen, false);
    lv_obj_move_foreground(confirm_screen);
}


static void close_confirm() {
    lv_obj_set_hidden(confirm_screen, true);
}


static void confirm_clear_cb(lv_event_t * e) {
    close_confirm();

    // Clear all clears every stored target, shown or not, so a larger card size later shows none of them
    bool changed = false;
    for (int i = 0; i < DOPE_TARGET_MAX; i++) {
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
    lv_obj_set_scrollable(confirm_screen, false);
    lv_obj_set_scroll_chain_hor(confirm_screen, false);
    lv_obj_set_scroll_chain_ver(confirm_screen, false);
    lv_obj_set_clickable(confirm_screen, true);
    lv_obj_set_hidden(confirm_screen, true);

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
The Kestrel row: Pull asks how many targets, then the Kestrel's targets replace the card
----------------------------------------------------
*/

static int16_t hold_x10(float mil) {
    if (!isfinite(mil)) {
        return 0;
    }
    long x10 = lroundf(mil * 10.0f);
    return (int16_t) (x10 > INT16_MAX ? INT16_MAX : x10 < -INT16_MAX ? -INT16_MAX : x10);
}


// A wind hold the 0.1 mil card rounds to 0.0 keeps the Kestrel's side (0.02 L shows "L0.0"), so the shooter still
// sees which way the wind blows (owner, 2026-11-07). mil is in the card's signs, negative left
static uint8_t zero_side_flag(int16_t x10, float mil, uint8_t left_flag, uint8_t right_flag) {
    if (x10 != 0 || !isfinite(mil) || mil == 0.0f) {
        return 0;
    }
    return mil < 0 ? left_flag : right_flag;
}


// A complete pull replaces the card's first 10 targets, the Kestrel's Target Card: the pulled ones from the Kestrel
// (the Kestrel's signs are negative up and negative right, the card's positive), the rest of the 10 empty. Targets
// past 10 are entered by hand and stay (owner, 2026-11-07). The names follow the Kestrel's, which can differ
// between Target Cards (owner, 2026-11-07). Saved once each
static void apply_pull(const kestrel_status_t * s) {
    for (int i = 0; i < KL_TARGET_COUNT; i++) {
        dope_target_t * target = &dope_card.target[i];
        memset(target, 0, sizeof(*target));
        if (i < s->target_count) {
            const kl_target_t * k = &s->targets[i];
            target->elevation_x10 = hold_x10(-k->elevation_mil);
            target->wind_x10 = hold_x10(-k->wind1_mil);
            target->gust_x10 = hold_x10(-k->wind2_mil);
            uint32_t range_m = (k->range_cm + 50) / 100;
            target->range_m = (uint16_t) (range_m > UINT16_MAX ? UINT16_MAX : range_m);
            target->flags = DOPE_TARGET_SET | DOPE_TARGET_FROM_KESTREL |
                            zero_side_flag(target->wind_x10, -k->wind1_mil, DOPE_TARGET_WIND_LEFT,
                                           DOPE_TARGET_WIND_RIGHT) |
                            zero_side_flag(target->gust_x10, -k->wind2_mil, DOPE_TARGET_GUST_LEFT,
                                           DOPE_TARGET_GUST_RIGHT);
        }
        else {
            target->gust_x10 = DOPE_HOLD_NONE;
        }
    }
    save_card();

    uint8_t names = s->numbered ? DOPE_TARGET_NAMES_NUMBERS : DOPE_TARGET_NAMES_LETTERS;
    if (names != dope_settings.target_names) {
        dope_settings.target_names = names;
        ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_settings());
        ESP_LOGI(TAG, "target names now %s, as the Kestrel's", s->numbered ? "1, 2, 3" : "A, B, C");
    }
    refresh_card();
    ESP_LOGI(TAG, "first %d targets replaced by %d from the Kestrel", KL_TARGET_COUNT, s->target_count);
}


// Shows the Kestrel row while Kestrel is on, its status, and puts each new complete pull on the card, whichever
// tile is showing
static void kestrel_timer_cb(lv_timer_t * timer) {
    if (kestrel_row == NULL) {
        return;
    }
    if (kestrel_settings.mode == KESTREL_MODE_OFF) {
        lv_obj_set_hidden(kestrel_row, true);
    }
    else {
        lv_obj_set_hidden(kestrel_row, false);
    }

    kestrel_status_t s = kestrel_get_status();
    if (s.state == KESTREL_PULL_DONE && s.have_result && s.result_id != applied_result_id) {
        applied_result_id = s.result_id;
        apply_pull(&s);
    }
    if (s.sequence != kestrel_shown_sequence) {
        kestrel_shown_sequence = s.sequence;
        lv_label_set_text(kestrel_label, s.message);
    }
}


static void format_pull_count(char * buf, size_t size, int32_t value) {
    char last[DOPE_TARGET_NAME_SIZE];
    dope_target_name(last, sizeof(last), (int) value - 1);
    if (value == 1) {
        snprintf(buf, size, "1 target");
    }
    else {
        char first[DOPE_TARGET_NAME_SIZE];
        dope_target_name(first, sizeof(first), 0);
        snprintf(buf, size, "%s to %s", first, last);
    }
}


static void format_pull_count_step(char * buf, size_t size, int direction, int32_t step) {
    snprintf(buf, size, "%c%ld", direction < 0 ? '-' : '+', step);
}


// Done: remember the count (the next Pull starts from it) and pull
static void pull_editor_closed(int32_t value, void * ctx) {
    pull_editor_open = false;
    if (value != dope_settings.pull_count) {
        dope_settings.pull_count = (uint8_t) value;
        ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_settings());
    }
    kestrel_request_pull((int) value);  // the row shows the outcome
}


static void pull_clicked_cb(lv_event_t * e) {
    if (kestrel_get_status().state == KESTREL_PULL_RUNNING) {
        return;
    }
    // The Kestrel fills unused targets with defaults, so the card cannot tell which are set up: the shooter says
    // how many, from the first (owner, 2026-11-07). At most its 10, or fewer on a smaller card
    int max = dope_target_count() < KL_TARGET_COUNT ? dope_target_count() : KL_TARGET_COUNT;
    const value_editor_params_t params = {
        .title = "Targets to pull",
        .value = dope_settings.pull_count > max ? max : dope_settings.pull_count,
        .min = 1,
        .max = max,
        .step = 1,
        .fast_steps = 1,
        .format_value = format_pull_count,
        .format_step = format_pull_count_step,
        .closed = pull_editor_closed,
    };
    pull_editor_open = true;
    value_editor_open(editor, &params);
}


static void create_kestrel_row(lv_obj_t * parent) {
    kestrel_row = lv_obj_create(parent);
    lv_obj_remove_style_all(kestrel_row);
    lv_obj_set_size(kestrel_row, lv_pct(100), DOPE_CELL_PX);  // the rotation sets the locked height
    lv_obj_set_style_pad_row(kestrel_row, DOPE_ROW_INNER_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kestrel_row, DOPE_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_set_scrollable(kestrel_row, false);
    lv_obj_set_hidden(kestrel_row, true);

    kestrel_label = value_editor_create_label(kestrel_row, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(kestrel_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_label_set_long_mode(kestrel_label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(kestrel_label, "Kestrel");

    pull_button = value_editor_create_button(kestrel_row, LV_PALETTE_ORANGE);
    lv_obj_set_style_bg_color(pull_button, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(pull_button, &lv_font_montserrat_32), "Pull");
    lv_obj_add_event_cb(pull_button, pull_clicked_cb, LV_EVENT_SHORT_CLICKED, NULL);

    lv_timer_create(kestrel_timer_cb, DOPE_KESTREL_REFRESH_MS, NULL);
}


static void set_rotation_kestrel_row(bool portrait) {
    // Portrait: the status over a full-width Pull, each half the row (owner: Pull filling the rest of the 210 px row
    // was too big; about 95 px each, as a settings row); landscape: the status on the left, Pull on the right
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(1), DOPE_PULL_BUTTON_LANDSCAPE_PX, LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (portrait) {
        lv_obj_set_grid_dsc_array(kestrel_row, portrait_cols, portrait_rows);
        lv_obj_set_grid_cell(kestrel_label, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(pull_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_style_pad_left(kestrel_label, DOPE_HEADER_INSET_PX, LV_PART_MAIN);
    }
    else {
        lv_obj_set_grid_dsc_array(kestrel_row, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(kestrel_label, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(pull_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_style_pad_left(kestrel_label, DOPE_HEADER_INSET_PX, LV_PART_MAIN);
    }
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
    lv_obj_set_size(w->row, lv_pct(100), DOPE_CELL_PX);   // the rotation sets the locked height
    lv_obj_set_style_pad_row(w->row, DOPE_ROW_INNER_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(w->row, DOPE_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_set_scrollable(w->row, false);

    // The letter and the range; a long press here clears the target, as on the cells
    w->header = lv_obj_create(w->row);
    lv_obj_remove_style_all(w->header);
    lv_obj_set_style_pad_column(w->header, 12, LV_PART_MAIN);
    lv_obj_set_scrollable(w->header, false);
    lv_obj_set_clickable(w->header, true);
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
    static const int32_t portrait_rows[] = {DOPE_HEADER_PORTRAIT_PX, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {DOPE_HEADER_LANDSCAPE_PX, LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    for (int i = 0; i < DOPE_TARGET_MAX; i++) {
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

    set_rotation_kestrel_row(portrait);
    value_editor_set_rotation(editor, rotation);
}


// Whole rows only (owner, 2026-11-07): every row of the list is one height, as many as fit the list, stretched to
// fill it; the list snaps to row edges, so a scroll never stops on half a row. Portrait: 2 rows (cells 164 px);
// landscape: 2 rows of 122 px. Run when the list itself changes size, from the size it has then: the rotation
// handler runs before the list has its new size
static void realign_list_async(void * p) {
    lv_obj_update_layout(target_list);
    lv_obj_update_snap(target_list, LV_ANIM_OFF);    // back onto a row edge
}


static void lock_rows_to_list(void) {
    int32_t height = lv_obj_get_content_height(target_list);
    bool portrait = height > lv_obj_get_content_width(target_list);
    int32_t min_row = portrait ? DOPE_HEADER_PORTRAIT_PX + DOPE_ROW_INNER_GAP_PX + DOPE_CELL_PX : DOPE_CELL_PX;
    int32_t row_height = display_locked_row_height(height, min_row, DOPE_TILE_GAP_PX);

    lv_obj_set_height(kestrel_row, row_height);
    lv_obj_set_height(clear_all_button, row_height);
    for (int i = 0; i < DOPE_TARGET_MAX; i++) {
        lv_obj_set_height(target_widgets[i].row, row_height);
    }
    locked_rows_per_screen = (height + DOPE_TILE_GAP_PX) / (row_height + DOPE_TILE_GAP_PX);
    display_locked_list_snap(target_list, locked_rows_per_screen);
    // Inside a layout pass (the size-change event) a layout update is ignored: realign once it has finished
    lv_async_call(realign_list_async, NULL);
}


static void target_list_size_changed_cb(lv_event_t * e) {
    lock_rows_to_list();
}


// The card is read at start-up (app_main), so the level's band and the tile show the same targets
void create_dope_config_view(lv_obj_t * parent) {
    target_list = create_screen(parent);
    lv_obj_set_flex_flow(target_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(target_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(target_list, LV_SCROLL_SNAP_START);    // stops on row edges: no half rows
    lv_obj_add_event_cb(target_list, target_list_size_changed_cb, LV_EVENT_SIZE_CHANGED, NULL);

    create_kestrel_row(target_list);
    for (int i = 0; i < DOPE_TARGET_MAX; i++) {
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
    lv_obj_update_layout(target_list);
    lock_rows_to_list();
    refresh_card();
}


void dope_config_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_dope_card_config_view(system_config.rotation);
}


void enable_dope_config_view(bool enable) {
    if (!enable) {
        // Leaving the tile keeps an edit in progress, as Done does, and drops an unanswered confirmation. Nothing
        // else is saved here: every change saves when it is made (ADR-007). The pull count is a question, not an
        // edit: leaving drops it and pulls nothing
        if (pull_editor_open) {
            pull_editor_open = false;
            value_editor_cancel(editor);
        }
        else {
            value_editor_close(editor);
        }
        close_confirm();
    }
}


/*
----------------------------------------------------
Settings page: Dope (ADR-008: each change stored when made)
----------------------------------------------------
*/

static const char * const target_names_option_names[] = {"A, B, C", "1, 2, 3"};
static const char * const target_count_option_names[] = {"10 targets", "12 targets", "15 targets"};


static int get_target_names(void * ctx) {
    return dope_settings.target_names;
}


// The names show at once on the tile and the level
static void set_target_names(int index, void * ctx) {
    dope_settings.target_names = (uint8_t) index;
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_settings());
    refresh_card();
}


static int get_target_count(void * ctx) {
    for (int i = 0; i < (int) sizeof(target_count_values); i++) {
        if (target_count_values[i] == dope_settings.target_count) {
            return i;
        }
    }
    return 0;
}


// The size shows at once on the tile and the level. Targets past a smaller size are kept, hidden
static void set_target_count(int index, void * ctx) {
    dope_settings.target_count = target_count_values[index];
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_settings());
    refresh_card();
}


static bool get_next_on_recoil(void * ctx) {
    return dope_settings.next_target_on_recoil;
}


static void set_next_on_recoil(bool on, void * ctx) {
    dope_settings.next_target_on_recoil = on;
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_settings());
}


// The settings, not the card: Clear all on the tile clears the targets
static void reset_dope_settings(void * ctx) {
    memcpy(&dope_settings, &dope_settings_default, sizeof(dope_settings));
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_dope_settings());
    refresh_card();
}


void dope_settings_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "%d targets, %s", dope_target_count(),
             dope_settings.target_names == DOPE_TARGET_NAMES_NUMBERS ? "1, 2, 3" : "A, B, C");
}


settings_page_t * create_dope_settings_page(void) {
    settings_page_t * page = settings_page_create("Dope");
    settings_row_choice(page, "Target names", target_names_option_names, 2, get_target_names, set_target_names, NULL);
    settings_row_choice(page, "Card size", target_count_option_names, 3, get_target_count, set_target_count, NULL);
    settings_row_toggle(page, "Next target on recoil", get_next_on_recoil, set_next_on_recoil, NULL);
    settings_row_reset(page, "Reset Dope settings", "Reset the Dope settings? The targets stay.", reset_dope_settings,
                       NULL);
    return page;
}

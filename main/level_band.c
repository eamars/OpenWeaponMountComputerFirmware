#include "level_band.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "dope_config_view.h"
#include "font_dope_digits.h"
#include "lvgl_display.h"
#include "system_config.h"

#define TAG "LevelBand"

// Layout (ADR-007): the band starts 20 px below the level's ticks and runs to the bottom edge; text keeps 16 px
// from the sides (clear of the panel's 30 px corners at the band's bottom row). No background: white text reads on
// every level colour on the AMOLED (owner, 2026-11-07); the band is only a layout and a touch area
#define BAND_PAD_SIDE_PX 16
#define BAND_PAD_TOP_PX 8
#define BAND_PAD_BOTTOM_PX 8
#define BAND_PORTRAIT_TOP_PX 252        // ticks at 224..232 (half of 456)
#define BAND_LANDSCAPE_TOP_PX 164       // ticks at 136..144 (half of 280)
#define BAND_BAR_HEIGHT_PX 8
#define BAND_TOUCH_EXTRA_PX 16          // the time and the bar take touches a little beyond their edges
#define BAND_LAST_SECONDS_MS 10000      // the bar turns red, matching the beeps

static lv_obj_t * band = NULL;
static lv_obj_t * timer_bar = NULL;
static lv_obj_t * header_row = NULL;
static lv_obj_t * name_label = NULL;
static lv_obj_t * info_label = NULL;
static lv_obj_t * time_label = NULL;
static lv_obj_t * value_row = NULL;
static lv_obj_t * elevation_group = NULL;
static lv_obj_t * elevation_label = NULL;
static lv_obj_t * unit_label = NULL;
static lv_obj_t * wind_group = NULL;
static lv_obj_t * wind_label = NULL;
static lv_obj_t * gust_label = NULL;

static countdown_timer_t * band_timer = NULL;
static bool timer_shown = false;
static int current_target = -1;         // the target shown; RAM only, so stepping never writes NVS


static int find_set_target(int from, int direction) {
    for (int i = from; i >= 0 && i < DOPE_TARGET_COUNT; i += direction) {
        if (dope_target_is_set(&dope_card.target[i])) {
            return i;
        }
    }
    return -1;
}


static void update_visibility() {
    if (band == NULL) {
        return;
    }
    if (timer_shown || current_target >= 0) {
        lv_obj_remove_flag(band, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
    }

    // The bar keeps its place while hidden, so the dope does not move when the timer comes and goes
    lv_obj_set_style_opa(timer_bar, timer_shown ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    if (timer_shown) {
        lv_obj_add_flag(timer_bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(time_label, LV_OBJ_FLAG_CLICKABLE);
    }
    else {
        lv_obj_remove_flag(timer_bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(time_label, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_text(time_label, "");
    }
}


static void show_target() {
    if (band == NULL) {
        return;
    }

    if (current_target < 0) {
        lv_label_set_text(name_label, "");
        lv_label_set_text(info_label, "");
        lv_obj_add_flag(value_row, LV_OBJ_FLAG_HIDDEN);
        update_visibility();
        return;
    }

    const dope_target_t * target = &dope_card.target[current_target];
    char text[32];
    char name[DOPE_TARGET_NAME_SIZE];

    dope_target_name(name, sizeof(name), current_target);
    lv_label_set_text(name_label, name);

    // Range and the next target, as far as known
    char range[12] = "";
    char next[DOPE_TARGET_NAME_SIZE + 8] = "";
    if (target->range_m > 0) {
        snprintf(range, sizeof(range), "%u m", (unsigned) target->range_m);
    }
    int next_index = find_set_target(current_target + 1, 1);
    if (next_index >= 0) {
        dope_target_name(name, sizeof(name), next_index);
        snprintf(next, sizeof(next), "next %s", name);
    }
    snprintf(text, sizeof(text), "%s%s%s", range, range[0] && next[0] ? "   " : "", next);
    lv_label_set_text(info_label, text);

    // Elevation: the number large, the direction small after it
    int32_t elevation = target->elevation_x10;
    dope_format_elevation(text, sizeof(text), elevation < 0 ? -elevation : elevation);
    lv_label_set_text(elevation_label, text);
    lv_label_set_text(unit_label, elevation > 0 ? "U" : elevation < 0 ? "D" : "");

    // Wind, and the gust after it when known (its side only when it differs from the wind's)
    dope_format_wind(text, sizeof(text), target->wind_x10);
    lv_label_set_text(wind_label, text);
    if (target->gust_x10 != DOPE_HOLD_NONE) {
        bool same_side = (target->gust_x10 >= 0) == (target->wind_x10 >= 0);
        char gust[12];
        dope_format_wind(gust, sizeof(gust), same_side ? (target->gust_x10 < 0 ? -target->gust_x10 : target->gust_x10)
                                                       : target->gust_x10);
        lv_label_set_text_fmt(gust_label, "/ %s", gust);
    }
    else {
        lv_label_set_text(gust_label, "");
    }

    lv_obj_remove_flag(value_row, LV_OBJ_FLAG_HIDDEN);
    update_visibility();
}


void level_band_refresh_dope(void) {
    if (current_target < 0 || !dope_target_is_set(&dope_card.target[current_target])) {
        current_target = find_set_target(0, 1);
    }
    show_target();
}


void level_band_next_target(void) {
    int next = find_set_target(current_target + 1, 1);
    if (next >= 0) {        // after the last target it stays there
        current_target = next;
        show_target();
    }
}


void level_band_previous_target(void) {
    int previous = find_set_target(current_target - 1, -1);
    if (previous >= 0) {
        current_target = previous;
        show_target();
    }
}


void level_band_show_timer(bool show) {
    timer_shown = show;
    if (band != NULL) {
        update_visibility();
        if (show) {
            level_band_update_timer(band_timer->countdown_time_ms);
        }
    }
}


bool level_band_timer_shown(void) {
    return timer_shown;
}


void level_band_update_timer(int time_left_ms) {
    if (band == NULL || !timer_shown) {
        return;
    }

    const int total_ms = band_timer->countdown_time_ms > 0 ? band_timer->countdown_time_ms : 1;
    if (time_left_ms > total_ms) {
        time_left_ms = total_ms;
    }
    const int seconds = (int) ceilf(time_left_ms / 1000.0f);
    lv_label_set_text_fmt(time_label, "%d:%02d", seconds / 60, seconds % 60);
    lv_bar_set_value(timer_bar, (int32_t) ((int64_t) time_left_ms * 1000 / total_ms), LV_ANIM_OFF);

    // Orange while armed or running, red in the last seconds and at the end, grey while paused part-way
    countdown_timer_state_t state = get_countdown_timer_state(band_timer);
    lv_color_t colour;
    if (time_left_ms <= 0 || state == COUNTDOWN_TIMER_EXPIRED) {
        colour = lv_palette_main(LV_PALETTE_RED);
    }
    else if (state == COUNTDOWN_TIMER_RUN) {
        colour = time_left_ms <= BAND_LAST_SECONDS_MS ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_ORANGE);
    }
    else if (time_left_ms >= total_ms) {
        colour = lv_palette_main(LV_PALETTE_ORANGE);    // armed
    }
    else {
        colour = lv_palette_main(LV_PALETTE_GREY);      // paused
    }
    lv_obj_set_style_bg_color(timer_bar, colour, LV_PART_INDICATOR);
}


static void band_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
        level_band_next_target();
    }
    else {
        level_band_previous_target();
    }
}


static lv_obj_t * create_row(lv_obj_t * parent) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return row;
}


static lv_obj_t * create_text(lv_obj_t * parent, lv_color_t colour) {
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
    lv_label_set_text(label, "");
    return label;
}


void level_band_create(lv_obj_t * overlay, countdown_timer_t * timer) {
    band_timer = timer;
    const lv_color_t light_grey = lv_palette_lighten(LV_PALETTE_GREY, 3);

    band = lv_obj_create(overlay);
    lv_obj_remove_style_all(band);  // transparent
    lv_obj_set_style_pad_left(band, BAND_PAD_SIDE_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(band, BAND_PAD_SIDE_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_top(band, BAND_PAD_TOP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(band, BAND_PAD_BOTTOM_PX, LV_PART_MAIN);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(band, band_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(band, band_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    // The timer bar: its length is the time left
    timer_bar = lv_bar_create(band);
    lv_obj_set_size(timer_bar, lv_pct(100), BAND_BAR_HEIGHT_PX);
    lv_bar_set_range(timer_bar, 0, 1000);
    lv_obj_set_style_radius(timer_bar, BAND_BAR_HEIGHT_PX / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(timer_bar, BAND_BAR_HEIGHT_PX / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(timer_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(timer_bar, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(timer_bar, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(timer_bar, BAND_TOUCH_EXTRA_PX);
    lv_obj_add_event_cb(timer_bar, countdown_timer_button_press_event_cb, LV_EVENT_SHORT_CLICKED, timer);
    lv_obj_add_event_cb(timer_bar, countdown_timer_button_press_event_cb, LV_EVENT_LONG_PRESSED, timer);

    // The target's name, its range and the next target; the time at the right
    header_row = create_row(band);
    lv_obj_set_width(header_row, lv_pct(100));
    lv_obj_set_style_pad_column(header_row, 10, LV_PART_MAIN);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    name_label = create_text(header_row, lv_palette_main(LV_PALETTE_ORANGE));
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_28, LV_PART_MAIN);
    info_label = create_text(header_row, light_grey);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_flex_grow(info_label, 1);
    time_label = create_text(header_row, lv_color_white());
    lv_obj_set_ext_click_area(time_label, BAND_TOUCH_EXTRA_PX);
    lv_obj_add_event_cb(time_label, countdown_timer_button_press_event_cb, LV_EVENT_SHORT_CLICKED, timer);
    lv_obj_add_event_cb(time_label, countdown_timer_button_press_event_cb, LV_EVENT_LONG_PRESSED, timer);
    lv_group_add_obj(button_input_group, time_label);  // the hardware button works the timer, as before

    // The holds: elevation and wind (one row in landscape, two in portrait)
    value_row = create_row(band);
    lv_obj_set_width(value_row, lv_pct(100));
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(value_row, 28, LV_PART_MAIN);

    elevation_group = create_row(value_row);
    lv_obj_set_style_pad_column(elevation_group, 6, LV_PART_MAIN);
    elevation_label = create_text(elevation_group, lv_color_white());
    unit_label = create_text(elevation_group, light_grey);
    lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_24, LV_PART_MAIN);

    wind_group = create_row(value_row);
    lv_obj_set_style_pad_column(wind_group, 8, LV_PART_MAIN);
    wind_label = create_text(wind_group, lv_color_white());
    gust_label = create_text(wind_group, light_grey);
    lv_obj_set_style_text_font(gust_label, &lv_font_montserrat_28, LV_PART_MAIN);

    level_band_set_rotation(system_config.rotation);
    level_band_refresh_dope();
    update_visibility();
}


void level_band_set_rotation(lv_display_rotation_t rotation) {
    if (band == NULL) {
        return;
    }
    const bool portrait = rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180;
    const int32_t width = portrait ? 280 : 456;
    const int32_t height = portrait ? 456 : 280;
    const int32_t top = portrait ? BAND_PORTRAIT_TOP_PX : BAND_LANDSCAPE_TOP_PX;

    lv_obj_set_pos(band, 0, top);
    lv_obj_set_size(band, width, height - top);

    if (portrait) {
        // Bar, name and time, elevation, wind: four rows
        lv_obj_set_style_pad_row(band, 10, LV_PART_MAIN);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, LV_PART_MAIN);
        lv_obj_set_style_text_font(elevation_label, &font_dope_digits_84, LV_PART_MAIN);
        lv_obj_set_style_text_font(wind_label, &lv_font_montserrat_48, LV_PART_MAIN);
        lv_obj_set_style_pad_row(value_row, 6, LV_PART_MAIN);
        lv_obj_add_flag(wind_group, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }
    else {
        // Bar, name and time, then elevation and wind side by side: three rows
        lv_obj_set_style_pad_row(band, 6, LV_PART_MAIN);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_28, LV_PART_MAIN);
        lv_obj_set_style_text_font(elevation_label, &font_dope_digits_64, LV_PART_MAIN);
        lv_obj_set_style_text_font(wind_label, &lv_font_montserrat_40, LV_PART_MAIN);
        lv_obj_remove_flag(wind_group, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }
}

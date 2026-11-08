#include <math.h>
#include <string.h>

#include "digital_level_view_type_3.h"
#include "digital_level_view.h"
#include "app_cfg.h"
#include "bno085.h"  // RAD_TO_DEG

#include "misc/cache/instance/lv_image_cache.h"  // lv_image_cache_drop (not in lvgl.h)

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

#define TAG "DigitalLevelViewType3"

// Level style 3, "Chevrons" (ADR-010, owner 2026-11-08). Within the level threshold, the whole view in the levelled
// colour, as style 1. Outside it, the whole view in chevrons of the leaning side's colour on black, like hazard tape
// (equal stripe and gap, 45 degrees), that point and move the way to turn the rifle (the top leaning right turns
// left). The speed shows the size of the cant. Full screen by the owner's choice, after trying a band between the
// overlays, 80% and 100% (ADR-010 progress).
//
// Drawing: one period of the chevrons is rendered once per direction into an RGB565 image in PSRAM (anti-aliased by
// supersampling), and an image widget tiles it across the band. Each frame only changes the image's x offset, which
// LVGL draws as a row-by-row memcpy (an opaque RGB565 image without a mask). Drawn as thick anti-aliased lines, the
// chevrons took about 18 ms a frame (LVGL's skewed-line masks) for a band of half the screen.
//
// The animation timer reads the newest angle itself every frame: while the view redraws, the level loop's update
// mostly cannot get the LVGL lock (it waits 1 ms), so updates through it reached the view only about 6 times a second.
// While level nothing moves and nothing redraws; while idle the timer is paused with all the others (ADR-009). Drawn
// behind the overlays (timer, dope), as the other styles are

#define CHEVRON_ANIMATION_PERIOD_MS 16          // the panel refreshes about every 16.9 ms; the display delivers what it can
#define CHEVRON_MAX_STEP_MS 100                 // a paused timer (idle) comes back with a long gap: do not jump
#define CHEVRON_PERIOD_PER_HEIGHT 0.6f          // one stripe plus one gap, in view heights (as hazard tape)
#define CHEVRON_SUPERSAMPLE 4                   // samples per axis when a tile is rendered
#define CHEVRON_SWEEPS_MIN 0.3f                 // periods a second, just past the threshold
#define CHEVRON_SWEEPS_MAX 3.0f                 // and at CHEVRON_FULL_SPEED_DEG past it: 3 a second at most (WCAG 2.3.1)
#define CHEVRON_FULL_SPEED_DEG 3.0f
#define CHEVRON_LEVEL_HYSTERESIS_DEG 0.1f       // leaving the level needs the threshold plus this: no flicker at the edge

typedef enum {
    CHEVRON_STATE_NONE = -1,                    // nothing shown yet
    CHEVRON_STATE_LEVEL = 0,
    CHEVRON_STATE_LEANS_LEFT,
    CHEVRON_STATE_LEANS_RIGHT,
} chevron_state_t;

// One period of the chevrons, pointing one way, in one colour
typedef struct {
    lv_image_dsc_t dsc;
    uint16_t * pixels;                          // PSRAM
} chevron_tile_t;

enum { TILE_POINTS_LEFT = 0, TILE_POINTS_RIGHT, TILE_COUNT };

static lv_obj_t * chevron_view = NULL;
static lv_obj_t * chevron_strip = NULL;
static lv_timer_t * chevron_timer = NULL;
static chevron_tile_t chevron_tiles[TILE_COUNT];
static int32_t chevron_tile_h = 0;              // the view height the tiles were rendered for; 0: none
static chevron_state_t chevron_state = CHEVRON_STATE_NONE;
static float chevron_offset_px = 0;             // the strip's x offset, 0 .. period
static uint32_t chevron_last_tick = 0;
static digital_level_view_config_t chevron_shown_config;

digital_level_view_t digital_level_view_type_3_context = {
    .constructor = create_digital_level_view_type_3,
    .destructor = delete_digital_level_view_type_3,
    .update_callback = update_digital_level_view_type_3,
};


static chevron_state_t chevron_next_state(chevron_state_t state, float roll_rad) {
    const float threshold_deg = digital_level_view_config.delta_level_threshold;
    const float roll_deg = (float) RAD_TO_DEG(roll_rad);
    const float leave_deg = state == CHEVRON_STATE_LEVEL ? threshold_deg + CHEVRON_LEVEL_HYSTERESIS_DEG : threshold_deg;
    if (roll_deg > leave_deg) {
        return CHEVRON_STATE_LEANS_RIGHT;
    }
    if (roll_deg < -leave_deg) {
        return CHEVRON_STATE_LEANS_LEFT;
    }
    return CHEVRON_STATE_LEVEL;
}


static void chevron_free_tiles(void) {
    for (int i = 0; i < TILE_COUNT; i++) {
        if (chevron_tiles[i].pixels != NULL) {
            lv_image_cache_drop(&chevron_tiles[i].dsc);
            heap_caps_free(chevron_tiles[i].pixels);
        }
    }
    memset(chevron_tiles, 0, sizeof(chevron_tiles));
    chevron_tile_h = 0;
}


// One period, w x h: a point is coloured where (x +- its distance from the centre row) mod w falls in the first half;
// "+" gives chevrons pointing right (">"), "-" pointing left. Each pixel's coverage comes from CHEVRON_SUPERSAMPLE^2
// sample points, in whole units of 1 / (2 * CHEVRON_SUPERSAMPLE) px with a running wrap instead of a division: a
// floating-point fmodf per sample took about a second for the two full-screen tiles, with the LVGL lock held
static bool chevron_render_tile(chevron_tile_t * tile, int32_t w, int32_t h, bool points_right, lv_color_t colour) {
    tile->pixels = heap_caps_malloc((size_t) w * h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (tile->pixels == NULL) {
        ESP_LOGE(TAG, "No memory for a %ldx%ld chevron tile", (long) w, (long) h);
        return false;
    }

    const int32_t unit = 2 * CHEVRON_SUPERSAMPLE;   // units a pixel
    const int32_t period = unit * w;
    const int32_t stripe = period / 2;
    const int32_t samples = CHEVRON_SUPERSAMPLE * CHEVRON_SUPERSAMPLE;
    for (int32_t y = 0; y < h; y++) {
        // Per sample row: where x = 0's first sample falls in the period (sample centres at odd units)
        int32_t start[CHEVRON_SUPERSAMPLE];
        for (int sy = 0; sy < CHEVRON_SUPERSAMPLE; sy++) {
            int32_t d = unit * y + 2 * sy + 1 - CHEVRON_SUPERSAMPLE * h;  // distance from the centre row
            d = d < 0 ? -d : d;
            int32_t u = (1 + (points_right ? d : -d)) % period;
            start[sy] = u < 0 ? u + period : u;
        }
        for (int32_t x = 0; x < w; x++) {
            int32_t covered = 0;
            for (int sy = 0; sy < CHEVRON_SUPERSAMPLE; sy++) {
                for (int sx = 0; sx < CHEVRON_SUPERSAMPLE; sx++) {
                    int32_t u = start[sy] + 2 * sx;
                    if (u >= period) {
                        u -= period;
                    }
                    covered += u < stripe;
                }
                start[sy] += unit;          // the next pixel
                if (start[sy] >= period) {
                    start[sy] -= period;
                }
            }
            const lv_color_t px = lv_color_mix(colour, lv_color_black(), (uint8_t) (covered * 255 / samples));
            tile->pixels[y * w + x] = lv_color_to_u16(px);
        }
    }

    memset(&tile->dsc, 0, sizeof(tile->dsc));
    tile->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    tile->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    tile->dsc.header.w = w;
    tile->dsc.header.h = h;
    tile->dsc.header.stride = w * sizeof(uint16_t);
    tile->dsc.data_size = (uint32_t) w * h * sizeof(uint16_t);
    tile->dsc.data = (const uint8_t *) tile->pixels;
    return true;
}


// Leaning right turns left: its chevrons point left, in the right tilt colour
static chevron_tile_t * chevron_tile_for(chevron_state_t state) {
    return &chevron_tiles[state == CHEVRON_STATE_LEANS_RIGHT ? TILE_POINTS_LEFT : TILE_POINTS_RIGHT];
}


// Render the tiles for the view's present size and colours; false while the view has no size yet or without memory
static bool chevron_build_tiles(int32_t view_h) {
    lv_image_set_src(chevron_strip, NULL);
    chevron_free_tiles();
    if (view_h < 8) {
        return false;
    }
    const int32_t period = (int32_t) (view_h * CHEVRON_PERIOD_PER_HEIGHT);
    const lv_color_t right_tilt = lv_palette_main(digital_level_view_config.colour_right_tilt_indicator);
    const lv_color_t left_tilt = lv_palette_main(digital_level_view_config.colour_left_tilt_indicator);
    const int64_t start_us = esp_timer_get_time();
    if (!chevron_render_tile(&chevron_tiles[TILE_POINTS_LEFT], period, view_h, false, right_tilt) ||
        !chevron_render_tile(&chevron_tiles[TILE_POINTS_RIGHT], period, view_h, true, left_tilt)) {
        chevron_free_tiles();
        return false;
    }
    chevron_tile_h = view_h;
    chevron_offset_px = 0;
    ESP_LOGI(TAG, "Chevron tiles %ldx%ld rendered in %ld ms", (long) period, (long) view_h,
             (long) ((esp_timer_get_time() - start_us) / 1000));
    return true;
}


// Show a state: the levelled colour over the whole view, or black with the strip pointing the way to turn
static void chevron_show_state(chevron_state_t state) {
    chevron_state = state;
    if (state == CHEVRON_STATE_LEVEL) {
        lv_obj_set_style_bg_color(chevron_view,
                                  lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator),
                                  LV_PART_MAIN);
        lv_obj_set_hidden(chevron_strip, true);
        return;
    }
    lv_obj_set_style_bg_color(chevron_view, lv_color_black(), LV_PART_MAIN);
    lv_image_set_src(chevron_strip, &chevron_tile_for(state)->dsc);
    lv_image_set_offset_x(chevron_strip, (int32_t) chevron_offset_px);
    lv_obj_set_hidden(chevron_strip, false);
}


static void chevron_timer_cb(lv_timer_t * timer) {
    const uint32_t now = lv_tick_get();
    uint32_t elapsed_ms = lv_tick_diff(now, chevron_last_tick);
    chevron_last_tick = now;
    if (elapsed_ms > CHEVRON_MAX_STEP_MS) {
        elapsed_ms = CHEVRON_ANIMATION_PERIOD_MS;
    }
    if (chevron_view == NULL) {
        return;
    }

    // New size (rotation) or new colours: render the tiles again and show the state afresh
    const int32_t view_h = lv_obj_get_height(chevron_view);
    const bool config_changed = memcmp(&chevron_shown_config, &digital_level_view_config,
                                       sizeof(chevron_shown_config)) != 0;
    if (view_h != chevron_tile_h || config_changed) {
        chevron_shown_config = digital_level_view_config;
        chevron_state = CHEVRON_STATE_NONE;
        if (!chevron_build_tiles(view_h)) {
            return;
        }
    }

    // The newest angle, as the level loop last read it (a float written whole by one task)
    const float roll_rad = get_relative_roll_angle_rad_thread_unsafe();
    update_roll_deg_indicator(roll_rad);  // the pill's angle, which the level loop's update also mostly misses
    const chevron_state_t state = chevron_next_state(chevron_state, roll_rad);
    if (state != chevron_state) {
        chevron_show_state(state);
    }
    if (state == CHEVRON_STATE_LEVEL) {
        return;  // still: nothing to redraw
    }

    // Faster with more cant: CHEVRON_SWEEPS_MIN at the threshold, CHEVRON_SWEEPS_MAX at CHEVRON_FULL_SPEED_DEG past it
    const float past_deg = fabsf((float) RAD_TO_DEG(roll_rad)) - digital_level_view_config.delta_level_threshold;
    float share = past_deg / CHEVRON_FULL_SPEED_DEG;
    share = share < 0 ? 0 : (share > 1 ? 1 : share);
    const float sweeps = CHEVRON_SWEEPS_MIN + (CHEVRON_SWEEPS_MAX - CHEVRON_SWEEPS_MIN) * share;
    const float period = (float) chevron_tile_for(state)->dsc.header.w;
    const float dir = state == CHEVRON_STATE_LEANS_RIGHT ? -1.0f : 1.0f;  // the way to turn
    chevron_offset_px += dir * sweeps * period * elapsed_ms / 1000.0f;
    chevron_offset_px = fmodf(chevron_offset_px, period);
    if (chevron_offset_px < 0) {
        chevron_offset_px += period;
    }
    if ((int32_t) chevron_offset_px != lv_image_get_offset_x(chevron_strip)) {
        lv_image_set_offset_x(chevron_strip, (int32_t) chevron_offset_px);  // redraws the band only
    }
}


lv_obj_t * create_digital_level_view_type_3(lv_obj_t *parent) {
    chevron_view = lv_obj_create(parent);
    digital_level_view_type_3_context.container = chevron_view;
    lv_obj_remove_style_all(chevron_view);
    lv_obj_set_size(chevron_view, lv_pct(100), lv_pct(100));
    lv_obj_center(chevron_view);
    lv_obj_set_scrollable(chevron_view, false);
    lv_obj_set_style_bg_opa(chevron_view, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chevron_view, lv_color_black(), LV_PART_MAIN);

    chevron_strip = lv_image_create(chevron_view);
    lv_obj_set_size(chevron_strip, lv_pct(100), lv_pct(100));
    lv_image_set_inner_align(chevron_strip, LV_IMAGE_ALIGN_TILE);
    lv_obj_set_hidden(chevron_strip, true);

    memset(chevron_tiles, 0, sizeof(chevron_tiles));
    chevron_tile_h = 0;
    chevron_state = CHEVRON_STATE_NONE;
    chevron_offset_px = 0;
    chevron_shown_config = digital_level_view_config;
    chevron_last_tick = lv_tick_get();
    chevron_timer = lv_timer_create(chevron_timer_cb, CHEVRON_ANIMATION_PERIOD_MS, NULL);

    return chevron_view;
}


void delete_digital_level_view_type_3(lv_obj_t *container) {
    if (chevron_timer != NULL) {
        lv_timer_delete(chevron_timer);
        chevron_timer = NULL;
    }
    lv_obj_delete(container);  // the strip with it
    chevron_view = NULL;
    chevron_strip = NULL;
    chevron_free_tiles();
}


// The level loop, with the LVGL lock held. Nothing to do: the animation timer reads the newest angle every frame
void update_digital_level_view_type_3(float roll_rad, float pitch_rad) {
    (void) roll_rad;
    (void) pitch_rad;
}

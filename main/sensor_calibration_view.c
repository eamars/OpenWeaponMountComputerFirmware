#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"

#include "app_cfg.h"
#include "sensor_calibration_view.h"
#include "sensor_frame.h"
#include "low_power_mode.h"
#include "bno085.h"
#include "buzzer.h"
#include "system_config.h"
#include "digital_level_view.h"
#include "value_editor.h"
#include "display_shape.h"
#include "app.h"
#include "main_tileview.h"


#define TAG "SensorCalibration"

// The factory calibration routine (ADR-011), entered where the Tare page was. The unit stands on its rotation 0 edge
// on a flat, firm surface: placement A, then turned around 180 degrees about the vertical on the same edge (B), then
// back (C). A and B see the surface's tilt with opposite signs in the unit's frame, so their mean is the unit's own
// "up" and half their angle the surface's tilt; C against A is the repeat. Each capture waits until the unit is still
// and settled, and averages 2 s of gravity directions: after a placement the game rotation vector's tilt keeps
// creeping for seconds while its fusion settles (about 0.25 degrees, owner's first runs), so a window is taken only
// when its first and last second agree. The turns are seen from the game rotation vector's heading. Beeps guide the
// person while the screen faces away. The screen is turned to rotation 0 for the routine and back afterwards.

#define CAL_TIMER_MS 50                 // one sample a tick (the report comes every 20 ms)
#define CAL_WINDOW 40                   // samples per capture: 2 s
#define CAL_STILL_DEG 0.10f             // every sample of a capture within this of their mean: no movement
#define CAL_SETTLED_DEG 0.02f           // the window's first and last second agree within this: settled
#define CAL_ZONE_DEG 5.0f               // B within this of 180 degrees from A, C within this of A: the surface's tilt
                                        // cancels exactly at 180 (5 degrees off leaves 9% of it)
#define CAL_HEADING_STEADY_DEG 1.0f     // the heading's spread over CAL_HEADING_SAMPLES before a capture may start
#define CAL_HEADING_SAMPLES 20          // 1 s
#define CAL_REPEAT_PASS_DEG 0.10f       // C against A for a pass (owner may change)
#define DEGREE_SIGN "\xC2\xB0"
#define CAL_SCREEN_W 280                // the routine runs in rotation 0
#define CAL_SCREEN_H 456
#define CAL_BUTTON_H 96
#define CAL_GAP 20
#define CAL_EDGE_BAR_H 10

typedef enum {
    CAL_INTRO = 0,
    CAL_PLACE_A,
    CAL_TURN,
    CAL_BACK,
    CAL_RESULT,
} cal_step_t;

lv_obj_t * last_tile_before_enter_calibration = NULL;

static lv_obj_t * cal_screen = NULL;
static lv_obj_t * title_label = NULL;
static lv_obj_t * body_label = NULL;
static lv_obj_t * ring = NULL;
static lv_obj_t * ring_label = NULL;
static lv_obj_t * edge_bar = NULL;
static lv_obj_t * primary_button = NULL;
static lv_obj_t * primary_label = NULL;
static lv_obj_t * secondary_button = NULL;
static lv_obj_t * secondary_label = NULL;
static lv_timer_t * cal_timer = NULL;

static cal_step_t cal_step = CAL_INTRO;
static float window[CAL_WINDOW][3];
static int window_count = 0;
static uint32_t last_report_count = 0;
static float up_a[3];
static float up_b[3];
static float up_c[3];
static float yaw_a_deg = 0;
static float heading_history[CAL_HEADING_SAMPLES];  // degrees to go, the newest last
static int heading_count = 0;
static bool capture_started = false;                // a capture of this placement has begun (for "It moved")
static sensor_frame_config_t result;
static bool result_pass = false;
static lv_display_rotation_t rotation_before = LV_DISPLAY_ROTATION_0;


static float yaw_deg_from_quaternion(const float q[4]) {
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    return atan2f(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z)) * 180.0f / (float) M_PI;
}


static float wrap_deg(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}


static void window_mean(float mean[3]) {
    mean[0] = mean[1] = mean[2] = 0;
    for (int i = 0; i < window_count; i++) {
        mean[0] += window[i][0];
        mean[1] += window[i][1];
        mean[2] += window[i][2];
    }
    const float n = sqrtf(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]);
    if (n > 0) {
        mean[0] /= n;
        mean[1] /= n;
        mean[2] /= n;
    }
}


// The mean direction of window[from, to)
static void window_mean_of(int from, int to, float mean[3]) {
    mean[0] = mean[1] = mean[2] = 0;
    for (int i = from; i < to; i++) {
        mean[0] += window[i][0];
        mean[1] += window[i][1];
        mean[2] += window[i][2];
    }
    const float n = sqrtf(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]);
    if (n > 0) {
        mean[0] /= n;
        mean[1] /= n;
        mean[2] /= n;
    }
}


// Add a sample to the sliding window; true with the capture in `captured` once the last CAL_WINDOW samples are still
// (each within CAL_STILL_DEG of their mean; a sample off it starts again) and settled (the means of the window's two
// halves within CAL_SETTLED_DEG). While full but still creeping, the window slides on
static bool window_settling = false;

static bool window_add(const float up[3], float captured[3]) {
    window_settling = false;
    if (window_count > 0) {
        float mean[3];
        window_mean(mean);
        if (sensor_frame_angle_deg(up, mean) > CAL_STILL_DEG) {
            window_count = 0;
        }
    }
    if (window_count == CAL_WINDOW) {
        memmove(window[0], window[1], sizeof(window[0]) * (CAL_WINDOW - 1));
        window_count--;
    }
    memcpy(window[window_count], up, sizeof(window[0]));
    window_count++;
    if (window_count < CAL_WINDOW) {
        return false;
    }
    float first[3];
    float last[3];
    window_mean_of(0, CAL_WINDOW / 2, first);
    window_mean_of(CAL_WINDOW / 2, CAL_WINDOW, last);
    if (sensor_frame_angle_deg(first, last) > CAL_SETTLED_DEG) {
        window_settling = true;
        return false;
    }
    window_mean(captured);
    window_count = 0;
    return true;
}


static void beep(uint8_t count) {
    buzzer_run(80, 120, count, false);
}


static void set_buttons(const char * primary, const char * secondary) {
    lv_obj_set_hidden(primary_button, primary == NULL);
    if (primary != NULL) {
        lv_label_set_text(primary_label, primary);
    }
    lv_obj_set_hidden(secondary_button, secondary == NULL);
    if (secondary != NULL) {
        lv_label_set_text(secondary_label, secondary);
    }
}


static void show_ring(bool show) {
    lv_obj_set_hidden(ring, !show);
    lv_obj_set_hidden(edge_bar, !show);
    lv_arc_set_value(ring, 0);
    lv_label_set_text(ring_label, "");
}


static void show_step(cal_step_t step) {
    cal_step = step;
    window_count = 0;
    heading_count = 0;
    capture_started = false;
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);

    switch (step) {
    case CAL_INTRO: {
        const sensor_frame_config_t now = sensor_frame_get();
        char status[96];
        if (now.calibrated) {
            snprintf(status, sizeof(status), "About a minute, on a flat, firm surface.\n\nCalibrated, offset %.1f\xC2\xB0",
                     (double) now.offset_deg);
        }
        else {
            snprintf(status, sizeof(status), "About a minute, on a flat, firm surface.\n\nNot calibrated");
        }
        lv_label_set_text(title_label, "Sensor calibration");
        lv_label_set_text(body_label, status);
        show_ring(false);
        set_buttons("Start", "Cancel");
        break;
    }
    case CAL_PLACE_A:
        lv_label_set_text(title_label, "Stand it on this edge");
        lv_label_set_text(body_label, "Screen towards you. Hands off until it beeps.");
        show_ring(true);
        set_buttons(NULL, "Cancel");
        break;
    case CAL_TURN:
        lv_label_set_text(title_label, "Turn it around");
        lv_label_set_text(body_label, "Same edge down, same spot, until the ring shows 0\xC2\xB0. Two beeps when done.");
        show_ring(true);
        set_buttons(NULL, "Cancel");
        break;
    case CAL_BACK:
        lv_label_set_text(title_label, "Turn it back");
        lv_label_set_text(body_label, "Screen towards you, until the ring shows 0\xC2\xB0. Hands off until it beeps.");
        show_ring(true);
        set_buttons(NULL, "Cancel");
        break;
    case CAL_RESULT: {
        char text[96];
        snprintf(text, sizeof(text), "Offset %.1f\xC2\xB0\nSurface %.1f\xC2\xB0\nRepeat %.2f\xC2\xB0\n%s",
                 (double) result.offset_deg, (double) result.surface_deg, (double) result.repeat_deg,
                 result_pass ? "Saving also clears the rifle zero." : "Keep it still and try again.");
        lv_label_set_text(title_label, result_pass ? "Pass" : "Repeat too large");
        lv_obj_set_style_text_color(title_label, lv_palette_main(result_pass ? LV_PALETTE_LIGHT_GREEN :
                                                                               LV_PALETTE_DEEP_ORANGE), LV_PART_MAIN);
        lv_label_set_text(body_label, text);
        show_ring(false);
        set_buttons(result_pass ? "Save" : "Redo", result_pass ? "Redo" : "Cancel");
        break;
    }
    }
}


static void compute_result(void) {
    float r[3] = {up_a[0] + up_b[0], up_a[1] + up_b[1], up_a[2] + up_b[2]};
    const float n = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int i = 0; i < 3; i++) {
        r[i] /= n;
    }
    memcpy(result.up, r, sizeof(r));
    result.offset_deg = atan2f(r[0], r[1]) * 180.0f / (float) M_PI;
    result.surface_deg = sensor_frame_angle_deg(up_a, up_b) / 2.0f;
    // The repeat is judged on the cant (the angle in the screen's plane, which the level shows), not the whole angle:
    // most of A against C is the unit rocking on its edge out of that plane (owner's runs: 0.20 in all, 0.006 in cant)
    const float cant_a = atan2f(up_a[0], up_a[1]) * 180.0f / (float) M_PI;
    const float cant_c = atan2f(up_c[0], up_c[1]) * 180.0f / (float) M_PI;
    result.repeat_deg = fabsf(cant_a - cant_c);
    result.calibrated = true;
    result_pass = result.repeat_deg <= CAL_REPEAT_PASS_DEG;
    ESP_LOGI(TAG, "A=%.4f,%.4f,%.4f B=%.4f,%.4f,%.4f C=%.4f,%.4f,%.4f offset=%.2f surface=%.2f repeat=%.3f "
             "(whole %.3f) %s", (double) up_a[0], (double) up_a[1], (double) up_a[2], (double) up_b[0],
             (double) up_b[1], (double) up_b[2], (double) up_c[0], (double) up_c[1], (double) up_c[2],
             (double) result.offset_deg, (double) result.surface_deg, (double) result.repeat_deg,
             (double) sensor_frame_angle_deg(up_a, up_c), result_pass ? "pass" : "fail");
}


// The capture's ring (owner): orange, filling while the unit is still and full while the reading settles; the
// degrees to go stay in it, a reminder not to move it (none for placement A, which sets the reference)
static void show_capture_ring(float to_go_deg) {
    lv_obj_set_style_arc_color(ring, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    lv_arc_set_value(ring, window_count * 100 / CAL_WINDOW);
    if (to_go_deg < 0) {
        lv_label_set_text(ring_label, "");
    }
    else {
        lv_label_set_text_fmt(ring_label, "%d%s", (int) lroundf(to_go_deg), DEGREE_SIGN);
    }
}


static void cal_timer_cb(lv_timer_t * timer) {
    float q[4];
    const uint32_t count = bno085_get_game_rotation_quaternion(bno085_dev, q);
    if (count == last_report_count) {
        return;  // no new report
    }
    last_report_count = count;

    float up[3];
    sensor_frame_up_from_quaternion(q, up);
    const float turned_deg = fabsf(wrap_deg(yaw_deg_from_quaternion(q) - yaw_a_deg));

    // Once a second while waiting: how far it has turned, and how far this sample is from the capture's mean so far
    static uint32_t log_tick = 0;
    if (cal_step != CAL_INTRO && cal_step != CAL_RESULT && ++log_tick % (1000 / CAL_TIMER_MS) == 0) {
        float mean[3] = {up[0], up[1], up[2]};
        if (window_count > 0) {
            window_mean(mean);
        }
        ESP_LOGD(TAG, "step=%d turned=%.1f samples=%d settling=%d off_mean=%.3f up=%.4f,%.4f,%.4f", (int) cal_step,
                 (double) turned_deg, window_count, (int) window_settling, (double) sensor_frame_angle_deg(up, mean),
                 (double) up[0], (double) up[1], (double) up[2]);
    }

    // Placement A sets the heading the turns are counted from; B and C wait for their position first (owner): the
    // heading within CAL_ZONE_DEG of its target and steady, then the still-and-settled capture. Moved away during a
    // capture: the capture restarts and the person is asked to put it back
    if (cal_step == CAL_PLACE_A) {
        if (window_count > 0) {
            capture_started = true;
        }
        if (window_add(up, up_a)) {
            yaw_a_deg = yaw_deg_from_quaternion(q);
            beep(1);
            show_step(CAL_TURN);
            return;
        }
        show_capture_ring(-1.0f);
        return;
    }
    if (cal_step != CAL_TURN && cal_step != CAL_BACK) {
        return;
    }

    const float to_go_deg = cal_step == CAL_TURN ? 180.0f - turned_deg : turned_deg;
    if (heading_count == CAL_HEADING_SAMPLES) {
        memmove(heading_history, heading_history + 1, sizeof(heading_history[0]) * (CAL_HEADING_SAMPLES - 1));
        heading_count--;
    }
    heading_history[heading_count++] = to_go_deg;
    float low = heading_history[0];
    float high = heading_history[0];
    for (int i = 1; i < heading_count; i++) {
        low = heading_history[i] < low ? heading_history[i] : low;
        high = heading_history[i] > high ? heading_history[i] : high;
    }
    const bool in_position = to_go_deg <= CAL_ZONE_DEG && heading_count == CAL_HEADING_SAMPLES &&
                             high - low <= CAL_HEADING_STEADY_DEG;

    if (!in_position) {
        if (window_count > 0 || window_settling) {
            capture_started = true;  // it was capturing here and moved away
        }
        window_count = 0;
        window_settling = false;
        if (capture_started) {
            lv_label_set_text(body_label, "It moved. Put it back to 0\xC2\xB0, hands off.");
        }
        // The turn gauge: fuller the nearer the position, with the degrees still to go
        lv_obj_set_style_arc_color(ring, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_INDICATOR);
        lv_arc_set_value(ring, (int32_t) ((180.0f - to_go_deg) * 100.0f / 180.0f));
        lv_label_set_text_fmt(ring_label, "%d\xC2\xB0", (int) lroundf(to_go_deg));
        return;
    }

    if (window_add(up, cal_step == CAL_TURN ? up_b : up_c)) {
        if (cal_step == CAL_TURN) {
            beep(2);
            show_step(CAL_BACK);
        }
        else {
            compute_result();
            beep(result_pass ? 1 : 3);
            show_step(CAL_RESULT);
        }
        return;
    }
    show_capture_ring(to_go_deg);
}


static void leave_calibration(void) {
    lv_obj_t * back = last_tile_before_enter_calibration != NULL ? last_tile_before_enter_calibration : default_tile;
    lv_tileview_set_tile(main_tileview, back, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
}


static void primary_clicked_cb(lv_event_t * e) {
    if (cal_step == CAL_INTRO) {
        show_step(CAL_PLACE_A);
    }
    else if (cal_step == CAL_RESULT && result_pass) {
        esp_err_t ret = sensor_frame_save(&result);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save the calibration: %s", esp_err_to_name(ret));
        }
        digital_level_view_clear_zero();
        leave_calibration();
    }
    else if (cal_step == CAL_RESULT) {
        show_step(CAL_PLACE_A);  // Redo
    }
}


static void secondary_clicked_cb(lv_event_t * e) {
    if (cal_step == CAL_RESULT && result_pass) {
        show_step(CAL_PLACE_A);  // Redo
        return;
    }
    leave_calibration();  // Cancel: the old calibration stays
}


void enter_sensor_calibration_mode(bool enable) {
    ESP_LOGI(TAG, "Sensor calibration %s", enable ? "entered" : "left");

    if (enable) {
        prevent_idle_mode_enter(true);
        rotation_before = system_config.rotation;
        system_config_set_rotation(LV_DISPLAY_ROTATION_0);  // RAM only: the unit stands on the rotation 0 edge
        bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS);
        last_report_count = 0;
        show_step(CAL_INTRO);
        if (cal_timer == NULL) {
            cal_timer = lv_timer_create(cal_timer_cb, CAL_TIMER_MS, NULL);
        }
    }
    else {
        if (cal_timer != NULL) {
            lv_timer_delete(cal_timer);
            cal_timer = NULL;
        }
        // The level switches the report on again when it is the next tile
        bno085_enable_game_rotation_vector_report(bno085_dev, 0);
        system_config_set_rotation(rotation_before);
        prevent_idle_mode_enter(false);
    }
}


static lv_obj_t * create_button(lv_obj_t * parent, lv_color_t colour, lv_obj_t ** label, lv_event_cb_t cb) {
    lv_obj_t * button = value_editor_create_button(parent, LV_PALETTE_GREY);
    lv_obj_set_size(button, CAL_SCREEN_W - 2 * DISPLAY_MARGIN_PX, CAL_BUTTON_H);
    lv_obj_set_style_bg_color(button, colour, LV_PART_MAIN);
    *label = value_editor_create_label(button, &lv_font_montserrat_32);
    lv_obj_add_event_cb(button, cb, LV_EVENT_SHORT_CLICKED, NULL);
    return button;
}


void create_sensor_calibration_view(lv_obj_t * parent) {
    cal_screen = lv_obj_create(parent);
    lv_obj_remove_style_all(cal_screen);
    lv_obj_set_size(cal_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(cal_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cal_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(cal_screen, false);

    const int32_t inner_w = CAL_SCREEN_W - 2 * 20;

    title_label = lv_label_create(cal_screen);
    lv_obj_set_width(title_label, inner_w);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_pos(title_label, 20, 24);

    body_label = lv_label_create(cal_screen);
    lv_obj_set_width(body_label, inner_w);
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(body_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(body_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_obj_set_pos(body_label, 20, 100);

    ring = lv_arc_create(cal_screen);
    lv_obj_set_size(ring, 110, 110);
    lv_obj_set_pos(ring, (CAL_SCREEN_W - 110) / 2, 220);
    lv_arc_set_range(ring, 0, 100);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_rotation(ring, 270);
    lv_obj_set_clickable(ring, false);
    lv_obj_set_style_arc_width(ring, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
    ring_label = lv_label_create(ring);
    lv_obj_set_style_text_font(ring_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(ring_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(ring_label);

    // The edge that goes down: the bottom edge in rotation 0
    edge_bar = lv_obj_create(cal_screen);
    lv_obj_remove_style_all(edge_bar);
    lv_obj_set_size(edge_bar, CAL_SCREEN_W - 60, CAL_EDGE_BAR_H);
    lv_obj_set_pos(edge_bar, 30, CAL_SCREEN_H - CAL_EDGE_BAR_H);
    lv_obj_set_style_bg_color(edge_bar, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(edge_bar, LV_OPA_COVER, LV_PART_MAIN);

    primary_button = create_button(cal_screen, lv_palette_darken(LV_PALETTE_ORANGE, 2), &primary_label,
                                   primary_clicked_cb);
    lv_obj_set_pos(primary_button, DISPLAY_MARGIN_PX,
                   CAL_SCREEN_H - CAL_EDGE_BAR_H - DISPLAY_MARGIN_PX - 2 * CAL_BUTTON_H - CAL_GAP);
    secondary_button = create_button(cal_screen, lv_palette_darken(LV_PALETTE_GREY, 3), &secondary_label,
                                     secondary_clicked_cb);
    lv_obj_set_pos(secondary_button, DISPLAY_MARGIN_PX, CAL_SCREEN_H - CAL_EDGE_BAR_H - DISPLAY_MARGIN_PX - CAL_BUTTON_H);

    show_step(CAL_INTRO);
}

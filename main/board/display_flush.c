#include "display_flush.h"
#include "app_cfg.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#define TAG "DisplayFlush"

#define DISPLAY_FLUSH_DRAW_BUF_BYTES (DISP_H_RES_PIXEL * DISP_V_RES_PIXEL * sizeof(uint16_t))

// Longest wait for the TE falling edge before a frame is sent anyway: a little over two panel refreshes (~16.7 ms)
#define DISPLAY_FLUSH_TE_TIMEOUT_MS 40

// Panel scan model (ADR-002 steps 2 and 3): the TE falling edge is where the panel starts scanning at physical row 0,
// and the scan covers all DISP_V_RES_PIXEL rows within one TE period. Row time is taken as period / rows, which can
// only overstate it. Until the first period is measured, the longest measured period is used.
#define DISPLAY_FLUSH_TE_PERIOD_DEFAULT_US 16900
// Longest rise-to-rise time taken as the panel's refresh period. A longer one is a gap in the pulses (the panel asleep
// while idle, ADR-009), not a period: taken as one, it once made the flush wait seconds for its scan position
#define DISPLAY_FLUSH_TE_PERIOD_MAX_US (DISPLAY_FLUSH_TE_PERIOD_DEFAULT_US * 3 / 2)
// Write time per pixel in ns: bounds around the measured 159-183 ns/px (full frames, all four rotations)
#define DISPLAY_FLUSH_WRITE_NS_PER_PX_MIN 150
#define DISPLAY_FLUSH_WRITE_NS_PER_PX_MAX 190
// Kept free inside the safe start window for the model's timing error
#define DISPLAY_FLUSH_START_MARGIN_US 1000
// A late start waits one more pass only when its safe window is at least this long
#define DISPLAY_FLUSH_RETRY_MIN_WINDOW_US 2000

// Two rows of the widest possible window must fit, so a strip always holds an even number of rows (at least 2)
_Static_assert(DISPLAY_FLUSH_STAGING_BYTES >= 2 * DISP_V_RES_PIXEL * sizeof(uint16_t),
               "staging buffer must hold at least two rows of the widest window");


typedef struct {
    esp_lcd_panel_handle_t panel_handle;
    uint16_t *staging[2];

    // Protected by display_flush_strip_lock (written by the LVGL task and the SPI ISR)
    uint32_t strips_total;
    uint32_t strips_done;
    int64_t frame_start_us;
} display_flush_ctx_t;

// Last TE edges: written by the TE ISR, protected by display_flush_timing_lock, never reset
typedef struct {
    int64_t last_rise_us;
    int64_t last_fall_us;
    uint32_t period_us;          // last rising-to-rising interval, 0 until measured
} display_flush_te_state_t;

// Window totals behind display_flush_take_timing(); protected by display_flush_timing_lock
typedef struct {
    uint32_t te_pulses;
    uint32_t te_period_min_us;
    uint32_t te_period_max_us;
    uint64_t te_high_us_total;
    uint32_t te_high_count;
    uint32_t frames;
    uint64_t write_us_total;
    uint32_t write_max_us;
    uint32_t te_waits;
    uint64_t te_wait_us_total;
    uint32_t te_wait_max_us;
    uint32_t te_timeouts;
    uint32_t te_retries;
    uint32_t te_late;
} display_flush_timing_acc_t;

static display_flush_ctx_t display_flush_ctx;

// Anti-tearing (ADR-002): each write starts where the panel's scan cannot cross it. Off (ADR-010 step 2, RAM only,
// for the owner's trial): writes start at once, for more frames a second and a possible tear line
static volatile bool display_flush_tear_free = true;


void display_flush_set_tear_free(bool on) {
    display_flush_tear_free = on;
}


bool display_flush_get_tear_free(void) {
    return display_flush_tear_free;
}
static portMUX_TYPE display_flush_strip_lock = portMUX_INITIALIZER_UNLOCKED;
static display_flush_timing_acc_t display_flush_timing_acc;
static display_flush_te_state_t display_flush_te_state;
static portMUX_TYPE display_flush_timing_lock = portMUX_INITIALIZER_UNLOCKED;

// Given on each TE falling edge (end of V-blank: the panel starts scanning from row 0)
static StaticSemaphore_t display_flush_te_sem_buf;
static SemaphoreHandle_t display_flush_te_sem = NULL;

// One-shot timer that wakes the flush at the planned start time
static StaticSemaphore_t display_flush_start_sem_buf;
static SemaphoreHandle_t display_flush_start_sem = NULL;
static esp_timer_handle_t display_flush_start_timer = NULL;


static inline uint16_t display_flush_swap(uint16_t px) {
    return (uint16_t) ((px >> 8) | (px << 8));
}


// This function is required by some LVGL display drivers to align the pixel
static void IRAM_ATTR display_flush_rounder(lv_area_t * area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of area down to the nearest even number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;

    // round the end of area up to the nearest odd number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}


static void display_flush_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_INVALIDATE_AREA) {
        lv_area_t *area = (lv_area_t *) lv_event_get_param(e);
        if (area != NULL) {
            display_flush_rounder(area);
        }
    }

    // Wake LVGL task, if needed
    lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, NULL);
}


// Strip fills. `src` is the LVGL area (w x h, stride `src_stride` pixels); `dst` is one staging buffer holding
// window rows [r0, r1) of width `pw`. Every pixel is byte-swapped on the way.

// 0 degrees: dx = sx, dy = sy (pw == w)
static void display_flush_fill_0(const uint16_t *src, int32_t src_stride, int32_t w,
                                 uint16_t *dst, int32_t r0, int32_t r1) {
    for (int32_t sy = r0; sy < r1; sy++) {
        const uint16_t *s = src + sy * src_stride;
        for (int32_t sx = 0; sx < w; sx++) {
            *dst++ = display_flush_swap(s[sx]);
        }
    }
}

// 180 degrees: dx = w - 1 - sx, dy = h - 1 - sy (pw == w)
static void display_flush_fill_180(const uint16_t *src, int32_t src_stride, int32_t w, int32_t h,
                                   uint16_t *dst, int32_t r0, int32_t r1) {
    for (int32_t dy = r0; dy < r1; dy++) {
        const uint16_t *s = src + (h - 1 - dy) * src_stride + (w - 1);
        for (int32_t dx = 0; dx < w; dx++) {
            *dst++ = display_flush_swap(s[-dx]);
        }
    }
}

// 90 degrees: dx = sy, dy = w - 1 - sx (pw == h). Reads each source row once over columns
// [w - r1, w - 1 - r0] and writes down one staging column.
static void display_flush_fill_90(const uint16_t *src, int32_t src_stride, int32_t w, int32_t h,
                                  uint16_t *dst, int32_t pw, int32_t r0, int32_t r1) {
    const int32_t n = r1 - r0;
    const int32_t last_row_offset = (n - 1) * pw;

    for (int32_t sy = 0; sy < h; sy++) {
        const uint16_t *s = src + sy * src_stride + (w - r1);
        int32_t o = last_row_offset + sy;
        for (int32_t i = 0; i < n; i++) {
            dst[o] = display_flush_swap(s[i]);
            o -= pw;
        }
    }
}

// 270 degrees: dx = h - 1 - sy, dy = sx (pw == h). Reads each source row once over columns
// [r0, r1 - 1] and writes down one staging column.
static void display_flush_fill_270(const uint16_t *src, int32_t src_stride, int32_t h,
                                   uint16_t *dst, int32_t pw, int32_t r0, int32_t r1) {
    const int32_t n = r1 - r0;

    for (int32_t sy = 0; sy < h; sy++) {
        const uint16_t *s = src + sy * src_stride + r0;
        int32_t o = h - 1 - sy;
        for (int32_t i = 0; i < n; i++) {
            dst[o] = display_flush_swap(s[i]);
            o += pw;
        }
    }
}


// Fill staging rows [r0, r1) of the physical window for the given rotation
static void display_flush_fill_strip(const uint16_t *src, int32_t src_stride, int32_t w, int32_t h,
                                     lv_display_rotation_t rotation, uint16_t *dst, int32_t pw,
                                     int32_t r0, int32_t r1) {
    switch (rotation) {
        case LV_DISPLAY_ROTATION_90:
            display_flush_fill_90(src, src_stride, w, h, dst, pw, r0, r1);
            break;
        case LV_DISPLAY_ROTATION_180:
            display_flush_fill_180(src, src_stride, w, h, dst, r0, r1);
            break;
        case LV_DISPLAY_ROTATION_270:
            display_flush_fill_270(src, src_stride, h, dst, pw, r0, r1);
            break;
        case LV_DISPLAY_ROTATION_0:
        default:
            display_flush_fill_0(src, src_stride, w, dst, r0, r1);
            break;
    }
}


// Wake profiling (ADR-009): set by the flush-done ISR for the first frame after arming
static volatile bool display_flush_first_frame_armed = false;
static volatile int64_t display_flush_first_frame_done_us = 0;


void display_flush_arm_first_frame(void) {
    display_flush_first_frame_done_us = 0;
    display_flush_first_frame_armed = true;
}


int64_t display_flush_first_frame_us(void) {
    return display_flush_first_frame_done_us;
}


// SPI ISR context: no allocation, blocking or logging
static bool IRAM_ATTR display_flush_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                                  esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void) panel_io;
    (void) edata;

    lv_display_t *disp = (lv_display_t *) user_ctx;
    bool frame_done;

    int64_t frame_start_us;

    portENTER_CRITICAL_ISR(&display_flush_strip_lock);
    display_flush_ctx.strips_done++;
    frame_done = (display_flush_ctx.strips_done == display_flush_ctx.strips_total);
    frame_start_us = display_flush_ctx.frame_start_us;
    portEXIT_CRITICAL_ISR(&display_flush_strip_lock);

    if (frame_done) {
        uint32_t write_us = (uint32_t) (esp_timer_get_time() - frame_start_us);

        portENTER_CRITICAL_ISR(&display_flush_timing_lock);
        display_flush_timing_acc.frames++;
        display_flush_timing_acc.write_us_total += write_us;
        if (write_us > display_flush_timing_acc.write_max_us) {
            display_flush_timing_acc.write_max_us = write_us;
        }
        portEXIT_CRITICAL_ISR(&display_flush_timing_lock);

        if (display_flush_first_frame_armed) {
            display_flush_first_frame_done_us = esp_timer_get_time();
            display_flush_first_frame_armed = false;
        }

        lv_display_flush_ready(disp);
    }

    return false;
}


// GPIO ISR on both TE edges: counts pulses, times the period (rising to rising) and the high time
static void IRAM_ATTR display_flush_te_isr(void *arg) {
    (void) arg;

    const int64_t now_us = esp_timer_get_time();
    const bool high = gpio_ll_get_level(&GPIO, LCD_TE_OUT) != 0;

    portENTER_CRITICAL_ISR(&display_flush_timing_lock);
    if (high) {
        const int64_t since_rise_us = now_us - display_flush_te_state.last_rise_us;
        if (display_flush_te_state.last_rise_us != 0 && since_rise_us <= DISPLAY_FLUSH_TE_PERIOD_MAX_US) {
            uint32_t period_us = (uint32_t) since_rise_us;
            display_flush_te_state.period_us = period_us;
            if (display_flush_timing_acc.te_period_min_us == 0 || period_us < display_flush_timing_acc.te_period_min_us) {
                display_flush_timing_acc.te_period_min_us = period_us;
            }
            if (period_us > display_flush_timing_acc.te_period_max_us) {
                display_flush_timing_acc.te_period_max_us = period_us;
            }
        }
        display_flush_timing_acc.te_pulses++;
        display_flush_te_state.last_rise_us = now_us;
    }
    else {
        if (display_flush_te_state.last_rise_us != 0) {
            display_flush_timing_acc.te_high_us_total += (uint64_t) (now_us - display_flush_te_state.last_rise_us);
            display_flush_timing_acc.te_high_count++;
        }
        display_flush_te_state.last_fall_us = now_us;
    }
    portEXIT_CRITICAL_ISR(&display_flush_timing_lock);

    if (!high) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(display_flush_te_sem, &woken);
        if (woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}


void display_flush_take_timing(display_flush_timing_t *timing) {
    display_flush_timing_acc_t acc;

    portENTER_CRITICAL(&display_flush_timing_lock);
    acc = display_flush_timing_acc;
    display_flush_timing_acc = (display_flush_timing_acc_t) {0};
    portEXIT_CRITICAL(&display_flush_timing_lock);

    timing->te_pulses = acc.te_pulses;
    timing->te_period_min_us = acc.te_period_min_us;
    timing->te_period_max_us = acc.te_period_max_us;
    timing->te_high_avg_us = acc.te_high_count ? (uint32_t) (acc.te_high_us_total / acc.te_high_count) : 0;
    timing->frames = acc.frames;
    timing->write_avg_us = acc.frames ? (uint32_t) (acc.write_us_total / acc.frames) : 0;
    timing->write_max_us = acc.write_max_us;
    timing->te_wait_avg_us = acc.te_waits ? (uint32_t) (acc.te_wait_us_total / acc.te_waits) : 0;
    timing->te_wait_max_us = acc.te_wait_max_us;
    timing->te_timeouts = acc.te_timeouts;
    timing->te_retries = acc.te_retries;
    timing->te_late = acc.te_late;
}


static void display_flush_start_timer_cb(void *arg) {
    (void) arg;
    xSemaphoreGive(display_flush_start_sem);
}


// Wait until `target_us` (esp_timer time); returns at once if it has passed
static void display_flush_wait_until(int64_t target_us) {
    const int64_t remaining_us = target_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        return;
    }
    xSemaphoreTake(display_flush_start_sem, 0);
    if (esp_timer_start_once(display_flush_start_timer, (uint64_t) remaining_us) != ESP_OK) {
        return;
    }
    if (xSemaphoreTake(display_flush_start_sem,
                       pdMS_TO_TICKS(remaining_us / 1000 + DISPLAY_FLUSH_TE_TIMEOUT_MS)) != pdTRUE) {
        esp_timer_stop(display_flush_start_timer);
    }
}


// Start writing a physical window (rows [y0, y0 + h), pw pixels wide) at a point where the panel scan cannot cross
// the write, so the panel never shows the window half old and half new.
//
// The scan takes s per row and the write v per row (pw pixels, v_min..v_max). The write starts when the scan is
// h * max(0, s - v_min) past row y0. A write slower than the scan (full-width rows) starts right behind it. A faster
// one (narrow windows) starts far enough behind that it reaches the window's last row only after the scan has.
// The next pass of the scan then finds the window finished, or the write ahead of it and pulling away. Starting
// later than planned is safe for up to P - h * (max(0, s - v_min) + max(0, v_max - s)), where P is the TE period;
// later than that, the next pass would cross the write, so the flush waits one more pass, but only when the window
// is at least DISPLAY_FLUSH_RETRY_MIN_WINDOW_US long. A full-width window at row 0 (every full frame) starts on the
// TE falling edge, as in step 2.
static void display_flush_wait_scan(int32_t y0, int32_t h, int32_t pw) {
    if (display_flush_te_sem == NULL) {
        return;
    }

    const int64_t start_us = esp_timer_get_time();

    portENTER_CRITICAL(&display_flush_timing_lock);
    const display_flush_te_state_t te = display_flush_te_state;
    portEXIT_CRITICAL(&display_flush_timing_lock);

    const int64_t period_us = te.period_us ? te.period_us : DISPLAY_FLUSH_TE_PERIOD_DEFAULT_US;
    const int64_t s_ns = (period_us * 1000 + DISP_V_RES_PIXEL - 1) / DISP_V_RES_PIXEL;
    const int64_t v_min_ns = (int64_t) pw * DISPLAY_FLUSH_WRITE_NS_PER_PX_MIN;
    const int64_t v_max_ns = (int64_t) pw * DISPLAY_FLUSH_WRITE_NS_PER_PX_MAX;
    const int64_t lead_ns = (int64_t) h * (s_ns > v_min_ns ? s_ns - v_min_ns : 0);
    const int64_t lap_ns = (int64_t) h * (v_max_ns > s_ns ? v_max_ns - s_ns : 0);
    const int64_t offset_us = ((int64_t) y0 * s_ns + lead_ns + 999) / 1000;
    int64_t budget_us = period_us - (lead_ns + lap_ns) / 1000 - DISPLAY_FLUSH_START_MARGIN_US;
    if (budget_us < 0) {
        budget_us = 0;
    }

    xSemaphoreTake(display_flush_te_sem, 0);  // drop an edge that passed before this window was ready

    // The pass under way is used only while its start point is still ahead; otherwise wait for the next pass
    int64_t target_us = te.last_fall_us + offset_us;
    if (te.last_fall_us == 0 || start_us - te.last_fall_us > period_us || target_us < start_us) {
        target_us = 0;
    }

    bool got_edge = true;
    bool retried = false;
    bool late = false;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (target_us == 0) {
            got_edge = xSemaphoreTake(display_flush_te_sem, pdMS_TO_TICKS(DISPLAY_FLUSH_TE_TIMEOUT_MS)) == pdTRUE;
            if (!got_edge) {
                break;
            }
            portENTER_CRITICAL(&display_flush_timing_lock);
            target_us = display_flush_te_state.last_fall_us + offset_us;
            portEXIT_CRITICAL(&display_flush_timing_lock);
        }

        display_flush_wait_until(target_us);

        late = esp_timer_get_time() - target_us > budget_us;
        if (!late) {
            break;
        }
        // A window shorter than the LVGL task's usual wake-up delay is missed again on the next pass (ADR-002
        // step 3: 235 of 289 retries at 90 degrees), so the extra pass would cost time and protect nothing
        if (attempt > 0 || budget_us < DISPLAY_FLUSH_RETRY_MIN_WINDOW_US) {
            break;
        }
        retried = true;
        target_us = 0;
        xSemaphoreTake(display_flush_te_sem, 0);
    }

    const uint32_t wait_us = (uint32_t) (esp_timer_get_time() - start_us);

    portENTER_CRITICAL(&display_flush_timing_lock);
    display_flush_timing_acc.te_waits++;
    display_flush_timing_acc.te_wait_us_total += wait_us;
    if (wait_us > display_flush_timing_acc.te_wait_max_us) {
        display_flush_timing_acc.te_wait_max_us = wait_us;
    }
    if (!got_edge) {
        display_flush_timing_acc.te_timeouts++;
    }
    if (retried) {
        display_flush_timing_acc.te_retries++;
    }
    if (got_edge && late) {
        display_flush_timing_acc.te_late++;
    }
    portEXIT_CRITICAL(&display_flush_timing_lock);
}


static esp_err_t display_flush_te_init() {
    const gpio_config_t te_conf = {
        .pin_bit_mask = (1ULL << LCD_TE_OUT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    const esp_timer_create_args_t start_timer_args = {
        .callback = display_flush_start_timer_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "flush_start",
    };

    display_flush_start_sem = xSemaphoreCreateBinaryStatic(&display_flush_start_sem_buf);
    ESP_RETURN_ON_ERROR(esp_timer_create(&start_timer_args, &display_flush_start_timer), TAG,
                        "start timer create failed");

    display_flush_te_sem = xSemaphoreCreateBinaryStatic(&display_flush_te_sem_buf);

    ESP_RETURN_ON_ERROR(gpio_config(&te_conf), TAG, "TE GPIO config failed");
    // The GPIO ISR service is installed in app_main before the display is created
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(LCD_TE_OUT, display_flush_te_isr, NULL), TAG, "TE ISR add failed");

    return ESP_OK;
}


static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *) px_map;
    const lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    const int32_t src_stride = (int32_t) (lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565) / sizeof(uint16_t));

    // Physical resolution, as lvgl_port_rotate_area() computes it
    int32_t hres = lv_display_get_horizontal_resolution(disp);
    int32_t vres = lv_display_get_vertical_resolution(disp);
    if (rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270) {
        vres = lv_display_get_horizontal_resolution(disp);
        hres = lv_display_get_vertical_resolution(disp);
    }

    // Physical window: top-left (px1, py1), size pw x ph
    int32_t px1;
    int32_t py1;
    int32_t pw;
    int32_t ph;
    switch (rotation) {
        case LV_DISPLAY_ROTATION_90: {
            int32_t py2 = vres - area->x1 - 1;
            px1 = area->y1;
            py1 = py2 - w + 1;
            pw = h;
            ph = w;
            break;
        }
        case LV_DISPLAY_ROTATION_180: {
            int32_t py2 = vres - area->y1 - 1;
            int32_t px2 = hres - area->x1 - 1;
            py1 = py2 - h + 1;
            px1 = px2 - w + 1;
            pw = w;
            ph = h;
            break;
        }
        case LV_DISPLAY_ROTATION_270: {
            int32_t py2 = area->x2;
            px1 = hres - area->y2 - 1;
            py1 = py2 - w + 1;
            pw = h;
            ph = w;
            break;
        }
        case LV_DISPLAY_ROTATION_0:
        default:
            px1 = area->x1;
            py1 = area->y1;
            pw = w;
            ph = h;
            break;
    }

    // Rows per strip: as many full rows as fit, rounded down to even (SH8601: start row even, end row odd)
    int32_t rows = DISPLAY_FLUSH_STAGING_BYTES / (pw * (int32_t) sizeof(uint16_t));
    rows &= ~1;
    if (rows < 2) {
        rows = 2;
    }
    const uint32_t strips = (uint32_t) ((ph + rows - 1) / rows);

    // Strip 0 is filled before the wait, so the write starts as close to the planned point as possible
    display_flush_fill_strip(src, src_stride, w, h, rotation, display_flush_ctx.staging[0], pw, 0,
                             (rows < ph) ? rows : ph);

    if (display_flush_tear_free) {
        display_flush_wait_scan(py1, ph, pw);
    }

    portENTER_CRITICAL(&display_flush_strip_lock);
    display_flush_ctx.strips_total = strips;
    display_flush_ctx.strips_done = 0;
    display_flush_ctx.frame_start_us = esp_timer_get_time();
    portEXIT_CRITICAL(&display_flush_strip_lock);

    // Two staging buffers alternate. The next strip's draw_bitmap() returns only after the previous strip has left
    // the bus (esp_lcd SPI tx_param waits for every in-flight transaction), so a buffer is free to refill once the
    // draw_bitmap() call after the one that sent it has returned.
    uint32_t queued = 0;
    for (int32_t r0 = 0; r0 < ph; r0 += rows) {
        const int32_t r1 = (r0 + rows < ph) ? (r0 + rows) : ph;
        uint16_t *dst = display_flush_ctx.staging[queued & 1];

        if (r0 > 0) {
            display_flush_fill_strip(src, src_stride, w, h, rotation, dst, pw, r0, r1);
        }

        esp_err_t ret = esp_lcd_panel_draw_bitmap(display_flush_ctx.panel_handle,
                                                  px1, py1 + r0, px1 + pw, py1 + r1, dst);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "draw_bitmap failed at strip %" PRIu32 "/%" PRIu32 ": %s, frame dropped",
                     queued, strips, esp_err_to_name(ret));
            break;
        }
        queued++;
    }

    if (queued != strips) {
        // Only the strips queued successfully will complete; never leave LVGL waiting
        bool frame_done;
        portENTER_CRITICAL(&display_flush_strip_lock);
        display_flush_ctx.strips_total = queued;
        frame_done = (display_flush_ctx.strips_done == display_flush_ctx.strips_total);
        portEXIT_CRITICAL(&display_flush_strip_lock);

        if (frame_done) {
            lv_display_flush_ready(disp);
        }
    }
}


lv_display_t *display_flush_create(esp_lcd_panel_io_handle_t io_handle,
                                   esp_lcd_panel_handle_t panel_handle) {
    const esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = display_flush_trans_done_cb,
    };
    void *draw_buf[2] = {NULL, NULL};
    uint16_t *staging[2] = {NULL, NULL};
    lv_display_t *disp = NULL;
    esp_err_t ret;

    // LVGL draw buffers: full frame, PSRAM
    for (int i = 0; i < 2; i++) {
        draw_buf[i] = heap_caps_aligned_alloc(CONFIG_LV_DRAW_BUF_ALIGN, DISPLAY_FLUSH_DRAW_BUF_BYTES,
                                              HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
        if (draw_buf[i] == NULL) {
            ESP_LOGE(TAG, "Not enough memory for draw buffer %d (%u B)", i, (unsigned) DISPLAY_FLUSH_DRAW_BUF_BYTES);
            goto err;
        }
    }

    // Staging buffers: internal, DMA-capable, so the SPI master sends them without a bounce copy
    for (int i = 0; i < 2; i++) {
        staging[i] = heap_caps_aligned_alloc(4, DISPLAY_FLUSH_STAGING_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (staging[i] == NULL) {
            ESP_LOGE(TAG, "Not enough memory for staging buffer %d (%d B)", i, DISPLAY_FLUSH_STAGING_BYTES);
            goto err;
        }
    }

    display_flush_ctx.panel_handle = panel_handle;
    display_flush_ctx.staging[0] = staging[0];
    display_flush_ctx.staging[1] = staging[1];
    display_flush_ctx.strips_total = 0;
    display_flush_ctx.strips_done = 0;

    disp = lv_display_create(DISP_H_RES_PIXEL, DISP_V_RES_PIXEL);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        goto err;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, draw_buf[0], draw_buf[1], DISPLAY_FLUSH_DRAW_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, display_flush_cb);

    if (lv_display_add_event_cb(disp, display_flush_event_cb, LV_EVENT_INVALIDATE_AREA, NULL) == NULL ||
        lv_display_add_event_cb(disp, display_flush_event_cb, LV_EVENT_REFR_REQUEST, NULL) == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display event callbacks");
        goto err;
    }

    ret = esp_lcd_panel_io_register_event_callbacks(io_handle, &io_cbs, disp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register panel IO callback: %s", esp_err_to_name(ret));
        goto err;
    }

    // A missing TE input costs the sync, never the display
    ret = display_flush_te_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TE input not set up, frames are sent without TE sync: %s", esp_err_to_name(ret));
        display_flush_te_sem = NULL;
    }

    ESP_LOGI(TAG, "Display created: draw_buf=2x%u B (PSRAM) staging=2x%d B (internal DMA)",
             (unsigned) DISPLAY_FLUSH_DRAW_BUF_BYTES, DISPLAY_FLUSH_STAGING_BYTES);

    return disp;

err:
    if (disp != NULL) {
        lv_display_delete(disp);
    }
    for (int i = 0; i < 2; i++) {
        heap_caps_free(staging[i]);
        heap_caps_free(draw_buf[i]);
    }
    display_flush_ctx.panel_handle = NULL;
    display_flush_ctx.staging[0] = NULL;
    display_flush_ctx.staging[1] = NULL;

    return NULL;
}

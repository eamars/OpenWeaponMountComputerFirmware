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

// Window totals behind display_flush_take_timing(); protected by display_flush_timing_lock
typedef struct {
    uint32_t te_pulses;
    int64_t te_last_rise_us;
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
} display_flush_timing_acc_t;

static display_flush_ctx_t display_flush_ctx;
static portMUX_TYPE display_flush_strip_lock = portMUX_INITIALIZER_UNLOCKED;
static display_flush_timing_acc_t display_flush_timing_acc;
static portMUX_TYPE display_flush_timing_lock = portMUX_INITIALIZER_UNLOCKED;

// Given on each TE falling edge (end of V-blank: the panel starts scanning from row 0)
static StaticSemaphore_t display_flush_te_sem_buf;
static SemaphoreHandle_t display_flush_te_sem = NULL;


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
        if (display_flush_timing_acc.te_last_rise_us != 0) {
            uint32_t period_us = (uint32_t) (now_us - display_flush_timing_acc.te_last_rise_us);
            if (display_flush_timing_acc.te_period_min_us == 0 || period_us < display_flush_timing_acc.te_period_min_us) {
                display_flush_timing_acc.te_period_min_us = period_us;
            }
            if (period_us > display_flush_timing_acc.te_period_max_us) {
                display_flush_timing_acc.te_period_max_us = period_us;
            }
        }
        display_flush_timing_acc.te_pulses++;
        display_flush_timing_acc.te_last_rise_us = now_us;
    }
    else if (display_flush_timing_acc.te_last_rise_us != 0) {
        display_flush_timing_acc.te_high_us_total += (uint64_t) (now_us - display_flush_timing_acc.te_last_rise_us);
        display_flush_timing_acc.te_high_count++;
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
    // Keep the last rising edge so the next window's first period is measured
    const int64_t last_rise_us = display_flush_timing_acc.te_last_rise_us;
    display_flush_timing_acc = (display_flush_timing_acc_t) {0};
    display_flush_timing_acc.te_last_rise_us = last_rise_us;
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
}


// Start a frame where the panel starts its scan, so a frame never shows two halves: the write (20-23 ms) stays behind
// the first scan (16.7 ms) and finishes long before the second scan could catch it. Never blocks longer than the timeout.
static void display_flush_wait_te() {
    if (display_flush_te_sem == NULL) {
        return;
    }

    const int64_t start_us = esp_timer_get_time();
    xSemaphoreTake(display_flush_te_sem, 0);  // drop an edge that passed before this frame was ready
    const bool got_edge = xSemaphoreTake(display_flush_te_sem, pdMS_TO_TICKS(DISPLAY_FLUSH_TE_TIMEOUT_MS)) == pdTRUE;
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

    display_flush_wait_te();

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

#ifndef DISPLAY_FLUSH_H
#define DISPLAY_FLUSH_H

#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_FLUSH_STAGING_BYTES 16384

// Create the LVGL display for the SH8601 panel: two full-frame RGB565 draw
// buffers in PSRAM (HEAPS_CAPS_ALLOC_DEFAULT_FLAGS, CONFIG_LV_DRAW_BUF_ALIGN),
// partial render mode, two internal DMA staging buffers, the flush callback,
// the even/odd rounder and LVGL task wake on LV_EVENT_INVALIDATE_AREA and
// LV_EVENT_REFR_REQUEST, and the panel IO on_color_trans_done callback.
// Call with the LVGL lock held, after lvgl_port_init(). Returns NULL and frees
// everything it allocated if any allocation fails.
lv_display_t *display_flush_create(esp_lcd_panel_io_handle_t io_handle,
                                   esp_lcd_panel_handle_t panel_handle);

// Panel tearing-effect (TE, GPIO18) and frame write timing since the last call. Each frame starts on the TE falling edge.
typedef struct {
    uint32_t te_pulses;          // TE rising edges
    uint32_t te_period_min_us;   // shortest rising-to-rising interval (0 with fewer than two pulses)
    uint32_t te_period_max_us;
    uint32_t te_high_avg_us;     // average time TE stays high
    uint32_t frames;             // flushes whose last strip completed
    uint32_t write_avg_us;       // first strip queued to last strip done
    uint32_t write_max_us;
    uint32_t te_wait_avg_us;     // time a flush waited for the TE falling edge
    uint32_t te_wait_max_us;
    uint32_t te_timeouts;        // flushes sent without an edge after DISPLAY_FLUSH_TE_TIMEOUT_MS
} display_flush_timing_t;

// Copy the timing gathered since the previous call and start a new window
void display_flush_take_timing(display_flush_timing_t *timing);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FLUSH_H

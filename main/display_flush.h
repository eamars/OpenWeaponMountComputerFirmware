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

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FLUSH_H

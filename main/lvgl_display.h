#ifndef LVGL_DISPLAY_H_
#define LVGL_DISPLAY_H_

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_display_init(i2c_master_bus_handle_t tp_i2c_handle);

// Pause the LVGL lock-up watchdog exactly while LVGL is stopped on purpose (lvgl_port_stop()
// to lvgl_port_resume()). While paused, lvgl_watchdog_feed_while_paused() feeds it, but only while the
// LVGL lock is free, so a lock held through the whole timeout still reboots the unit.
void lvgl_watchdog_pause(bool pause);

// Called every second by the low-power task; feeds only while paused
void lvgl_watchdog_feed_while_paused();

// Shared state, defined in lvgl_display.c
extern esp_lcd_panel_io_handle_t io_handle;
extern esp_lcd_panel_handle_t panel_handle;
extern lv_indev_t * lvgl_touch_handle;  // low power wraps its read callback
extern lv_group_t * button_input_group;

#ifdef __cplusplus
}
#endif


#endif  // LVGL_DISPLAY_H_
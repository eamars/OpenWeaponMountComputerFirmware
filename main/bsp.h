#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "sdkconfig.h"


esp_err_t display_init(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle, uint8_t brightness_pct);
esp_err_t touchscreen_init(esp_lcd_touch_handle_t *touch_handle, i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax, uint16_t rotation);
esp_err_t set_display_brightness(esp_lcd_panel_io_handle_t *io_handle, uint8_t brightness_pct);

/**
 * @brief Put the panel into its sleep (sleep-in) or out of it (sleep-out), after display off and before display on.
 * Sleep-out returns once the panel may be switched on. The panel keeps its picture and settings (ADR-009)
 */
esp_err_t set_display_sleep(esp_lcd_panel_io_handle_t io_handle, bool sleep);

#endif // BSP_H
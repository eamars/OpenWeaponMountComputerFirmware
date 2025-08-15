#ifndef ESP_LCD_TOUCH_FT3168_H
#define ESP_LCD_TOUCH_FT3168_H

#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

esp_err_t esp_lcd_touch_new_ft3168(esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch);

#endif // ESP_LCD_TOUCH_FT3168_H
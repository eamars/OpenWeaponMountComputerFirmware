/* FreeRTOS Real Time Stats Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_err.h"

#include "bsp_display.h"
#include "bsp_touch.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"

#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "main_tileview.h"
#include "app_cfg.h"
#include "bno085.h"

bno085_ctx_t bno085_dev;

void app_main(void)
{
    //Initialize NVS storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize I2C
    i2c_master_bus_handle_t i2c_bus_handle = bsp_i2c_init();

    // Initialize BNO085 sensor
    // ESP_ERROR_CHECK(bno085_init(&bno085_dev, UART_NUM_0, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_8));
    ESP_ERROR_CHECK(bno085_init_i2c(&bno085_dev, i2c_bus_handle));
    ESP_ERROR_CHECK(bno085_enable_report(&bno085_dev, SH2_GAME_ROTATION_VECTOR, 10));

    // Initialize SPI touch screen and I2C display
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    bsp_spi_init();
    bsp_display_init(&io_handle, &panel_handle, 0);
    bsp_touch_init(&touch_handle, i2c_bus_handle, DISP_H_RES_PIXEL, DISP_V_RES_PIXEL, DISP_ROTATION);
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, DISP_PANEL_H_GAP, DISP_PANEL_V_GAP));

    // Initialize LVGL
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ret = lvgl_port_init(&lvgl_cfg);
    ESP_ERROR_CHECK(ret);

    // Add display to LVGL
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = DISP_H_RES_PIXEL * DISP_V_RES_PIXEL / 4,
        .double_buffer = true,
        .hres = DISP_H_RES_PIXEL,
        .vres = DISP_V_RES_PIXEL,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        }
    };

    lv_display_t *lvgl_disp = NULL;
    // lv_indev_t *lvgl_touch_indev = NULL;
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // Add touch input to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp, 
        .handle = touch_handle
    };
    lvgl_port_add_touch(&touch_cfg);

    // Set display brightness
    bsp_display_brightness_init();
    bsp_display_set_brightness(100); // Set brightness to 100%

    // Create LVGL application
    if (lvgl_port_lock(0)) {
        create_main_tileview(lv_screen_active());
        lvgl_port_unlock();
    }
}

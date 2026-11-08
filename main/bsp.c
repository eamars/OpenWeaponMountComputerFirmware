#include "bsp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_cfg.h"
#include "esp_check.h"

#define TAG "BSP"

#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft3168.h"
#include "esp_lcd_panel_ops.h"


esp_err_t set_display_brightness(esp_lcd_panel_io_handle_t *io_handle, uint8_t brightness_pct) {
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    uint8_t param = 255 * brightness_pct / 100;
    esp_lcd_panel_io_tx_param(*io_handle, lcd_cmd, &param,1);

    return ESP_OK;
}

// The panel module (DO0164, ICNA3311 / CO5300 compatible) powers down as display off, then sleep-in; it needs 50 ms
// from sleep-out to display on (DO0164 p.20-21). Sleep-in to sleep-out is kept at least 120 ms apart, the usual rule
// for this command set; a wake right after idle entry waits out the rest
#define DISPLAY_SLEEP_OUT_TO_ON_MS      60
#define DISPLAY_SLEEP_IN_TO_OUT_MIN_MS  120

static int64_t display_sleep_in_us = 0;


static esp_err_t display_send_cmd(esp_lcd_panel_io_handle_t io_handle, uint8_t cmd) {
    // The SH8601 driver's QSPI command form, as set_display_brightness
    uint32_t lcd_cmd = (0x02u << 24) | ((uint32_t) cmd << 8);
    return esp_lcd_panel_io_tx_param(io_handle, lcd_cmd, NULL, 0);
}


esp_err_t set_display_sleep(esp_lcd_panel_io_handle_t io_handle, bool sleep) {
    if (sleep) {
        esp_err_t ret = display_send_cmd(io_handle, 0x10);  // SLPIN
        display_sleep_in_us = esp_timer_get_time();
        return ret;
    }

    int64_t since_ms = (esp_timer_get_time() - display_sleep_in_us) / 1000;
    if (display_sleep_in_us != 0 && since_ms < DISPLAY_SLEEP_IN_TO_OUT_MIN_MS) {
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_SLEEP_IN_TO_OUT_MIN_MS - since_ms));
    }
    esp_err_t ret = display_send_cmd(io_handle, 0x11);  // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_SLEEP_OUT_TO_ON_MS));
    return ret;
}


static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 80},   
    {0xC4, (uint8_t []){0x80}, 1, 0},
   
    {0x35, (uint8_t []){0x00}, 1, 0},

    {0x53, (uint8_t []){0x20}, 1, 1},
    {0x63, (uint8_t []){0xFF}, 1, 1},
    {0x51, (uint8_t []){0x00}, 1, 1},

    {0x29, (uint8_t []){0x00}, 0, 10},

    {0x51, (uint8_t []){0xFF}, 1, 0},    //亮度
};

esp_err_t display_init(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle, uint8_t brightness_pct) {
    // Initialize QSPI Host
    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        LCD_PCLK, 
        LCD_DATA0,
        LCD_DATA1,
        LCD_DATA2, 
        LCD_DATA3,
        LCD_QSPI_MAX_TRANSFER_SIZE
    );
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_QSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);

    // Attach LCD to QSPI
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) LCD_QSPI_HOST, &io_config, io_handle));

    // Initialize LCD display
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(*io_handle, &panel_config, panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));

    // Set brightness
    set_display_brightness(io_handle, brightness_pct);

    // Set lcd gap
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(*panel_handle, DISP_PANEL_H_GAP, DISP_PANEL_V_GAP));

    return ESP_OK;
}

esp_err_t touchscreen_init(esp_lcd_touch_handle_t *touch_handle, i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax, uint16_t rotation) {
    static i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ADDR_FT3168,
        .scl_speed_hz = 300000,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle), TAG, "Failed to add FT3168");

    // Add touch driver
    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = xmax < ymax ? xmax : ymax;
    tp_cfg.y_max = xmax < ymax ? ymax : xmax;
    tp_cfg.driver_data = dev_handle;
    tp_cfg.rst_gpio_num = TOUCHSCREEN_RST_PIN;
    tp_cfg.int_gpio_num = TOUCHSCREEN_INT_PIN;
    tp_cfg.levels.reset = 0;

    if (90 == rotation)
    {
        tp_cfg.flags.swap_xy = 0;
        tp_cfg.flags.mirror_x = 1;
        tp_cfg.flags.mirror_y = 0;
    }
    else if (180 == rotation)
    {
        tp_cfg.flags.swap_xy = 0;
        tp_cfg.flags.mirror_x = 0;
        tp_cfg.flags.mirror_y = 1;
    }
    else if (270 == rotation)
    {
        tp_cfg.flags.swap_xy = 1;
        tp_cfg.flags.mirror_x = 1;
        tp_cfg.flags.mirror_y = 1;
    }
    else
    {
        tp_cfg.flags.swap_xy = 1;
        tp_cfg.flags.mirror_x = 0;
        tp_cfg.flags.mirror_y = 0;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_ft3168(&tp_cfg, touch_handle), TAG, "Failed to initialize FT3168");

    return ESP_OK;
}


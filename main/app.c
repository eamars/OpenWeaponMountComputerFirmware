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

// #include "bsp_display.h"  // to be removed
// #include "bsp_touch.h"  // to be removed

#include "driver/i2c_master.h"

#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "main_tileview.h"
#include "app_cfg.h"
#include "bno085.h"
#include "system_config.h"
#include "sensor_config.h"


#define TAG "App"

bno085_ctx_t bno085_dev;
extern system_config_t system_config;

void mem_monitor_task(void *pvParameters) {
    while (1) {
        // Heap info
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);


        ESP_LOGI(TAG, "Free heap: %u kbytes | Min free heap: %u kbytes",
                (unsigned)free_heap / 1024, (unsigned)min_free_heap / 1024);

        // Optional: Internal RAM only
        ESP_LOGI(TAG, "Free internal heap: %u kbytes",
                (unsigned)esp_get_free_internal_heap_size() / 1024);

#if CONFIG_IDF_TARGET_ESP32C6
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon); // Fill the monitor struct

        ESP_LOGI(TAG, "LVGL free: %u kbytes | LVGL total: %u kbytes",
                mon.free_size / 1024, mon.total_size / 1024);
#endif  // CONFIG_IDF_TARGET_ESP32C6

        ESP_LOGI(TAG, "---------------------------");
        vTaskDelay(pdMS_TO_TICKS(2000)); // Every 2 seconds
    }
}

i2c_master_bus_handle_t initialize_i2c_master() {
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = (i2c_port_num_t) I2C_PORT_NUM;
    i2c_mst_config.scl_io_num = I2C_MASTER_SCL;
    i2c_mst_config.sda_io_num = I2C_MASTER_SDA;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));
    return i2c_bus_handle;
}

esp_err_t initialize_nvs_flash() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}


#if CONFIG_IDF_TARGET_ESP32C6
    esp_err_t initialize_spi_master() {
        spi_bus_config_t buscfg = {
            .miso_io_num = SPI_MISO,
            .mosi_io_num = SPI_MOSI,
            .sclk_io_num = SPI_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1
        };

        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
        return ESP_OK;
    }


    esp_err_t initialize_display(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle, uint8_t brightness_pct) {
        // Initialize SPI host
        spi_bus_config_t buscfg = {
            .miso_io_num = SPI_MISO,
            .mosi_io_num = SPI_MOSI,
            .sclk_io_num = SPI_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1
        };

        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_spi_config_t io_config = JD9853_PANEL_IO_SPI_CONFIG(LCD_CS, LCD_DC, NULL, NULL);
        io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;

        // Attach LCD to the SPI bus
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI_HOST, &io_config, io_handle));

        // Initialize LCD display
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_jd9853(*io_handle, &panel_config, panel_handle));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_handle, true));
        // ESP_ERROR_CHECK(esp_lcd_panel_set_gap(*panel_handle, 0, 34));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(*panel_handle, false, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));

        // Initialize backlight
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LCD_BL_LEDC_MODE,
            .timer_num = LCD_BL_LEDC_TIMER,
            .duty_resolution = LCD_BL_LEDC_DUTY_RES,
            .freq_hz = LCD_BL_LEDC_FREQUENCY,
            .clk_cfg = LEDC_AUTO_CLK
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        // Prepare and apply the LEDC PWM configuration
        ledc_channel_config_t ledc_channel = {
            .speed_mode = LCD_BL_LEDC_MODE,
            .channel = LCD_BL_LEDC_CHANNEL,
            .timer_sel = LCD_BL_LEDC_TIMER,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = LCD_BL,
            .duty = 0, // Set duty to 0% 
            .hpoint = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

        // Set brightness
        uint32_t duty = (brightness_pct * (LCD_BL_LEDC_DUTY - 1)) / 100;
        ESP_ERROR_CHECK(ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL));

        return ESP_OK;
    }

    esp_err_t initialize_touch(esp_lcd_touch_handle_t *touch_handle, i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax, uint16_t rotation) {
        static i2c_master_dev_handle_t dev_handle;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS,
            .scl_speed_hz = 400000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

        
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = xmax < ymax ? xmax : ymax;
        tp_cfg.y_max = xmax < ymax ? ymax : xmax;
        tp_cfg.rst_gpio_num = TP_RST;
        tp_cfg.int_gpio_num = TP_INT;

        if (90 == rotation)
        {
            tp_cfg.flags.swap_xy = 1;
            tp_cfg.flags.mirror_x = 0;
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
            tp_cfg.flags.swap_xy = 0;
            tp_cfg.flags.mirror_x = 1;
            tp_cfg.flags.mirror_y = 0;
        }

        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs5106(dev_handle, &tp_cfg, touch_handle));

        return ESP_OK;
    }
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_err_t initialize_display(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle, uint8_t brightness_pct) {
        // Initialize QSPI Host
        spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
            LCD_PCLK, 
            LCD_DATA0,
            LCD_DATA1,
            LCD_DATA2, 
            LCD_DATA3,
            DISP_H_RES_PIXEL * 80 * sizeof(uint16_t)
        );
        ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);

        // Attach LCD to QSPI
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI_HOST, &io_config, io_handle));

        // Initialize LCD display
        co5300_vendor_config_t vendor_config = {
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

        ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(*io_handle, &panel_config, panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));

        // Set brightness
        uint32_t lcd_cmd = 0x51;
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= 0x02 << 24;
        uint8_t param = 255 * brightness_pct / 100;
        esp_lcd_panel_io_tx_param(*io_handle, lcd_cmd, &param,1);

        return ESP_OK;
    }

    esp_err_t initialize_touch(esp_lcd_touch_handle_t *touch_handle, i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax, uint16_t rotation) {
        static i2c_master_dev_handle_t dev_handle;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = I2C_ADDR_FT3168,
            .scl_speed_hz = 300000,
        };

        // Detect the presence of device
        ESP_RETURN_ON_ERROR(i2c_master_probe(bus_handle, I2C_ADDR_FT3168, -1), TAG, "Failed to probe FT3168");

        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle), TAG, "Failed to add FT3168");

        // Add touch driver
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = xmax < ymax ? xmax : ymax;
        tp_cfg.y_max = xmax < ymax ? ymax : xmax;
        tp_cfg.driver_data = dev_handle;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;
        tp_cfg.int_gpio_num = GPIO_NUM_NC;

        if (90 == rotation)
        {
            tp_cfg.flags.swap_xy = 1;
            tp_cfg.flags.mirror_x = 0;
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
            tp_cfg.flags.swap_xy = 0;
            tp_cfg.flags.mirror_x = 1;
            tp_cfg.flags.mirror_y = 0;
        }

        ESP_RETURN_ON_ERROR(esp_lcd_touch_new_ft3168(&tp_cfg, touch_handle), TAG, "Failed to initialize FT3168");

        return ESP_OK;
    }

#endif

void app_main(void)
{
    // Create task to monitor memory usage
    xTaskCreate(mem_monitor_task, "mem_monitor_task", 2048, NULL, 3, NULL);

    // Install interrupt service (so each module don't need to run again)
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    //Initialize NVS storage
    ESP_ERROR_CHECK(initialize_nvs_flash());

    // Read system configurations
    ESP_ERROR_CHECK(load_system_config());
    ESP_ERROR_CHECK(load_sensor_config());

    // Initialize I2C
    i2c_master_bus_handle_t i2c_bus_handle = initialize_i2c_master();
    
    // Initialize SPI touch screen and I2C display
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(initialize_display(&io_handle, &panel_handle, 100));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, DISP_PANEL_H_GAP, DISP_PANEL_V_GAP));
    ESP_ERROR_CHECK(initialize_touch(&touch_handle, i2c_bus_handle, DISP_H_RES_PIXEL, DISP_V_RES_PIXEL, DISP_ROTATION));

    // Initialize BNO085 sensor
    ESP_ERROR_CHECK(bno085_init_i2c(&bno085_dev, i2c_bus_handle, BNO085_INT_PIN));

    ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(&bno085_dev, 20));
    ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(&bno085_dev, 20));

    // Initialize LVGL
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
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
#if CONFIG_IDF_TARGET_ESP32S3
        .trans_size = 8 * 1024,
#endif  // CONFIG_IDF_TARGET_ESP32S3
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
            .sw_rotate = true,
#if CONFIG_IDF_TARGET_ESP32S3
            .buff_spiram = true,
#endif  // CONFIG_TARGET_ESP32S3
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

    // Create LVGL application
    if (lvgl_port_lock(0)) {
        create_main_tileview(lv_screen_active());
        lv_display_set_rotation(lvgl_disp, system_config.rotation);
        lvgl_port_unlock();
    }
}

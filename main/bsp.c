#include "bsp.h"
#include "app_cfg.h"
#include "esp_check.h"

#define TAG "BSP"


#if USE_LCD_JD9853
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ledc.h"


esp_err_t set_display_brightness(esp_lcd_panel_io_handle_t *io_handle, uint8_t brightness_pct) {
    ESP_UNUSED(io_handle);

    uint32_t duty = (brightness_pct * (LCD_BL_LEDC_DUTY - 1)) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL));

    return ESP_OK;
}

esp_err_t display_init(esp_lcd_panel_io_handle_t *io_handle, esp_lcd_panel_handle_t *panel_handle, uint8_t brightness_pct) {
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
    set_display_brightness(io_handle, brightness_pct);

    // Set lcd gap
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(*panel_handle, DISP_PANEL_H_GAP, DISP_PANEL_V_GAP));

    return ESP_OK;
}
#elif USE_LCD_SH8601
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
        4096
    );
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);

    // Attach LCD to QSPI
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI_HOST, &io_config, io_handle));

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

#endif  // USE_LCD_JD9853


#if USE_TOUCH_AXS5106

#include "esp_lcd_touch_axs5106.h"
esp_err_t touchscreen_init(esp_lcd_touch_handle_t *touch_handle, i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax, uint16_t rotation) {
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

#elif USE_TOUCH_FT3168

esp_err_t touchscreen_init(esp_lcd_touch_handle_t *touch_handle, i2c_master_bus_handle_t bus_handle, uint16_t xmax, uint16_t ymax, uint16_t rotation) {
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

#endif  // USE_TOUCH_AXS5106
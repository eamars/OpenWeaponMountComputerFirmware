#ifndef APP_CFG_H
#define APP_CFG_H

#include "sdkconfig.h"

#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"


// By default the memory will be allocated to PSRAM
#define HEAPS_CAPS_ALLOC_DEFAULT_FLAGS MALLOC_CAP_SPIRAM

// By default the heaps static memory will be allocated to PSRAM
#define HEAPS_CAPS_ATTR EXT_RAM_BSS_ATTR

// #define USE_BNO085 1
// #define USE_LCD_JD9853 1  // Use JD9853 LCD module https://www.waveshare.com/product/displays/lcd-oled/lcd-oled-3/1.47inch-touch-lcd.htm
// #define USE_TOUCH_AXS5106 1 // Use AXS5106 touch controller

#define USE_LCD_SH8601 1  // Use SH8601 LCD module
#define USE_TOUCH_FT3168 1 // Use FT3168 touch controller


#if USE_LCD_SH8601
    // Define SPI ports
    #define SPI_HOST (SPI2_HOST)
    #define SPI_MISO (GPIO_NUM_40)
    #define SPI_MOSI (GPIO_NUM_39)
    #define SPI_SCLK (GPIO_NUM_41)

    // Define I2C ports
    #define I2C_PORT_NUM (I2C_NUM_0)
    #define I2C_MASTER_SDA (GPIO_NUM_47)
    #define I2C_MASTER_SCL (GPIO_NUM_48)

    // Define QSPI for display
    #define LCD_CS            (GPIO_NUM_9)
    #define LCD_PCLK          (GPIO_NUM_10) 
    #define LCD_DATA0         (GPIO_NUM_11)
    #define LCD_DATA1         (GPIO_NUM_12)
    #define LCD_DATA2         (GPIO_NUM_13)
    #define LCD_DATA3         (GPIO_NUM_14)
    #define LCD_RST           (GPIO_NUM_21)

    #define I2C_ADDR_FT3168 0x38

    #if CONFIG_LV_COLOR_DEPTH == 32
        #define LCD_BIT_PER_PIXEL       (24)
    #elif CONFIG_LV_COLOR_DEPTH == 16
        #define LCD_BIT_PER_PIXEL       (16)
    #endif  // CONFIG_LV_COLOR_DEPTH

    #define BNO085_INT_PIN (GPIO_NUM_45)

    #define DISP_H_RES_PIXEL (280)
    #define DISP_V_RES_PIXEL (456)

    #define DISP_PANEL_H_GAP (20) // Horizontal gap for display panel
    #define DISP_PANEL_V_GAP (0)  // Vertical gap for display panel

    #define BNO085_INT_PIN (GPIO_NUM_45)
#elif USE_LCD_JD9853
    // Define SPI ports
    #define SPI_HOST (SPI2_HOST)
    #define SPI_MISO (GPIO_NUM_NC)
    #define SPI_MOSI (GPIO_NUM_39)
    #define SPI_SCLK (GPIO_NUM_38)

    // Define I2C ports
    #define I2C_PORT_NUM (I2C_NUM_0)
    #define I2C_MASTER_SDA (GPIO_NUM_42)
    #define I2C_MASTER_SCL (GPIO_NUM_41)

    // Define SPI for display
    #define LCD_CS (GPIO_NUM_21)
    #define LCD_DC (GPIO_NUM_45)
    #define LCD_RST (GPIO_NUM_40)
    #define LCD_BL (GPIO_NUM_46)
    #define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
    #define LCD_BL_LEDC_TIMER LEDC_TIMER_0
    #define LCD_BL_LEDC_MODE LEDC_LOW_SPEED_MODE
    #define LCD_BL_LEDC_CHANNEL LEDC_CHANNEL_0
    #define LCD_BL_LEDC_DUTY_RES LEDC_TIMER_10_BIT // Set duty resolution to 13 bits
    #define LCD_BL_LEDC_DUTY (1024)                // Set duty to 50%. 1024 * 50% = 4096
    #define LCD_BL_LEDC_FREQUENCY (5000)          // Frequency in Hertz. Set frequency at 5 kHz

    #define TP_RST (GPIO_NUM_47)
    #define TP_INT (GPIO_NUM_48)

    #define DISP_H_RES_PIXEL (172)
    #define DISP_V_RES_PIXEL (320)

    #define DISP_PANEL_H_GAP (34) // Horizontal gap for display panel
    #define DISP_PANEL_V_GAP (0)  // Vertical gap for display panel

    #define BNO085_INT_PIN (GPIO_NUM_9)


    // New settings for custom board
    #define SPI3_HOST (SPI3_HOST)
    #define SPI3_MISO (GPIO_NUM_3)
    #define SPI3_MOSI (GPIO_NUM_2)
    #define SPI3_SCLK (GPIO_NUM_4)

#endif  // USE_LCD_CO5300

// Screen related definitions
#define DISP_ROTATION (0) // 0: 0 degrees, 1: 90 degrees, 2: 180 degrees, 3: 270 degrees


#define SENSOR_EVENT_POLLER_TASK_STACK 3072
#define SENSOR_EVENT_POLLER_TASK_PRIORITY 5
#define ACCELERATION_EVENT_POLLER_TASK_STACK 3072
#define ACCELERATION_EVENT_POLLER_TASK_PRIORITY 4

#define SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS 20
#define SENSOR_GAME_ROTATION_VECTOR_LOW_POWER_MODE_REPORT_PERIOD_MS 0
#define SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS 20
#define SENSOR_LINEAR_ACCELERATION_LOW_POWER_MODE_REPORT_PERIOD_MS 0
#define SENSOR_ROTATION_VECTOR_REPORT_PERIOD_MS 20
#define SENSOR_ROTATION_VECTOR_LOW_POWER_MODE_REPORT_PERIOD_MS 0

#define DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS 20

#define LOW_POWER_MODE_MONITOR_TASK_STACK 4096
#define LOW_POWER_MODE_MONITOR_TASK_PRIORITY 6
#define LOW_POWER_MODE_MONITOR_TASK_PERIOD_MS 1000

#define SENSOR_STABILITY_CLASSIFIER_TASK_STACK 3072
#define SENSOR_STABILITY_CLASSIFIER_TASK_PRIORITY 3
#define SENSOR_STABILITY_CLASSIFIER_REPORT_PERIOD_MS 500

#define LVGL_UNLOCK_WAIT_TIME_MS 1


#endif // APP_CFG_H

#ifndef APP_CFG_H
#define APP_CFG_H

#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "driver/ledc.h"


// By default the memory will be allocated to PSRAM
#define HEAPS_CAPS_ALLOC_DEFAULT_FLAGS MALLOC_CAP_SPIRAM

// By default the heaps static memory will be allocated to PSRAM
#define HEAPS_CAPS_ATTR EXT_RAM_BSS_ATTR


// Define SPI ports
// #define SPI_HOST (SPI3_HOST)
// #define SPI_MISO (GPIO_NUM_3)
// #define SPI_MOSI (GPIO_NUM_2)
// #define SPI_SCLK (GPIO_NUM_4)

#define SPI_HOST (SPI3_HOST)
#define SPI_MISO (GPIO_NUM_3)
#define SPI_MOSI (GPIO_NUM_2)
#define SPI_SCLK (GPIO_NUM_1)

// Define I2C ports
#define I2C_PORT_NUM (I2C_NUM_0)
#define I2C_MASTER_SDA (GPIO_NUM_47)
#define I2C_MASTER_SCL (GPIO_NUM_48)

#define I2C1_PORT_NUM (I2C_NUM_1)
#define I2C1_MASTER_SDA (GPIO_NUM_7)
#define I2C1_MASTER_SCL (GPIO_NUM_6)

// Define QSPI for display
#define USE_LCD_SH8601 1  // Use SH8601 LCD module
#define LCD_QSPI_HOST            (SPI2_HOST)
// #define LCD_CS                   (GPIO_NUM_10)
// #define LCD_PCLK                 (GPIO_NUM_12) 
// #define LCD_DATA0                (GPIO_NUM_13)
// #define LCD_DATA1                (GPIO_NUM_11)
// #define LCD_DATA2                (GPIO_NUM_9)
// #define LCD_DATA3                (GPIO_NUM_14)
// #define LCD_RST                  (GPIO_NUM_17)
// #define LCD_TE_OUT               (GPIO_NUM_18)


#define LCD_CS            (GPIO_NUM_9)
#define LCD_PCLK          (GPIO_NUM_10) 
#define LCD_DATA0         (GPIO_NUM_11)
#define LCD_DATA1         (GPIO_NUM_12)
#define LCD_DATA2         (GPIO_NUM_13)
#define LCD_DATA3         (GPIO_NUM_14)
#define LCD_RST           (GPIO_NUM_21)


// Touchscreen
#define USE_TOUCH_FT3168 1 // Use FT3168 touch controller
#define TOUCHSCREEN_INT_PIN      (GPIO_NUM_15)
#define TOUCHSCREEN_RST_PIN      (GPIO_NUM_16)
#define I2C_ADDR_FT3168          0x38

// BNO085 Sensor
#define USE_BNO085 1
#define USE_BNO085_SPI 0
#define BNO085_INT_PIN           (GPIO_NUM_5)
#define BNO085_CS_PIN            (GPIO_NUM_6)
#define BNO085_PS0_WAKE_PIN      (GPIO_NUM_7)
// #define BNO085_BOOT_PIN          (GPIO_NUM_1)
#define BNO085_BOOT_PIN          (GPIO_NUM_15)
#define BNO085_RESET_PIN         (GPIO_NUM_8)

// Other hardware
#define BUZZER_OUT_PIN           (GPIO_NUM_21)
#define BUZZER_LEDC_DRIVER       (LEDC_TIMER_0)


// AXP2101 power management
#define USE_PMIC 0
#define XPOWERS_CHIP_AXP2101
#define I2C_ADDR_AXP2101         0x34
#define PMIC_AXP2101_INT_PIN     (GPIO_NUM_47)


#if USE_LCD_SH8601
    #if CONFIG_LV_COLOR_DEPTH == 32
        #define LCD_BIT_PER_PIXEL       (24)
    #elif CONFIG_LV_COLOR_DEPTH == 16
        #define LCD_BIT_PER_PIXEL       (16)
    #endif  // CONFIG_LV_COLOR_DEPTH

    #define DISP_H_RES_PIXEL (280)
    #define DISP_V_RES_PIXEL (456)

    #define DISP_PANEL_H_GAP (20) // Horizontal gap for display panel
    #define DISP_PANEL_V_GAP (0)  // Vertical gap for display panel

    #define DISP_ROTATION (0) // 0: 0 degrees, 1: 90 degrees, 2: 180 degrees, 3: 270 degrees

#endif  // USE_LCD_SH8601


// Other software configurations
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
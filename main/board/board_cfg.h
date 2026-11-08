#ifndef BOARD_CFG_H
#define BOARD_CFG_H

// The board: buses, pins, the panel and the chips on it (docs/technical_brief.md describes the same hardware).
// Software tuning lives in app_cfg.h, which includes this file.

#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// Define I2C ports
#define I2C0_PORT_NUM (I2C_NUM_0)
#define I2C0_MASTER_SDA (GPIO_NUM_40)
#define I2C0_MASTER_SCL (GPIO_NUM_39)

#define I2C1_PORT_NUM (I2C_NUM_1)
#define I2C1_MASTER_SDA (GPIO_NUM_7)
#define I2C1_MASTER_SCL (GPIO_NUM_6)

// Define QSPI for display
#define LCD_QSPI_HOST            (SPI2_HOST)
#define LCD_CS                   (GPIO_NUM_10)
#define LCD_PCLK                 (GPIO_NUM_12) 
#define LCD_DATA0                (GPIO_NUM_13)
#define LCD_DATA1                (GPIO_NUM_11)
#define LCD_DATA2                (GPIO_NUM_9)
#define LCD_DATA3                (GPIO_NUM_14)
#define LCD_RST                  (GPIO_NUM_17)
#define LCD_TE_OUT               (GPIO_NUM_18)
#define LCD_QSPI_MAX_TRANSFER_SIZE 4091

#if CONFIG_LV_COLOR_DEPTH == 32
    #define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
    #define LCD_BIT_PER_PIXEL       (16)
#endif  // CONFIG_LV_COLOR_DEPTH

#define DISP_H_RES_PIXEL (280)
#define DISP_V_RES_PIXEL (456)

// The pixel area's corners are rounded: R2.30 mm at 0.078 mm a pixel (DO0164FMST05 datasheet, page 6; ADR-006)
#define DISP_CORNER_RADIUS_PX (30)

#define DISP_PANEL_H_GAP (20) // Horizontal gap for display panel
#define DISP_PANEL_V_GAP (0)  // Vertical gap for display panel

#define DISP_ROTATION (0) // 0: 0 degrees, 1: 90 degrees, 2: 180 degrees, 3: 270 degrees



// Touchscreen
#define TOUCHSCREEN_INT_PIN      (GPIO_NUM_15)
#define TOUCHSCREEN_RST_PIN      (GPIO_NUM_16)
#define I2C_ADDR_FT3168          0x38

// BNO085 Sensor
#define BNO085_INT_PIN           (GPIO_NUM_5)
#define BNO085_BOOT_PIN          (GPIO_NUM_1)
#define BNO085_RESET_PIN         (GPIO_NUM_8)

//Buzzer
#define BUZZER_OUT_PIN           (GPIO_NUM_47)
#define BUZZER_TIMER             (LEDC_TIMER_0)
#define BUZZER_CHANNEL           (LEDC_CHANNEL_0)


// AXP2101 power management
#define XPOWERS_CHIP_AXP2101
#define PMIC_AXP2101_INT_PIN     (GPIO_NUM_21)

// Physical button
#define EXT_BUTTON_PIN            (GPIO_NUM_0)

#endif  // BOARD_CFG_H

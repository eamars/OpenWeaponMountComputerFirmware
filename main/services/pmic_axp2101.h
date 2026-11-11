#ifndef PMIC_AXP2101_H
#define PMIC_AXP2101_H

#include <stdint.h>

#include "esp_err.h"

#include "driver/i2c_master.h"  
#include "driver/gpio.h"


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "esp_event.h"

#ifndef AXP2101_I2C_SLAVE_ADDRESS
    #define AXP2101_I2C_SLAVE_ADDRESS 0x34 // Default I2C address for AXP2101
#endif  // AXP2101_I2C_SLAVE_ADDRESS

#ifndef AXP2101_I2C_WRITE_TIMEOUT_MS
    #define AXP2101_I2C_WRITE_TIMEOUT_MS 1000
#endif  // AXP2101_I2C_WRITE_TIMEOUT_MS


#ifndef AXP2101_MONITOR_TASK_PRIORITY
    #define AXP2101_MONITOR_TASK_PRIORITY 10
#endif // AXP2101_MONITOR_TASK_PRIORITY

#ifndef AXP2101_MONITOR_TASK_STACK
    #define AXP2101_MONITOR_TASK_STACK 4096
#endif // AXP2101_MONITOR_TASK_STACK

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    PMIC_INTERRUPT_EVENT_BIT = (1 << 0),
    PMIC_INIT_SUCCESS_EVENT_BIT = (1 << 1),
    PMIC_APPLY_CHARGE_SETTINGS_EVENT_BIT = (1 << 2),  // the monitor task writes the saved charge settings
    PMIC_POWER_OFF_EVENT_BIT = (1 << 3),  // the monitor task shuts the unit down
    PMIC_READ_CHARGER_REGS_EVENT_BIT = (1 << 4),  // the monitor task reads the charger registers
    PMIC_CHARGER_REGS_READ_EVENT_BIT = (1 << 5),  // set by the monitor task when it has read them
    PMIC_DIE_TEMP_ADC_ON_EVENT_BIT = (1 << 6),    // the monitor task turns the die temperature ADC channel on
    PMIC_DIE_TEMP_ADC_OFF_EVENT_BIT = (1 << 7),   // and off
} pmic_event_control_bit_e;


// The PMIC service (ADR-005 step 3): the monitor task owns the AXP2101 after init and is the only code that
// talks to it. It publishes its readings as events and never calls LVGL; the UI bridge shows them.
ESP_EVENT_DECLARE_BASE(PMIC_SERVICE_EVENT);

typedef enum {
    PMIC_SERVICE_EVENT_STATUS_CHANGED,      // a reading changed; read it with pmic_get_status(). No data
    PMIC_SERVICE_EVENT_POWER_KEY_LONG_PRESS,  // the power key was held: show the power menu. No data
} pmic_service_event_e;


typedef enum {
    PMIC_VBUS_CURRENT_LIMIT_100MA = 0,
    PMIC_VBUS_CURRENT_LIMIT_500MA,
    PMIC_VBUS_CURRENT_LIMIT_900MA,
    PMIC_VBUS_CURRENT_LIMIT_1000MA,
    PMIC_VBUS_CURRENT_LIMIT_1500MA,
    PMIC_VBUS_CURRENT_LIMIT_2000MA,
} pmic_vbus_current_limit_e;


typedef enum {
    PMIC_BATTERY_CHARGE_CURRENT_100MA = 0,
    PMIC_BATTERY_CHARGE_CURRENT_200MA,
    PMIC_BATTERY_CHARGE_CURRENT_300MA,
    PMIC_BATTERY_CHARGE_CURRENT_400MA,
    PMIC_BATTERY_CHARGE_CURRENT_500MA,
    PMIC_BATTERY_CHARGE_CURRENT_600MA,
    PMIC_BATTERY_CHARGE_CURRENT_700MA,
    PMIC_BATTERY_CHARGE_CURRENT_800MA,
    PMIC_BATTERY_CHARGE_CURRENT_900MA,
    PMIC_BATTERY_CHARGE_CURRENT_1000MA,
} pmic_battery_charge_current_e;


typedef enum {
    PMIC_BATTERY_CHARGE_VOLTAGE_4V = 0,
    PMIC_BATTERY_CHARGE_VOLTAGE_4V1,
    PMIC_BATTERY_CHARGE_VOLTAGE_4V2,
    PMIC_BATTERY_CHARGE_VOLTAGE_4V35,
    PMIC_BATTERY_CHARGE_VOLTAGE_4V4,
} pmic_battery_charge_voltage_e;


typedef enum {
    IDLE_TIMEOUT_NEVER = 0,
    IDLE_TIMEOUT_1_MIN,
    IDLE_TIMEOUT_5_MIN,
    IDLE_TIMEOUT_10_MIN, 
    IDLE_TIMEOUT_10_SEC,  // debug
} idle_timeout_t;


typedef enum {
    SLEEP_TIMEOUT_NEVER = 0,
    SLEEP_TIMEOUT_1_HRS,
    SLEEP_TIMEOUT_2_HRS,
    SLEEP_TIMEOUT_5_HRS,
    SLEEP_TIMEOUT_1_MIN,  // debug
} sleep_timeout_t;


typedef struct {
    pmic_vbus_current_limit_e vbus_current_limit;
    pmic_battery_charge_current_e battery_charge_current;
    pmic_battery_charge_voltage_e battery_charge_voltage;
    idle_timeout_t idle_timeout;
    sleep_timeout_t sleep_timeout;

} power_management_config_t;

typedef struct {
    bool valid;  // false until the monitor task's first reading, and on a unit without a PMIC

    int charge_status;
    int battery_percentage;
    uint16_t ts_raw;        // TS ADC, 0.5 mV a step (ADR-009)
    float ts_temperature;   // degC from TH1 (the board), NAN when out of range
    
    uint16_t vbus_voltage_mv;
    uint16_t vbatt_voltage_mv;
    uint16_t vsys_voltage_mv;

    bool is_usb_connected;

} axp2101_status_t;


// The charger's registers as the chip holds them (datasheet 6.13.2), for diagnostics
typedef struct {
    uint8_t status1;            // 0x00 PMU status 1
    uint8_t status2;            // 0x01 PMU status 2
    uint8_t die_temp_ctrl;      // 0x13 die temperature control
    uint8_t vsys_dpm;           // 0x14 linear charger Vsys voltage DPM
    uint8_t vindpm;             // 0x15 input voltage limit
    uint8_t input_current;      // 0x16 input current limit
    uint8_t charger_ctrl;       // 0x18 charger, fuel gauge, watchdog on/off
    uint8_t adc_ctrl;           // 0x30 ADC channel enable
    uint8_t precharge;          // 0x61 precharge current
    uint8_t charge_current;     // 0x62 constant current charge current
    uint8_t termination;        // 0x63 termination current and its enable
    uint8_t charge_voltage;     // 0x64 constant voltage charge voltage
    uint8_t thermal_regulation; // 0x65 thermal regulation threshold
    uint8_t charge_timer;       // 0x67 charger safety timers
    uint8_t battery_detect;     // 0x68 battery detection
    uint16_t tdie_raw;          // 0x3C-0x3D, 14 bits; meaningful only with 0x30 bit 4 set
} pmic_charger_regs_t;


typedef struct {
    EventGroupHandle_t pmic_event_control;
    i2c_master_dev_handle_t dev_handle;
    TaskHandle_t pmic_monitor_task_handle;
    axp2101_status_t status;

    gpio_num_t interrupt_pin;
} axp2101_ctx_t;


esp_err_t axp2101_init(axp2101_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin);
esp_err_t axp2101_deinit(axp2101_ctx_t *ctx);

/** @brief A copy of the newest readings; callable from any task. */
axp2101_status_t pmic_get_status();

/**
 * @brief Have the PMIC monitor task write the VBUS current limit, charge current and charge voltage from
 * power_management_config to the chip. Returns at once; callable from any task (ADR-005 B15).
 */
esp_err_t axp2101_request_apply_charge_settings(axp2101_ctx_t *ctx);

esp_err_t save_pmic_config();
esp_err_t load_pmic_config();

/**
 * @brief Have the PMIC monitor task shut the unit down. Returns at once; callable from any task. Returns
 * ESP_ERR_INVALID_STATE on a unit without a PMIC.
 */
esp_err_t pmic_request_power_off();

/**
 * @brief Have the PMIC monitor task read the charger registers, and wait for them. Reads only. One caller at a
 * time. Returns ESP_ERR_INVALID_STATE on a unit without a PMIC, ESP_ERR_TIMEOUT if the task did not answer in
 * timeout_ms, ESP_FAIL if a read failed.
 */
esp_err_t pmic_read_charger_regs(pmic_charger_regs_t *regs, uint32_t timeout_ms);

/**
 * @brief Have the PMIC monitor task turn the die temperature ADC channel (reg 0x30 bit 4) on or off. Not saved; the
 * chip keeps it until it loses power, and start-up does not change it. Returns at once.
 */
esp_err_t pmic_request_die_temp_adc(bool on);

uint32_t idle_timeout_to_secs(idle_timeout_t timeout);
uint32_t sleep_timeout_to_secs(sleep_timeout_t timeout);


extern const power_management_config_t default_power_management_config;
extern power_management_config_t power_management_config;


#ifdef __cplusplus
}
#endif

#endif  // PMIC_AXP2101_H
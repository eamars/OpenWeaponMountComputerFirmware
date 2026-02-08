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

#include "lvgl.h"

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
} pmic_event_control_bit_e;


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



typedef struct {
    uint32_t crc32;
    pmic_vbus_current_limit_e vbus_current_limit;
    pmic_battery_charge_current_e battery_charge_current;
    pmic_battery_charge_voltage_e battery_charge_voltage;

} power_management_config_t;

typedef struct {
    int charge_status;
    int battery_percentage;
    float ts_temperature;
    
    uint16_t vbus_voltage_mv;
    uint16_t vbatt_voltage_mv;
    uint16_t vsys_voltage_mv;

    bool is_usb_connected;

} axp2101_status_t;


typedef struct {
    EventGroupHandle_t pmic_event_control;
    i2c_master_dev_handle_t dev_handle;
    TaskHandle_t pmic_monitor_task_handle;
    axp2101_status_t status;

    gpio_num_t interrupt_pin;
} axp2101_ctx_t;


esp_err_t axp2101_init(axp2101_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin);
esp_err_t axp2101_deinit(axp2101_ctx_t *ctx);

esp_err_t save_pmic_config();
esp_err_t load_pmic_config();

void pmic_power_off();


lv_obj_t * create_power_management_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page);
void power_management_view_update_status(axp2101_ctx_t *ctx);

extern const power_management_config_t default_power_management_config_t;
extern power_management_config_t power_management_config;


#ifdef __cplusplus
}
#endif

#endif  // PMIC_AXP2101_H
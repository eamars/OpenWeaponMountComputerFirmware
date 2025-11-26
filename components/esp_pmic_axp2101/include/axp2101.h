#ifndef AXP2101_H
#define AXP2101_H


#include "esp_err.h"

#include "driver/i2c_master.h"  
#include "driver/gpio.h"


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

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
    #define AXP2101_MONITOR_TASK_STACK 2048
#endif // AXP2101_MONITOR_TASK_STACK

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    EventGroupHandle_t pmic_event_control;
    i2c_master_dev_handle_t dev_handle;
    TaskHandle_t pmic_monitor_task_handle;

    gpio_num_t interrupt_pin;
} axp2101_ctx_t;

esp_err_t axp2101_init(axp2101_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin);

#ifdef __cplusplus
}
#endif

#endif  // AXP2101_H
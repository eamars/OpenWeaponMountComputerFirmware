#ifndef BNO085_H
#define BNO085_H

#include <math.h>

#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/i2c_master.h"  
#include "driver/uart.h"
#include "sh2.h"
#include "sh2_SensorValue.h"

#define BNO085_SENSOR_POLLER_TASK_PRIORITY 2  // low priority for high frequency poller
#define BNO085_SENSOR_POLLER_TASK_STACK 4096
// #define BNO085_SENSOR_POLLER_PERIOD_MS 10

#define DEG_TO_RAD(deg) ((deg) * M_PI / 180.0f)
#define RAD_TO_DEG(rad) ((rad) * 180.0f / M_PI)


typedef struct {
    sh2_SensorId_t sensor_id;
    uint32_t interval_ms;
    QueueHandle_t sensor_value_queue;
} sensor_config_t;


typedef struct {
    sh2_Hal_t _HAL; // SH2 HAL interface -> Align the memory with the context structure allowing better type casting
    TaskHandle_t sensor_poller_task_handle;
    sensor_config_t enabled_sensor_report_list[SH2_MAX_SENSOR_EVENT_LEN];

    // Implement specific atributes
    i2c_master_dev_handle_t dev_handle;
    int reset_pin;
    int interrupt_pin;
    
} bno085_ctx_t;


/**
 * @brief Initialize the BNO085 sensor.
 *
 * @param ctx Pointer to the BNO085 context.
 * @param i2c_bus_handle I2C bus handle for communication.
 * @param interrupt_pin GPIO pin for the interrupt.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_init_i2c(bno085_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, int interrupt_pin);

/**
 * @brief Enable BNO085 report.
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_enable_game_rotation_vector_report(bno085_ctx_t *ctx, uint32_t interval_ms);

/**
 * @brief Enable linear acceleration report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_enable_linear_acceleration_report(bno085_ctx_t *ctx, uint32_t interval_ms);


/**
 * @brief Wait for BNO085 game rotation vector roll and pitch values.
 *
 * @param ctx Pointer to the BNO085 context.
 * @param roll Pointer to store the roll value.
 * @param pitch Pointer to store the pitch value.
 * @param block_wait Whether to block wait for the values.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_wait_for_game_rotation_vector_roll_pitch(bno085_ctx_t *ctx, float *roll, float *pitch, bool block_wait);

/**
 * @brief Wait for linear acceleration report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_wait_for_linear_acceleration_report(bno085_ctx_t *ctx, float *x, float *y, float *z, bool block_wait);


#endif // BNO085_H
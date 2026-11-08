#ifndef BNO085_H
#define BNO085_H

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"  
#include "driver/spi_master.h"

#include "sh2.h"
#include "sh2_SensorValue.h"

#ifndef BNO085_SENSOR_POLLER_TASK_PRIORITY
    #define BNO085_SENSOR_POLLER_TASK_PRIORITY 8  // Higher priority for interrupt driven task
#endif  // BNO085_SENSOR_POLLER_TASK_PRIORITY

#ifndef BNO085_SENSOR_POLLER_TASK_STACK
    #define BNO085_SENSOR_POLLER_TASK_STACK 4096
#endif
// #define BNO085_SENSOR_POLLER_PERIOD_MS 10

#ifndef BNO085_EVENT_QUEUE_DEPTH
    #define BNO085_EVENT_QUEUE_DEPTH 1
#endif  // BNO085_EVENT_QUEUE_DEPTH


#ifndef BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
    #define BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN 0
#endif  // BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN


#define DEG_TO_RAD(deg) ((deg) * M_PI / 180.0f)
#define RAD_TO_DEG(rad) ((rad) * 180.0f / M_PI)

// One slot per sensor ID (0 .. SH2_MAX_SENSOR_ID); the sh2 decoder rejects any other ID before the table is used
#define BNO085_REPORT_TABLE_LEN (SH2_MAX_SENSOR_ID + 1)


typedef struct {
    float real;
    float i;
    float j;
    float k;
} quaternion_t;


typedef struct {
    sh2_SensorConfig_t config;
    QueueHandle_t sensor_value_queue;
} sensor_report_config_t;


typedef struct {
    sh2_Hal_t _HAL; // SH2 HAL interface -> Align the memory with the context structure allowing better type casting
    TaskHandle_t sensor_poller_task_handle;
    sensor_report_config_t enabled_sensor_report_list[BNO085_REPORT_TABLE_LEN];
    EventGroupHandle_t sensor_event_control;
    // The sh2 library keeps one shared, single-threaded state: every sh2_*() call holds this lock.
    // Recursive, because sh2 callbacks run inside sh2_service() and may call into sh2 again.
    SemaphoreHandle_t sh2_lock;

    gpio_num_t interrupt_pin;
    gpio_num_t reset_pin;
    gpio_num_t boot_pin;
    gpio_num_t ps0_wake_pin;

    // Wake profile (ADR-009): when the last bno085_wake_up() started, how long its command took, and per sensor
    // the first report after it and the reports in its first 100 ms. Written by the poller task, under sh2_lock
    int64_t wake_us;
    uint32_t wake_cmd_us;
    int64_t wake_first_report_us[BNO085_REPORT_TABLE_LEN];
    uint16_t wake_burst_reports[BNO085_REPORT_TABLE_LEN];

    // Resets the hub announced (SH2_RESET): each restarts its sensors, which drops state built from their reports
    volatile uint32_t reset_count;

    // The newest game rotation vector (real, i, j, k) and how many have arrived; written by the sensor callback,
    // read under sh2_lock (ADR-011)
    float grv_quat[4];
    uint32_t grv_count;
} bno085_ctx_t;


typedef struct {
    bno085_ctx_t parent;

    // I2C specific attributes
    i2c_master_dev_handle_t dev_handle;
} bno085_i2c_ctx_t;


typedef struct {
    bno085_ctx_t parent;

    // SPI specific attributes
    spi_device_handle_t dev_handle;
    gpio_num_t spi_cs_pin;
} bno085_spi_ctx_t;


/**
 * @brief Initialize the BNO085 sensor using I2C communication
 *
 * @param ctx Pointer to the BNO085 context.
 * @param i2c_bus_handle I2C bus handle for communication.
 * @param interrupt_pin GPIO pin for the interrupt.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_init_i2c(bno085_i2c_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin, gpio_num_t reset_pin, gpio_num_t boot_pin);


/**
 * @brief Initialize the BNO085 sensor using SPI communication
 */
esp_err_t bno085_init_spi(bno085_spi_ctx_t *ctx, spi_host_device_t spi_host, gpio_num_t spi_cs_pin, gpio_num_t interrupt_pin, gpio_num_t reset_pin, gpio_num_t boot_pin, gpio_num_t ps0_wake_pin);


/**
 * @brief Put sensor in sleep state
 */
esp_err_t bno085_enter_sleep(bno085_ctx_t *ctx);

/** 
 * @brief Wake up the sensor from sleep state
 */
esp_err_t bno085_wake_up(bno085_ctx_t *ctx);

/**
 * @brief The last wake's profile for one sensor (ADR-009): microseconds from the wake command to the sensor's first
 * report after it (0 if none yet), how many of its reports came within 100 ms of the command, and how long the
 * command itself took
 */
void bno085_get_wake_profile(bno085_ctx_t *ctx, sh2_SensorId_t sensor_id, uint32_t *first_report_us,
                             uint32_t *burst_reports, uint32_t *wake_cmd_us);

/**
 * @brief How many resets the hub has announced since init. A reset restarts every sensor without a report, so
 * state built from a sensor's earlier reports (for example the stability detector's still or moving) no longer holds
 */
uint32_t bno085_get_reset_count(bno085_ctx_t *ctx);

/**
 * @brief The newest game rotation vector as a quaternion (real, i, j, k) and how many have arrived since init
 * (ADR-011). Returns 0 before the first report
 */
uint32_t bno085_get_game_rotation_quaternion(bno085_ctx_t *ctx, float quat[4]);

/**
 * @brief Clear any tare: the run-time reorientation, and the persisted one (the master sensor orientation record,
 * FRS 0x2D3E, SH-2 6.4.3.3) (ADR-011)
 */
esp_err_t bno085_clear_tare(bno085_ctx_t *ctx);

/**
 * @brief If a tare is persisted (the master sensor orientation record holds data), clear it as bno085_clear_tare
 * does; *cleared says whether there was one. Nothing is written when there is none (ADR-011)
 */
esp_err_t bno085_clear_persisted_tare(bno085_ctx_t *ctx, bool *cleared);

/**
 * @brief Set whether a report keeps running while the hub sleeps (always-on), and send that to the sensor if the
 * report is on. Kept for the sensor's reset recovery; the report's enable function sets it back to its default
 */
esp_err_t bno085_set_report_always_on(bno085_ctx_t *ctx, sh2_SensorId_t sensor_id, bool always_on);

/**
 * @brief Pulse the reset line while running, so the sensor restarts as after an unexpected reset.
 * For tests of the reset recovery; returns ESP_ERR_NOT_SUPPORTED without a reset pin.
 */
esp_err_t bno085_hardware_reset(bno085_ctx_t *ctx);

/**
 * @brief Tare the given axes now (sh2_setTareNow)
 */
esp_err_t bno085_tare_now(bno085_ctx_t *ctx, uint8_t axes, sh2_TareBasis_t basis);

/**
 * @brief Persist the current tare in the sensor (sh2_persistTare)
 */
esp_err_t bno085_persist_tare(bno085_ctx_t *ctx);


/**
 * @brief Enable Game Rotation Vector Report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report. Setting to 0 to disable.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_enable_game_rotation_vector_report(bno085_ctx_t *ctx, uint32_t interval_ms);

/**
 * @brief Enable linear acceleration report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report. Setting to 0 to disable.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_enable_linear_acceleration_report(bno085_ctx_t *ctx, uint32_t interval_ms);

/**
 * @brief Enable Rotation Vector Report
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report. Setting to 0 to disable.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_enable_rotation_vector_report(bno085_ctx_t *ctx, uint32_t interval_ms);


/** 
 * @brief Enable stability detection and classification report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report. Setting to 0 to disable.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_enable_stability_classification_report(bno085_ctx_t *ctx, uint32_t interval_ms);


/** 
 * @brief Wait for significatn motion event report
 */

esp_err_t bno085_enable_stability_detector_report(bno085_ctx_t *ctx, uint32_t interval_ms);

/**
 * @brief Wait for BNO085 game rotation vector roll and pitch values.
 *
 * @param ctx Pointer to the BNO085 context.
 * @param roll Pointer to store the roll value.
 * @param pitch Pointer to store the pitch value.
 * @param block_wait Whether to block wait for the values.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_wait_for_game_rotation_vector_roll_pitch_yaw(bno085_ctx_t *ctx, float *roll, float *pitch, float *yaw, bool block_wait);

/** @brief As bno085_wait_for_game_rotation_vector_roll_pitch_yaw(), waiting at most `wait_ticks` for the report */
esp_err_t bno085_wait_for_game_rotation_vector_roll_pitch_yaw_ticks(bno085_ctx_t *ctx, float *roll, float *pitch,
                                                                    float *yaw, TickType_t wait_ticks);

/**
 * @brief Wait for linear acceleration report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_wait_for_linear_acceleration_report(bno085_ctx_t *ctx, float *x, float *y, float *z, bool block_wait);


/**
 * @brief Wait for stability classification report
 *
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_wait_for_stability_classification_report(bno085_ctx_t *ctx, uint8_t * classification, bool block_wait);


/**
 * @brief Wait for rotation vector roll pitch pan
 * @param ctx Pointer to the BNO085 context.
 * @param interval_ms Interval in milliseconds for the report.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t bno085_wait_for_rotation_vector_roll_pitch_yaw(bno085_ctx_t * ctx, float *roll, float *pitch, float *yaw, float *accuracy, bool block_wait);


/** 
 * @brief Wait for stability detector report
 * @param ctx Pointer to the BNO085 context.
 */
esp_err_t bno085_wait_for_stability_detector_report(bno085_ctx_t *ctx, uint16_t *stability, bool block_wait);


#endif // BNO085_H
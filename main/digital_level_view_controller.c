#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"

#include "digital_level_view_controller.h"
#include "digital_level_view.h"
#include "app_cfg.h"
#include "bno085.h"
#include "system_config.h"
#include "common.h"

#define TAG "DigitalLevelViewController"

static TaskHandle_t sensor_rotation_vector_poller_task_handle;
static TaskHandle_t sensor_acceleration_poller_task_handle;
static SemaphoreHandle_t sensor_rotation_vector_poller_task_control;
static SemaphoreHandle_t sensor_acceleration_poller_task_control;

float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;
float sensor_x_acceleration_thread_unsafe, sensor_y_acceleration_thread_unsafe, sensor_z_acceleration_thread_unsafe;

extern bno085_ctx_t bno085_dev;
extern system_config_t system_config;
extern digital_level_view_config_t digital_level_view_config;



static void sensor_rotation_vector_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        // Block wait for the task is allowed to run
        xSemaphoreTake(sensor_rotation_vector_poller_task_control, portMAX_DELAY);

        // Wait for data
        bno085_wait_for_game_rotation_vector_roll_pitch(&bno085_dev, &sensor_roll_thread_unsafe, &sensor_pitch_thread_unsafe, true);

        // Redraw the screen
        if (lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {  // prevent a deadlock if the LVGL event wants to continue
            float display_roll = wrap_angle(sensor_roll_thread_unsafe + digital_level_view_config.user_roll_rad_offset);
            update_digital_level_view(display_roll, sensor_pitch_thread_unsafe);
            lvgl_port_unlock();
        }

        xSemaphoreGive(sensor_rotation_vector_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
    }
}


static void sensor_acceleration_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        // Block wait for the task is allowed to run
        xSemaphoreTake(sensor_acceleration_poller_task_control, portMAX_DELAY);

        // Wait for data
        bno085_wait_for_linear_acceleration_report(&bno085_dev, 
            &sensor_x_acceleration_thread_unsafe, &sensor_y_acceleration_thread_unsafe, &sensor_z_acceleration_thread_unsafe, 
            true);

        xSemaphoreGive(sensor_acceleration_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
    }
}


void enable_digital_level_view_controller(bool enable) {
    if (enable) {
        xSemaphoreGive(sensor_rotation_vector_poller_task_control);
        xSemaphoreGive(sensor_acceleration_poller_task_control);
    } else {;
        xSemaphoreTake(sensor_rotation_vector_poller_task_control, portMAX_DELAY);
        xSemaphoreTake(sensor_acceleration_poller_task_control, portMAX_DELAY);
    }
}


esp_err_t digital_level_view_controller_init() {
    // Initialize the digital level view controller


    // Create sensor rotation vector poller task 
    sensor_rotation_vector_poller_task_control = xSemaphoreCreateBinary();
    BaseType_t rtos_return = xTaskCreate(
        sensor_rotation_vector_poller_task, 
        "dlv_poller", 
        SENSOR_EVENT_POLLER_TASK_STACK,
        NULL,
        SENSOR_EVENT_POLLER_TASK_PRIORITY,
        &sensor_rotation_vector_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_poller");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Create acceleration poller task
    sensor_acceleration_poller_task_control = xSemaphoreCreateBinary();
    rtos_return = xTaskCreate(
        sensor_acceleration_poller_task,
        "accel_poller",
        SENSOR_EVENT_POLLER_TASK_STACK,
        NULL,
        ACCELERATION_EVENT_POLLER_TASK_PRIORITY,
        &sensor_acceleration_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_accel_poller");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    return ESP_OK;
}

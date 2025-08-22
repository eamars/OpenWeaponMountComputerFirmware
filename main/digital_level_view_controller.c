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
#include "sensor_config.h"
#include "countdown_timer.h"

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
extern sensor_config_t sensor_config;
extern countdown_timer_t countdown_timer;


static void sensor_rotation_vector_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        // Block wait for the task is allowed to run
        xSemaphoreTake(sensor_rotation_vector_poller_task_control, portMAX_DELAY);

        // Wait for data
        esp_err_t err = bno085_wait_for_game_rotation_vector_roll_pitch(&bno085_dev, &sensor_roll_thread_unsafe, &sensor_pitch_thread_unsafe, true);

        if (err == ESP_OK) {
            // Redraw the screen
            if (lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {  // prevent a deadlock if the LVGL event wants to continue
                float display_roll = wrap_angle(sensor_roll_thread_unsafe + digital_level_view_config.user_roll_rad_offset);
                update_digital_level_view(display_roll, sensor_pitch_thread_unsafe);
                lvgl_port_unlock();
            }
        }

        xSemaphoreGive(sensor_rotation_vector_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
    }
}


static void sensor_acceleration_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    float last_value = 0;

    while (1) {
        // Block wait for the task is allowed to run
        xSemaphoreTake(sensor_acceleration_poller_task_control, portMAX_DELAY);

        // Wait for data
        esp_err_t err = bno085_wait_for_linear_acceleration_report(&bno085_dev, 
            &sensor_x_acceleration_thread_unsafe, &sensor_y_acceleration_thread_unsafe, &sensor_z_acceleration_thread_unsafe, 
            false);

        if (err == ESP_OK) {
            // ESP_LOGI(TAG, "Digital Level View Controller Analysis: x=%.2f, y=%.2f, z=%.2f", sensor_x_acceleration_thread_unsafe, sensor_y_acceleration_thread_unsafe, sensor_z_acceleration_thread_unsafe);
            float x_abs = fabsf(sensor_x_acceleration_thread_unsafe);
            last_value = sensor_x_acceleration_thread_unsafe;

            if (last_value < sensor_config.recoil_acceleration_trigger_level && 
                x_abs >= sensor_config.recoil_acceleration_trigger_level && 
                sensor_config.trigger_edge == TRIGGER_RISING_EDGE
            ) {

                // ESP_LOGI(TAG, "Recoil detected, auto_start_countdown_timer_on_recoil: %d, get_countdown_timer_widget_enabled: %d, get_countdown_timer_state: %d",
                //          digital_level_view_config.auto_start_countdown_timer_on_recoil,
                //          get_countdown_timer_widget_enabled(),
                //          get_countdown_timer_state(&countdown_timer));
                // Shot has fired, start the timer if not started already
                if (digital_level_view_config.auto_start_countdown_timer_on_recoil &&     // recoil detected
                    get_countdown_timer_widget_enabled() &&                               // widget is enabled
                    (get_countdown_timer_state(&countdown_timer) == COUNTDOWN_TIMER_PAUSE)  // timer is ready
                ) {
                    countdown_timer_continue(&countdown_timer);
                    ESP_LOGI(TAG, "Countdown timer started due to recoil");
                }
            }
        }

        xSemaphoreGive(sensor_acceleration_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
    }
}


void enable_digital_level_view_controller(bool enable) {
    if (enable) {
        xSemaphoreGive(sensor_rotation_vector_poller_task_control);
        xSemaphoreGive(sensor_acceleration_poller_task_control);
    } else {
        xSemaphoreTake(sensor_rotation_vector_poller_task_control, pdMS_TO_TICKS(200));
        xSemaphoreTake(sensor_acceleration_poller_task_control, pdMS_TO_TICKS(200));
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

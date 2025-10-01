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
#include "esp_task_wdt.h"
#include "bno085.h"

#define TAG "DigitalLevelViewController"

#define SENSOR_POLL_EVENT_RUN   (1 << 0)


static TaskHandle_t sensor_poller_task_handle;
static EventGroupHandle_t sensor_task_control;

float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;
float sensor_x_acceleration_thread_unsafe, sensor_y_acceleration_thread_unsafe, sensor_z_acceleration_thread_unsafe;

extern bno085_ctx_t * bno085_dev;
extern system_config_t system_config;
extern digital_level_view_config_t digital_level_view_config;
extern sensor_config_t sensor_config;
extern countdown_timer_t countdown_timer;


float get_relative_roll_angle_rad_thread_unsafe() {
    float raw_roll = sensor_roll_thread_unsafe - system_config.rotation * M_PI_2 + digital_level_view_config.user_roll_rad_offset;
    return wrap_angle(raw_roll);
}


void unified_ensor_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    TickType_t last_poll_tick;
    float last_sensor_x_acceleration = 0;

    while (1) {
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        last_poll_tick = xTaskGetTickCount();
        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {
            // Wait for watched sensor ids
            // Game rotation vectors
            if (bno085_wait_for_game_rotation_vector_roll_pitch(bno085_dev, &sensor_roll_thread_unsafe, &sensor_pitch_thread_unsafe, false) == ESP_OK) {
                // Roll is calculated based on the base measurement - screen rotation offset + user roll offset
                float display_roll = get_relative_roll_angle_rad_thread_unsafe();

                // Redraw the screen
                if (lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {  // prevent a deadlock if the LVGL event wants to continue
                    update_digital_level_view(display_roll, sensor_pitch_thread_unsafe);
                    // ESP_LOGI(TAG, "Screen upated");
                    lvgl_port_unlock();
                }
            }

            // Linear acceleration
            if (bno085_wait_for_linear_acceleration_report(bno085_dev, &sensor_x_acceleration_thread_unsafe, &sensor_y_acceleration_thread_unsafe, &sensor_z_acceleration_thread_unsafe, false) == ESP_OK) {
                // ESP_LOGI(TAG, "Digital Level View Controller Analysis: x=%.2f, y=%.2f, z=%.2f", sensor_x_acceleration_thread_unsafe, sensor_y_acceleration_thread_unsafe, sensor_z_acceleration_thread_unsafe);
                float x_abs = fabsf(sensor_x_acceleration_thread_unsafe);
                last_sensor_x_acceleration = sensor_x_acceleration_thread_unsafe;

                if (last_sensor_x_acceleration < sensor_config.recoil_acceleration_trigger_level && 
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

            vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS));
        }
    }
}


void enable_digital_level_view_controller(bool enable) {
    if (enable) {
        // Enable sensor report
        if (sensor_config.enable_game_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS));
        }
        if (sensor_config.enable_linear_acceleration_report) {
            ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS));
        }

        xEventGroupSetBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);
    } else {
        // Disable sensor report
        if (sensor_config.enable_game_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
        }
        if (sensor_config.enable_linear_acceleration_report) {
            ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, 0));
        }
        xEventGroupClearBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);
    }
}


esp_err_t digital_level_view_controller_init() {
    sensor_task_control = xEventGroupCreate();
    if (sensor_task_control == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor_task_control");
        return ESP_FAIL;
    }

    BaseType_t rtos_return;
    // Create acceleration poller task
    rtos_return = xTaskCreate(
        unified_ensor_poller_task,
        "sensor_poller",
        SENSOR_EVENT_POLLER_TASK_STACK,
        NULL,
        SENSOR_EVENT_POLLER_TASK_PRIORITY,
        &sensor_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for unified_ensor_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Initialize sensor reports
    if (sensor_config.enable_game_rotation_vector_report) {
        ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS));
    }
    else {
        ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
    }
    if (sensor_config.enable_linear_acceleration_report) {
        ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS));
    }
    else {
        ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, 0));
    }

    return ESP_OK;
}

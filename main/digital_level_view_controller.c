#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <inttypes.h>

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


#if DISPLAY_PROFILING_ENABLED
typedef struct {
    uint64_t report_start_us;
    uint64_t update_us_total;
    uint32_t poll_loops;
    uint32_t game_reports;
    uint32_t accel_reports;
    uint32_t ui_update_attempts;
    uint32_t ui_update_success;
    uint32_t ui_lock_fail;
    uint32_t update_us_max;
} digital_level_profile_t;

static digital_level_profile_t digital_level_profile;

static uint64_t digital_level_profile_now_us() {
    return (uint64_t) esp_timer_get_time();
}

static void digital_level_profile_reset_window() {
    digital_level_profile.poll_loops = 0;
    digital_level_profile.game_reports = 0;
    digital_level_profile.accel_reports = 0;
    digital_level_profile.ui_update_attempts = 0;
    digital_level_profile.ui_update_success = 0;
    digital_level_profile.ui_lock_fail = 0;
    digital_level_profile.update_us_total = 0;
    digital_level_profile.update_us_max = 0;
    digital_level_profile.report_start_us = digital_level_profile_now_us();
}

static void digital_level_profile_report_if_due() {
    uint64_t now_us = digital_level_profile_now_us();
    uint32_t window_ms = (uint32_t) ((now_us - digital_level_profile.report_start_us) / 1000);

    if (window_ms < DISPLAY_PROFILING_REPORT_PERIOD_MS) {
        return;
    }

    uint32_t update_avg_us = digital_level_profile.ui_update_success ?
        (uint32_t) (digital_level_profile.update_us_total / digital_level_profile.ui_update_success) : 0;

    ESP_LOGI(TAG,
        "digital_level_prof: window=%" PRIu32 "ms loops=%" PRIu32 " game=%" PRIu32
        " accel=%" PRIu32 " ui_try=%" PRIu32 " ui_ok=%" PRIu32 " lock_fail=%" PRIu32
        " avg/max_update=%" PRIu32 "/%" PRIu32 "us",
        window_ms,
        digital_level_profile.poll_loops,
        digital_level_profile.game_reports,
        digital_level_profile.accel_reports,
        digital_level_profile.ui_update_attempts,
        digital_level_profile.ui_update_success,
        digital_level_profile.ui_lock_fail,
        update_avg_us,
        digital_level_profile.update_us_max);

    digital_level_profile_reset_window();
}

static void digital_level_profile_record_update(uint64_t start_us) {
    uint64_t elapsed_us = digital_level_profile_now_us() - start_us;

    digital_level_profile.update_us_total += elapsed_us;
    if (elapsed_us > digital_level_profile.update_us_max) {
        digital_level_profile.update_us_max = (uint32_t) elapsed_us;
    }
}
#endif  // DISPLAY_PROFILING_ENABLED


float get_relative_roll_angle_rad_thread_unsafe() {
    float raw_roll = sensor_roll_thread_unsafe - system_config.rotation * M_PI_2 + digital_level_view_config.user_roll_rad_offset;
    return wrap_angle(raw_roll);
}


void unified_sensor_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    TickType_t last_poll_tick;
    float last_sensor_x_acceleration = 0;

#if DISPLAY_PROFILING_ENABLED
    digital_level_profile_reset_window();
#endif

    while (1) {
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        last_poll_tick = xTaskGetTickCount();
        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {
#if DISPLAY_PROFILING_ENABLED
            digital_level_profile.poll_loops++;
#endif
            // Wait for watched sensor ids
            // Game rotation vectors
            if (bno085_wait_for_game_rotation_vector_roll_pitch_yaw(bno085_dev, &sensor_roll_thread_unsafe, &sensor_pitch_thread_unsafe, NULL, false) == ESP_OK) {
#if DISPLAY_PROFILING_ENABLED
                digital_level_profile.game_reports++;
                digital_level_profile.ui_update_attempts++;
                uint64_t update_start_us = digital_level_profile_now_us();
#endif
                // Roll is calculated based on the base measurement - screen rotation offset + user roll offset
                float display_roll = get_relative_roll_angle_rad_thread_unsafe();

                // Redraw the screen
                if (lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {  // prevent a deadlock if the LVGL event wants to continue
                    update_digital_level_view(display_roll, sensor_pitch_thread_unsafe);
                    // ESP_LOGI(TAG, "Screen upated");
                    lvgl_port_unlock();
#if DISPLAY_PROFILING_ENABLED
                    digital_level_profile.ui_update_success++;
                    digital_level_profile_record_update(update_start_us);
#endif
                }
#if DISPLAY_PROFILING_ENABLED
                else {
                    digital_level_profile.ui_lock_fail++;
                }
#endif
            }

            // Linear acceleration
            if (bno085_wait_for_linear_acceleration_report(bno085_dev, &sensor_x_acceleration_thread_unsafe, &sensor_y_acceleration_thread_unsafe, &sensor_z_acceleration_thread_unsafe, false) == ESP_OK) {
#if DISPLAY_PROFILING_ENABLED
                digital_level_profile.accel_reports++;
#endif
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

#if DISPLAY_PROFILING_ENABLED
            digital_level_profile_report_if_due();
#endif
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
        unified_sensor_poller_task,
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

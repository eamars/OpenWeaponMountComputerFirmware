#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <inttypes.h>
#include <math.h>

#include "digital_level_view_controller.h"
#include "sensor_frame.h"
#include "digital_level_view.h"
#include "timer_view.h"
#include "app_cfg.h"
#include "bno085.h"
#include "system_config.h"
#include "common.h"
#include "sensor_config.h"
#include "countdown_timer.h"
#include "dope_view.h"
#include "level_band.h"
#include "esp_task_wdt.h"
#include "bno085.h"
#include "app.h"

#define TAG "DigitalLevelViewController"

#define SENSOR_POLL_EVENT_RUN   (1 << 0)
// Longest wait for a rotation report before the loop checks whether it should still run
#define LEVEL_REPORT_WAIT_MS    500


static TaskHandle_t sensor_poller_task_handle;
static EventGroupHandle_t sensor_task_control;

float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;

// After a wake the BNO085's fusion restarts up to about 1 deg off and takes seconds to converge (ADR-014). For the
// first seconds after a wake, the shown cant and pitch are corrected toward the accelerometer's, which is right at
// once while the unit is still: the correction starts from the average of the first still samples, follows with a
// short time constant, and fades out by the end of the window, handing back to the fusion without a step
#define LEVEL_WAKE_FIX_MS 8000
#define LEVEL_WAKE_FIX_FADE_MS 2000
#define LEVEL_WAKE_FIX_TAU_S 0.3f
#define LEVEL_WAKE_FIX_FIRST_SAMPLES 5      // averaged before the time constant takes over (100 ms at 50 Hz)
#define LEVEL_STILL_G_TOLERANCE 0.3f        // m/s^2 from the unit's own 1 g: closer counts as still
#define LEVEL_G_REF_TAU_S 2.0f              // the unit's own 1 g (its accelerometer's scale), followed slowly
bool level_wake_fix_enabled = true;
float level_grv_cant_rad, level_fix_rad;
static float level_fix_pitch_rad;
static float level_g_ref = 0;
static int64_t level_fix_wake_us = 0;
static uint32_t level_fix_samples = 0;      // still samples used since the wake
static uint32_t level_fix_accel_count = 0;
static int64_t level_fix_last_us = 0;


static float level_wrap_pi(float a) {
    while (a > (float) M_PI) {
        a -= 2.0f * (float) M_PI;
    }
    while (a < -(float) M_PI) {
        a += 2.0f * (float) M_PI;
    }
    return a;
}


// The newest accelerometer sample, if one came since the last call: the unit's own 1 g follows it, and while the
// correction window is open and the unit is still, the correction moves toward it
static void level_wake_fix_take_accel(float grv_cant, float grv_pitch, int64_t now_us, int64_t since_ms) {
    float accel[3];
    const uint32_t count = bno085_get_accelerometer(bno085_dev, accel);
    if (count == level_fix_accel_count || count == 0) {
        return;
    }
    level_fix_accel_count = count;
    const float g = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (g < 1.0f) {
        return;
    }
    const float dt = level_fix_last_us ? (float) (now_us - level_fix_last_us) / 1e6f : 0.02f;
    level_fix_last_us = now_us;
    level_g_ref = level_g_ref == 0 ? g : level_g_ref + dt / (LEVEL_G_REF_TAU_S + dt) * (g - level_g_ref);

    if (since_ms > LEVEL_WAKE_FIX_MS || fabsf(g - level_g_ref) >= LEVEL_STILL_G_TOLERANCE) {
        return;
    }
    const float up[3] = {accel[0] / g, accel[1] / g, accel[2] / g};
    float accel_cant, accel_pitch;
    sensor_frame_cant_pitch(up, &accel_cant, &accel_pitch);
    level_fix_samples++;
    // The average of the first samples (one noisy sample must not set the start), then the time constant
    float k = 1.0f / (float) level_fix_samples;
    if (level_fix_samples > LEVEL_WAKE_FIX_FIRST_SAMPLES) {
        k = dt / (LEVEL_WAKE_FIX_TAU_S + dt);
    }
    level_fix_rad += k * (level_wrap_pi(accel_cant - grv_cant) - level_fix_rad);
    level_fix_pitch_rad += k * ((accel_pitch - grv_pitch) - level_fix_pitch_rad);
}


// Sensor poller task, with each new fusion reading: the cant and pitch to show
static void level_wake_fix(float grv_cant, float grv_pitch, float * cant, float * pitch) {
    level_grv_cant_rad = grv_cant;
    *cant = grv_cant;
    *pitch = grv_pitch;
    const int64_t now_us = esp_timer_get_time();
    const int64_t wake_us = bno085_dev->wake_us;
    if (wake_us != level_fix_wake_us) {     // a new wake
        level_fix_wake_us = wake_us;
        level_fix_samples = 0;
        level_fix_rad = 0;
        level_fix_pitch_rad = 0;
        // The stored accelerometer value is from before the sleep (its report stops while the hub sleeps): only
        // samples that arrive after this count
        float stale[3];
        level_fix_accel_count = bno085_get_accelerometer(bno085_dev, stale);
    }
    const int64_t since_ms = wake_us != 0 ? (now_us - wake_us) / 1000 : INT64_MAX;
    level_wake_fix_take_accel(grv_cant, grv_pitch, now_us, since_ms);

    if (!level_wake_fix_enabled || since_ms > LEVEL_WAKE_FIX_MS) {
        level_fix_rad = 0;
        level_fix_pitch_rad = 0;
        return;
    }
    float fade = 1.0f;
    if (since_ms > LEVEL_WAKE_FIX_MS - LEVEL_WAKE_FIX_FADE_MS) {
        fade = (float) (LEVEL_WAKE_FIX_MS - since_ms) / LEVEL_WAKE_FIX_FADE_MS;
    }
    *cant = level_wrap_pi(grv_cant + level_fix_rad * fade);
    *pitch = grv_pitch + level_fix_pitch_rad * fade;
}


float sensor_x_acceleration_thread_unsafe, sensor_y_acceleration_thread_unsafe, sensor_z_acceleration_thread_unsafe;



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
    float last_sensor_x_acceleration = 0;

#if DISPLAY_PROFILING_ENABLED
    digital_level_profile_reset_window();
#endif

    while (1) {
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {
#if DISPLAY_PROFILING_ENABLED
            digital_level_profile.poll_loops++;
#endif
            // Wait for watched sensor ids
            // Game rotation vectors
            // Wait for the next report (ADR-009): each one is shown as it arrives, and the loop sleeps while there
            // are none (idle, the sensor asleep). The timeout only rechecks the run flag
            if (bno085_wait_for_game_rotation_vector_roll_pitch_yaw_ticks(bno085_dev, NULL, NULL, NULL, pdMS_TO_TICKS(LEVEL_REPORT_WAIT_MS)) == ESP_OK) {
                // Cant and pitch from gravity in the sensor's own frame, against the factory calibration (ADR-011)
                float quat[4];
                float up[3];
                bno085_get_game_rotation_quaternion(bno085_dev, quat);
                sensor_frame_up_from_quaternion(quat, up);
                float grv_cant, grv_pitch;
                sensor_frame_cant_pitch(up, &grv_cant, &grv_pitch);
                level_wake_fix(grv_cant, grv_pitch, &sensor_roll_thread_unsafe, &sensor_pitch_thread_unsafe);
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

                    // Shot has fired, start the timer if not started already
                    if (countdown_timer_start_on_first_shot() &&                              // the Timer page's setting
                        get_countdown_timer_widget_enabled() &&                               // widget is enabled
                        (get_countdown_timer_state(&countdown_timer) == COUNTDOWN_TIMER_PAUSE)  // timer is ready
                    ) {
                        countdown_timer_continue(&countdown_timer);
                        ESP_LOGI(TAG, "Countdown timer started due to recoil");
                    }

                    // The Dope page's setting: the level shows the next target (ADR-007)
                    if (dope_settings.next_target_on_recoil && lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {
                        level_band_next_target();
                        lvgl_port_unlock();
                    }
                }
            }

#if DISPLAY_PROFILING_ENABLED
            digital_level_profile_report_if_due();
#endif
        }
    }
}


void enable_digital_level_view_controller(bool enable) {
    if (enable) {
        // Enable sensor report
        if (sensor_config.enable_game_rotation_vector_report) {
            bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS);  // a failure is logged by the driver; not fatal (ADR-005 B7)
            // The correction after a wake (ADR-014); not always-on, so it stops by itself while the hub sleeps
            bno085_enable_accelerometer_report(bno085_dev, SENSOR_ACCELEROMETER_REPORT_PERIOD_MS);
        }
        if (sensor_config.enable_linear_acceleration_report) {
            bno085_enable_linear_acceleration_report(bno085_dev, SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS);  // a failure is logged by the driver; not fatal (ADR-005 B7)
        }

        xEventGroupSetBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);
    } else {
        // Disable sensor report
        if (sensor_config.enable_game_rotation_vector_report) {
            bno085_enable_game_rotation_vector_report(bno085_dev, 0);  // a failure is logged by the driver; not fatal (ADR-005 B7)
            bno085_enable_accelerometer_report(bno085_dev, 0);
        }
        if (sensor_config.enable_linear_acceleration_report) {
            bno085_enable_linear_acceleration_report(bno085_dev, 0);  // a failure is logged by the driver; not fatal (ADR-005 B7)
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
    rtos_return = xTaskCreateWithCaps(
        unified_sensor_poller_task,
        "sensor_poller",
        SENSOR_EVENT_POLLER_TASK_STACK,
        NULL,
        SENSOR_EVENT_POLLER_TASK_PRIORITY,
        &sensor_poller_task_handle,
        HEAPS_CAPS_ALLOC_DEFAULT_FLAGS
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for unified_sensor_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Initialize sensor reports
    if (sensor_config.enable_game_rotation_vector_report) {
        bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS);  // a failure is logged by the driver; not fatal (ADR-005 B7)
        bno085_enable_accelerometer_report(bno085_dev, SENSOR_ACCELEROMETER_REPORT_PERIOD_MS);
    }
    else {
        bno085_enable_game_rotation_vector_report(bno085_dev, 0);  // a failure is logged by the driver; not fatal (ADR-005 B7)
        bno085_enable_accelerometer_report(bno085_dev, 0);
    }
    if (sensor_config.enable_linear_acceleration_report) {
        bno085_enable_linear_acceleration_report(bno085_dev, SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS);  // a failure is logged by the driver; not fatal (ADR-005 B7)
    }
    else {
        bno085_enable_linear_acceleration_report(bno085_dev, 0);  // a failure is logged by the driver; not fatal (ADR-005 B7)
    }

    return ESP_OK;
}

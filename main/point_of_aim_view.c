#include <math.h>

#include "point_of_aim_view.h"
#include "bno085.h"
#include "sensor_config.h"
#include "app_cfg.h"
#include "common.h"
#include "esp_lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

#include "lvgl.h"

#define TAG "POIView"


typedef struct {
    float user_yaw_rad_offset;
    float user_pitch_rad_offset;
    float horizontal_fov_rad;
    float vertical_fov_rad;
    float target_distance;

    // Below variables are calcualted based on target distance and fovs
    float x_max;
    float y_max;
} point_of_aim_view_config_t;


HEAPS_CAPS_ATTR point_of_aim_view_config_t point_of_aim_view_config;
const point_of_aim_view_config_t default_point_of_aim_view_config = {
    .user_yaw_rad_offset = 0.0f,
    .user_pitch_rad_offset = 0.0f,
    .horizontal_fov_rad = 1.0472f,  // 60deg
    .vertical_fov_rad = 0.5236f,    // 30deg
};


#define SENSOR_POLL_EVENT_RUN (1 << 0)
static EventGroupHandle_t sensor_task_control;
static TaskHandle_t sensor_event_poller_task_handle;
static lv_obj_t * chart;
static lv_chart_series_t * data_series;

extern bno085_ctx_t * bno085_dev;
extern sensor_config_t sensor_config;

const float eps = 1e-6f;
static float sensor_rv_pitch_thread_unsafe, sensor_rv_yaw_thread_unsafe;

IRAM_ATTR esp_err_t euler_to_xy(float pitch, float yaw, float plane_distance, float *out_x, float *out_y) {
    *out_x = -plane_distance * tanf(yaw);
    *out_y = -plane_distance * tanf(pitch);

    return ESP_OK;
}


static void sensor_event_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        // Block wait for event
        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {
            // Wait for rotation vector
            if (bno085_wait_for_rotation_vector_roll_pitch_yaw(bno085_dev, NULL, &sensor_rv_pitch_thread_unsafe, &sensor_rv_yaw_thread_unsafe, false) == ESP_OK) {
                // Round angle
                float pitch, yaw;
                pitch = wrap_angle(sensor_rv_pitch_thread_unsafe - point_of_aim_view_config.user_pitch_rad_offset);
                yaw = wrap_angle(sensor_rv_yaw_thread_unsafe - point_of_aim_view_config.user_yaw_rad_offset);

                // ESP_LOGI(TAG, "Roll: %f, Pitch: %f, Yaw: %f", roll, pitch, yaw);
                float proj_x = 0, proj_y = 0;

                euler_to_xy(pitch, yaw, 100, &proj_x, &proj_y);
                // ESP_LOGI(TAG, "X: %f, Y: %f", proj_x, proj_y);

                // Display
                if (lvgl_port_lock(0)) {
                    lv_chart_set_next_value2(chart, data_series, (int32_t) (proj_x * 100), (int32_t) (proj_y * 100));
                    lvgl_port_unlock();
                }
                
            }

            vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
        }
    }
}


void enable_point_of_aim_view(bool enable) {
    if (enable) {
        // Enable rotation vector report
        if (sensor_config.enable_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, SENSOR_ROTATION_VECTOR_REPORT_PERIOD_MS));
        }

        // Allow task to run
        xEventGroupSetBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);

    }
    else {
        // Disable rotation vector report
        if (sensor_config.enable_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));
        }

        // Stop task
        xEventGroupClearBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);
    }
}


static void chart_touch_event_cb(lv_event_t *e) {
    // Record current point of aim
    point_of_aim_view_config.user_yaw_rad_offset = sensor_rv_yaw_thread_unsafe;
    point_of_aim_view_config.user_pitch_rad_offset = sensor_rv_pitch_thread_unsafe;
}


void create_point_of_aim_view(lv_obj_t * parent) {
    // Copy configuration
    // TODO: Load from NVS
    memcpy(&point_of_aim_view_config, &default_point_of_aim_view_config, sizeof(point_of_aim_view_config));

    // Calculate max x and y
    point_of_aim_view_config.x_max = point_of_aim_view_config.target_distance * tanf(point_of_aim_view_config.horizontal_fov_rad * 0.5f);
    point_of_aim_view_config.y_max = point_of_aim_view_config.target_distance * tanf(point_of_aim_view_config.vertical_fov_rad * 0.5f);
    

    chart = lv_chart_create(parent);
    lv_obj_set_size(chart, lv_pct(100), lv_pct(100));
    lv_obj_set_align(chart, LV_ALIGN_CENTER);

    // Make it touchable so I can zero my POI by touching the screen
    lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chart, chart_touch_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    // Set data type to scatter
    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);

    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_X, -2 * 100, 2 * 100);    // 2m
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, -2 * 100, 2 * 100);    // 2m

    lv_chart_set_point_count(chart, 1);  // for now set to 1 point

    // Create data series
    data_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);

    // Initialize the report record
    ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));

    // Task controller
    sensor_task_control = xEventGroupCreate();
    if (sensor_task_control == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor_task_control");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    BaseType_t rtos_return = xTaskCreate(
        sensor_event_poller_task,
        "poi_sensor_poller",
        POI_EVENT_POLLER_TASK_STACK,
        NULL,
        POI_EVENT_POLLER_TASK_PRIORITY,
        &sensor_event_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for poi_sensor_poller");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
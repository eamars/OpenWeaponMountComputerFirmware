#include "acceleration_analysis_view.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_cfg.h"
#include "bno085.h"
#include "circular_buffer.h"
#include "sensor_config.h"

#define TAG "AccelerationAnalysisView"

#define BUFFER_LENGTH 100
#define SENSOR_POLL_EVENT_RUN   (1 << 1)


static circular_buffer_t *accel_buffer = NULL;

extern sensor_config_t sensor_config;
extern bno085_ctx_t * bno085_dev;
static TaskHandle_t acceleration_event_poller_task_handle;
static EventGroupHandle_t sensor_task_control;

lv_obj_t * chart = NULL;
lv_chart_series_t * x_accel_series = NULL;
lv_obj_t * info_label = NULL;


static void acceleration_event_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    TickType_t last_poll_tick = xTaskGetTickCount();
    float last_value = 0;
    float highest_value = 0;

    while (1) {
        // Block until allowed 
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        // Block waiting for BNO085 acceleration event
        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {
            float x, y, z;
            esp_err_t err = bno085_wait_for_linear_acceleration_report(bno085_dev, &x, &y, &z, true);

            if (err == ESP_OK) {
                // ESP_LOGI(TAG, "Acceleration Analysis: x=%.2f, y=%.2f, z=%.2f", x, y, z);

                // Oscilloscope processing
                float x_abs = fabsf(x);
                if (x_abs > highest_value) {
                    highest_value = x_abs;
                }
                // push to buffer
                circular_buffer_push(accel_buffer, x_abs);

                // Look for rising edge
                if (last_value < sensor_config.recoil_acceleration_trigger_level && 
                    x_abs >= sensor_config.recoil_acceleration_trigger_level && 
                    sensor_config.trigger_edge == TRIGGER_RISING_EDGE) {
                    // Align the offset to align the trigger to the center
                    circular_buffer_set_read_offset(accel_buffer, -1 * (BUFFER_LENGTH / 2)); 

                    // Populate all data to the chart
                    if (lvgl_port_lock(0)) {
                        // Update scale
                        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t) (highest_value * 1100));
                        for (int i = 0; i < BUFFER_LENGTH; i++) {
                            float value = circular_buffer_read_next(accel_buffer);
                            lv_chart_set_next_value(chart, x_accel_series, (int32_t) value * 1000);
                        }
                        lv_chart_refresh(chart);

                        // Update information label
                        lv_label_set_text_fmt(info_label, "TRIG: %ld mm/s^2\nX_ACCEL: %ld mm/s^2\nPEAK: %ld mm/s^2", 
                            sensor_config.recoil_acceleration_trigger_level * 1000, (int32_t) x * 1000, (int32_t) highest_value * 1000);

                        lvgl_port_unlock();
                    }
                }

                last_value = x;
            }
            vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
        }
    }
}


void create_acceleration_analysis_view(lv_obj_t *parent) {
    accel_buffer = circular_buffer_create(BUFFER_LENGTH);
    chart = lv_chart_create(parent);

    info_label = lv_label_create(parent);
    lv_obj_align(info_label, LV_ALIGN_TOP_LEFT, 10, 0);
    lv_label_set_text(info_label, "Not Triggered");


    // rotate the chart by 90 deg
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_size(chart, lv_pct(100), lv_pct(100));

    lv_chart_set_point_count(chart, BUFFER_LENGTH);
    x_accel_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);

    // Prefill data
    for (int i = 0; i < BUFFER_LENGTH; i++) {
        lv_chart_set_next_value(chart, x_accel_series, 0);
        // lv_chart_set_next_value(chart, y_accel_series, 0);
        // lv_chart_set_next_value(chart, z_accel_series, 0);
    }


    sensor_task_control = xEventGroupCreate();
    if (sensor_task_control == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor_poll_event");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    BaseType_t rtos_return = xTaskCreate(
        acceleration_event_poller_task, 
        "accel_poller", 
        ACCELERATION_EVENT_POLLER_TASK_STACK,
        NULL,
        ACCELERATION_EVENT_POLLER_TASK_PRIORITY,
        &acceleration_event_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for acceleration_event_poller");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}



void enable_acceleration_analysis_view(bool enable) {
    if (enable) {
        // Allow the poller to run
        xEventGroupSetBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);

    } else {
        // Disable the poller
        xEventGroupClearBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);
    }
}
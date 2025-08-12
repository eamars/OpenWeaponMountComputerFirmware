#include "acceleration_analysis_view.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_cfg.h"
#include "bno085.h"

#define TAG "AccelerationAnalysisView"


extern bno085_ctx_t bno085_dev;
static TaskHandle_t acceleration_event_poller_task_handle;
static SemaphoreHandle_t acceleration_event_poller_task_control;


static void acceleration_event_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        // Block until allowed 
        xSemaphoreTake(acceleration_event_poller_task_control, portMAX_DELAY);

        // Block waiting for BNO085 acceleration event
        float x, y, z;
        bno085_wait_for_linear_acceleration_report(&bno085_dev, &x, &y, &z, true);

        ESP_LOGI(TAG, "Acceleration: x=%.2f, y=%.2f, z=%.2f", x, y, z);

        // Allow the task to run
        xSemaphoreGive(acceleration_event_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
    }
}


void create_acceleration_analysis_view(lv_obj_t *parent) {
    lv_obj_t * chart = lv_chart_create(parent);

    // rotate the chart by 90 deg
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_size(chart, lv_pct(100), lv_pct(100));

    acceleration_event_poller_task_control = xSemaphoreCreateBinary();
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
        // Start acceleration reporting
        bno085_enable_linear_acceleration_report(&bno085_dev, 20);

        // Allow the poller to run
        xSemaphoreGive(acceleration_event_poller_task_control);

    } else {
        // Disable the poller
        xSemaphoreTake(acceleration_event_poller_task_control, portMAX_DELAY);
    }
}
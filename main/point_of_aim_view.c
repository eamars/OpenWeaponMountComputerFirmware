#include "point_of_aim_view.h"
#include "bno085.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

#define TAG "POIView"


#define SENSOR_POLL_EVENT_RUN (1 << 0)
static EventGroupHandle_t sensor_task_control;

static TaskHandle_t sensor_event_poller_task_handle;

extern bno085_ctx_t * bno085_dev;


static void sensor_event_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        // Block wait for event
        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {

            vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
        }
    }
}


void enable_point_of_aim_view(bool enable) {
    if (enable) {

    }
    else {

    }
}


void create_point_of_aim_view(lv_obj_t * parent) {

    // Task controller
    // sensor_task_control = xEventGroupCreate();
    // if (sensor_task_control == NULL) {
    //     ESP_LOGE(TAG, "Failed to create sensor_task_control");
    //     ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    // }

    // BaseType_t rtos_return = xTaskCreate(
    //     sensor_event_poller_task,
    //     "poi_sensor_poller",
    //     POI_EVENT_POLLER_TASK_STACK,
    //     NULL,
    //     POI_EVENT_POLLER_TASK_PRIORITY,
    //     &sensor_event_poller_task_handle
    // );
    // if (rtos_return != pdPASS) {
    //     ESP_LOGE(TAG, "Failed to allocate memory for poi_sensor_poller");
    //     ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    // }
}
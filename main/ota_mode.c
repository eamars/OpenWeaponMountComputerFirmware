#include "ota_mode.h"
#include "low_power_mode.h"
#include "wifi.h"
#include "main_tileview.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#define TAG "OTAMode"

static lv_obj_t * progress_label;
static lv_obj_t * progress_bar;

extern lv_obj_t * tile_ota_mode_view;
extern lv_obj_t * main_tileview;

static TaskHandle_t ota_poller_task_handle;
static lv_obj_t * last_tile = NULL;  // last tile before entering the OTA mode


void ota_poller_task(void *p) {
    // Wait for wifi to be connected
    while (true) {
        esp_err_t ret = wifi_wait_for_sta_connected(1000);
        if (ret == ESP_OK) {
            break;
        }
    }

    ESP_LOGI(TAG, "Network connected, will poll OTA source");

    // // FIXME: automatically enter OTA Mode
    // if (lvgl_port_lock(0)) {
    //     // Record the last tile
    //     last_tile = lv_tileview_get_tile_active(main_tileview);

    //     // Shift to low power tileview
    //     lv_tileview_set_tile(main_tileview, tile_ota_mode_view, LV_ANIM_OFF);
    //     lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
    //     lvgl_port_unlock();
    // }

    vTaskDelete(NULL);   // safely remove this task
 }


void create_ota_mode_view(lv_obj_t * parent) {
    // Draw a container to allow vertical stacking
    lv_obj_t * container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, 
        LV_FLEX_ALIGN_CENTER, 
        LV_FLEX_ALIGN_CENTER, 
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);


    // Put Title Label
    lv_obj_t * title_label = lv_label_create(container);
    lv_label_set_text(title_label, "OTA Update");

    // Put progress label
    progress_label = lv_label_create(container);
    lv_label_set_text(progress_label, "Progress: 0%");

    // Put progress bar
    progress_bar = lv_bar_create(container);
    lv_obj_set_size(progress_bar, lv_pct(80), 20);

    // Create the task to poll for OTA update
    BaseType_t rtos_return;
    rtos_return = xTaskCreate(
        ota_poller_task, 
        "ota_poller",
        OTA_POLLER_TASK_STACK,
        NULL, 
        OTA_POLLER_TASK_PRIORITY,
        &ota_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for ota_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}

void update_ota_mode_progress(int progress) {
    if (progress_label && progress_bar) {
        lv_label_set_text_fmt(progress_label, "Progress: %d", progress);
        lv_bar_set_value(progress_bar, progress, LV_ANIM_OFF);
    }
}

void enter_ota_mode(bool enable) {
    ESP_LOGI(TAG, "OTA Mode %s", enable ? "enabled" : "disabled");

    if (enable) {
        prevent_low_power_mode_enter(true);
    }
    else {
        prevent_low_power_mode_enter(false);
    }
}
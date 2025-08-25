#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "low_power_mode.h"
#include "system_config.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "system_config.h"
#include "app_cfg.h"
#include "bsp.h"

#define TAG "LowPowerMode"

extern system_config_t system_config;
extern lv_obj_t * tile_low_power_mode_view;
extern lv_obj_t * main_tileview;
extern lv_obj_t * last_tile;
extern esp_lcd_panel_io_handle_t io_handle;
extern lv_indev_t * lvgl_touch_handle;

lv_indev_read_cb_t original_read_cb;  // the original touchpad read callback
TaskHandle_t low_power_monitor_task_handle;
TickType_t last_activity_tick = 0;
bool in_low_power_mode = false;


void IRAM_ATTR touchpad_read_cb_wrapper(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    // Call the original read callback
    if (original_read_cb) {
        original_read_cb(indev_drv, data);
    }

    // Update last activity tick if there is any touch activity
    if (data->state == LV_INDEV_STATE_PR || data->enc_diff != 0) {
        last_activity_tick = xTaskGetTickCount();
    }
}


void on_low_power_mode_view_touched_callback(lv_event_t * e) {
    // Exit low power mode on touch
    if (in_low_power_mode) {
        if (main_tileview && last_tile) {
            if (lvgl_port_lock(0)) {
                lv_tileview_set_tile(main_tileview, last_tile, LV_ANIM_OFF);
                lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
                lvgl_port_unlock();
            }
            ESP_LOGI(TAG, "Exiting low power mode due to touch activity");
        }
    }
}


void low_power_monitor_task(void *p) {
    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        if (system_config.idle_timeout != IDLE_TIMEOUT_NEVER && !in_low_power_mode) {
            uint32_t idle_timeout_secs = idle_timeout_to_secs(system_config.idle_timeout);
            if (current_tick - last_activity_tick > pdMS_TO_TICKS(idle_timeout_secs * 1000)) {
                if (main_tileview && tile_low_power_mode_view) {
                    if (lvgl_port_lock(0)) {
                        
                        lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
                        lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
                        lvgl_port_unlock();
                    }
                    ESP_LOGI(TAG, "Entering low power mode due to inactivity");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



void create_low_power_mode_view(lv_obj_t * parent) {
    // Set background to save power on AMOLED screen
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    // Put a simple label to indicate low power mode
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, "Low Power Mode");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);

    BaseType_t rtos_return = xTaskCreate(
        low_power_monitor_task,
        "LPMON",
        LOW_POWER_MODE_MONITOR_TASK_STACK,
        NULL,
        LOW_POWER_MODE_MONITOR_TASK_PRIORITY,
        &low_power_monitor_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_poller");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Inject the wrapper to the pointer input
    original_read_cb = lv_indev_get_read_cb(lvgl_touch_handle);
    lv_indev_set_read_cb(lvgl_touch_handle, touchpad_read_cb_wrapper);

    // Catch touch event to the low power mode view to exit
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, on_low_power_mode_view_touched_callback, LV_EVENT_PRESSED, NULL);
}


void enable_low_power_mode(bool enable) {
    // This function is called by LVGL
    ESP_LOGI(TAG, "Low Power Mode %s", enable ? "enabled" : "disabled");

    if (enable) {
        in_low_power_mode = true;

        // Dim the display
        if (io_handle) {
            set_display_brightness(&io_handle, 1);
        }

        lvgl_port_stop();

    } else {
        in_low_power_mode = false;

        // Additional actions to take when disabling low power mode
        if (io_handle) {
            set_display_brightness(&io_handle, 100);
        }

        lvgl_port_resume();
    }
}
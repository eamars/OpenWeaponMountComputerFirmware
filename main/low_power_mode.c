#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_check.h"
#include "esp_task_wdt.h"

#include "low_power_mode.h"
#include "system_config.h"
#include "app_cfg.h"
#include "bsp.h"
#include "sensor_config.h"
#include "bno085.h"

#define TAG "LowPowerMode"

extern system_config_t system_config;
extern lv_obj_t * tile_low_power_mode_view;
extern lv_obj_t * main_tileview;
extern lv_obj_t * last_tile;
extern esp_lcd_panel_io_handle_t io_handle;
extern lv_indev_t * lvgl_touch_handle;
extern sensor_config_t sensor_config;
extern bno085_ctx_t * bno085_dev;

lv_indev_read_cb_t original_read_cb;  // the original touchpad read callback
TaskHandle_t low_power_monitor_task_handle;
TaskHandle_t sensor_stability_classifier_poller_task_handle;
TickType_t last_activity_tick = 0;
bool in_low_power_mode = false;


void IRAM_ATTR update_low_power_mode_last_activity_event() {
    last_activity_tick = xTaskGetTickCount();
}


void IRAM_ATTR touchpad_read_cb_wrapper(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    // Call the original read callback
    if (original_read_cb) {
        original_read_cb(indev_drv, data);
    }

    // Update last activity tick if there is any touch activity
    if (data->state == LV_INDEV_STATE_PR || data->enc_diff != 0) {
        update_low_power_mode_last_activity_event();
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
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        if (system_config.idle_timeout != IDLE_TIMEOUT_NEVER && 
            !in_low_power_mode && 
            main_tileview && tile_low_power_mode_view) {
            uint32_t idle_timeout_secs_ms = idle_timeout_to_secs(system_config.idle_timeout) * 1000;
            TickType_t current_tick = xTaskGetTickCount();
            uint32_t idle_duration_ms = pdTICKS_TO_MS(current_tick - last_activity_tick);
            // ESP_LOGI(TAG, "Idle duration: %d ms, threshold: %d ms", idle_duration_ms, idle_timeout_secs_ms);

            if (idle_duration_ms > idle_timeout_secs_ms) {
                if (lvgl_port_lock(0)) {
                    
                    lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
                    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
                    lvgl_port_unlock();
                }
                ESP_LOGI(TAG, "Entering low power mode due to inactivity");
            }
        }

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(LOW_POWER_MODE_MONITOR_TASK_PERIOD_MS));
    }
}


void sensor_stability_classifier_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    while (1) {
        uint8_t stability_classification = STABILITY_CLASSIFIER_UNKNOWN;
        esp_err_t err = bno085_wait_for_stability_classification_report(bno085_dev, &stability_classification, true);

        if (err == ESP_OK) {
            
            if (stability_classification == STABILITY_CLASSIFIER_MOTION) {
                // If the sensor is moving in sleep move then wake up the system
                if (in_low_power_mode) {
                    if (main_tileview && last_tile) {
                        if (lvgl_port_lock(0)) {
                            lv_tileview_set_tile(main_tileview, last_tile, LV_ANIM_OFF);
                            lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
                            lvgl_port_unlock();
                        }
                        ESP_LOGI(TAG, "Exiting low power mode due to sensor activity");
                    }
                }
                // If the sensor is moving then update the last activity tick to prevent the system from entering the sleep state
                else {
                    update_low_power_mode_last_activity_event();
                }
            }
        }
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

    // Enable sensor stability classification report
    ESP_ERROR_CHECK(bno085_enable_stability_classification_report(bno085_dev, SENSOR_STABILITY_CLASSIFIER_REPORT_PERIOD_MS));

    // Create user event poller task
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

    // Create sensor stability classifier task
    rtos_return = xTaskCreate(
        sensor_stability_classifier_poller_task,
        "STABILITY",
        SENSOR_STABILITY_CLASSIFIER_TASK_STACK,
        NULL,
        SENSOR_STABILITY_CLASSIFIER_TASK_PRIORITY,
        &sensor_stability_classifier_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_stability_classifier_poller");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Inject the wrapper to the pointer input
    original_read_cb = lv_indev_get_read_cb(lvgl_touch_handle);
    lv_indev_set_read_cb(lvgl_touch_handle, touchpad_read_cb_wrapper);

    // Catch touch event to the low power mode view to exit
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, on_low_power_mode_view_touched_callback, LV_EVENT_PRESSED, NULL);
}


static void delayed_stop_lvgl(lv_timer_t *timer) {
    lvgl_port_stop();
    lv_timer_del(timer);
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

        // Lower the report period
        if (sensor_config.enable_game_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_LOW_POWER_MODE_REPORT_PERIOD_MS));
        }
        if (sensor_config.enable_linear_acceleration_report) {
            ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, SENSOR_LINEAR_ACCELERATION_LOW_POWER_MODE_REPORT_PERIOD_MS));
        }

        // lvgl_port_stop();
        lv_timer_create(delayed_stop_lvgl, 1, NULL);

    } 
    else {
        // Update the last activity tick in making sure the count is updated before any other event
        update_low_power_mode_last_activity_event();

        in_low_power_mode = false;

        lvgl_port_resume();

        // Enable sensor report
        if (sensor_config.enable_game_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS));
        }
        if (sensor_config.enable_linear_acceleration_report) {
            ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS));
        }

        // Additional actions to take when disabling low power mode
        if (io_handle) {
            set_display_brightness(&io_handle, 100);
        }
    }
}
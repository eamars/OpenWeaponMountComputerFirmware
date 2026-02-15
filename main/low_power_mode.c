#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_check.h"
#include "esp_task_wdt.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include "low_power_mode.h"
#include "system_config.h"
#include "app_cfg.h"
#include "bsp.h"
#include "sensor_config.h"
#include "bno085.h"
#include "pmic_axp2101.h"
#include "wifi.h"
#include "main_tileview.h"

#define TAG "LowPowerMode"

typedef enum {
    IN_LOW_POWER_MODE = (1 << 0),
    PREVENT_ENTER_LOW_POWER_MODE = (1 << 1),
} low_power_mode_control_event_e;


extern system_config_t system_config;
extern esp_lcd_panel_io_handle_t io_handle;
extern lv_indev_t * lvgl_touch_handle;
extern sensor_config_t sensor_config;
extern bno085_ctx_t * bno085_dev;
extern axp2101_ctx_t * axp2101_dev;
extern wifi_user_config_t wifi_user_config;
extern lv_obj_t * main_tileview;
extern lv_obj_t * default_tile;
extern lv_obj_t * tile_low_power_mode_view;

TaskHandle_t low_power_monitor_task_handle;
TaskHandle_t sensor_stability_detector_poller_task_handle;
uint32_t wakeup_cause;

TickType_t last_activity_tick = 0;
TickType_t last_low_power_mode_tick = 0;
static EventGroupHandle_t low_power_control_event;
static lv_indev_read_cb_t original_read_cb;  // the original touchpad read callback


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


void low_power_monitor_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    // Configure the wakeup sources. This includes
    // 1) Touch interrupt: to wake up the system when there is a touch event
    // 2) BNO085 interrupt: to wake up the system when there is a sensor event, which indicates the user is interacting with the system
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
            (1 << TOUCHSCREEN_INT_PIN) |
            (1 << BNO085_INT_PIN)
            // (1 << PMIC_AXP2101_INT_PIN)  // PMIC interrupt, to wake up the system when there is a change in power status, e.g. USB plugged in, battery low, etc.
            ,
        ESP_EXT1_WAKEUP_ANY_LOW)); 


    while (1) {
        // Can enter low power mode
        if (system_config.idle_timeout != IDLE_TIMEOUT_NEVER &&                                     // Low power mode enabled
            !(xEventGroupGetBits(low_power_control_event) & IN_LOW_POWER_MODE) &&                   // Not already in low power mode
            !(xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_LOW_POWER_MODE))          // Low power mode is not temporarily blocked
        {                                            
            uint32_t idle_timeout_ms = idle_timeout_to_secs(system_config.idle_timeout) * 1000;
            TickType_t current_tick = xTaskGetTickCount();
            uint32_t idle_duration_ms = pdTICKS_TO_MS(current_tick - last_activity_tick);
            ESP_LOGI(TAG, "Idle duration: %d ms, threshold: %d ms", idle_duration_ms, idle_timeout_ms);

            // Have idled long enough
            if (idle_duration_ms > idle_timeout_ms) {
                ESP_LOGI(TAG, "Entering low power mode due to inactivity");
                lv_obj_t * prev_tile = NULL;

                // Set flag
                xEventGroupSetBits(low_power_control_event, IN_LOW_POWER_MODE);
                last_low_power_mode_tick = xTaskGetTickCount();
                ESP_LOGI(TAG, "System entered low power mode at tick %ld", last_activity_tick);

                // Stop Wifi
                wifi_request_stop();


                if (lvgl_port_lock(0)) {
                    prev_tile = lv_tileview_get_tile_active(main_tileview);

                    // Move the tileview to the low power mode page and invalidate to trigger the redraw immediately, so that the low power mode view is shown before entering sleep
                    lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
                    lv_obj_invalidate(main_tileview);  // force an update
                    vTaskDelay(pdMS_TO_TICKS(1));  // Wait for the redraw to complete

                    // Stop LVGL timer
                    lvgl_port_stop();
                    lvgl_port_unlock();
                }

#if USE_BNO085
                // Stop the reporting
                if (sensor_config.enable_game_rotation_vector_report) {
                    ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
                }
                if (sensor_config.enable_linear_acceleration_report) {
                    ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, 0));
                }
                if (sensor_config.enable_rotation_vector_report) {
                    ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));
                }
#endif  // USE_BNO085
                
                // Dim the display  
                if (io_handle) {
                    set_display_brightness(&io_handle, system_config.screen_brightness_idle_pct);
                }

                // Enter light sleep mode
                ESP_ERROR_CHECK(esp_light_sleep_start());
                // vTaskDelay(pdMS_TO_TICKS(5000));  // Sleep for a while to allow the system to enter sleep mode. The actual sleep duration is determined by the hardware and the wakeup source, so we don't use esp_light_sleep_start() here to have better control over the flow after waking up.
                
                /* Sleep Starts */

                /* Sleep Ends */
                wakeup_cause = esp_sleep_get_wakeup_cause();
                ESP_LOGI(TAG, "Woke up from sleep, wakeup cause: %d", wakeup_cause);

                // Resume the previous operation
                update_low_power_mode_last_activity_event();  // Update the last activity tick to prevent immediately re-entering low power mode
                xEventGroupClearBits(low_power_control_event, IN_LOW_POWER_MODE);

                // Resume display brightness
                if (io_handle) {
                    set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
                }

                // Re-enable LVGL
                lvgl_port_resume();

                // Move the tileview to the previous page
                if (lvgl_port_lock(0)) {
                    lv_tileview_set_tile(main_tileview, prev_tile, LV_ANIM_OFF);
                    
                    lvgl_port_unlock();
                }

                // Restore wifi
                wifi_request_start();
            }
        }

        // // Auto power off checks
        // if ((system_config.power_off_timeout != POWER_OFF_TIMEOUT_NEVER) &&                       // Auto power off enabled
        //     (xEventGroupGetBits(low_power_control_event) & IN_LOW_POWER_MODE) &&                // Already in low power mode
        //     (!axp2101_dev->status.is_usb_connected)) {                                          // Not on VBUS power

        //     // The system is already in the sleep mode, then start the timer
        //     uint32_t power_off_timeout_ms = power_off_timeout_to_secs(system_config.power_off_timeout) * 1000;
        //     TickType_t current_tick = xTaskGetTickCount();
        //     uint32_t low_power_mode_duration_ms = pdTICKS_TO_MS(current_tick - last_low_power_mode_tick);
        //     ESP_LOGI(TAG, "Low power mode duration: %d ms, threshold: %d ms", low_power_mode_duration_ms, power_off_timeout_ms);

        //     if (low_power_mode_duration_ms > power_off_timeout_ms) {
        //         ESP_LOGI(TAG, "Powering off the system...");

        //         pmic_power_off();
        //     }
        // }

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(LOW_POWER_MODE_MONITOR_TASK_PERIOD_MS));
    }
}


void sensor_stability_detector_poller_task(void *p) {
    // Initialize sensor
    ESP_ERROR_CHECK(bno085_enable_stability_detector_report(bno085_dev, SENSOR_STABILITY_DETECTOR_REPORT_PERIOD_MS));

    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    while (1) {
        uint16_t stability;
        esp_err_t err = bno085_wait_for_stability_detector_report(bno085_dev, &stability, true);

        if (err == ESP_OK) {
            update_low_power_mode_last_activity_event();
        }
    }
}

void create_low_power_mode_view(lv_obj_t * parent) {
    low_power_control_event = xEventGroupCreate();
    if (low_power_control_event == NULL) {
        ESP_LOGE(TAG, "Failed to create low_power_control_event");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // Set background to save power on AMOLED screen
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    // Put a simple label to indicate low power mode
    lv_obj_t * low_power_mode_label = lv_label_create(parent);
    lv_label_set_text(low_power_mode_label, "Low Power Mode");
    lv_obj_set_style_text_color(low_power_mode_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(low_power_mode_label);


    // Create event poller task
    BaseType_t rtos_return = xTaskCreate(
        low_power_monitor_task,
        "LPMON",
        LOW_POWER_MODE_MONITOR_TASK_STACK,
        NULL,
        LOW_POWER_MODE_MONITOR_TASK_PRIORITY,
        &low_power_monitor_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for low_power_monitor_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

#if USE_BNO085
    // Create sensor stability classifier task
    rtos_return = xTaskCreate(
        sensor_stability_detector_poller_task,
        "STABILITY",
        SENSOR_STABILITY_DETECTOR_POLLER_TASK_STACK,
        NULL,
        SENSOR_STABILITY_DETECTOR_POLLER_TASK_PRIORITY,
        &sensor_stability_detector_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_stability_detector_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
#endif  // USE_BNO085

    // Inject the wrapper to the pointer input
    original_read_cb = lv_indev_get_read_cb(lvgl_touch_handle);
    lv_indev_set_read_cb(lvgl_touch_handle, touchpad_read_cb_wrapper);
}

bool is_low_power_mode_activated() {
    return xEventGroupGetBits(low_power_control_event) & IN_LOW_POWER_MODE;
}


void prevent_low_power_mode_enter(bool prevent) {
    // Update last tick 
    update_low_power_mode_last_activity_event();

    if (prevent) {
        ESP_LOGI(TAG, "Temporarily disable low power mode");
        xEventGroupSetBits(low_power_control_event, PREVENT_ENTER_LOW_POWER_MODE);
    }
    else {
        ESP_LOGI(TAG, "Re-enable low power mode");
        xEventGroupClearBits(low_power_control_event, PREVENT_ENTER_LOW_POWER_MODE);
    }
}

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
#include "esp_pm.h"
#include "esp_lcd_panel_ops.h"

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
    IN_IDLE_MODE = (1 << 0),
    IN_SLEEP_MODE = (1 << 1),
    PREVENT_ENTER_IDLE_MODE = (1 << 2),
    PREVENT_ENTER_SLEEP_MODE = (1 << 3),
} low_power_mode_control_event_e;


extern esp_lcd_panel_io_handle_t io_handle;
extern esp_lcd_panel_handle_t panel_handle;

extern system_config_t system_config;
extern lv_indev_t * lvgl_touch_handle;
extern sensor_config_t sensor_config;
extern bno085_ctx_t * bno085_dev;
extern axp2101_ctx_t * axp2101_dev;
extern wifi_user_config_t wifi_user_config;

extern lv_obj_t * main_tileview;
extern lv_obj_t * default_tile;
extern lv_obj_t * tile_low_power_mode_view;

lv_obj_t * pre_low_power_mode_tile;

TaskHandle_t low_power_monitor_task_handle;
TaskHandle_t sensor_stability_detector_poller_task_handle;
uint32_t wakeup_cause;

TickType_t last_activity_tick = 0;
TickType_t last_idle_tick = 0;
static EventGroupHandle_t low_power_control_event;
static lv_indev_read_cb_t original_read_cb;  // the original touchpad read callback

// Forward declaration of internal functions
void enter_idle_mode(bool enter);


void IRAM_ATTR update_low_power_mode_last_activity_event() {
    last_activity_tick = xTaskGetTickCount();
}


static void delayed_enter_idle_mode(lv_timer_t * timer) {
    enter_idle_mode(true);
    lv_timer_del(timer);
}

void delayed_exit_idle_mode(lv_timer_t * timer) {
    enter_idle_mode(false);
    lv_timer_del(timer);
}

static void delayed_stop_lvgl(lv_timer_t *timer) {
    lvgl_port_stop();
    lv_timer_del(timer);
}

void IRAM_ATTR touchpad_read_cb_wrapper(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    // Call the original read callback
    if (original_read_cb) {
        original_read_cb(indev_drv, data);
    }

    // Update last activity tick if there is any touch activity
    if (data->state == LV_INDEV_STATE_PR || data->enc_diff != 0) {
        update_low_power_mode_last_activity_event();

        if (is_idle_mode_activated()) {
            // If LVGL timer is not running then resume the timer ifrst
            lvgl_port_resume();
            lv_timer_create(delayed_exit_idle_mode, 1, NULL); 
        }
    }
}


void enter_idle_mode(bool enter) {
    static esp_pm_config_t idle_pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = false  // do not automatically enter light sleep, we will handle it in the low power mode task
    };
    static esp_pm_config_t active_pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false  // do not automatically enter light sleep, we will handle it in the low power mode task
    };

    ESP_LOGI(TAG, "%s idle mode", enter ? "Entering" : "Exiting");

    if (enter) {
        xEventGroupSetBits(low_power_control_event, IN_IDLE_MODE);
        // Dim the display  
        if (system_config.screen_brightness_idle_pct == 0) {
            // set_display_brightness(&io_handle, 0);
            esp_lcd_panel_disp_on_off(panel_handle, false);
        }
        else {
            set_display_brightness(&io_handle, system_config.screen_brightness_idle_pct);
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

        // Move to low power mode view
        if (lvgl_port_lock(0)) {
            pre_low_power_mode_tile = lv_tileview_get_tile_active(main_tileview);
            lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
            lv_obj_invalidate(main_tileview);

            lvgl_port_unlock();
        }

        // turn off LVGL
        lv_timer_create(delayed_stop_lvgl, 1, NULL);

        // Enable ESP32 power management module (automatically adjust CPU frequency based on RTOS scheduler)
        ESP_ERROR_CHECK(esp_pm_configure(&idle_pm_config));

    }
    else {
        // Reset idle timer
        last_idle_tick = xTaskGetTickCount();
        xEventGroupClearBits(low_power_control_event, IN_IDLE_MODE);
        ESP_ERROR_CHECK(esp_pm_configure(&active_pm_config));

        lvgl_port_resume();

        // Move out of low power mode view
        if (lvgl_port_lock(0)) {
            if (pre_low_power_mode_tile) {
                lv_tileview_set_tile(main_tileview, pre_low_power_mode_tile, LV_ANIM_OFF);
            }
            else {
                // If for some reason we don't have the previous tile, just move to the default tile
                lv_tileview_set_tile(main_tileview, default_tile, LV_ANIM_OFF);
            }
            lv_obj_invalidate(main_tileview);

            lvgl_port_unlock();
        }

        // Restore display brightness
        if (system_config.screen_brightness_idle_pct == 0) {
            esp_lcd_panel_disp_on_off(panel_handle, true);
        }
        else {
            set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
        }
    }
}


void enter_sleep_mode() {
    ESP_LOGI(TAG, "Entering sleep power mode due to inactivity");

    // Set flag
    xEventGroupSetBits(low_power_control_event, IN_SLEEP_MODE);

    // Stop Wifi
    wifi_request_stop();

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
        set_display_brightness(&io_handle, 0);
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
    last_idle_tick = xTaskGetTickCount();  // Update the last idle tick to prevent immediately entering sleep mode after waking up
    xEventGroupClearBits(low_power_control_event, IN_SLEEP_MODE);

    // Resume display brightness
    if (io_handle) {
        set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
    }

    // Exit idle mode
    if (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) {
        lv_timer_create(delayed_exit_idle_mode, 1, NULL); 
    }

    if (lvgl_port_lock(0)) {
        lv_obj_t * prev_tile = lv_tileview_get_tile_active(main_tileview);
        lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
        lv_tileview_set_tile(main_tileview, prev_tile, LV_ANIM_OFF);
        lv_obj_invalidate(main_tileview);

        lvgl_port_unlock();
    }

    // Restore wifi
    wifi_request_start();
}


void low_power_monitor_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    // Configure the wakeup sources. This includes
    // 1) Touch interrupt: to wake up the system when there is a touch event
    // 2) BNO085 interrupt: to wake up the system when there is a sensor event, which indicates the user is interacting with the system
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
            (1 << TOUCHSCREEN_INT_PIN) |
            // (1 << GPIO_NUM_0) | 
            (1 << BNO085_INT_PIN) 
            // (1 << PMIC_AXP2101_INT_PIN)  // PMIC interrupt, to wake up the system when there is a change in power status, e.g. USB plugged in, battery low, etc.
            ,
        ESP_EXT1_WAKEUP_ANY_LOW)); 


    while (1) {
        TickType_t current_tick = xTaskGetTickCount();
        uint32_t active_duration_ms = pdTICKS_TO_MS(current_tick - last_activity_tick);
        uint32_t idle_duration_ms = pdTICKS_TO_MS(current_tick - last_idle_tick);

        ESP_LOGI(TAG, "Current Idle Mode: %d, Current Sleep Mode: %d, Prevent Enter Idle Mode: %d, Prevent Enter Sleep Mode: %d", 
            (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) != 0,
            (xEventGroupGetBits(low_power_control_event) & IN_SLEEP_MODE) != 0,
            (xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_IDLE_MODE) != 0,
            (xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_SLEEP_MODE) != 0
        );
        ESP_LOGI(TAG, "Active duration: %d ms, Idle duration: %d ms", active_duration_ms, idle_duration_ms);

        // In Idle mode
        if (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) {
            // Update idle mode tick
            last_activity_tick = current_tick;
        }

        if (xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_IDLE_MODE) {
            // Update sleep mode tick
            update_low_power_mode_last_activity_event();
            last_idle_tick = current_tick;
        }

        if (xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_SLEEP_MODE) {
            // Update sleep mode tick
            last_idle_tick = current_tick;
        }


        // Can enter idle mode
        if (system_config.idle_timeout != IDLE_TIMEOUT_NEVER &&                                     // Low power mode enabled
            !(xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) &&                        // Not already in idle mode
            !(xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_IDLE_MODE))               // Idle mode is not temporarily blocked
        {
            uint32_t idle_timeout_ms = idle_timeout_to_secs(system_config.idle_timeout) * 1000;
            
            // Have stayed in active long enough
            if (active_duration_ms > idle_timeout_ms) {
                ESP_LOGI(TAG, "Entering idle mode due to inactivity");

                // Set flag
                xEventGroupSetBits(low_power_control_event, IN_IDLE_MODE);
                last_idle_tick = current_tick;

                // Move to idle mode
                lv_timer_create(delayed_enter_idle_mode, 1, NULL);  // Use timer to delay the actual entering of idle mode to allow the current flow to complete, e.g. allowing the system to process the current touch event and update the UI accordingly before dimming the screen.
            }
        }

        // Can enter sleep mode
        if (system_config.sleep_timeout != SLEEP_TIMEOUT_NEVER &&                                   // Low power mode enabled
            (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) &&                         // In idle mode already
            !(xEventGroupGetBits(low_power_control_event) & IN_SLEEP_MODE) &&                       // Not already in sleep mode
            !(xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_SLEEP_MODE))              // Sleep mode is not temporarily blocked
        {                                            
            uint32_t sleep_timeout_ms = sleep_timeout_to_secs(system_config.sleep_timeout) * 1000;

            // Have sayed in idle long enough
            if (idle_duration_ms > sleep_timeout_ms) {
                // enter_sleep_mode();
                pmic_power_off();
            }
        }

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

            if (is_idle_mode_activated()) {
                lvgl_port_resume();
                lv_timer_create(delayed_exit_idle_mode, 1, NULL); 
            }
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

bool is_idle_mode_activated() {
    // Both idle mode and sleep mode are considered as low power mode
    return xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE;
}

bool is_sleep_mode_activated() {
    return xEventGroupGetBits(low_power_control_event) & IN_SLEEP_MODE;
}


void prevent_idle_mode_enter(bool prevent) {
    // Update last tick 
    update_low_power_mode_last_activity_event();

    if (prevent) {
        xEventGroupSetBits(low_power_control_event, PREVENT_ENTER_IDLE_MODE);
    }
    else {
        xEventGroupClearBits(low_power_control_event, PREVENT_ENTER_IDLE_MODE);
    }
}

void prevent_sleep_mode_enter(bool prevent) {

    if (prevent) {
        xEventGroupSetBits(low_power_control_event, PREVENT_ENTER_SLEEP_MODE);
    }
    else {
        xEventGroupClearBits(low_power_control_event, PREVENT_ENTER_SLEEP_MODE);
    }
}

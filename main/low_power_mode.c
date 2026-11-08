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
#include "src/misc/lv_timer_private.h"  // the paused flag; LVGL 9.6 has no getter for it

#include "low_power_mode.h"
#include "system_config.h"
#include "app_cfg.h"
#include "bsp.h"
#include "sensor_config.h"
#include "bno085.h"
#include "pmic_axp2101.h"
#include "wifi.h"
#include "lvgl_display.h"
#include "main_tileview.h"
#include "app.h"
#include "display_flush.h"

#define TAG "LowPowerMode"

typedef enum {
    IN_IDLE_MODE = (1 << 0),
    IN_SLEEP_MODE = (1 << 1),
    PREVENT_ENTER_IDLE_MODE = (1 << 2),
    PREVENT_ENTER_SLEEP_MODE = (1 << 3),
    REQUEST_IDLE = (1 << 4),
    REQUEST_WAKE = (1 << 5),
} low_power_mode_control_event_e;





lv_obj_t * pre_low_power_mode_tile;

TaskHandle_t low_power_monitor_task_handle;
TaskHandle_t sensor_stability_detector_poller_task_handle;
uint32_t wakeup_cause;

TickType_t last_activity_tick = 0;
TickType_t last_idle_tick = 0;
static EventGroupHandle_t low_power_control_event;
static lv_indev_read_cb_t original_read_cb;  // the original touchpad read callback


// LVGL's timers paused while idle (ADR-009). lvgl_port_stop() alone disables LVGL's timers, and LVGL 9.6's
// lv_timer_handler() then asks to be called again in 1 ms, so the port's task woke about every 2 ms all through idle.
// Instead every running timer is paused and timers stay enabled: the task sleeps until an input event (touch and
// button still reach it) or its longest sleep. No timer runs, so no frame is started while idle (the ADR-002 rule)
#define LVGL_IDLE_MAX_PAUSED_TIMERS 64
static lv_timer_t * lvgl_idle_paused_timers[LVGL_IDLE_MAX_PAUSED_TIMERS];
static int lvgl_idle_paused_count = 0;


// LVGL lock held, LVGL stopped (lvgl_port_stop)
static void lvgl_idle_pause_timers(void) {
    lvgl_idle_paused_count = 0;
    for (lv_timer_t * t = lv_timer_get_next(NULL); t != NULL; t = lv_timer_get_next(t)) {
        if (t->paused) {
            continue;  // paused by its owner; left to it
        }
        if (lvgl_idle_paused_count == LVGL_IDLE_MAX_PAUSED_TIMERS) {
            // More timers than slots: leave LVGL's timers disabled, as lvgl_port_stop() did (correct, not quiet)
            ESP_LOGW(TAG, "More than %d LVGL timers; LVGL stays disabled while idle", LVGL_IDLE_MAX_PAUSED_TIMERS);
            for (int i = 0; i < lvgl_idle_paused_count; i++) {
                lv_timer_resume(lvgl_idle_paused_timers[i]);
            }
            lvgl_idle_paused_count = 0;
            return;
        }
        lv_timer_pause(t);
        lvgl_idle_paused_timers[lvgl_idle_paused_count++] = t;
    }
    lv_timer_enable(true);
}


// LVGL lock held, before lvgl_port_resume(). Resumes only timers that still exist (one may have been deleted while
// idle, by a UI bridge update)
static void lvgl_idle_resume_timers(void) {
    for (lv_timer_t * t = lv_timer_get_next(NULL); t != NULL; t = lv_timer_get_next(t)) {
        for (int i = 0; i < lvgl_idle_paused_count; i++) {
            if (lvgl_idle_paused_timers[i] == t) {
                lv_timer_resume(t);
                break;
            }
        }
    }
    lvgl_idle_paused_count = 0;
}


#if WAKE_PROFILING_ENABLED
// One wake from idle, timed (ADR-009): requested, exit started, panel on, LVGL resumed; reported by the monitor task
// a second after the resume, with the first frame and the BNO085's first reports
static struct {
    const char * volatile cause;
    volatile int64_t request_us;
    int64_t idle_start_us;
    int64_t exit_us;
    int64_t panel_on_us;
    int64_t resumed_us;
    uint32_t idle_te_pulses;    // panel tearing-effect pulses while idle: the panel scanning
    bool pending;
} wake_prof;
#endif  // WAKE_PROFILING_ENABLED


void low_power_mode_note_wake_cause(const char *cause) {
#if WAKE_PROFILING_ENABLED
    if (wake_prof.request_us == 0) {
        wake_prof.cause = cause;
    }
#else
    (void) cause;
#endif  // WAKE_PROFILING_ENABLED
}



void IRAM_ATTR update_low_power_mode_last_activity_event() {
    last_activity_tick = xTaskGetTickCount();
}


bool low_power_mode_swallow_wake_press(bool pressed, bool *wake_press) {
    if (!pressed) {
        // The wake gesture ends with the release; LVGL already sees the input as released
        *wake_press = false;
        return false;
    }

    update_low_power_mode_last_activity_event();
    if (!*wake_press && is_idle_mode_activated()) {
        *wake_press = true;
        low_power_mode_note_wake_cause("press");
        wake_from_idle_mode();
    }
    return *wake_press;
}


void IRAM_ATTR touchpad_read_cb_wrapper(lv_indev_t *indev_drv, lv_indev_data_t *data) {
    static bool wake_press = false;

    // Read first: LVGL clears data before every read, so the press is only known after the real read
    // (ADR-005 B3; the old test before the read never saw a touch)
    if (original_read_cb) {
        original_read_cb(indev_drv, data);
    }

    if (low_power_mode_swallow_wake_press(data->state == LV_INDEV_STATE_PRESSED, &wake_press)) {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


void wake_from_idle_mode() {
#if WAKE_PROFILING_ENABLED
    if (wake_prof.request_us == 0 && is_idle_mode_activated()) {
        wake_prof.request_us = esp_timer_get_time();
    }
#endif  // WAKE_PROFILING_ENABLED
    // Runs in the low power monitor task, never inside an LVGL pass
    xEventGroupSetBits(low_power_control_event, REQUEST_WAKE);
    xTaskNotifyGive(low_power_monitor_task_handle);
}


static const esp_pm_config_t idle_pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 80,
    .light_sleep_enable = false  // do not automatically enter light sleep, we will handle it in the low power mode task
};
static const esp_pm_config_t active_pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = false  // do not automatically enter light sleep, we will handle it in the low power mode task
};


// Low power monitor task only. No display DMA may be in flight while Wi-Fi stops or the clock config changes
// (a flush strip that never completed froze LVGL with its lock held), so LVGL stops and the panel drains first.
static void enter_idle_mode() {
    // Holding the lock: the LVGL task is between passes, so no frame is being rendered or flushed
    lvgl_port_lock(0);
    if (is_idle_mode_activated()) {
        lvgl_port_unlock();
        return;
    }

    ESP_LOGI(TAG, "Entering idle mode");
#if WAKE_PROFILING_ENABLED
    wake_prof.idle_start_us = esp_timer_get_time();
    wake_prof.request_us = 0;
    wake_prof.cause = NULL;
    display_flush_timing_t te_reset;
    display_flush_take_timing(&te_reset);
#endif  // WAKE_PROFILING_ENABLED
    xEventGroupSetBits(low_power_control_event, IN_IDLE_MODE);
    last_idle_tick = xTaskGetTickCount();

    // No LVGL timer runs after this, so no new flush starts
    lvgl_port_stop();
    lvgl_idle_pause_timers();
    lvgl_watchdog_pause(true);

    // A panel command waits for every queued strip of the last frame to leave the bus
    esp_lcd_panel_disp_on_off(panel_handle, false);
    // Then the panel's own sleep: display off alone leaves its supplies and scan running (about 0.6 mA asleep by
    // its datasheet; its TE pulses stop) (ADR-009)
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_display_sleep(io_handle, true));

#if USE_BNO085
    // // Stop the reporting
    // if (sensor_config.enable_game_rotation_vector_report) {
    //     ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
    // }
    // if (sensor_config.enable_linear_acceleration_report) {
    //     ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, 0));
    // }
    // if (sensor_config.enable_rotation_vector_report) {
    //     ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));
    // }
    bno085_enter_sleep(bno085_dev);
#endif  // USE_BNO085

    // Move to low power mode view
    // if (lvgl_port_lock(0)) {
    //     pre_low_power_mode_tile = lv_tileview_get_tile_active(main_tileview);
    //     lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
    //     lv_obj_invalidate(main_tileview);

    //     lvgl_port_unlock();
    // }

    lvgl_port_unlock();

    // Outside the LVGL lock: the Wi-Fi service posts to the UI bridge, which takes it. Returns once the radio is
    // off (ADR-005 step 3)
    wifi_request_suspend(WIFI_SUSPEND_IDLE);

    // Enable ESP32 power management module (automatically adjust CPU frequency based on RTOS scheduler). A failure
    // is logged and the unit idles at the active clock (ADR-005 C1)
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_configure(&idle_pm_config));
}


// Low power monitor task only; the reverse of enter_idle_mode()
static void exit_idle_mode() {
    if (!is_idle_mode_activated()) {
        return;
    }

    ESP_LOGI(TAG, "Exiting idle mode");
#if WAKE_PROFILING_ENABLED
    wake_prof.exit_us = esp_timer_get_time();
    display_flush_timing_t te_idle;
    display_flush_take_timing(&te_idle);
    wake_prof.idle_te_pulses = te_idle.te_pulses;
#endif  // WAKE_PROFILING_ENABLED
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_configure(&active_pm_config));  // logged; the wake goes on

    lvgl_port_lock(0);

#if USE_BNO085
    bno085_wake_up(bno085_dev);
#endif  // USE_BNO085

    // Move out of low power mode view
    // if (lvgl_port_lock(0)) {
    //     if (pre_low_power_mode_tile) {
    //         lv_tileview_set_tile(main_tileview, pre_low_power_mode_tile, LV_ANIM_OFF);
    //     }
    //     else {
    //         // If for some reason we don't have the previous tile, just move to the default tile
    //         lv_tileview_set_tile(main_tileview, default_tile, LV_ANIM_OFF);
    //     }
    //     lv_obj_invalidate(main_tileview);

    //     lvgl_port_unlock();
    // }

    // Restore display brightness. LVGL is stopped, so the panel bus is idle. Sleep-out first; it returns once the
    // panel may be switched on (the sensor's first report, 99 ms after its wake above, comes later anyway)
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_display_sleep(io_handle, false));
    esp_lcd_panel_disp_on_off(panel_handle, true);
#if WAKE_PROFILING_ENABLED
    wake_prof.panel_on_us = esp_timer_get_time();
    display_flush_arm_first_frame();
#endif  // WAKE_PROFILING_ENABLED

    // Reset idle timer; clear the flag before Wi-Fi starts (its disconnect handler reconnects only when active)
    last_idle_tick = xTaskGetTickCount();
    xEventGroupClearBits(low_power_control_event, IN_IDLE_MODE);

    lvgl_watchdog_pause(false);
    lvgl_idle_resume_timers();
    lvgl_port_resume();
#if WAKE_PROFILING_ENABLED
    wake_prof.resumed_us = esp_timer_get_time();
    wake_prof.pending = true;
#endif  // WAKE_PROFILING_ENABLED

    lvgl_port_unlock();

    // Outside the LVGL lock: the Wi-Fi service posts to the UI bridge, which takes it
    wifi_request_suspend(WIFI_SUSPEND_NONE);
}


// Low power monitor task only. A wake beats an idle request posted at the same time (the user is active).
static void run_idle_requests() {
    EventBits_t requests = xEventGroupClearBits(low_power_control_event, REQUEST_IDLE | REQUEST_WAKE);

    if (requests & REQUEST_WAKE) {
        exit_idle_mode();
    }
    else if (requests & REQUEST_IDLE) {
        enter_idle_mode();
    }
}


void enter_sleep_mode() {
    ESP_LOGI(TAG, "Entering sleep power mode due to inactivity");

    // Set flag
    xEventGroupSetBits(low_power_control_event, IN_SLEEP_MODE);

    // Stop Wifi; returns once the radio is off, so light sleep may follow
    wifi_request_suspend(WIFI_SUSPEND_SLEEP);

#if USE_BNO085
    // // Stop the reporting
    // if (sensor_config.enable_game_rotation_vector_report) {
    //     ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
    // }
    // if (sensor_config.enable_linear_acceleration_report) {
    //     ESP_ERROR_CHECK(bno085_enable_linear_acceleration_report(bno085_dev, 0));
    // }
    // if (sensor_config.enable_rotation_vector_report) {
    //     ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));
    // }
    bno085_enter_sleep(bno085_dev);
#endif  // USE_BNO085
    
    // Dim the display  
    if (io_handle) {
        set_display_brightness(&io_handle, 0);
    }

    // Enter light sleep mode; if it is refused, the wake path below runs at once (logged)
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_light_sleep_start());
    // vTaskDelay(pdMS_TO_TICKS(5000));  // Sleep for a while to allow the system to enter sleep mode. The actual sleep duration is determined by the hardware and the wakeup source, so we don't use esp_light_sleep_start() here to have better control over the flow after waking up.
    
    /* Sleep Starts */

    /* Sleep Ends */
    wakeup_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Woke up from sleep, wakeup cause: %" PRIu32, wakeup_cause);

    // Resume the previous operation
    update_low_power_mode_last_activity_event();  // Update the last activity tick to prevent immediately re-entering low power mode
    last_idle_tick = xTaskGetTickCount();  // Update the last idle tick to prevent immediately entering sleep mode after waking up
    xEventGroupClearBits(low_power_control_event, IN_SLEEP_MODE);

#if USE_BNO085
    bno085_wake_up(bno085_dev);
#endif  // USE_BNO085

    // Resume display brightness
    if (io_handle) {
        set_display_brightness(&io_handle, system_config.screen_brightness_normal_pct);
    }

    // Exit idle mode
    if (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) {
        wake_from_idle_mode();
    }

    if (lvgl_port_lock(0)) {
        lv_obj_t * prev_tile = lv_tileview_get_tile_active(main_tileview);
        lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
        lv_tileview_set_tile(main_tileview, prev_tile, LV_ANIM_OFF);
        lv_obj_invalidate(main_tileview);

        lvgl_port_unlock();
    }

    // Restore wifi
    wifi_request_suspend(WIFI_SUSPEND_NONE);
}


#if WAKE_PROFILING_ENABLED
// Low power monitor task. Times in ms from the wake request; the BNO085's from its wake command
static void wake_profile_report_if_due(void) {
    if (!wake_prof.pending || esp_timer_get_time() - wake_prof.resumed_us < 1000000) {
        return;
    }
    wake_prof.pending = false;

    const int64_t t0 = wake_prof.request_us ? wake_prof.request_us : wake_prof.exit_us;
    display_flush_timing_t te_awake;
    display_flush_take_timing(&te_awake);
    const int64_t frame_us = display_flush_first_frame_us();
    uint32_t cmd_us = 0, grv_us = 0, grv_n = 0, la_us = 0, la_n = 0, st_us = 0, st_n = 0;
#if USE_BNO085
    bno085_get_wake_profile(bno085_dev, SH2_GAME_ROTATION_VECTOR, &grv_us, &grv_n, &cmd_us);
    bno085_get_wake_profile(bno085_dev, SH2_LINEAR_ACCELERATION, &la_us, &la_n, NULL);
    bno085_get_wake_profile(bno085_dev, SH2_STABILITY_DETECTOR, &st_us, &st_n, NULL);
#endif  // USE_BNO085

    ESP_LOGI(TAG, "wake_prof: cause=%s idle_ms=%" PRIu32 " exit=%.1f panel_on=%.1f resumed=%.1f frame=%.1f"
             " bno_cmd=%.1f grv_first=%.1f grv_100ms=%" PRIu32 " la_first=%.1f la_100ms=%" PRIu32
             " stab_first=%.1f stab_100ms=%" PRIu32 " te_idle=%" PRIu32 " te_awake=%" PRIu32,
             wake_prof.cause ? wake_prof.cause : "other",
             (uint32_t) ((wake_prof.exit_us - wake_prof.idle_start_us) / 1000),
             (wake_prof.exit_us - t0) / 1000.0, (wake_prof.panel_on_us - t0) / 1000.0,
             (wake_prof.resumed_us - t0) / 1000.0, frame_us ? (frame_us - t0) / 1000.0 : -1.0,
             cmd_us / 1000.0, grv_us ? grv_us / 1000.0 : -1.0, grv_n, la_us ? la_us / 1000.0 : -1.0, la_n,
             st_us ? st_us / 1000.0 : -1.0, st_n, wake_prof.idle_te_pulses, te_awake.te_pulses);
}
#endif  // WAKE_PROFILING_ENABLED


void low_power_mode_request_idle() {
    // Runs in the low power monitor task, never inside an LVGL pass; does nothing when already idle
    xEventGroupSetBits(low_power_control_event, REQUEST_IDLE);
    xTaskNotifyGive(low_power_monitor_task_handle);
}


void low_power_monitor_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    // Configure the wakeup sources. This includes
    // 1) Touch interrupt: to wake up the system when there is a touch event
    // 2) BNO085 interrupt: to wake up the system when there is a sensor event, which indicates the user is interacting with the system
    // Only light sleep uses them, so a failure is logged and idle/wake carry on (ADR-005 C1)
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_enable_ext1_wakeup_io(
            (1 << TOUCHSCREEN_INT_PIN) |
            // (1 << GPIO_NUM_0) | 
            (1 << BNO085_INT_PIN) 
            // (1 << PMIC_AXP2101_INT_PIN)  // PMIC interrupt, to wake up the system when there is a change in power status, e.g. USB plugged in, battery low, etc.
            ,
        ESP_EXT1_WAKEUP_ANY_LOW)); 


    while (1) {
        // Idle and wake requests run at once; the poll below keeps its period
        run_idle_requests();

        TickType_t poll_elapsed = xTaskGetTickCount() - last_poll_tick;
        TickType_t poll_period = pdMS_TO_TICKS(LOW_POWER_MODE_MONITOR_TASK_PERIOD_MS);
        if (poll_elapsed < poll_period) {
            ulTaskNotifyTake(pdTRUE, poll_period - poll_elapsed);
            continue;
        }
        last_poll_tick += poll_period;

        TickType_t current_tick = xTaskGetTickCount();
        uint32_t active_duration_ms = pdTICKS_TO_MS(current_tick - last_activity_tick);
        uint32_t idle_duration_ms = pdTICKS_TO_MS(current_tick - last_idle_tick);

        // ESP_LOGI(TAG, "Current Idle Mode: %d, Current Sleep Mode: %d, Prevent Enter Idle Mode: %d, Prevent Enter Sleep Mode: %d", 
        //     (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) != 0,
        //     (xEventGroupGetBits(low_power_control_event) & IN_SLEEP_MODE) != 0,
        //     (xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_IDLE_MODE) != 0,
        //     (xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_SLEEP_MODE) != 0
        // );
        // Debug level: once a second at info it was a console line (and a wake-up) every second, idle or not
        ESP_LOGD(TAG, "Active duration: %" PRIu32 " ms, Idle duration: %" PRIu32 " ms", active_duration_ms, idle_duration_ms);

        // In Idle mode
        if (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) {
            // Update idle mode tick
            last_activity_tick = current_tick;
        }

        // LVGL stopped on purpose (idle): keep its lock-up watchdog fed; no-op otherwise
        lvgl_watchdog_feed_while_paused();

#if WAKE_PROFILING_ENABLED
        wake_profile_report_if_due();
#endif  // WAKE_PROFILING_ENABLED

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
        if (power_management_config.idle_timeout != IDLE_TIMEOUT_NEVER &&                                     // Low power mode enabled
            !(xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) &&                        // Not already in idle mode
            !(xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_IDLE_MODE))               // Idle mode is not temporarily blocked
        {
            uint32_t idle_timeout_ms = idle_timeout_to_secs(power_management_config.idle_timeout) * 1000;
            
            // Have stayed in active long enough
            if (active_duration_ms > idle_timeout_ms) {
                ESP_LOGI(TAG, "Entering idle mode due to inactivity");
                low_power_mode_request_idle();
            }
        }

        // Can enter sleep mode
        if (power_management_config.sleep_timeout != SLEEP_TIMEOUT_NEVER &&                                   // Low power mode enabled
            (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) &&                         // In idle mode already
            !(xEventGroupGetBits(low_power_control_event) & IN_SLEEP_MODE) &&                       // Not already in sleep mode
            !(xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_SLEEP_MODE))              // Sleep mode is not temporarily blocked
        {                                            
            uint32_t sleep_timeout_ms = sleep_timeout_to_secs(power_management_config.sleep_timeout) * 1000;

            // Have stayed in idle long enough
            if (idle_duration_ms > sleep_timeout_ms) {
                // enter_sleep_mode();
                // The PMIC service shuts the unit down from its own task (ADR-005 step 3). Without a PMIC
                // there is nothing to do: the request returns ESP_ERR_INVALID_STATE
                (void) pmic_request_power_off();
            }
        }

    }
}


void sensor_stability_detector_poller_task(void *p) {
    // Initialize sensor
    bno085_enable_stability_detector_report(bno085_dev, SENSOR_STABILITY_DETECTOR_REPORT_PERIOD_MS);  // a failure is logged by the driver; not fatal (ADR-005 B7)

    while (1) {
        uint16_t stability;
        esp_err_t err = bno085_wait_for_stability_detector_report(bno085_dev, &stability, true);

        ESP_LOGI(TAG, "Stability detector report: %d, err: %d", stability, err);

        if (err == ESP_OK) {
            update_low_power_mode_last_activity_event();

            if (is_idle_mode_activated()) {
                low_power_mode_note_wake_cause("motion");
                wake_from_idle_mode();
            }
        }
    }
}

esp_err_t low_power_mode_init() {
    low_power_control_event = xEventGroupCreate();
    if (low_power_control_event == NULL) {
        ESP_LOGE(TAG, "Failed to create low_power_control_event");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


void create_low_power_mode_view(lv_obj_t * parent) {

    // Set background to save power on AMOLED screen
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    // Put a simple label to indicate low power mode
    lv_obj_t * low_power_mode_label = lv_label_create(parent);
    lv_label_set_text(low_power_mode_label, "Low Power Mode");
    lv_obj_set_style_text_color(low_power_mode_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(low_power_mode_label);
}


esp_err_t low_power_mode_start() {
    // Inject the wrapper to the pointer input, before the tasks that can put the unit to idle exist
    lvgl_port_lock(0);
    original_read_cb = lv_indev_get_read_cb(lvgl_touch_handle);
    lv_indev_set_read_cb(lvgl_touch_handle, touchpad_read_cb_wrapper);
    lvgl_port_unlock();


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
        return ESP_ERR_NO_MEM;
    }

#if USE_BNO085
    // Create sensor stability classifier task
    rtos_return = xTaskCreateWithCaps(
        sensor_stability_detector_poller_task,
        "STABILITY",
        SENSOR_STABILITY_DETECTOR_POLLER_TASK_STACK,
        NULL,
        SENSOR_STABILITY_DETECTOR_POLLER_TASK_PRIORITY,
        &sensor_stability_detector_poller_task_handle,
        HEAPS_CAPS_ALLOC_DEFAULT_FLAGS
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_stability_detector_poller_task");
        return ESP_ERR_NO_MEM;
    }
#endif  // USE_BNO085

    return ESP_OK;
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

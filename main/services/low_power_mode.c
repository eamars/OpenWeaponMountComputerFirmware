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
#include "soc/gpio_struct.h"
#include "src/misc/lv_timer_private.h"  // the paused flag; LVGL 9.6 has no getter for it

#include "low_power_mode.h"
#include "app_cfg.h"
#include "bsp.h"
#include "bno085.h"
#include "pmic_axp2101.h"
#include "wifi.h"
#include "lvgl_display.h"
#include "app.h"
#include "display_flush.h"

#define TAG "LowPowerMode"

typedef enum {
    IN_IDLE_MODE = (1 << 0),
    PREVENT_ENTER_IDLE_MODE = (1 << 2),
    PREVENT_ENTER_SLEEP_MODE = (1 << 3),
    REQUEST_IDLE = (1 << 4),
    REQUEST_WAKE = (1 << 5),
} low_power_mode_control_event_e;






TaskHandle_t low_power_monitor_task_handle;
TaskHandle_t sensor_stability_detector_poller_task_handle;

TickType_t last_activity_tick = 0;

// Moving counts as in use (ADR-009, owner 2026-11-08). The stability detector reports only when the unit enters or
// leaves a still state, so continuous movement longer than the idle timeout gave no activity: the screen went dark
// in the hand and came back at the next still report. Written by the stability task only. The state belongs to the
// detector's run: a hub reset restarts it without a report, so a "moving" from before the last reset does not count
static volatile bool unit_moving = false;
static volatile uint32_t unit_moving_reset_count;
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


// Idle sleeps between events (ADR-009): automatic light sleep whenever every task waits and no PM lock is held
// (USB power holds one: low_power_mode_set_usb_power()). Switched on by light_sleep_wake_init()
// once the wake lines are set up; without them a sleeping unit would not wake. Active never sleeps: the unit must
// stay responsive on the line and never sleep while shooting
static esp_pm_config_t idle_pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 80,
    .light_sleep_enable = false
};
static const esp_pm_config_t active_pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = false
};


/*
 * Light sleep wake-up (ADR-009). Edge interrupts do not wake the S3 from light sleep; the GPIO wake-up is
 * level-only, and it sets the pin's interrupt type to that level. So the four lines are armed (wake on low) only
 * around each sleep, from the PM's light-sleep callbacks, and given back their falling-edge interrupts on the way
 * out. Their CPU interrupts are off while armed: a line held low would otherwise run its ISR again and again on the
 * other core. A line still low on the way out is raised again in software, so the event that woke the chip reaches
 * its ISR. (The earlier ext1 arming moved the pads to the RTC domain and never back, which stops their digital
 * interrupts after the first sleep.)
 */
static const gpio_num_t light_sleep_wake_pins[] = {
    TOUCHSCREEN_INT_PIN,
#if USE_BNO085
    BNO085_INT_PIN,
#endif  // USE_BNO085
#if USE_EXT_BUTTON
    EXT_BUTTON_PIN,
#endif  // USE_EXT_BUTTON
#if USE_PMIC
    PMIC_AXP2101_INT_PIN,
#endif  // USE_PMIC
};
#define LIGHT_SLEEP_WAKE_PIN_COUNT (sizeof(light_sleep_wake_pins) / sizeof(light_sleep_wake_pins[0]))

// Pins that must keep their level while asleep. ESP-IDF isolates every pin in light sleep
// (CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND selects CONFIG_PM_SLP_DISABLE_GPIO: no input, no output, no pull), which
// would float the reset lines of the panel, touch and BNO085, the panel's chip select and the I2C buses (internal
// pull-ups). The wake pins are kept by gpio_wakeup_enable() while armed
static const gpio_num_t light_sleep_held_pins[] = {
    LCD_CS, LCD_RST, TOUCHSCREEN_RST_PIN,
#if USE_BNO085
    BNO085_RESET_PIN, BNO085_BOOT_PIN,
#endif  // USE_BNO085
#if USE_BUZZER
    BUZZER_OUT_PIN,
#endif  // USE_BUZZER
    I2C0_MASTER_SDA, I2C0_MASTER_SCL, I2C1_MASTER_SDA, I2C1_MASTER_SCL,
};

// No light sleep while USB power is present (ADR-009). The USB console does not survive light sleep on the S3, and
// its link did not come back afterwards even with the unit awake. ESP-IDF's own guard
// (CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION) judges the connection by the host's 1 ms frame pulses, so a pause in them
// (a host idling its port) counted as unplugged and let the chip sleep with the cable in. VBUS, read by the PMIC, is
// what plugged in means. Held from boot until the PMIC first reports no USB power; without a PMIC, never released
static esp_pm_lock_handle_t usb_power_pm_lock;
static bool usb_power_present = true;

// Counted by the light-sleep exit callback; read by low_power_mode_get_sleep_stats()
static portMUX_TYPE sleep_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static low_power_sleep_stats_t sleep_stats;
// Idle on battery, the only time light sleep may run: counted from each change of idle or USB power
static bool sleep_acct_idle;
static bool sleep_acct_running;
static int64_t sleep_acct_mark_us;


// sleep_stats_lock held
static void sleep_acct_update(void) {
    int64_t now = esp_timer_get_time();
    if (sleep_acct_running) {
        sleep_stats.idle_battery_us += now - sleep_acct_mark_us;
    }
    sleep_acct_running = sleep_acct_idle && !usb_power_present;
    sleep_acct_mark_us = now;
}


static esp_err_t IRAM_ATTR light_sleep_enter_cb(int64_t sleep_time_us, void *arg) {
    (void) sleep_time_us;
    (void) arg;
    // Called with the scheduler's sleep lock held and interrupts off on this core, with the flash cache on
    for (size_t i = 0; i < LIGHT_SLEEP_WAKE_PIN_COUNT; i++) {
        gpio_intr_disable(light_sleep_wake_pins[i]);
        gpio_wakeup_enable(light_sleep_wake_pins[i], GPIO_INTR_LOW_LEVEL);
    }
    return ESP_OK;
}


static esp_err_t IRAM_ATTR light_sleep_exit_cb(int64_t slept_us, void *arg) {
    (void) arg;
    uint32_t low_mask = 0;
    uint32_t low_index_mask = 0;
    for (size_t i = 0; i < LIGHT_SLEEP_WAKE_PIN_COUNT; i++) {
        gpio_num_t pin = light_sleep_wake_pins[i];
        gpio_wakeup_disable(pin);
        gpio_set_intr_type(pin, GPIO_INTR_NEGEDGE);
        if (gpio_get_level(pin) == 0) {
            low_mask |= BIT(pin);
            low_index_mask |= BIT(i);
        }
        gpio_intr_enable(pin);
    }

    // Called also when the sleep was too short to start (slept_us 0)
    if (slept_us <= 0) {
        return ESP_OK;
    }

    uint32_t causes = esp_sleep_get_wakeup_causes();
    bool by_gpio = causes & BIT(ESP_SLEEP_WAKEUP_GPIO);
    if (by_gpio && low_mask == 0) {
        // A line that pulsed and went high again before this point; the touch controller is the only one that
        // pulses (the others hold their line low until serviced), and a spare touch read costs one I2C read
        low_mask = BIT(TOUCHSCREEN_INT_PIN);
    }
    // The ISRs run for the lines still low: their falling edge came while they were armed as levels
    GPIO.status_w1ts = low_mask;

    portENTER_CRITICAL_SAFE(&sleep_stats_lock);
    sleep_stats.sleeps++;
    sleep_stats.slept_us += slept_us;
    if (by_gpio) {
        sleep_stats.wake_gpio++;
        if (low_index_mask == 0) {
            sleep_stats.wake_gpio_no_line++;
        }
        for (size_t i = 0; i < LIGHT_SLEEP_WAKE_PIN_COUNT && i < LOW_POWER_SLEEP_STATS_LINES; i++) {
            if (low_index_mask & BIT(i)) {
                sleep_stats.wake_line[i]++;
            }
        }
    }
    else if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        sleep_stats.wake_timer++;
    }
    else {
        sleep_stats.wake_other++;
    }
    portEXIT_CRITICAL_SAFE(&sleep_stats_lock);
    return ESP_OK;
}


// Once, before the first idle
static esp_err_t light_sleep_wake_init(void) {
    for (size_t i = 0; i < sizeof(light_sleep_held_pins) / sizeof(light_sleep_held_pins[0]); i++) {
        ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis(light_sleep_held_pins[i]), TAG, "hold pin %d", light_sleep_held_pins[i]);
    }
    ESP_RETURN_ON_ERROR(esp_sleep_enable_gpio_wakeup(), TAG, "GPIO wake-up");
    esp_pm_sleep_cbs_register_config_t cbs = {
        .enter_cb = light_sleep_enter_cb,
        .exit_cb = light_sleep_exit_cb,
    };
    ESP_RETURN_ON_ERROR(esp_pm_light_sleep_register_cbs(&cbs), TAG, "light sleep callbacks");
    idle_pm_config.light_sleep_enable = true;
    return ESP_OK;
}


void low_power_mode_set_usb_power(bool present) {
    // The PMIC monitor task only, so the flag and the lock change together
    if (present == usb_power_present || usb_power_pm_lock == NULL) {
        return;
    }
    portENTER_CRITICAL(&sleep_stats_lock);
    usb_power_present = present;
    sleep_acct_update();
    portEXIT_CRITICAL(&sleep_stats_lock);
    ESP_ERROR_CHECK_WITHOUT_ABORT(present ? esp_pm_lock_acquire(usb_power_pm_lock)
                                          : esp_pm_lock_release(usb_power_pm_lock));
    ESP_LOGI(TAG, "USB power %s: light sleep while idle %s", present ? "in" : "out", present ? "off" : "on");
}


void low_power_mode_get_sleep_stats(low_power_sleep_stats_t *out) {
    portENTER_CRITICAL(&sleep_stats_lock);
    sleep_acct_update();
    *out = sleep_stats;
    out->usb_power = usb_power_present;
    portEXIT_CRITICAL(&sleep_stats_lock);
}


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
    bno085_enter_sleep(bno085_dev);
#endif  // USE_BNO085

    lvgl_port_unlock();

    // Outside the LVGL lock: the Wi-Fi service posts to the UI bridge, which takes it. Returns once the radio is
    // off (ADR-005 step 3)
    wifi_request_suspend(WIFI_SUSPEND_IDLE);

    // Enable ESP32 power management module (automatically adjust CPU frequency based on RTOS scheduler). A failure
    // is logged and the unit idles at the active clock (ADR-005 C1)
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_configure(&idle_pm_config));
    portENTER_CRITICAL(&sleep_stats_lock);
    sleep_acct_idle = true;
    sleep_acct_update();
    sleep_stats.idle_periods++;
    portEXIT_CRITICAL(&sleep_stats_lock);
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
    portENTER_CRITICAL(&sleep_stats_lock);
    sleep_acct_idle = false;
    sleep_acct_update();
    portEXIT_CRITICAL(&sleep_stats_lock);

    lvgl_port_lock(0);

#if USE_BNO085
    bno085_wake_up(bno085_dev);
#endif  // USE_BNO085

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

        // Debug level: once a second at info it was a console line (and a wake-up) every second, idle or not
        ESP_LOGD(TAG, "Active duration: %" PRIu32 " ms, Idle duration: %" PRIu32 " ms", active_duration_ms, idle_duration_ms);

        // In Idle mode
        if (xEventGroupGetBits(low_power_control_event) & IN_IDLE_MODE) {
            // Update idle mode tick
            last_activity_tick = current_tick;
        }

        // LVGL stopped on purpose (idle): keep its lock-up watchdog fed; no-op otherwise
        lvgl_watchdog_feed_while_paused();

#if USE_BNO085
        // Still moving since the detector last said so: the unit is in use
        if (unit_moving && unit_moving_reset_count == bno085_get_reset_count(bno085_dev)) {
            update_low_power_mode_last_activity_event();
        }
#endif  // USE_BNO085

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
            !(xEventGroupGetBits(low_power_control_event) & PREVENT_ENTER_SLEEP_MODE))              // Sleep mode is not temporarily blocked
        {                                            
            uint32_t sleep_timeout_ms = sleep_timeout_to_secs(power_management_config.sleep_timeout) * 1000;

            // Have stayed in idle long enough
            if (idle_duration_ms > sleep_timeout_ms) {
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
            // Bit 0: entered the still state; bit 1: left it (SH-2 manual 6.5.35). Still wins if both are set
            if (stability & STABILITY_ENTERED) {
                unit_moving = false;
            }
            else if (stability & STABILITY_EXITED) {
                unit_moving_reset_count = bno085_get_reset_count(bno085_dev);
                unit_moving = true;
            }

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
    // Before any PMIC report: awake until it says USB power is out
    ESP_RETURN_ON_ERROR(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "usb_power", &usb_power_pm_lock), TAG,
                        "usb_power lock");
    ESP_RETURN_ON_ERROR(esp_pm_lock_acquire(usb_power_pm_lock), TAG, "usb_power lock");
    return ESP_OK;
}


esp_err_t low_power_mode_start() {
    // Inject the wrapper to the pointer input, before the tasks that can put the unit to idle exist
    lvgl_port_lock(0);
    original_read_cb = lv_indev_get_read_cb(lvgl_touch_handle);
    lv_indev_set_read_cb(lvgl_touch_handle, touchpad_read_cb_wrapper);
    lvgl_port_unlock();


    // Light sleep while idle only once its wake lines work; a failure is logged and idle stays awake (ADR-009)
    ESP_ERROR_CHECK_WITHOUT_ABORT(light_sleep_wake_init());

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

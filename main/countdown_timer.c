#include <math.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_lvgl_port.h"

#include "system_config.h"
#include "countdown_timer.h"
#include "low_power_mode.h"
#include "buzzer.h"
#include "app_cfg.h"
#include "lvgl_display.h"
#include "level_band.h"

#define TAG "CountdownTimer"


countdown_timer_state_t get_countdown_timer_state(countdown_timer_t * ctx) {
    countdown_timer_state_t current_state = COUNTDOWN_TIMER_READY;
    if (xSemaphoreTake(ctx->state_mux, portMAX_DELAY) == pdTRUE) {
        current_state = ctx->countdown_timer_state;
        xSemaphoreGive(ctx->state_mux);
    }
    return current_state;
}

void set_countdown_timer_state(countdown_timer_t *ctx, countdown_timer_state_t new_state) {
    if (xSemaphoreTake(ctx->state_mux, portMAX_DELAY) == pdTRUE) {
        ctx->countdown_timer_state = new_state;
        xSemaphoreGive(ctx->state_mux);
    }
}


void countdown_timer_task(void *p) {
    // Cast object back
    countdown_timer_t * ctx = (countdown_timer_t *) p;
    TickType_t last_poll_tick = xTaskGetTickCount();
    int time_left_ms = 0;

    while (1) {
        countdown_timer_state_t current_state = get_countdown_timer_state(ctx);
        switch (current_state) {
            case COUNTDOWN_TIMER_READY: {
                // Refill time
                time_left_ms = ctx->countdown_time_ms;

                // Update GUI
                if (ctx->timer_update_cb) {
                    ctx->timer_update_cb(ctx->timer_update_cb_args, ctx->countdown_time_ms);
                }
                
                // Move to next state
                set_countdown_timer_state(ctx, COUNTDOWN_TIMER_PAUSE);
                last_poll_tick = xTaskGetTickCount();  // update tick status
                break;
            }
            case COUNTDOWN_TIMER_RUN: {
                // Calculate time left
                time_left_ms -= ctx->update_period_ms;
                if (time_left_ms < 0) {
                    time_left_ms = 0;
                }

                if (ctx->timer_update_cb) {
                    ctx->timer_update_cb(ctx->timer_update_cb_args, time_left_ms);
                }

                // Update low power mode state (prevent sleep when the timer is running)
                update_low_power_mode_last_activity_event();

                if (time_left_ms == 0) {
                    // Finished, move to next state
                    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_EXPIRED);
                }
                else {
                    vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(ctx->update_period_ms));
                }
                
                break;
            }
            case COUNTDOWN_TIMER_PAUSE:
            case COUNTDOWN_TIMER_EXPIRED: {
                // Block until resume or reset
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                last_poll_tick = xTaskGetTickCount();  // update tick status
                break;
            }
            default:
                break;
        }
    }
}


esp_err_t countdown_timer_init(countdown_timer_t *ctx) {
    ctx->state_mux = xSemaphoreCreateMutex();
    if (ctx->state_mux == NULL) {
        return ESP_FAIL;
    }
    if (ctx->update_period_ms <= 0) {
        ctx->update_period_ms = COUNTDOWN_TIMER_TASK_DEFAULT_UPDATE_PERIOD_MS;
    }
    if (ctx->countdown_time_ms <= 0) {
        ctx->countdown_time_ms = 120 * 1000;
    }

    BaseType_t rtos_return = xTaskCreateWithCaps(
        countdown_timer_task,
        "countdown_timer",
        COUNTDOWN_TIMER_TASK_STACK,
        (void *) ctx,
        COUNTDOWN_TIMER_TASK_PRIORITY,
        &ctx->countdown_timer_task_handle,
        HEAPS_CAPS_ALLOC_DEFAULT_FLAGS
    );
    if (rtos_return != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

// The time left as last shown, so a state change redraws the bar's colour without waiting for the task
static int shown_time_left_ms = 0;


static void show_state(countdown_timer_t *ctx) {
    if (lvgl_port_lock(0)) {
        level_band_update_timer(shown_time_left_ms);
        lvgl_port_unlock();
    }
}


void countdown_timer_start(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_READY);
    xTaskNotifyGive(ctx->countdown_timer_task_handle);
    // The task refills the time and shows it
}


void countdown_timer_pause(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_PAUSE);
    show_state(ctx);
}


void countdown_timer_continue(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_RUN);
    xTaskNotifyGive(ctx->countdown_timer_task_handle);

#if USE_BUZZER
    // Start buzzer
    buzzer_run(100, 50, 2, false);
#endif  // USE_BUZZER

    show_state(ctx);
}

/*------------------------------------
LVGL Widgets and Callbacks
-------------------------------------*/


void countdown_timer_button_press_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    countdown_timer_t * countdown_timer = (countdown_timer_t *) lv_event_get_user_data(e);

    // Skip if the button is not visible
    if (!get_countdown_timer_widget_enabled()) {
        return;
    }

    if (code == LV_EVENT_SHORT_CLICKED) {
        countdown_timer_state_t current_state = get_countdown_timer_state(countdown_timer);
        ESP_LOGI(TAG, "Current state %d", current_state);
        switch (current_state)
        {
        case COUNTDOWN_TIMER_EXPIRED:
            countdown_timer_start(countdown_timer);

            break;
        case COUNTDOWN_TIMER_PAUSE:
            countdown_timer_continue(countdown_timer);
            
            break;

        case COUNTDOWN_TIMER_RUN:
            countdown_timer_pause(countdown_timer);
            
            break;
        default:
            break;
        }
    }
    else if (code == LV_EVENT_LONG_PRESSED) {
        countdown_timer_start(countdown_timer);
    }
}



void update_timer_cb(void *p, int time_left_ms) {
    countdown_timer_t * countdown_timer = (countdown_timer_t *) p;

#if USE_BUZZER

    // 30s beep
    if (countdown_timer->countdown_time_ms > 30 * 1000) {
        if ((time_left_ms <= (30 * 1000)) && 
            (time_left_ms > ((30 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(200, 50, 3, false);
        }
    }

    // 20s beep
    if (countdown_timer->countdown_time_ms > 20 * 1000) {
        if ((time_left_ms <= (20 * 1000)) && 
            (time_left_ms > ((20 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(200, 50, 2, false);
        }
    }

    // 10s beep
    if (countdown_timer->countdown_time_ms > 10 * 1000) {
        if ((time_left_ms <= (10 * 1000)) && 
            (time_left_ms > ((10 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(200, 50, 1, false);
        }
    }

    // 5s beep
    if (countdown_timer->countdown_time_ms > 5 * 1000) {
        if ((time_left_ms <= (5 * 1000)) && 
            (time_left_ms > ((5 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(100, 50, 1, false);
        }
    }

    // 4s beep
    if (countdown_timer->countdown_time_ms > 4 * 1000) {
        if ((time_left_ms <= (4 * 1000)) && 
            (time_left_ms > ((4 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(100, 50, 1, false);
        }
    }

    // 3s beep
    if (countdown_timer->countdown_time_ms > 3 * 1000) {
        if ((time_left_ms <= (3 * 1000)) && 
            (time_left_ms > ((3 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(100, 50, 1, false);
        }
    }

    // 2s beep
    if (countdown_timer->countdown_time_ms > 2 * 1000) {
        if ((time_left_ms <= (2 * 1000)) && 
            (time_left_ms > ((2 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(100, 50, 1, false);
        }
    }

    // 1s beep
    if (countdown_timer->countdown_time_ms > 1 * 1000) {
        if ((time_left_ms <= (1 * 1000)) && 
            (time_left_ms > ((1 * 1000) - countdown_timer->update_period_ms))) {
            buzzer_run(1000, 50, 1, false);
        }
    }

#endif  // USE_BUZZER


    // The band shows the time and the bar
    shown_time_left_ms = time_left_ms;
    if (lvgl_port_lock(0)) {
        level_band_update_timer(time_left_ms);
        lvgl_port_unlock();
    }
}


void countdown_timer_update_time(countdown_timer_t *ctx, int new_time_ms) {
    ctx->countdown_time_ms = new_time_ms;
    countdown_timer_start(ctx);
}


void enable_countdown_timer_widget(bool enable) {
    ESP_LOGI(TAG, "Setting countdown timer visibility to %d", enable);
    if (lvgl_port_lock(0)) {
        level_band_show_timer(enable);
        lvgl_port_unlock();
    }
}


bool get_countdown_timer_widget_enabled(void) {
    bool enabled = false;
    if (lvgl_port_lock(0)) {
        enabled = level_band_timer_shown();
        lvgl_port_unlock();
    }

    return enabled;
}


// The timer's task and its link to the band, which shows it (ADR-007); hidden until a preset arms it
void countdown_timer_setup(countdown_timer_t * countdown_timer) {
    countdown_timer->timer_update_cb = update_timer_cb;
    countdown_timer->timer_update_cb_args = countdown_timer;
    ESP_ERROR_CHECK(countdown_timer_init(countdown_timer));
}

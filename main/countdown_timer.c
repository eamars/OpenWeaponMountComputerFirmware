#include <math.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "system_config.h"
#include "countdown_timer.h"
#include "low_power_mode.h"
#include "buzzer.h"
#include "app_cfg.h"

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


// The countdown's beeps at 30, 20, 10, 5, 4, 3, 2 and 1 s left
static void beep_at(countdown_timer_t * countdown_timer, int time_left_ms) {

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

#else
    ESP_UNUSED(countdown_timer);
    ESP_UNUSED(time_left_ms);
#endif  // USE_BUZZER
}


// Beeps, then the listener (the level band) with the time left
static void report(countdown_timer_t *ctx, int time_left_ms) {
    ctx->time_left_ms = time_left_ms;
    beep_at(ctx, time_left_ms);
    if (ctx->timer_update_cb) {
        ctx->timer_update_cb(ctx->timer_update_cb_args, time_left_ms);
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

                report(ctx, ctx->countdown_time_ms);
                
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

                report(ctx, time_left_ms);

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

// A state change is reported with the time left as last reported, so the listener redraws the bar's colour without
// waiting for the task
static void show_state(countdown_timer_t *ctx) {
    if (ctx->timer_update_cb) {
        ctx->timer_update_cb(ctx->timer_update_cb_args, ctx->time_left_ms);
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






void countdown_timer_update_time(countdown_timer_t *ctx, int new_time_ms) {
    ctx->countdown_time_ms = new_time_ms;
    countdown_timer_start(ctx);
}




// Start the timer's task with its listener (ADR-007: the level band shows it, hidden until a preset arms it)
void countdown_timer_setup(countdown_timer_t * countdown_timer, timer_update_cb_t update_cb, void * update_cb_args) {
    countdown_timer->timer_update_cb = update_cb;
    countdown_timer->timer_update_cb_args = update_cb_args;
    ESP_ERROR_CHECK(countdown_timer_init(countdown_timer));
}

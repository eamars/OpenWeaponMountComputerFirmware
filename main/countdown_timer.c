#include <math.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "countdown_timer.h"

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
                time_left_ms = ctx->countdown_time_sec * 1000;

                // Update GUI
                if (ctx->timer_update_cb) {
                    ctx->timer_update_cb(ctx->timer_update_cb_args, ctx->countdown_time_sec);
                }
                
                // Move to next state
                set_countdown_timer_state(ctx, COUNTDOWN_TIMER_PAUSE);
                last_poll_tick = xTaskGetTickCount();  // update tick status
                break;
            }
            case COUNTODWN_TIMER_RUN: {
                // Calculate time left
                time_left_ms -= ctx->update_period_ms;
                if (time_left_ms < 0) {
                    time_left_ms = 0;
                }

                if (ctx->timer_update_cb) {
                    ctx->timer_update_cb(ctx->timer_update_cb_args, (int) ceilf(time_left_ms / 1000.0));
                }

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
    if (ctx->countdown_time_sec <= 0) {
        ctx->countdown_time_sec = 120;
    }

    BaseType_t rtos_return = xTaskCreate(
        countdown_timer_task,
        "countdown_timer",
        COUNTDOWN_TIMER_TASK_STACK,
        (void *) ctx,
        COUNTDOWN_TIMER_TASK_PRIORITY,
        &ctx->countdown_timer_task_handle
    );
    if (rtos_return != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void countdown_timer_start(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_READY);
    xTaskNotifyGive(ctx->countdown_timer_task_handle);
}


void countdown_timer_pause(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_PAUSE);
}


void countdown_timer_continue(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTODWN_TIMER_RUN);
    xTaskNotifyGive(ctx->countdown_timer_task_handle);
}

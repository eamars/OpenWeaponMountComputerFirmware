#include <math.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_lvgl_port.h"

#include "system_config.h"
#include "countdown_timer.h"

#define TAG "CountdownTimer"


lv_obj_t * countdown_timer_arc = NULL;
lv_obj_t * countdown_timer_label = NULL;
lv_obj_t * countdown_timer_button = NULL;
extern system_config_t system_config;

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

    if (lvgl_port_lock(0)) {
        lv_obj_set_style_arc_color(countdown_timer_arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);
        lvgl_port_unlock();
    }
}


void countdown_timer_pause(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_PAUSE);

    if (lvgl_port_lock(0)) {
        lv_obj_set_style_arc_color(countdown_timer_arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);
        lvgl_port_unlock();
    }
}


void countdown_timer_continue(countdown_timer_t *ctx) {
    set_countdown_timer_state(ctx, COUNTDOWN_TIMER_RUN);
    xTaskNotifyGive(ctx->countdown_timer_task_handle);

    if (lvgl_port_lock(0)) {
        lv_obj_set_style_arc_color(countdown_timer_arc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
        lvgl_port_unlock();
    }
}

/*------------------------------------
LVGL Widgets and Callbacks
-------------------------------------*/


void countdown_timer_button_short_press_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    countdown_timer_t * countdown_timer = (countdown_timer_t *) lv_event_get_user_data(e);

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
}


void countdown_timer_button_long_press_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    countdown_timer_t * countdown_timer = (countdown_timer_t *) lv_event_get_user_data(e);

    if (code == LV_EVENT_LONG_PRESSED) {
        countdown_timer_start(countdown_timer);
    }
}


void update_timer_cb(void *p, int time_left_ms) {
    countdown_timer_t * countdown_timer = (countdown_timer_t *) p;
    int time_left_sec = (int) ceilf(time_left_ms / 1000.0);
    int percentage = (int) ceilf(time_left_ms * 100.0 / countdown_timer->countdown_time_ms);
    int minute = time_left_sec / 60;
    int second = time_left_sec % 60;

    // Calculate the percentage 
    if (lvgl_port_lock(0)) {
        lv_label_set_text_fmt(countdown_timer_label, "%d:%02d", minute, second);
        lv_arc_set_value(countdown_timer_arc, percentage);
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
        if (enable) {
            lv_obj_clear_flag(countdown_timer_button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(countdown_timer_arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(countdown_timer_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(countdown_timer_button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(countdown_timer_arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(countdown_timer_label, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
    }
}


bool get_countdown_timer_widget_enabled(void) {
    bool enabled = false;
    if (lvgl_port_lock(0)) {
        enabled = !lv_obj_has_flag(countdown_timer_button, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }

    return enabled;
}

void set_rotation_countdown_timer_widget(lv_display_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        lv_obj_align(countdown_timer_button, LV_ALIGN_CENTER, 0, 0);
    }
    else {
        lv_obj_align(countdown_timer_button, LV_ALIGN_LEFT_MID, 20, 0);
    }
}


lv_obj_t * create_countdown_timer_widget(lv_obj_t * parent, countdown_timer_t * countdown_timer) {
    countdown_timer->timer_update_cb = update_timer_cb;
    countdown_timer->timer_update_cb_args = countdown_timer;
    countdown_timer_init(countdown_timer);

    /* 
    For arc, the main is the background, indicator is the foreground
    */
    countdown_timer_button = lv_btn_create(parent);
    lv_obj_set_style_radius(countdown_timer_button, LV_RADIUS_CIRCLE, LV_PART_MAIN); // Make it fully round
    lv_obj_set_style_bg_opa(countdown_timer_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(countdown_timer_button, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(countdown_timer_button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(countdown_timer_button, 0, LV_PART_MAIN);
    lv_obj_set_size(countdown_timer_button, 150, 150);

    lv_obj_add_event_cb(countdown_timer_button, countdown_timer_button_short_press_event_cb, LV_EVENT_SHORT_CLICKED, (void *) countdown_timer);
    lv_obj_add_event_cb(countdown_timer_button, countdown_timer_button_long_press_event_cb, LV_EVENT_LONG_PRESSED, (void *) countdown_timer);

    countdown_timer_arc = lv_arc_create(countdown_timer_button);
    lv_obj_set_style_arc_color(countdown_timer_arc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    // lv_obj_set_style_arc_color(countdown_timer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(countdown_timer_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    // lv_obj_set_style_arc_width(countdown_timer_arc, 5, 0);

    lv_arc_set_rotation(countdown_timer_arc, 270);
    lv_arc_set_bg_angles(countdown_timer_arc, 0, 360);
    lv_obj_remove_style(countdown_timer_arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
    lv_obj_remove_flag(countdown_timer_arc, LV_OBJ_FLAG_CLICKABLE);  /*To not allow adjusting by click*/
    lv_arc_set_range(countdown_timer_arc, 0, 100);  // 100 divisions for the full arc
    lv_obj_align(countdown_timer_arc, LV_ALIGN_CENTER, 0, 0);

    countdown_timer_label = lv_label_create(countdown_timer_button);

    lv_label_set_text(countdown_timer_label, "0:00");
    lv_obj_set_style_text_color(countdown_timer_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(countdown_timer_label, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_align(countdown_timer_label, LV_ALIGN_CENTER, 0, 0);

    // Set layout based on the rotation
    set_rotation_countdown_timer_widget(system_config.rotation);

    // By default don't show the timer
    enable_countdown_timer_widget(false);

    return countdown_timer_button;
}

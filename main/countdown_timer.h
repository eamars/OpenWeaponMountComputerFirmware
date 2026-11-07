#ifndef COUNTDOWN_TIMER_H_
#define COUNTDOWN_TIMER_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lvgl.h"

#define COUNTDOWN_TIMER_TASK_DEFAULT_UPDATE_PERIOD_MS 250

#ifndef COUNTDOWN_TIMER_TASK_STACK
    #define COUNTDOWN_TIMER_TASK_STACK 4096
#endif  // COUNTDOWN_TIMER_TASK_STACK


#ifndef COUNTDOWN_TIMER_TASK_PRIORITY
    #define COUNTDOWN_TIMER_TASK_PRIORITY 6
#endif  // COUNTDOWN_TIMER_TASK_PRIORITY


typedef enum {
    COUNTDOWN_TIMER_READY,
    COUNTDOWN_TIMER_RUN,
    COUNTDOWN_TIMER_PAUSE,
    COUNTDOWN_TIMER_EXPIRED,
} countdown_timer_state_t;


typedef void (*timer_update_cb_t) (void *, int);


typedef struct {
    TaskHandle_t countdown_timer_task_handle;
    timer_update_cb_t timer_update_cb;
    void *timer_update_cb_args;
    int countdown_time_ms;
    int update_period_ms;
    SemaphoreHandle_t state_mux;
    countdown_timer_state_t countdown_timer_state;
} countdown_timer_t;


esp_err_t countdown_timer_init(countdown_timer_t *ctx);
void countdown_timer_start(countdown_timer_t *ctx);
void countdown_timer_pause(countdown_timer_t *ctx);
void countdown_timer_continue(countdown_timer_t *ctx);
void countdown_timer_update_time(countdown_timer_t *ctx, int new_time_ms);

countdown_timer_state_t get_countdown_timer_state(countdown_timer_t *ctx);
// void set_countdown_timer_state(countdown_timer_t *ctx, countdown_timer_state_t new_state);

/** @brief Start the timer's task; the level view's band shows it (level_band.c) */
void countdown_timer_setup(countdown_timer_t * countdown_timer);
/** @brief Show or hide the timer on the level view (armed by a preset, hidden when disarmed) */
void enable_countdown_timer_widget(bool enable);
bool get_countdown_timer_widget_enabled(void);
/**
 * @brief The timer's controls (the band's time and bar, the hardware button): a tap starts, pauses or continues;
 * a long press re-arms with the full time. User data: the countdown_timer_t.
 */
void countdown_timer_button_press_event_cb(lv_event_t * e);

#endif  // COUNTDOWN_TIMER_H_
#ifndef COUNTDOWN_TIMER_H_
#define COUNTDOWN_TIMER_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lvgl.h"

#define COUNTDOWN_TIMER_TASK_DEFAULT_UPDATE_PERIOD_MS 250

#ifndef COUNTDOWN_TIMER_TASK_STACK
    #define COUNTDOWN_TIMER_TASK_STACK 2048
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

lv_obj_t * create_countdown_timer_widget(lv_obj_t * parent, countdown_timer_t * countdown_timer);
void enable_countdown_timer_widget(bool enable);
bool get_countdown_timer_widget_enabled(void);
void set_rotation_countdown_timer_widget(lv_display_rotation_t rotation);

#endif  // COUNTDOWN_TIMER_H_
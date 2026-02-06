#ifndef BUZZER_H_
#define BUZZER_H_

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app_cfg.h"


#ifndef BUZZER_TASK_PRIORITY
    #define BUZZER_TASK_PRIORITY 4
#endif  // BUZZER_TASK_PRIORITY

#ifndef BUZZER_TASK_STACK
    #define BUZZER_TASK_STACK 4096
#endif  // BUZZER_TASK_STACK

#ifndef BUZZER_FREQ_HZ
    #define BUZZER_FREQ_HZ 4000
#endif  // BUZZER_FREQ_HZ

#ifndef BUZZER_DUTY_CYCLE_PCT
    #define BUZZER_DUTY_CYCLE_PCT 50
#endif  // BUZZER_DUTY_CYCLE_PCT


typedef struct {
    TaskHandle_t buzzer_task_handle;
    QueueHandle_t buzzer_command_queue;
} buzzer_t;


esp_err_t buzzer_init();
esp_err_t buzzer_run(uint32_t on_duration_ms, uint32_t off_duration_ms, uint8_t beep_count, bool block_wait);
esp_err_t buzzer_off(bool block_wait);

#endif  // BUZZER_H_
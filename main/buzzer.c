#include "buzzer.h"
#include "app_cfg.h"
#include "driver/ledc.h"
#include "esp_log.h"


#define TAG "BUZZER"



typedef struct {
    uint32_t on_duration_ms;
    uint32_t off_duration_ms;
    uint8_t beep_count;
} buzzer_ctrl_t;


buzzer_t buzzer_ctx;


void buzzer_set_tone(uint32_t frequency_hz) {
    // Set the frequency of the buzzer
    ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, frequency_hz));
    
    uint32_t current_duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, current_duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL));
}

void buzzer_set_duty(uint32_t duty_percent) {
    uint32_t max_duty = (1 << 13) - 1;  // 13-bit resolution
    uint32_t duty = (duty_percent * max_duty) / 100;
    ESP_LOGI(TAG, "Setting buzzer duty: %d (for %d%%)", duty, duty_percent);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL));
}

void buzzer_task(void *p) {
    buzzer_t *ctx = (buzzer_t *) p;
    buzzer_ctrl_t ctrl;

    // Local state for the beep schedule
    uint8_t beep_left = 0;
    bool state_on = false;

    while (1) {
        TickType_t delay;
        // Check if we are still within a schedule
        if (beep_left == 0) {
            buzzer_set_duty(0);
            delay = portMAX_DELAY;  // Wait indefinitely for the next command
        }
        else {
            if (state_on) {
                buzzer_set_duty(BUZZER_DUTY_CYCLE_PCT);  // on
                delay = pdMS_TO_TICKS(ctrl.on_duration_ms);
                state_on = false;  // Next state will be off
            }
            else {
                buzzer_set_duty(0);  // off
                delay = pdMS_TO_TICKS(ctrl.off_duration_ms);
                state_on = true;  // Next state will be on

                beep_left -= 1;  // Full schedule is compelte
            }
        }

        // Use xQueueReceive with timeout to allow for schedule override and block delay
        if (xQueueReceive(ctx->buzzer_command_queue, &ctrl, delay) == pdPASS) {
            // New command received, update the control and reset the schedule
            ESP_LOGI(TAG, "Received new buzzer command: on_duration=%d ms, off_duration=%d ms, beep_count=%d", 
                ctrl.on_duration_ms, ctrl.off_duration_ms, ctrl.beep_count);

            beep_left = ctrl.beep_count;

            if (beep_left == 0) {
                // No beeps, ensure it's off
                state_on = false;
                buzzer_set_duty(0);
            }
            else {
                // Start with the on state
                state_on = true;
            }
        }
    }
}

esp_err_t buzzer_init() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BUZZER_TIMER,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = BUZZER_FREQ_HZ,                          // This controls the tone. 
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .gpio_num = BUZZER_OUT_PIN,
        .duty = 0,                                // Start with 0 duty (off)
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Set initial state to off
    buzzer_set_duty(0);

    // Create command queue
    buzzer_ctx.buzzer_command_queue = xQueueCreate(1, sizeof(buzzer_ctrl_t));
    if (buzzer_ctx.buzzer_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create buzzer command queue");
        return ESP_FAIL;
    }

    // Initialize task
    BaseType_t rtos_return = xTaskCreate(
        buzzer_task,
        "buzzer_task",
        BUZZER_TASK_STACK,
        (void *) &buzzer_ctx,
        BUZZER_TASK_PRIORITY,
        &buzzer_ctx.buzzer_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to create buzzer_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buzzer Initialization complete");

    return ESP_OK;
}


esp_err_t buzzer_run(uint32_t on_duration_ms, uint32_t off_duration_ms, uint8_t beep_count, bool block_wait) {
    buzzer_ctrl_t ctrl = {
        .on_duration_ms = on_duration_ms,
        .off_duration_ms = off_duration_ms,
        .beep_count = beep_count
    };

    // Send to queue
    xQueueSend(buzzer_ctx.buzzer_command_queue, &ctrl, block_wait ? portMAX_DELAY : 0);

    return ESP_OK;
}

esp_err_t buzzer_off(bool block_wait) {
    return buzzer_run(0, 0, 0, block_wait);
}

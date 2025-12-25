#include "buzzer.h"
#include "app_cfg.h"
#include "driver/ledc.h"


esp_err_t buzzer_init() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,                          // This controls the tone. 
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
         
    };

    return ESP_OK;
}
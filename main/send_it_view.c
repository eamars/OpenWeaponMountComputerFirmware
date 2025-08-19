#include "send_it_view.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "app_cfg.h"
#include "bno085.h"
#include "digital_level_view.h"
#include "system_config.h"

#define TAG "SendItView"

lv_obj_t * left_tilt_led;
lv_obj_t * center_tilt_led;
lv_obj_t * right_tilt_led;

static TaskHandle_t sensor_event_poller_task_handle;
static SemaphoreHandle_t sensor_event_poller_task_control;
extern bno085_ctx_t bno085_dev;
extern digital_level_view_config_t digital_level_view_config;
extern system_config_t system_config;


void update_send_it_view(float roll_rad) {
    float roll_deg = RAD_TO_DEG(roll_rad);

    if (roll_deg < -5) {
        lv_led_set_color(center_tilt_led, lv_palette_main(LV_PALETTE_LIGHT_BLUE));

        lv_led_on(left_tilt_led);
        lv_led_off(center_tilt_led);
        lv_led_off(right_tilt_led);
    }
    else if (roll_deg < -2) {
        lv_led_set_color(center_tilt_led, lv_palette_main(LV_PALETTE_LIGHT_BLUE));

        lv_led_on(left_tilt_led);
        lv_led_on(center_tilt_led);
        lv_led_off(right_tilt_led);
    }
    else if (roll_deg > -1 && roll_deg < 1) {
        lv_led_set_color(center_tilt_led, lv_palette_main(LV_PALETTE_LIGHT_GREEN));

        lv_led_off(left_tilt_led);
        lv_led_on(center_tilt_led);
        lv_led_off(right_tilt_led);
    }
    else if (roll_deg > 5) {
        lv_led_set_color(center_tilt_led, lv_palette_main(LV_PALETTE_RED));

        lv_led_off(left_tilt_led);
        lv_led_off(center_tilt_led);
        lv_led_on(right_tilt_led);
    }
    else if (roll_deg > 2) {
        lv_led_set_color(center_tilt_led, lv_palette_main(LV_PALETTE_RED));

        lv_led_off(left_tilt_led);
        lv_led_on(center_tilt_led);
        lv_led_on(right_tilt_led);
    }
}

 static void sensor_event_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        // Block wait for the task is allowed to run
        xSemaphoreTake(sensor_event_poller_task_control, portMAX_DELAY);

        // Wait for data
        float roll, pitch;
        bno085_wait_for_game_rotation_vector_roll_pitch(&bno085_dev, &roll, &pitch, true);

        // Redraw the screen
        if (lvgl_port_lock(5)) {  // prevent a deadlock if the LVGL event wants to continue
            update_send_it_view(roll + digital_level_view_config.user_roll_rad_offset);
            lvgl_port_unlock();
        }

        xSemaphoreGive(sensor_event_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
    }
}


void set_rotation_send_it_view(lv_disp_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        lv_obj_align(left_tilt_led, LV_ALIGN_CENTER, 0, 100);
        lv_obj_align(center_tilt_led, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(right_tilt_led, LV_ALIGN_CENTER, 0, -100);
    }
    else {
        lv_obj_align(left_tilt_led, LV_ALIGN_CENTER, -100, 0);
        lv_obj_align(center_tilt_led, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(right_tilt_led, LV_ALIGN_CENTER, 100, 0);
    }
}

void create_send_it_view(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    left_tilt_led = lv_led_create(parent);
    
    lv_led_set_color(left_tilt_led, lv_palette_main(LV_PALETTE_LIGHT_BLUE));
    lv_led_off(left_tilt_led);
    center_tilt_led = lv_led_create(parent);
    
    lv_led_set_color(center_tilt_led, lv_palette_main(LV_PALETTE_LIGHT_GREEN));
    lv_led_off(center_tilt_led);
    right_tilt_led = lv_led_create(parent);

    // Build layout based on screen rotation
    set_rotation_send_it_view(system_config.rotation);
    
    lv_led_set_color(right_tilt_led, lv_palette_main(LV_PALETTE_RED));

    lv_led_off(right_tilt_led);

    // Set initial state
    update_send_it_view(0);

    // Create event poller task 
    sensor_event_poller_task_control = xSemaphoreCreateBinary();
    BaseType_t rtos_return = xTaskCreate(
        sensor_event_poller_task, 
        "send_it_poller", 
        SENSOR_EVENT_POLLER_TASK_STACK,
        NULL,
        SENSOR_EVENT_POLLER_TASK_PRIORITY,
        &sensor_event_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_poller");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}

void enable_send_it_view(bool enable) {
    ESP_LOGI(TAG, "Send It View %d", enable);
    if (enable) {
        xSemaphoreGive(sensor_event_poller_task_control);
    }
    else {
        xSemaphoreTake(sensor_event_poller_task_control, portMAX_DELAY);
    }
}


void send_it_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_send_it_view(system_config.rotation);
}
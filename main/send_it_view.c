#include "send_it_view.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "app_cfg.h"
#include "bno085.h"
#include "digital_level_view_controller.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "common.h"

#define TAG "SendItView"

lv_obj_t * left_tilt_led;
lv_obj_t * center_tilt_led;
lv_obj_t * right_tilt_led;
lv_obj_t * parent_view;


static TaskHandle_t sensor_event_poller_task_handle;
static SemaphoreHandle_t sensor_event_poller_task_control;
extern bno085_ctx_t bno085_dev;
extern system_config_t system_config;
extern float sensor_pitch_thread_unsafe, sensor_roll_thread_unsafe;
extern digital_level_view_config_t digital_level_view_config;

void update_send_it_view(float roll_rad) {
    float roll_deg = RAD_TO_DEG(roll_rad);

    // Set base colour
    // FIXME: Those colours needed to be set dynamically based on digital_level_view_config
    //  It cannot be set in create_send_it_view as the object digital_level_view_config is not loaded from NVS yet
    lv_obj_set_style_bg_color(parent_view, lv_palette_main(digital_level_view_config.colour_foreground), 0);

    if (roll_deg < -3 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
    }
    else if (roll_deg < -2 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_left_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
    }
    else if (roll_deg > -1 * digital_level_view_config.delta_level_threshold && roll_deg < 1 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_horizontal_level_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);

    }
    else if (roll_deg > 5 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_PART_MAIN);

    }
    else if (roll_deg > 2 * digital_level_view_config.delta_level_threshold) {
        lv_obj_set_style_bg_color(left_tilt_led, lv_palette_main(digital_level_view_config.colour_foreground), LV_PART_MAIN);
        lv_obj_set_style_bg_color(center_tilt_led, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_PART_MAIN);
        lv_obj_set_style_bg_color(right_tilt_led, lv_palette_main(digital_level_view_config.colour_right_tilt_indicator), LV_PART_MAIN);
    }
}

 static void sensor_event_poller_task(void *p) {
    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        // Block wait for the task is allowed to run
        xSemaphoreTake(sensor_event_poller_task_control, portMAX_DELAY);

        // Wait for data
        esp_err_t err = bno085_wait_for_game_rotation_vector_roll_pitch(&bno085_dev, &sensor_roll_thread_unsafe, &sensor_pitch_thread_unsafe, true);

        if (err == ESP_OK) {
            float display_roll = get_relative_roll_angle_rad_thread_unsafe();

            // Redraw the screen
            if (lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {  // prevent a deadlock if the LVGL event wants to continue
                update_send_it_view(display_roll);
                lvgl_port_unlock();
            }
        }

        xSemaphoreGive(sensor_event_poller_task_control);  // allow the task to run

        vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS));
    }
}


void set_rotation_send_it_view(lv_disp_rotation_t rotation) {
    // if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
    //     lv_obj_set_size(left_tilt_led, lv_pct(100), lv_pct(33));
    //     lv_obj_align(left_tilt_led, LV_ALIGN_BOTTOM_MID, 0, 0);

    //     lv_obj_set_size(center_tilt_led, lv_pct(100), lv_pct(33));
    //     lv_obj_align(center_tilt_led, LV_ALIGN_CENTER, 0, 0);

    //     lv_obj_set_size(right_tilt_led, lv_pct(100), lv_pct(33));
    //     lv_obj_align(right_tilt_led, LV_ALIGN_TOP_MID, 0, 0);

    // }
    // else {
    //     lv_obj_set_size(left_tilt_led, lv_pct(33), lv_pct(100));
    //     lv_obj_align(left_tilt_led, LV_ALIGN_LEFT_MID, 0, 0);

    //     lv_obj_set_size(center_tilt_led, lv_pct(33), lv_pct(100));
    //     lv_obj_align(center_tilt_led, LV_ALIGN_CENTER, 0, 0);

    //     lv_obj_set_size(right_tilt_led, lv_pct(33), lv_pct(100));
    //     lv_obj_align(right_tilt_led, LV_ALIGN_RIGHT_MID, 0, 0);
    // }
}

void create_send_it_view(lv_obj_t *parent) {
    parent_view = parent;
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    left_tilt_led = lv_obj_create(parent);
    lv_obj_set_style_border_width(left_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(left_tilt_led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(left_tilt_led, 0, LV_PART_MAIN);


    right_tilt_led = lv_obj_create(parent);
    lv_obj_set_style_border_width(right_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(right_tilt_led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(right_tilt_led, 0, LV_PART_MAIN);


    center_tilt_led = lv_obj_create(parent);
    lv_obj_set_style_border_width(center_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(center_tilt_led, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(center_tilt_led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(center_tilt_led, 0, LV_PART_MAIN);

    lv_obj_set_size(left_tilt_led, lv_pct(33), lv_pct(100));
    lv_obj_align(left_tilt_led, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_set_size(center_tilt_led, lv_pct(33), lv_pct(100));
    lv_obj_align(center_tilt_led, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_size(right_tilt_led, lv_pct(33), lv_pct(100));
    lv_obj_align(right_tilt_led, LV_ALIGN_RIGHT_MID, 0, 0);


    // Build layout based on screen rotation
    set_rotation_send_it_view(system_config.rotation);

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
        xSemaphoreTake(sensor_event_poller_task_control, pdMS_TO_TICKS(200));
    }
}


void send_it_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_send_it_view(system_config.rotation);
}
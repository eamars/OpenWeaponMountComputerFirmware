#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" 

#include "sh2_err.h"


#include "app_cfg.h"
#include "sensor_calibration_view.h"
#include "low_power_mode.h"
#include "bno085.h"
#include "sensor_config.h"


#define TAG "SensorCalibrationView"

typedef enum {
    RV_POLLER_RUN = (1 << 0),
} sensor_calibration_task_control_bit_e;


extern bno085_ctx_t * bno085_dev;
extern sensor_config_t sensor_config;

static EventGroupHandle_t sensor_calibration_task_control;

lv_obj_t * rv_measurements_label = NULL;
lv_obj_t * rv_accuracy_label = NULL;
lv_obj_t * last_tile_before_enter_calibration = NULL;

extern lv_obj_t * main_tileview;                 // from main_tileview.c
extern lv_obj_t * default_tile;                         // from main_tileview.c`

HEAPS_CAPS_ATTR static char rv_measurements[64] = {0};
HEAPS_CAPS_ATTR static char rv_accuracy_str[32] = {0};

void rotation_vector_poller_task(void *p) {
    float roll, pitch, yaw, accuracy;

    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    while (1) {
        xEventGroupWaitBits(sensor_calibration_task_control, RV_POLLER_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        while (xEventGroupGetBits(sensor_calibration_task_control) & RV_POLLER_RUN) {
            
            esp_err_t ret = bno085_wait_for_rotation_vector_roll_pitch_yaw(bno085_dev, &roll, &pitch, &yaw, &accuracy, true);

            if (ret == ESP_OK) {
                // ESP_LOGI(TAG, "Received rotation vector report: roll=%f, pitch=%f, yaw=%f, accuracy=%f", roll, pitch, yaw, accuracy);
                snprintf(rv_measurements, sizeof(rv_measurements), "Roll: %.2f\nPitch: %.2f\nYaw: %.2f", roll, pitch, yaw);
                snprintf(rv_accuracy_str, sizeof(rv_accuracy_str), "Accuracy: %.2f", accuracy);

                // ESP_LOGI(TAG, "Updated RV measurements: %s, %s", rv_measurements, rv_accuracy_str);

                // Force an update
                if (lvgl_port_lock(0)) {
                    lv_obj_invalidate(lv_screen_active());
                    lvgl_port_unlock();
                }
            }
        }
    }
}

void on_tare_button_clicked(lv_event_t * e) {
    ESP_LOGI(TAG, "Tare function called");

    int sh2_ret;

    sh2_ret = sh2_setTareNow(SH2_TARE_X | SH2_TARE_Y | SH2_TARE_Z, SH2_TARE_BASIS_ROTATION_VECTOR);
    if (sh2_ret == SH2_OK) {
        ESP_LOGI(TAG, "Tare command sent successfully");

        // persist th tare
        sh2_ret = sh2_persistTare();
        if (sh2_ret == SH2_OK) {
            ESP_LOGI(TAG, "Tare persisted successfully");
        } else {
            ESP_LOGE(TAG, "Failed to persist tare: %d", sh2_ret);
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to send tare command: %d", sh2_ret);
    }

    // Move back to the previous tile
    if (last_tile_before_enter_calibration) {
        lv_tileview_set_tile(main_tileview, last_tile_before_enter_calibration, LV_ANIM_OFF);
    }
    else {
        // Move to default tile
        lv_tileview_set_tile(main_tileview, default_tile, LV_ANIM_OFF);
    }
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
}


void enter_sensor_calibration_mode(bool enable) {
    ESP_LOGI(TAG, "Sensor Calibration Mode %s", enable ? "enabled" : "disabled");

    if (enable) {
        prevent_idle_mode_enter(true);
        
        // Enable RV report from BNO085
        ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, SENSOR_ROTATION_VECTOR_REPORT_PERIOD_MS));

        // Enable the poller task to update the RV measurement on the screen
        xEventGroupSetBits(sensor_calibration_task_control, RV_POLLER_RUN);
    }
    else {
        prevent_idle_mode_enter(false);

        // Disable the poller task
        xEventGroupClearBits(sensor_calibration_task_control, RV_POLLER_RUN);

        // Set RV report back to idle mode period
        ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, SENSOR_ROTATION_VECTOR_LOW_POWER_MODE_REPORT_PERIOD_MS));
    }
}

void create_sensor_calibration_view(lv_obj_t * parent) {
    // Create a static label to show the sensor calibration status
    rv_measurements_label = lv_label_create(parent);
    lv_label_set_text_static(rv_measurements_label, rv_measurements);
    lv_obj_set_align(rv_measurements_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_text_color(rv_measurements_label, lv_color_black(), LV_PART_MAIN);

    rv_accuracy_label = lv_label_create(parent);
    lv_label_set_text_static(rv_accuracy_label, rv_accuracy_str);
    lv_obj_set_align(rv_accuracy_label, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_text_color(rv_accuracy_label, lv_color_black(), LV_PART_MAIN);


    // A button to run tare command. 
    lv_obj_t * tare_button = lv_btn_create(parent);
    lv_obj_set_size(tare_button, LV_PCT(30), LV_PCT(30));
    lv_obj_center(tare_button);
    lv_obj_add_event_cb(tare_button, on_tare_button_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t * tare_label = lv_label_create(tare_button);
    lv_label_set_text(tare_label, "Tare");
    lv_obj_center(tare_label);
    // set font
    lv_obj_set_style_text_font(tare_label, &lv_font_montserrat_40, 0);


    // Initialize the task control
    sensor_calibration_task_control = xEventGroupCreate();

    
    // Create RV report poller
    BaseType_t rtos_return = xTaskCreate(
        rotation_vector_poller_task,
        "RVReportPoller",
        SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_STACK,
        NULL,
        SENSOR_CALIBRATION_VIEW_RV_REPORT_POLLER_TASK_PRIORITY,
        NULL
    );

    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rotation vector poller task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}
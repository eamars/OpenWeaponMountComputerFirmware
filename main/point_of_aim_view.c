#include <math.h>

#include "point_of_aim_view.h"
#include "bno085.h"
#include "sensor_config.h"
#include "app_cfg.h"
#include "common.h"
#include "esp_lvgl_port.h"
#include "system_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

#include "lvgl.h"

#define TAG "POIView"
#define TARGET_RADIUS_MARGIN_PX 5      // target margin relates to the chart size (shorter edge)
#define MRAD_PER_DIV    0.3f           // 

typedef struct {
    float user_yaw_rad_offset;
    float user_pitch_rad_offset;
    float target_distance;
    float target_diameter;

    // Below variables are calcualted based on target distance and fovs
    float pixel_per_meter;
    float target_scale;
} point_of_aim_view_config_t;


HEAPS_CAPS_ATTR point_of_aim_view_config_t point_of_aim_view_config;
const point_of_aim_view_config_t default_point_of_aim_view_config = {
    .user_yaw_rad_offset = 0.0f,
    .user_pitch_rad_offset = 0.0f,
    
    .target_distance = 100,     
    .target_diameter = 1.118,   // 44" target
};

const float icfra_target_radius_ratio[] = {
    0.727,  // 32"/44"
    0.455,  // 20"/44"
    0.227,  // 10"/44"
    0,
};


#define SENSOR_POLL_EVENT_RUN (1 << 0)
static EventGroupHandle_t sensor_task_control;
static TaskHandle_t sensor_event_poller_task_handle;
static lv_obj_t * chart;
static lv_chart_series_t * data_series;

extern bno085_ctx_t * bno085_dev;
extern sensor_config_t sensor_config;
extern system_config_t system_config;

const float eps = 1e-6f;
static float sensor_rv_pitch_thread_unsafe, sensor_rv_yaw_thread_unsafe;

IRAM_ATTR esp_err_t euler_to_xy(float pitch, float yaw, float *out_x, float *out_y) {
    *out_x = -point_of_aim_view_config.target_distance * tanf(yaw);
    *out_y = -point_of_aim_view_config.target_distance * tanf(pitch);

    return ESP_OK;
}


static void sensor_event_poller_task(void *p) {
    // Disable the task watchdog as the task is expected to block indefinitely
    esp_task_wdt_delete(NULL);

    TickType_t last_poll_tick = xTaskGetTickCount();

    while (1) {
        xEventGroupWaitBits(sensor_task_control, SENSOR_POLL_EVENT_RUN, pdFALSE, pdFALSE, portMAX_DELAY);

        // Block wait for event
        while (xEventGroupGetBits(sensor_task_control) & SENSOR_POLL_EVENT_RUN) {
            // Wait for rotation vector
            float roll;
            if (bno085_wait_for_game_rotation_vector_roll_pitch_yaw(bno085_dev, &roll, &sensor_rv_pitch_thread_unsafe, &sensor_rv_yaw_thread_unsafe, false) == ESP_OK) {
                // Round angle
                float pitch, yaw;
                pitch = wrap_angle(sensor_rv_pitch_thread_unsafe - point_of_aim_view_config.user_pitch_rad_offset);
                yaw = wrap_angle(sensor_rv_yaw_thread_unsafe - point_of_aim_view_config.user_yaw_rad_offset);

                // ESP_LOGI(TAG, "Roll: %f, Pitch: %f, Yaw: %f", roll, pitch, yaw);
                float proj_x_m = 0, proj_y_m = 0;

                euler_to_xy(pitch, yaw, &proj_x_m, &proj_y_m);
                // ESP_LOGI(TAG, "X: %f, Y: %f", proj_x, proj_y);

                // Display
                if (lvgl_port_lock(0)) {
                    // Convert to mm and send to drawing
                    lv_chart_set_next_value2(chart, data_series, (int32_t) (proj_x_m * 1000), (int32_t) (proj_y_m * 1000));
                    lvgl_port_unlock();
                }
                
            }

            vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(20));
        }
    }
}


void enable_point_of_aim_view(bool enable) {
    if (enable) {
        // Enable rotation vector report
        if (sensor_config.enable_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS));
        }

        // Allow task to run
        xEventGroupSetBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);

    }
    else {
        // Disable rotation vector report
        if (sensor_config.enable_rotation_vector_report) {
            ESP_ERROR_CHECK(bno085_enable_game_rotation_vector_report(bno085_dev, 0));
        }

        // Stop task
        xEventGroupClearBits(sensor_task_control, SENSOR_POLL_EVENT_RUN);
    }
}


static void chart_touch_event_cb(lv_event_t *e) {
    // Record current point of aim
    point_of_aim_view_config.user_yaw_rad_offset = sensor_rv_yaw_thread_unsafe;
    point_of_aim_view_config.user_pitch_rad_offset = sensor_rv_pitch_thread_unsafe;
}


static void chart_draw_event_cb(lv_event_t *e) {
    lv_layer_t * layer = lv_event_get_layer(e);
    lv_obj_t * chart = lv_event_get_target(e);

    // Get chart area and coordinate
    lv_area_t coords;
    lv_obj_get_coords(chart, &coords);

    // Center of chart
    lv_coord_t cx = (coords.x1 + coords.x2) / 2;
    lv_coord_t cy = (coords.y1 + coords.y2) / 2;

    // Shortest half-dimension
    lv_coord_t half_w = lv_area_get_width(&coords) / 2;
    lv_coord_t half_h = lv_area_get_height(&coords) / 2;
    lv_coord_t radius_outer = LV_MIN(half_w, half_h) - TARGET_RADIUS_MARGIN_PX;

    // === Filled black circle (disk) ===
    lv_draw_rect_dsc_t dsc_fill;
    lv_draw_rect_dsc_init(&dsc_fill);
    dsc_fill.bg_color = lv_color_black();
    dsc_fill.bg_opa   = LV_OPA_COVER;
    dsc_fill.radius   = LV_RADIUS_CIRCLE;

    lv_area_t circle_area;
    circle_area.x1 = cx - radius_outer;
    circle_area.y1 = cy - radius_outer;
    circle_area.x2 = cx + radius_outer;
    circle_area.y2 = cy + radius_outer;

    lv_draw_rect(layer, &dsc_fill, &circle_area);

    // Draw circle
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);

    // === Three thin white circles inside ===
    for (uint8_t idx = 0; icfra_target_radius_ratio[idx] != 0; idx += 1) {
        lv_draw_arc_dsc_t dsc_inner;
        lv_draw_arc_dsc_init(&dsc_inner);
        dsc_inner.color = lv_color_white();
        dsc_inner.width = 2;              // thin line
        dsc_inner.center.x = cx;
        dsc_inner.center.y = cy;
        dsc_inner.radius   = radius_outer * icfra_target_radius_ratio[idx];
        dsc_inner.start_angle = 0;
        dsc_inner.end_angle   = 360;
        lv_draw_arc(layer, &dsc_inner);
    }
}



void create_point_of_aim_view(lv_obj_t * parent) {
    // Copy configuration
    // TODO: Load from NVS
    memcpy(&point_of_aim_view_config, &default_point_of_aim_view_config, sizeof(point_of_aim_view_config));

    chart = lv_chart_create(parent);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_size(chart, lv_pct(100), lv_pct(100));
    lv_obj_set_align(chart, LV_ALIGN_CENTER);

    // Set data type to scatter
    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER);

    // Calculate divisions
    lv_obj_update_layout(chart);
    int32_t chart_width = lv_obj_get_width(chart);
    int32_t chart_height = lv_obj_get_height(chart);
    float projected_target_diameter_px = LV_MIN(chart_width, chart_height) - (2 * TARGET_RADIUS_MARGIN_PX);
    point_of_aim_view_config.pixel_per_meter = projected_target_diameter_px / point_of_aim_view_config.target_diameter;

    ESP_LOGI(TAG, "pixel_per_meter: %f", point_of_aim_view_config.pixel_per_meter);

    // Calculate division
    // float meter_per_division = point_of_aim_view_config.target_distance * MRAD_PER_DIV * 0.001;
    // float pixel_per_divsion = point_of_aim_view_config.pixel_per_meter * meter_per_division;
    // // ESP_LOGI(TAG, "meter_per_division: %f, pixel_per_meter: %f, pixel_per_divsion: %f", meter_per_division, pixel_per_meter, pixel_per_divsion);
    // int8_t hdiv = chart_width / pixel_per_divsion;
    // int8_t vdiv = chart_height / pixel_per_divsion;
    // // ESP_LOGI(TAG, "hdiv: %d, vdiv: %d", hdiv, vdiv);
    // lv_chart_set_div_line_count(chart, hdiv, vdiv);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_obj_set_style_line_color(chart, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);


    // Make it touchable so I can zero my POI by touching the screen
    lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chart, chart_touch_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    // Hook to some drawing functions to override drawing the background and lines
    lv_obj_add_event_cb(chart, chart_draw_event_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    // lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);  // start the initial event callback

    // Calculate charts in milimeters
    int32_t y_range_2 = chart_width * 1000 / (point_of_aim_view_config.pixel_per_meter * 2.0);
    int32_t x_range_2 = chart_height * 1000 / (point_of_aim_view_config.pixel_per_meter * 2.0);
    ESP_LOGI(TAG, "x_range_2: %ld, y_range_2: %ld", x_range_2, y_range_2);


    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_X, -x_range_2, x_range_2);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, -y_range_2, y_range_2);

    lv_chart_set_point_count(chart, 25);  // for now set to 1 point

    // Create data series
    data_series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);

    // Initialize the report record
    ESP_ERROR_CHECK(bno085_enable_rotation_vector_report(bno085_dev, 0));

    // Task controller
    sensor_task_control = xEventGroupCreate();
    if (sensor_task_control == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor_task_control");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    BaseType_t rtos_return = xTaskCreate(
        sensor_event_poller_task,
        "poi_sensor_poller",
        POI_EVENT_POLLER_TASK_STACK,
        NULL,
        POI_EVENT_POLLER_TASK_PRIORITY,
        &sensor_event_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for poi_sensor_poller");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
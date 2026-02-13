#include "lvgl_display.h"
#include "app_cfg.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "bsp.h"
#include "esp_check.h"
#include "esp_err.h"

#include "system_config.h"
#include "main_tileview.h"


#define TAG "LVGLDisplay"


typedef enum {
    LVGL_DISPLAY_IS_READY = (1 << 0),
} lvgl_display_stateful_event_e;


// Initialize SPI touch screen and I2C display
esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;

// LVGL touch input device
lv_indev_t *lvgl_touch_handle = NULL;  // allow the low power module to inject callback

// Event control
EventGroupHandle_t lvgl_display_event_group = NULL;


// External services
extern system_config_t system_config;


static inline esp_err_t create_lvgl_display_event_group() {
    // If not created, then create the event group. This function may be called before the `lvgl_display_init()`. 
    if (lvgl_display_event_group == NULL) {
        lvgl_display_event_group = xEventGroupCreate();
        if (lvgl_display_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create lvgl_display_event_group");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}


void IRAM_ATTR lvgl_port_rounder_callback(lv_area_t * area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of area down to the nearest even number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;

    // round the end of area up to the nearest odd number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}


esp_err_t lvgl_display_init(i2c_master_bus_handle_t tp_i2c_handle) {
    esp_err_t ret;

    // Create event group
    ESP_ERROR_CHECK(create_lvgl_display_event_group());

    // Initialize display modules
    ESP_ERROR_CHECK(display_init(&io_handle, &panel_handle, system_config.screen_brightness_normal_pct));
    ESP_LOGI(TAG, "Display initialized successfully");

    ESP_ERROR_CHECK(touchscreen_init(&touch_handle, tp_i2c_handle, DISP_H_RES_PIXEL, DISP_V_RES_PIXEL, DISP_ROTATION));
    ESP_LOGI(TAG, "Touchscreen initialized successfully");

    // Initialize LVGL
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack_caps = HEAPS_CAPS_ALLOC_DEFAULT_FLAGS;
    lvgl_cfg.task_stack = 8192;
    
    ret = lvgl_port_init(&lvgl_cfg);
    ESP_ERROR_CHECK(ret);

    // Add display to LVGL
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = DISP_H_RES_PIXEL * DISP_V_RES_PIXEL,
        .trans_size = 8192,
        .double_buffer = true,
        .hres = DISP_H_RES_PIXEL,
        .vres = DISP_V_RES_PIXEL,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rounder_cb = lvgl_port_rounder_callback,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false
        },
        .flags = {
            .swap_bytes = true,
            .sw_rotate = true,
            .buff_spiram = true,
            .buff_dma = false,
            .direct_mode = false,
            .full_refresh = false
        }
    };

    lv_display_t *lvgl_disp = NULL;
    // lv_indev_t *lvgl_touch_indev = NULL;
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // Add touch input to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp, 
        .handle = touch_handle
    };
    lvgl_touch_handle = lvgl_port_add_touch(&touch_cfg);

    // Add button input to LVGL
    // lv_group_t * input_group = lv_group_create();
    // lv_indev_t * btn_gpio0 = lv_indev_create();
    // lv_indev_set_type(btn_gpio0, LV_INDEV_TYPE_KEYPAD);
    // lv_indev_set_read_cb(btn_gpio0, btn_gpio_read);
    // lv_indev_set_group(btn_gpio0, input_group);


    // Create LVGL application
    if (lvgl_port_lock(0)) {
        create_main_tileview(lv_screen_active());
        lv_display_set_rotation(lvgl_disp, system_config.rotation);
        lvgl_port_unlock();
    }

    // Set display ready event
    xEventGroupSetBits(lvgl_display_event_group, LVGL_DISPLAY_IS_READY);

    ESP_LOGI(TAG, "LVGL display initialized successfully");

    return ret;
}


esp_err_t lvgl_display_wait_for_ready(uint32_t block_wait_ms) {
    create_lvgl_display_event_group();

    // Wait for display ready event
    EventBits_t asserted_bits = xEventGroupWaitBits(
        lvgl_display_event_group, 
        LVGL_DISPLAY_IS_READY, 
        pdFALSE,        // don't clear on assert
        pdTRUE,         // wait for all bits to assert
        pdMS_TO_TICKS(block_wait_ms)
    );

    if (asserted_bits & LVGL_DISPLAY_IS_READY) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}


bool lvgl_display_is_ready() {
    create_lvgl_display_event_group();
    EventBits_t asserted_bits = xEventGroupGetBits(lvgl_display_event_group);

    return asserted_bits & LVGL_DISPLAY_IS_READY;
}
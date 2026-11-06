#include "lvgl_display.h"
#include "app_cfg.h"
#include "display_flush.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "bsp.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_check.h"

#include <inttypes.h>

#include "esp_task_wdt.h"

#include "system_config.h"
#include "main_tileview.h"
#include "low_power_mode.h"
#include "ui_bridge.h"

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
lv_group_t * button_input_group = NULL;

// Event control
EventGroupHandle_t lvgl_display_event_group = NULL;


// External services
extern system_config_t system_config;


#if DISPLAY_PROFILING_ENABLED
typedef struct {
    uint64_t render_start_us;
    uint64_t flush_start_us;
    uint64_t flush_wait_start_us;
    uint64_t report_start_us;

    uint32_t refr_count;
    uint32_t render_count;
    uint32_t flush_count;
    uint32_t flush_wait_count;
    uint32_t invalidate_count;
    uint32_t full_flush_count;
    uint32_t full_invalidate_count;

    uint64_t render_us_total;
    uint64_t flush_cb_us_total;
    uint64_t flush_wait_us_total;
    uint64_t flush_px_total;
    uint64_t invalidate_px_total;

    uint32_t render_us_max;
    uint32_t flush_cb_us_max;
    uint32_t flush_wait_us_max;
    uint32_t flush_px_max;
    uint32_t invalidate_px_max;
} display_profile_t;

static display_profile_t display_profile;
static size_t display_profile_qspi_actual_max_transfer_size;

static uint64_t display_profile_now_us() {
    return (uint64_t) esp_timer_get_time();
}

static uint32_t display_profile_area_px(const lv_area_t *area) {
    if (area == NULL || area->x2 < area->x1 || area->y2 < area->y1) {
        return 0;
    }

    return (uint32_t) lv_area_get_size(area);
}

static void display_profile_add_duration(uint64_t start_us, uint64_t *total_us, uint32_t *max_us) {
    if (start_us == 0) {
        return;
    }

    uint64_t elapsed_us = display_profile_now_us() - start_us;
    *total_us += elapsed_us;
    if (elapsed_us > *max_us) {
        *max_us = (uint32_t) elapsed_us;
    }
}

static void display_profile_reset_window() {
    display_profile.refr_count = 0;
    display_profile.render_count = 0;
    display_profile.flush_count = 0;
    display_profile.flush_wait_count = 0;
    display_profile.invalidate_count = 0;
    display_profile.full_flush_count = 0;
    display_profile.full_invalidate_count = 0;
    display_profile.render_us_total = 0;
    display_profile.flush_cb_us_total = 0;
    display_profile.flush_wait_us_total = 0;
    display_profile.flush_px_total = 0;
    display_profile.invalidate_px_total = 0;
    display_profile.render_us_max = 0;
    display_profile.flush_cb_us_max = 0;
    display_profile.flush_wait_us_max = 0;
    display_profile.flush_px_max = 0;
    display_profile.invalidate_px_max = 0;
    display_profile.report_start_us = display_profile_now_us();
}

static void display_profile_report_timer_cb(lv_timer_t *timer) {
    ESP_UNUSED(timer);

    uint64_t now_us = display_profile_now_us();
    uint32_t window_ms = (uint32_t) ((now_us - display_profile.report_start_us) / 1000);
    uint32_t fps_x10 = window_ms ? (uint32_t) ((display_profile.refr_count * 10000ULL) / window_ms) : 0;
    uint32_t render_avg_us = display_profile.render_count ?
        (uint32_t) (display_profile.render_us_total / display_profile.render_count) : 0;
    uint32_t flush_cb_avg_us = display_profile.flush_count ?
        (uint32_t) (display_profile.flush_cb_us_total / display_profile.flush_count) : 0;
    uint32_t flush_wait_avg_us = display_profile.flush_wait_count ?
        (uint32_t) (display_profile.flush_wait_us_total / display_profile.flush_wait_count) : 0;
    uint32_t flush_px_avg = display_profile.flush_count ?
        (uint32_t) (display_profile.flush_px_total / display_profile.flush_count) : 0;
    uint32_t invalidate_px_avg = display_profile.invalidate_count ?
        (uint32_t) (display_profile.invalidate_px_total / display_profile.invalidate_count) : 0;

    ESP_LOGI(TAG,
        "display_prof: window=%" PRIu32 "ms fps=%" PRIu32 ".%" PRIu32
        " refr=%" PRIu32 " render=%" PRIu32 " avg/max_render=%" PRIu32 "/%" PRIu32 "us"
        " flush=%" PRIu32 " full_flush=%" PRIu32 " avg/max_flush_px=%" PRIu32 "/%" PRIu32
        " avg/max_flush_cb=%" PRIu32 "/%" PRIu32 "us avg/max_wait=%" PRIu32 "/%" PRIu32 "us"
        " invalid=%" PRIu32 " full_invalid=%" PRIu32 " avg/max_invalid_px=%" PRIu32 "/%" PRIu32
        " qspi_actual_max=%" PRIu32
        " free_int=%" PRIu32 " largest_int=%" PRIu32
        " min_int=%" PRIu32
        " free_dma=%" PRIu32 " largest_dma=%" PRIu32
        " min_dma=%" PRIu32
        " free_8bit=%" PRIu32 " largest_8bit=%" PRIu32
        " min_8bit=%" PRIu32
        " free_spiram=%" PRIu32 " largest_spiram=%" PRIu32,
        window_ms,
        fps_x10 / 10,
        fps_x10 % 10,
        display_profile.refr_count,
        display_profile.render_count,
        render_avg_us,
        display_profile.render_us_max,
        display_profile.flush_count,
        display_profile.full_flush_count,
        flush_px_avg,
        display_profile.flush_px_max,
        flush_cb_avg_us,
        display_profile.flush_cb_us_max,
        flush_wait_avg_us,
        display_profile.flush_wait_us_max,
        display_profile.invalidate_count,
        display_profile.full_invalidate_count,
        invalidate_px_avg,
        display_profile.invalidate_px_max,
        (uint32_t) display_profile_qspi_actual_max_transfer_size,
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_DMA),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_DMA),
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    display_flush_timing_t timing;
    display_flush_take_timing(&timing);
    ESP_LOGI(TAG,
        "te_prof: window=%" PRIu32 "ms te=%" PRIu32 " period_min/max=%" PRIu32 "/%" PRIu32 "us"
        " high_avg=%" PRIu32 "us frames=%" PRIu32 " write_avg/max=%" PRIu32 "/%" PRIu32 "us"
        " te_wait_avg/max=%" PRIu32 "/%" PRIu32 "us te_timeouts=%" PRIu32 " te_retries=%" PRIu32 " te_late=%" PRIu32,
        window_ms, timing.te_pulses, timing.te_period_min_us, timing.te_period_max_us,
        timing.te_high_avg_us, timing.frames, timing.write_avg_us, timing.write_max_us,
        timing.te_wait_avg_us, timing.te_wait_max_us, timing.te_timeouts, timing.te_retries, timing.te_late);

    display_profile_reset_window();
}

static void display_profile_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    const lv_area_t *area = lv_event_get_param(e);
    uint32_t area_px = display_profile_area_px(area);
    uint32_t full_screen_px = DISP_H_RES_PIXEL * DISP_V_RES_PIXEL;

    switch (code) {
        case LV_EVENT_REFR_START:
            display_profile.refr_count++;
            break;

        case LV_EVENT_RENDER_START:
            display_profile.render_start_us = display_profile_now_us();
            break;

        case LV_EVENT_RENDER_READY:
            display_profile.render_count++;
            display_profile_add_duration(
                display_profile.render_start_us,
                &display_profile.render_us_total,
                &display_profile.render_us_max);
            display_profile.render_start_us = 0;
            break;

        case LV_EVENT_FLUSH_START:
            display_profile.flush_start_us = display_profile_now_us();
            display_profile.flush_count++;
            display_profile.flush_px_total += area_px;
            if (area_px > display_profile.flush_px_max) {
                display_profile.flush_px_max = area_px;
            }
            if (area_px >= full_screen_px) {
                display_profile.full_flush_count++;
            }
            break;

        case LV_EVENT_FLUSH_FINISH:
            display_profile_add_duration(
                display_profile.flush_start_us,
                &display_profile.flush_cb_us_total,
                &display_profile.flush_cb_us_max);
            display_profile.flush_start_us = 0;
            break;

        case LV_EVENT_FLUSH_WAIT_START:
            display_profile.flush_wait_start_us = display_profile_now_us();
            break;

        case LV_EVENT_FLUSH_WAIT_FINISH:
            display_profile.flush_wait_count++;
            display_profile_add_duration(
                display_profile.flush_wait_start_us,
                &display_profile.flush_wait_us_total,
                &display_profile.flush_wait_us_max);
            display_profile.flush_wait_start_us = 0;
            break;

        case LV_EVENT_INVALIDATE_AREA:
            display_profile.invalidate_count++;
            display_profile.invalidate_px_total += area_px;
            if (area_px > display_profile.invalidate_px_max) {
                display_profile.invalidate_px_max = area_px;
            }
            if (area_px >= full_screen_px) {
                display_profile.full_invalidate_count++;
            }
            break;

        default:
            break;
    }
}

static void display_profile_start(lv_display_t *disp) {
    if (disp == NULL) {
        return;
    }

    display_profile_reset_window();
    lv_display_add_event_cb(disp, display_profile_event_cb, LV_EVENT_ALL, NULL);
    lv_timer_create(display_profile_report_timer_cb, DISPLAY_PROFILING_REPORT_PERIOD_MS, NULL);

    esp_err_t ret = spi_bus_get_max_transaction_len(LCD_QSPI_HOST, &display_profile_qspi_actual_max_transfer_size);
    if (ret != ESP_OK) {
        display_profile_qspi_actual_max_transfer_size = 0;
    }

    ESP_LOGI(TAG,
        "display_prof_cfg: hres=%d vres=%d buffer_px=%d staging_bytes=%d qspi_cfg_max_transfer=%d qspi_actual_max_transfer=%u double=1 sw_rotate=1 rotation=%d qspi_pclk_hz=%d",
        DISP_H_RES_PIXEL,
        DISP_V_RES_PIXEL,
        DISP_H_RES_PIXEL * DISP_V_RES_PIXEL,
        DISPLAY_FLUSH_STAGING_BYTES,
        LCD_QSPI_MAX_TRANSFER_SIZE,
        (unsigned) display_profile_qspi_actual_max_transfer_size,
        system_config.rotation,
        40 * 1000 * 1000);
}
#endif  // DISPLAY_PROFILING_ENABLED


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


#if USE_EXT_BUTTON
// button
volatile bool ext_button_interrupt_occurred = false;
lv_timer_t * volatile ext_button_indev_timer = NULL;

static void IRAM_ATTR btn_gpio_interrupt_handler(void *arg) {
    (void) arg;

    ext_button_interrupt_occurred = true;
    lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, NULL);
}


void btn_gpio_read(lv_indev_t *indev, lv_indev_data_t *data) {
    static uint32_t last_interrupt_tick = 0;
    uint32_t tick_now = lv_tick_get();

    bool pressed = gpio_get_level(EXT_BUTTON_PIN) == 0;

    // If no interrupt has happened in the last 450 ms (longer than the default long_press_time), pause the indev
    // timer, but only once released: the interrupt sees the press edge only, so pausing while held lost the release
    if (!pressed && lv_tick_diff(tick_now, last_interrupt_tick) > 450)
    {
        lv_timer_pause(ext_button_indev_timer);
    }

    if (ext_button_interrupt_occurred) {
        ext_button_interrupt_occurred = false;
        last_interrupt_tick = tick_now;

        if (ext_button_indev_timer) {
            lv_timer_resume(ext_button_indev_timer);
        }
    }

    // Activity and wake on a real press only: this callback also runs on every touch interrupt (the LVGL port
    // reads all keypads then), and it used to wake the unit on any read while idle. A press that wakes the unit
    // is not passed to LVGL (ADR-005 B3)
    static bool wake_press = false;
    bool swallow = low_power_mode_swallow_wake_press(pressed, &wake_press);
    data->state = (pressed && !swallow) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->key = LV_KEY_ENTER;
}

#endif  // USE_EXT_BUTTON


#define LVGL_WATCHDOG_FEED_PERIOD_MS 1000
// While idle, how long the low-power task waits for the LVGL lock before it skips a watchdog feed
#define LVGL_WATCHDOG_IDLE_LOCK_PROBE_MS 100

static esp_task_wdt_user_handle_t lvgl_watchdog_user = NULL;
static volatile bool lvgl_watchdog_paused = false;

// Runs in the LVGL task, so it only feeds while that task is really running its timers
static void lvgl_watchdog_feed_timer_cb(lv_timer_t *timer) {
    ESP_UNUSED(timer);
    esp_task_wdt_reset_user(lvgl_watchdog_user);
}


void lvgl_watchdog_pause(bool pause) {
    lvgl_watchdog_paused = pause;
    // Feed now either way: on resume this gives the LVGL task a full timeout to run its first timer
    if (lvgl_watchdog_user != NULL) {
        esp_task_wdt_reset_user(lvgl_watchdog_user);
    }
}


void lvgl_watchdog_feed_while_paused() {
    if (!lvgl_watchdog_paused || lvgl_watchdog_user == NULL) {
        return;
    }

    // Feed only if the LVGL lock is free. Its holders while idle keep it for milliseconds; one that
    // holds it for the whole watchdog timeout is a lock-up, and must reboot the unit then, not at the
    // next wake (ADR-005 B4)
    if (lvgl_port_lock(LVGL_WATCHDOG_IDLE_LOCK_PROBE_MS)) {
        lvgl_port_unlock();
        esp_task_wdt_reset_user(lvgl_watchdog_user);
    }
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
    lvgl_cfg.task_stack = 16384;
    
    ret = lvgl_port_init(&lvgl_cfg);
    ESP_ERROR_CHECK(ret);

    // Add display to LVGL
    lv_display_t *lvgl_disp = NULL;
    // lv_indev_t *lvgl_touch_indev = NULL;
    if (lvgl_port_lock(0)) {
        lvgl_disp = display_flush_create(io_handle, panel_handle);
        lvgl_port_unlock();
    }
    if (lvgl_disp == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // Add touch input to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp, 
        .handle = touch_handle
    };
    lvgl_touch_handle = lvgl_port_add_touch(&touch_cfg);

    // Create the button input group to allow widget to stay focus
    button_input_group = lv_group_create();

#if USE_EXT_BUTTON
    // Add button input to LVGL
    // Initialize GPIO0 as input interrupt driven button
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EXT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // falling edge
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_isr_handler_add(EXT_BUTTON_PIN, btn_gpio_interrupt_handler, NULL));

    lv_indev_t * btn_indev = lv_indev_create();
    ext_button_indev_timer = lv_indev_get_read_timer(btn_indev);
    lv_indev_set_type(btn_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(btn_indev, btn_gpio_read);

    // Associate the button with the group
    lv_indev_set_group(btn_indev, button_input_group);
#endif  // USE_EXT_BUTTON

    // Create LVGL application
    if (lvgl_port_lock(0)) {
#if DISPLAY_PROFILING_ENABLED
        display_profile_start(lvgl_disp);
#endif
        // The services' state, for the widgets to bind to (ADR-005 step 3)
        ui_bridge_init();

        create_main_tileview(lv_screen_active());
        lv_display_set_rotation(lvgl_disp, system_config.rotation);

        // Lock-up guard: if the LVGL task stops running its timers, the task watchdog panics
        // (core dump, then reboot). Idle mode stops LVGL on purpose: the watchdog is paused from
        // lvgl_port_stop() to lvgl_port_resume(), and the low-power task feeds it meanwhile.
        esp_err_t wdt_ret = esp_task_wdt_add_user("lvgl", &lvgl_watchdog_user);
        if (wdt_ret == ESP_OK) {
            lv_timer_create(lvgl_watchdog_feed_timer_cb, LVGL_WATCHDOG_FEED_PERIOD_MS, NULL);
        }
        else {
            ESP_LOGE(TAG, "Failed to add LVGL task watchdog user: %s", esp_err_to_name(wdt_ret));
        }
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

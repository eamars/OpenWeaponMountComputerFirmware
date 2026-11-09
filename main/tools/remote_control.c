#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_debug_helpers.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "lvgl.h"

#include "remote_control.h"
#include "mem_diag.h"
#include "app_cfg.h"
#include "power_page.h"
#include "ota_mode.h"
#include "common.h"
#include "bno085.h"
#include "digital_level_view_controller.h"  // the level's raw cant and pitch (grav)
#include "sensor_frame.h"
#include "low_power_mode.h"
#include "pmic_axp2101.h"
#include "ui_bridge.h"
#include "system_config.h"
#include "wifi.h"
#include "wifi_page.h"
#include "app.h"
#include "main_tileview.h"
#include "display_shape.h"
#include "kestrel_service.h"
#include "dope_view.h"
#include "display_flush.h"

#define TAG "RemoteControl"
#define NVS_NAMESPACE "RC"

// Every LVGL call from a command waits at most this long for the lock, then replies `@err busy`
#define RC_LVGL_LOCK_TIMEOUT_MS 1000

// Gestures: a gesture not consumed by LVGL within its duration plus this margin is dropped
#define RC_GESTURE_MARGIN_MS 1000
#define RC_GESTURE_POLL_MS 10
#define RC_TAP_PRESS_MS 100
#define RC_TAP_HOLD_MAX_MS 1500     // `tap x y hold_ms`: a long press, within the serial stop timeout
#define RC_SWIPE_STEPS 6
#define RC_SWIPE_STEP_MS 25         // 6 steps over 150 ms
#define RC_SWIPE_DISTANCE_PCT 60
#define RC_POINT_QUEUE_LEN (RC_SWIPE_STEPS + 2)  // press, moves, release

#define RC_REBOOT_DELAY_US (100 * 1000)
#define RC_IDLE_WAKE_MAX_S 3600     // `idle <s>`: the longest timed idle

#define RC_MAX_ARGS 6   // "settings open How to pair"

#define RC_BT_DEPTH 24
#define RC_HELP_COMMANDS "help state stats sleep bt pmic snap outline tap swipe tile rot wifi kestrel save idle wake reboot hang bnoreset settings ota bno top tear grav trace"


typedef struct {
    int32_t x;          // logical coordinates, as LVGL sees them at the current rotation
    int32_t y;
    bool pressed;
    uint32_t hold_ms;   // how long this point stays current before the next one
} rc_point_t;


HEAPS_CAPS_ATTR remote_control_config_t remote_control_config;
const remote_control_config_t remote_control_config_default = {
    .serial_enable = true,  // on while the firmware is in development; flip before a release build
    .wifi_enable = false,   // anyone on the network could send commands: on only when wanted
};


static SemaphoreHandle_t rc_execute_mutex = NULL;

// Where the command being executed sends its lines besides the reply (the snap data): set by the transport for each
// command, under rc_execute_mutex
static remote_control_write_fn rc_write = NULL;
static void * rc_write_ctx = NULL;
static esp_timer_handle_t rc_reboot_timer = NULL;
static esp_timer_handle_t rc_idle_wake_timer = NULL;

// Virtual pointer. The queue is written by the command task and read by the LVGL task (read_cb);
// the spinlock lets a timed-out command drop the queue without the LVGL lock.
static lv_indev_t * rc_pointer = NULL;
static portMUX_TYPE rc_point_lock = portMUX_INITIALIZER_UNLOCKED;
HEAPS_CAPS_ATTR static rc_point_t rc_point_queue[RC_POINT_QUEUE_LEN];
static size_t rc_point_count = 0;
static size_t rc_point_index = 0;
static bool rc_point_active = false;
static uint32_t rc_point_start_tick = 0;
static rc_point_t rc_point_current = {0};
static volatile bool rc_gesture_pending = false;


esp_err_t load_remote_control_config() {
    // Saved before the Wi-Fi switch existed: loads with it off
    return load_config_grown(NVS_NAMESPACE, &remote_control_config, &remote_control_config_default,
                             sizeof(remote_control_config), offsetof(remote_control_config_t, wifi_enable));
}


esp_err_t save_remote_control_config() {
    return save_config(NVS_NAMESPACE, &remote_control_config, sizeof(remote_control_config));
}


// One line (no line ending) to the transport the command came from
static void rc_write_line(const char *line) {
    if (rc_write != NULL) {
        rc_write(line, rc_write_ctx);
    }
}


static bool rc_reply(char *reply, size_t reply_size, bool ok, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(reply, reply_size, fmt, args);
    va_end(args);

    return ok;
}


static bool rc_parse_long(const char *text, long *value) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }

    *value = parsed;
    return true;
}


// LVGL rotates every pointer reading from panel coordinates into logical ones (lv_display_rotate_point).
// Apply the inverse so the queued logical point lands where it was asked for.
static void rc_pointer_logical_to_physical(lv_display_t * disp, int32_t x, int32_t y, lv_point_t * point) {
    int32_t hor_res = lv_display_get_original_horizontal_resolution(disp);
    int32_t ver_res = lv_display_get_original_vertical_resolution(disp);

    switch (lv_display_get_rotation(disp)) {
        case LV_DISPLAY_ROTATION_90:
            point->x = y;
            point->y = ver_res - x - 1;
            break;
        case LV_DISPLAY_ROTATION_180:
            point->x = hor_res - x - 1;
            point->y = ver_res - y - 1;
            break;
        case LV_DISPLAY_ROTATION_270:
            point->x = hor_res - y - 1;
            point->y = x;
            break;
        default:
            point->x = x;
            point->y = y;
            break;
    }
}


// Runs in the LVGL task with the LVGL lock held
static void rc_pointer_read_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    uint32_t now = lv_tick_get();
    bool queue_empty = false;

    taskENTER_CRITICAL(&rc_point_lock);
    if (rc_point_active && lv_tick_diff(now, rc_point_start_tick) >= rc_point_current.hold_ms) {
        rc_point_index++;
        rc_point_active = false;
    }

    if (!rc_point_active) {
        if (rc_point_index < rc_point_count) {
            rc_point_current = rc_point_queue[rc_point_index];
            rc_point_start_tick = now;
            rc_point_active = true;

            // The last point (the release) is handed to LVGL now and processed in this same read.
            // The waiting command may reply without needing another read, so a tap whose release
            // stops the serial reader (Remote control switch) cannot wait on the blocked LVGL task.
            if (rc_point_index == rc_point_count - 1) {
                rc_gesture_pending = false;
            }
        }
        else {
            rc_point_count = 0;
            rc_point_index = 0;
            queue_empty = true;
        }
    }

    rc_point_t point = rc_point_current;
    taskEXIT_CRITICAL(&rc_point_lock);

    lv_display_t * disp = lv_indev_get_display(indev);
    if (disp == NULL) {
        disp = lv_display_get_default();
    }
    rc_pointer_logical_to_physical(disp, point.x, point.y, &data->point);
    data->state = point.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    // Nothing queued: stop polling so the LVGL task can sleep as before
    if (queue_empty) {
        lv_timer_pause(lv_indev_get_read_timer(indev));
    }
}


// Call with the LVGL lock held
static void rc_pointer_start(const rc_point_t * points, size_t count) {
    taskENTER_CRITICAL(&rc_point_lock);
    memcpy(rc_point_queue, points, count * sizeof(rc_point_t));
    rc_point_count = count;
    rc_point_index = 0;
    rc_point_active = false;
    rc_gesture_pending = true;
    taskEXIT_CRITICAL(&rc_point_lock);

    lv_timer_t * read_timer = lv_indev_get_read_timer(rc_pointer);
    lv_timer_resume(read_timer);
    lv_timer_ready(read_timer);
}


// Call without the LVGL lock: LVGL must run to consume the gesture
static bool rc_pointer_wait(uint32_t duration_ms) {
    // Wake the LVGL task now rather than at the end of its sleep
    lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);

    uint32_t waited_ms = 0;
    while (rc_gesture_pending) {
        if (waited_ms >= duration_ms + RC_GESTURE_MARGIN_MS) {
            // LVGL is not running (idle mode) or is blocked: drop the gesture so it is not replayed later
            taskENTER_CRITICAL(&rc_point_lock);
            rc_point_count = 0;
            rc_point_index = 0;
            rc_point_active = false;
            rc_point_current.pressed = false;
            rc_gesture_pending = false;
            taskEXIT_CRITICAL(&rc_point_lock);

            ESP_LOGW(TAG, "Gesture not consumed within %u ms, dropped", (unsigned) (duration_ms + RC_GESTURE_MARGIN_MS));
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(RC_GESTURE_POLL_MS));
        waited_ms += RC_GESTURE_POLL_MS;
    }

    return true;
}


static int32_t rc_clamp(int32_t value, int32_t min, int32_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}


static bool rc_cmd_state(char *reply, size_t reply_size) {
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    int col = -1;
    int row = -1;
    lv_obj_t * active_tile = main_tileview ? lv_tileview_get_tile_active(main_tileview) : NULL;
    if (active_tile) {
        int32_t w = lv_obj_get_content_width(main_tileview);
        int32_t h = lv_obj_get_content_height(main_tileview);
        if (w > 0 && h > 0) {
            col = lv_obj_get_x(active_tile) / w;
            row = lv_obj_get_y(active_tile) / h;
        }
    }
    int rotation = lv_display_get_rotation(lv_display_get_default());
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true, "@ok tile=%d,%d rot=%d wifi=%s ip=%d idle=%d rc=1",
                    col, row, rotation,
                    wifi_user_config.wifi_enable ? "on" : "off",
                    wifi_is_sta_connected() ? 1 : 0,
                    is_idle_mode_activated() ? 1 : 0);
}


// Automatic light sleep while idle (ADR-009): the share of idle spent asleep and what ended each sleep, since boot.
// With CONFIG_PM_PROFILING the PM's own lock table goes to the console first (which lock kept the chip awake)
static bool rc_cmd_sleep(char *reply, size_t reply_size) {
#if CONFIG_PM_PROFILING
    esp_pm_dump_locks(stdout);
    fflush(stdout);
#endif  // CONFIG_PM_PROFILING
    low_power_sleep_stats_t st;
    low_power_mode_get_sleep_stats(&st);
    double share = st.idle_battery_us > 0 ? 100.0 * (double) st.slept_us / (double) st.idle_battery_us : 0.0;
    return rc_reply(reply, reply_size, true,
                    "@ok sleep usb_power=%d idle=%d idle_battery_s=%.1f idles=%" PRIu32 " slept_s=%.1f share=%.1f sleeps=%" PRIu32
                    " wake_gpio=%" PRIu32 " touch=%" PRIu32 " bno=%" PRIu32 " button=%" PRIu32 " pmic=%" PRIu32
                    " no_line=%" PRIu32 " wake_timer=%" PRIu32 " wake_other=%" PRIu32,
                    st.usb_power ? 1 : 0, is_idle_mode_activated() ? 1 : 0,
                    st.idle_battery_us / 1e6, st.idle_periods, st.slept_us / 1e6, share, st.sleeps,
                    st.wake_gpio, st.wake_line[0], st.wake_line[1], st.wake_line[2], st.wake_line[3],
                    st.wake_gpio_no_line, st.wake_timer, st.wake_other);
}


static bool rc_cmd_stats(char *reply, size_t reply_size) {
    int written = snprintf(reply, reply_size, "@ok free_int=%u largest_dma=%u min_int=%u free_spiram=%u",
                           (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (written < 0 || (size_t) written >= reply_size) {
        return true;  // reply buffer too small for even the heap figures; snprintf has truncated it
    }

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    static const char truncated_marker[] = " ...";
    size_t pos = (size_t) written;

    UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *tasks = heap_caps_malloc(capacity * sizeof(TaskStatus_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (tasks == NULL) {
        ESP_LOGE(TAG, "stats: no memory for %u tasks", (unsigned) capacity);
        return true;
    }

    UBaseType_t count = uxTaskGetSystemState(tasks, capacity, NULL);
    for (UBaseType_t i = 0; i < count; i++) {
        // Task names may contain spaces; keep each entry one token
        char name[configMAX_TASK_NAME_LEN + 1];
        strlcpy(name, tasks[i].pcTaskName, sizeof(name));
        for (char *c = name; *c; c++) {
            if (*c == ' ') {
                *c = '_';
            }
        }

        int entry_len = snprintf(NULL, 0, " stack:%s=%u", name, (unsigned) tasks[i].usStackHighWaterMark);
        // Keep room for the truncation marker so a cut reply stays recognisable
        if (entry_len < 0 || pos + (size_t) entry_len + sizeof(truncated_marker) > reply_size) {
            strlcpy(reply + pos, truncated_marker, reply_size - pos);
            break;
        }

        snprintf(reply + pos, reply_size - pos, " stack:%s=%u", name, (unsigned) tasks[i].usStackHighWaterMark);
        pos += (size_t) entry_len;
    }

    heap_caps_free(tasks);
#endif  // CONFIG_FREERTOS_USE_TRACE_FACILITY

    return true;
}


static bool rc_cmd_tap(int argc, char **argv, char *reply, size_t reply_size) {
    long x;
    long y;
    long hold_ms = RC_TAP_PRESS_MS;  // `tap x y hold_ms` holds longer, for a long press
    if ((argc != 3 && argc != 4) || !rc_parse_long(argv[1], &x) || !rc_parse_long(argv[2], &y) ||
        (argc == 4 && (!rc_parse_long(argv[3], &hold_ms) || hold_ms < 1 || hold_ms > RC_TAP_HOLD_MAX_MS))) {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    lv_display_t * disp = lv_display_get_default();
    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);
    if (x < 0 || x >= w || y < 0 || y >= h) {
        lvgl_port_unlock();
        return rc_reply(reply, reply_size, false, "@err args");
    }

    const rc_point_t points[] = {
        { .x = x, .y = y, .pressed = true, .hold_ms = (uint32_t) hold_ms },
        { .x = x, .y = y, .pressed = false, .hold_ms = 0 },
    };
    rc_pointer_start(points, sizeof(points) / sizeof(points[0]));
    lvgl_port_unlock();

    if (!rc_pointer_wait((uint32_t) hold_ms)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    return rc_reply(reply, reply_size, true, "@ok tap");
}


static bool rc_cmd_swipe(int argc, char **argv, char *reply, size_t reply_size) {
    if (argc != 2) {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    // Finger movement direction
    int32_t dir_x = 0;
    int32_t dir_y = 0;
    if (strcmp(argv[1], "left") == 0) {
        dir_x = -1;
    }
    else if (strcmp(argv[1], "right") == 0) {
        dir_x = 1;
    }
    else if (strcmp(argv[1], "up") == 0) {
        dir_y = -1;
    }
    else if (strcmp(argv[1], "down") == 0) {
        dir_y = 1;
    }
    else {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    lv_display_t * disp = lv_display_get_default();
    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);
    int32_t cx = w / 2;
    int32_t cy = h / 2;
    int32_t distance_x = dir_x * w * RC_SWIPE_DISTANCE_PCT / 100;
    int32_t distance_y = dir_y * h * RC_SWIPE_DISTANCE_PCT / 100;

    // Press at the centre, move in equal steps, release at the last point.
    // From the centre, 60% reaches past the edge: points are kept on the screen.
    rc_point_t points[RC_POINT_QUEUE_LEN];
    points[0] = (rc_point_t) { .x = cx, .y = cy, .pressed = true, .hold_ms = RC_SWIPE_STEP_MS };
    for (int step = 1; step <= RC_SWIPE_STEPS; step++) {
        points[step] = (rc_point_t) {
            .x = rc_clamp(cx + distance_x * step / RC_SWIPE_STEPS, 0, w - 1),
            .y = rc_clamp(cy + distance_y * step / RC_SWIPE_STEPS, 0, h - 1),
            .pressed = true,
            .hold_ms = RC_SWIPE_STEP_MS,
        };
    }
    points[RC_SWIPE_STEPS + 1] = points[RC_SWIPE_STEPS];
    points[RC_SWIPE_STEPS + 1].pressed = false;
    points[RC_SWIPE_STEPS + 1].hold_ms = 0;

    rc_pointer_start(points, RC_POINT_QUEUE_LEN);
    lvgl_port_unlock();

    if (!rc_pointer_wait((RC_SWIPE_STEPS + 1) * RC_SWIPE_STEP_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    return rc_reply(reply, reply_size, true, "@ok swipe %s", argv[1]);
}


static bool rc_cmd_tile(int argc, char **argv, char *reply, size_t reply_size) {
    long col;
    long row;
    if (argc != 3 || !rc_parse_long(argv[1], &col) || !rc_parse_long(argv[2], &row)) {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    lv_obj_t * target_tile = NULL;
    int32_t tx = 0;
    int32_t ty = 0;
    // Tile indices are uint8_t in lv_tileview_add_tile
    if (main_tileview && col >= 0 && col <= UINT8_MAX && row >= 0 && row <= UINT8_MAX) {
        lv_obj_update_layout(main_tileview);
        int32_t w = lv_obj_get_content_width(main_tileview);
        int32_t h = lv_obj_get_content_height(main_tileview);

        for (uint32_t i = 0; i < lv_obj_get_child_count(main_tileview); i++) {
            lv_obj_t * child = lv_obj_get_child(main_tileview, i);
            if (lv_obj_check_type(child, &lv_tileview_tile_class) &&
                lv_obj_get_x(child) == col * w && lv_obj_get_y(child) == row * h) {
                target_tile = child;
                tx = lv_obj_get_x(child);
                ty = lv_obj_get_y(child);
                break;
            }
        }
    }

    if (target_tile == NULL) {
        lvgl_port_unlock();
        return rc_reply(reply, reply_size, false, "@err no_tile");
    }

    // Scroll in one step, so the tileview sees one LV_EVENT_SCROLL_END, as at the end of a swipe:
    // its handler makes the tile active and sends LV_EVENT_VALUE_CHANGED once, which runs
    // tile_change_callback (disable the previous tile, enable this one). lv_tileview_set_tile()
    // scrolls x and y separately and can pass through, and enable, a tile on the way.
    int32_t dx = lv_obj_get_scroll_x(main_tileview) - tx;
    int32_t dy = lv_obj_get_scroll_y(main_tileview) - ty;
    lv_obj_scroll_by(main_tileview, dx, dy, LV_ANIM_OFF);
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true, "@ok tile=%ld,%ld", col, row);
}


static bool rc_cmd_rot(int argc, char **argv, char *reply, size_t reply_size) {
    long rotation;
    if (argc != 2 || !rc_parse_long(argv[1], &rotation) ||
        rotation < LV_DISPLAY_ROTATION_0 || rotation > LV_DISPLAY_ROTATION_270) {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    system_config_set_rotation((lv_display_rotation_t) rotation);
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true, "@ok rot=%ld", rotation);
}


// settings home | open <title> | row <n> | back | scroll <n> | power show|hide: look at the settings pages and the
// power button's menu without touching the screen (ADR-008). Never changes a setting: toggles and actions that do
// not ask are refused, back cancels
static bool rc_cmd_settings(int argc, char **argv, char *reply, size_t reply_size) {
    if (argc < 2) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    char title[48] = "";
    if (strcmp(argv[1], "open") == 0) {
        for (int i = 2; i < argc; i++) {
            size_t used = strlen(title);
            snprintf(&title[used], sizeof(title) - used, "%s%s", i > 2 ? " " : "", argv[i]);
        }
    }
    long n = 0;
    bool needs_n = strcmp(argv[1], "row") == 0 || strcmp(argv[1], "scroll") == 0;
    if (needs_n && (argc != 3 || !rc_parse_long(argv[2], &n))) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    bool ok = true;
    if (strcmp(argv[1], "home") == 0) {
        settings_ui_test_home();
    }
    else if (strcmp(argv[1], "open") == 0) {
        ok = settings_ui_test_open(title);
    }
    else if (strcmp(argv[1], "row") == 0) {
        ok = settings_ui_test_row((int) n);
    }
    else if (strcmp(argv[1], "back") == 0) {
        settings_ui_test_back();
    }
    else if (strcmp(argv[1], "scroll") == 0) {
        settings_ui_test_scroll((int) n);
    }
#if USE_PMIC
    else if (strcmp(argv[1], "power") == 0 && argc == 3 &&
             (strcmp(argv[2], "show") == 0 || strcmp(argv[2], "hide") == 0)) {
        power_menu_show(strcmp(argv[2], "show") == 0);  // only shows or closes it: its buttons are the owner's
        bool shown = power_menu_shown();
        lvgl_port_unlock();
        return rc_reply(reply, reply_size, true, "@ok settings power %s", shown ? "shown" : "hidden");
    }
#endif  // USE_PMIC
    else {
        ok = false;
    }
    char top[48];
    snprintf(top, sizeof(top), "%s", settings_ui_test_title());
    lvgl_port_unlock();
    return rc_reply(reply, reply_size, ok, ok ? "@ok settings %s" : "@err settings %s", top);
}


// ota mock [critical] | offer show|hide|end | view connecting|downloading|done|failed: the update's screens with mock
// data (ADR-008), memory only. `tile 0 1` shows the OTA tile; its view here does not start a download
static bool rc_cmd_ota(int argc, char **argv, char *reply, size_t reply_size) {
    if (argc < 2 || argc > 3) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    bool ok = true;
    if (strcmp(argv[1], "mock") == 0) {
        ota_mode_test_mock(argc == 3 && strcmp(argv[2], "critical") == 0);
    }
    else if (strcmp(argv[1], "offer") == 0 && argc == 3) {
        if (strcmp(argv[2], "end") == 0) {
            ota_mode_test_offer_end();
        }
        else {
            ota_mode_test_offer(strcmp(argv[2], "show") == 0);
        }
    }
    else if (strcmp(argv[1], "view") == 0 && argc == 3) {
        ok = ota_mode_test_view(argv[2]);
    }
    else {
        ok = false;
    }
    lvgl_port_unlock();
    return rc_reply(reply, reply_size, ok, ok ? "@ok ota %s" : "@err ota %s", argv[1]);
}


// kestrel: the pull's status in one line; kestrel pull: start a pull (ADR-007). The pull logs every phase and
// target; this command only starts it and reports
static bool rc_cmd_kestrel(int argc, char **argv, char *reply, size_t reply_size) {
#if USE_BNO085
    if ((argc == 2 || argc == 3) && strcmp(argv[1], "pull") == 0) {
        // kestrel pull [count]: the count the dope tile last used when left out
        int count = argc == 3 ? atoi(argv[2]) : dope_settings.pull_count;
        esp_err_t ret = kestrel_request_pull(count);
        if (ret != ESP_OK) {
            kestrel_status_t s = kestrel_get_status();
            return rc_reply(reply, reply_size, false, "@err kestrel %s", s.message);
        }
        return rc_reply(reply, reply_size, true, "@ok kestrel pull");
    }
    if (argc == 2 && strcmp(argv[1], "find") == 0) {
        // Phone mode: search for Kestrels; `kestrel` then lists them (found=)
        esp_err_t ret = kestrel_request_search();
        return rc_reply(reply, reply_size, ret == ESP_OK, ret == ESP_OK ? "@ok kestrel find" : "@err kestrel busy");
    }
    if (argc == 3 && strcmp(argv[1], "mode") == 0) {
        // kestrel mode 0|1|2: Off, HUD, Phone, in memory only (as the page's dropdown before Save)
        int mode = atoi(argv[2]);
        if (mode < KESTREL_MODE_OFF || mode > KESTREL_MODE_PHONE) {
            return rc_reply(reply, reply_size, false, "@err kestrel mode 0-2");
        }
        kestrel_settings.mode = (uint8_t) mode;
        kestrel_settings_changed();
        return rc_reply(reply, reply_size, true, "@ok kestrel mode=%d", mode);
    }
    if (argc == 3 && strcmp(argv[1], "pick") == 0) {
        esp_err_t ret = kestrel_choose(atoi(argv[2]));
        return rc_reply(reply, reply_size, ret == ESP_OK, ret == ESP_OK ? "@ok kestrel pick %s" : "@err kestrel pick %s",
                        kestrel_settings.peer_name);
    }
    if (argc != 1) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    kestrel_status_t s = kestrel_get_status();
    if (s.found_count > 0) {
        return rc_reply(reply, reply_size, true,
                        "@ok kestrel state=%d seq=%lu total_ms=%lu result=%lu n=%d found=%d first=%s,%d msg=%s",
                        (int) s.state, (unsigned long) s.sequence, (unsigned long) s.total_ms,
                        (unsigned long) (s.have_result ? s.result_id : 0), s.target_count, s.found_count,
                        s.found[0].name, s.found[0].rssi, s.message);
    }
    return rc_reply(reply, reply_size, true, "@ok kestrel state=%d seq=%lu total_ms=%lu result=%lu n=%d msg=%s",
                    (int) s.state, (unsigned long) s.sequence, (unsigned long) s.total_ms,
                    (unsigned long) (s.have_result ? s.result_id : 0), s.target_count, s.message);
#else
    return rc_reply(reply, reply_size, false, "@err unsupported");
#endif
}


static bool rc_cmd_wifi(int argc, char **argv, char *reply, size_t reply_size) {
    bool enable;
    if (argc == 2 && strcmp(argv[1], "on") == 0) {
        enable = true;
    }
    else if (argc == 2 && strcmp(argv[1], "off") == 0) {
        enable = false;
    }
    else if (argc == 2 && strcmp(argv[1], "expire") == 0) {
        // Debug: end the Wi-Fi window now, as the expiry timer would; Wi-Fi stays off until the next boot
        wifi_expire_now();
        return rc_reply(reply, reply_size, true, "@ok wifi=expire");
    }
    else if (argc == 2 && strcmp(argv[1], "drop") == 0) {
        // Debug: disconnect from the AP, handled as a lost connection (the AP gone), to test the reconnect
        wifi_drop_now();
        return rc_reply(reply, reply_size, true, "@ok wifi=drop");
    }
    else {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    // A request to the Wi-Fi service, as the switch makes it: no LVGL lock, the switch follows by itself
    // (ADR-005 step 3). The radio starts or stops in the service task, so a start/stop error is only logged there.
    // After the expiry, on starts a new Wi-Fi window, as the switch does
    wifi_request_enable(enable);
    return rc_reply(reply, reply_size, true, "@ok wifi=%s", enable ? "on" : "off");
}


static bool rc_cmd_save(int argc, char **argv, char *reply, size_t reply_size) {
    bool system_page;
    if (argc == 2 && strcmp(argv[1], "system") == 0) {
        system_page = true;
    }
    else if (argc == 2 && strcmp(argv[1], "wifi") == 0) {
        system_page = false;
    }
    else {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    // The configs are changed by UI callbacks in the LVGL task; save under the same lock the Save button runs under
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    esp_err_t ret;
    if (system_page) {
        // Same saves as the System page's Save button
        ret = save_system_config();
        if (ret == ESP_OK) {
            ret = save_remote_control_config();
        }
    }
    else {
        ret = save_wifi_user_config();
    }
    lvgl_port_unlock();

    if (ret != ESP_OK) {
        return rc_reply(reply, reply_size, false, "@err nvs %s", esp_err_to_name(ret));
    }

    return rc_reply(reply, reply_size, true, "@ok save %s", argv[1]);
}


// `idle <s>` wakes by this timer: over Wi-Fi the radio stops with the idle, so no `wake` can reach the unit
// (ADR-013). With automatic light sleep the timer is also a real wake from it
static void rc_idle_wake_timer_cb(void *arg) {
    if (is_idle_mode_activated()) {
        low_power_mode_note_wake_cause("timer");
        wake_from_idle_mode();
    }
}


// `idle`, or `idle <s>`: idle, then wake by a timer after s seconds
static bool rc_cmd_idle(int argc, char **argv, char *reply, size_t reply_size) {
    long seconds = 0;
    if (argc > 2 || (argc == 2 && (!rc_parse_long(argv[1], &seconds) || seconds < 1 ||
                                   seconds > RC_IDLE_WAKE_MAX_S))) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    low_power_mode_request_idle();
    lvgl_port_unlock();

    if (seconds == 0) {
        return rc_reply(reply, reply_size, true, "@ok idle");
    }
    esp_timer_stop(rc_idle_wake_timer);     // a timed idle before it: this one counts
    esp_err_t ret = esp_timer_start_once(rc_idle_wake_timer, (uint64_t) seconds * 1000000);
    if (ret != ESP_OK) {
        return rc_reply(reply, reply_size, false, "@err %s", esp_err_to_name(ret));
    }
    return rc_reply(reply, reply_size, true, "@ok idle %ld", seconds);
}


static bool rc_cmd_wake(char *reply, size_t reply_size) {
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    // Every existing caller wakes only from idle; waking an active device would rerun the exit path
    if (is_idle_mode_activated()) {
        low_power_mode_note_wake_cause("rc");
        wake_from_idle_mode();
    }
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true, "@ok wake");
}


static void rc_reboot_timer_cb(void *arg) {
    esp_restart();
}


static bool rc_cmd_reboot(char *reply, size_t reply_size) {
    // The transport prints and flushes the reply before the timer fires
    esp_err_t ret = esp_timer_start_once(rc_reboot_timer, RC_REBOOT_DELAY_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start reboot timer: %s", esp_err_to_name(ret));
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    return rc_reply(reply, reply_size, true, "@ok reboot");
}


// The PMIC service's newest reading, and the battery level the UI bridge holds for the status bar icon
// (101 = USB icon), so a script can see a reading reach the UI (ADR-005 step 3)
static bool rc_cmd_pmic(char *reply, size_t reply_size) {
    axp2101_status_t status = pmic_get_status();

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    int32_t icon = lv_subject_get_int(ui_battery_level);
    const axp2101_status_t *shown = (const axp2101_status_t *) lv_subject_get_pointer(ui_pmic_status);
    unsigned shown_vbat = shown->vbatt_voltage_mv;
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true,
                    "@ok pmic valid=%d soc=%d vbat=%u vbus=%u vsys=%u usb=%d chg=%d icon=%ld shown_vbat=%u"
                    " ts_raw=%u ts=%.1f",
                    status.valid ? 1 : 0, status.battery_percentage, (unsigned) status.vbatt_voltage_mv,
                    (unsigned) status.vbus_voltage_mv, (unsigned) status.vsys_voltage_mv,
                    status.is_usb_connected ? 1 : 0, status.charge_status, (long) icon, shown_vbat,
                    (unsigned) status.ts_raw, (double) status.ts_temperature);
}


// Debug: copy what the screen shows and print it, for layout checks without a camera (ADR-006). The image is
// RGB565 (LVGL's byte order), run-length encoded as little-endian (uint16 count, uint16 pixel) pairs, printed as
// base64 lines "@sd <data>" between "@snap <w> <h> rgb565rle" and the reply. The copy lives in PSRAM only while
// it is printed
#define RC_SNAP_PAIRS_PER_LINE 96    // 384 bytes of pairs, 512 characters of base64 per line

static const char rc_base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

HEAPS_CAPS_ATTR static uint8_t rc_snap_pairs[RC_SNAP_PAIRS_PER_LINE * 4];
HEAPS_CAPS_ATTR static char rc_snap_line[4 + (RC_SNAP_PAIRS_PER_LINE * 4 / 3 + 1) * 4 + 2];

// Write one data line from the first `len` bytes of rc_snap_pairs
static void rc_snap_write_line(size_t len) {
    size_t out = 0;
    memcpy(rc_snap_line, "@sd ", 4);
    out = 4;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t) rc_snap_pairs[i] << 16;
        if (i + 1 < len) {
            v |= (uint32_t) rc_snap_pairs[i + 1] << 8;
        }
        if (i + 2 < len) {
            v |= rc_snap_pairs[i + 2];
        }
        rc_snap_line[out++] = rc_base64_chars[(v >> 18) & 0x3F];
        rc_snap_line[out++] = rc_base64_chars[(v >> 12) & 0x3F];
        rc_snap_line[out++] = (i + 1 < len) ? rc_base64_chars[(v >> 6) & 0x3F] : '=';
        rc_snap_line[out++] = (i + 2 < len) ? rc_base64_chars[v & 0x3F] : '=';
    }
    rc_snap_line[out] = '\0';
    rc_write_line(rc_snap_line);
}


// The copy is rendered in the LVGL task: rendering needs its stack (the serial task's overflowed)
// rc_snap_wanted and rc_snap_buf change only with the LVGL lock held (the async call runs with it)
static lv_draw_buf_t * rc_snap_buf = NULL;
static bool rc_snap_wanted = false;
static SemaphoreHandle_t rc_snap_done = NULL;

static void rc_snap_render_async(void * p) {
    if (!rc_snap_wanted) {
        return;  // the command gave up waiting (LVGL was stopped, the unit idle)
    }
    rc_snap_buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    xSemaphoreGive(rc_snap_done);
}


static bool rc_cmd_snap(char *reply, size_t reply_size) {
    if (rc_snap_done == NULL) {
        rc_snap_done = xSemaphoreCreateBinary();
        if (rc_snap_done == NULL) {
            return rc_reply(reply, reply_size, false, "@err nomem");
        }
    }

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    rc_snap_buf = NULL;
    rc_snap_wanted = true;
    xSemaphoreTake(rc_snap_done, 0);
    lv_async_call(rc_snap_render_async, NULL);
    lvgl_port_unlock();
    lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);

    if (xSemaphoreTake(rc_snap_done, pdMS_TO_TICKS(RC_LVGL_LOCK_TIMEOUT_MS)) != pdTRUE) {
        // LVGL is stopped while the unit is idle: withdraw the request, so the call does nothing when LVGL
        // resumes; a copy that arrived meanwhile is freed here
        bool locked = lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS);
        rc_snap_wanted = false;
        if (locked && rc_snap_buf != NULL) {
            lv_draw_buf_destroy(rc_snap_buf);
            rc_snap_buf = NULL;
        }
        if (locked) {
            lvgl_port_unlock();
        }
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    rc_snap_wanted = false;
    lv_draw_buf_t * snap = rc_snap_buf;
    rc_snap_buf = NULL;
    if (snap == NULL) {
        return rc_reply(reply, reply_size, false, "@err nomem");
    }

    uint32_t w = snap->header.w;
    uint32_t h = snap->header.h;
    uint32_t stride = snap->header.stride;
    char header[48];
    snprintf(header, sizeof(header), "@snap %" PRIu32 " %" PRIu32 " rgb565rle", w, h);
    rc_write_line(header);

    // Pixels in row order; a run may cross rows
    size_t pair_bytes = 0;
    uint32_t lines = 0;
    uint16_t run_pixel = 0;
    uint32_t run_count = 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint16_t * row = (const uint16_t *) (snap->data + y * stride);
        for (uint32_t x = 0; x < w; x++) {
            uint16_t pixel = row[x];
            if (run_count > 0 && pixel == run_pixel && run_count < UINT16_MAX) {
                run_count++;
                continue;
            }
            if (run_count > 0) {
                rc_snap_pairs[pair_bytes++] = run_count & 0xFF;
                rc_snap_pairs[pair_bytes++] = run_count >> 8;
                rc_snap_pairs[pair_bytes++] = run_pixel & 0xFF;
                rc_snap_pairs[pair_bytes++] = run_pixel >> 8;
                if (pair_bytes == sizeof(rc_snap_pairs)) {
                    rc_snap_write_line(pair_bytes);
                    pair_bytes = 0;
                    lines++;
                }
            }
            run_pixel = pixel;
            run_count = 1;
        }
    }
    if (run_count > 0) {
        rc_snap_pairs[pair_bytes++] = run_count & 0xFF;
        rc_snap_pairs[pair_bytes++] = run_count >> 8;
        rc_snap_pairs[pair_bytes++] = run_pixel & 0xFF;
        rc_snap_pairs[pair_bytes++] = run_pixel >> 8;
    }
    if (pair_bytes > 0) {
        rc_snap_write_line(pair_bytes);
        lines++;
    }

    lv_draw_buf_destroy(snap);

    return rc_reply(reply, reply_size, true, "@ok snap %" PRIu32 " %" PRIu32 " lines=%" PRIu32, w, h, lines);
}


// Debug: draw the panel's rounded outline on top of everything (ADR-006)
static bool rc_cmd_outline(int argc, char **argv, char *reply, size_t reply_size) {
    if (argc != 2 || (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0)) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    display_shape_show_outline(strcmp(argv[1], "on") == 0);
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true, "@ok outline %s", argv[1]);
}


// Debug: print every task's backtrace to the console (ROM printing). Needs no LVGL lock, so it
// still works when the LVGL task is stuck; decode the addresses with the matching ELF.
static bool rc_cmd_bt(char *reply, size_t reply_size) {
    esp_err_t ret = esp_backtrace_print_all_tasks(RC_BT_DEPTH);
    return rc_reply(reply, reply_size, true, "@ok bt %s", ret == ESP_OK ? "complete" : "corrupt");
}


// Debug: take the LVGL lock and never give it back, the state of the LVGL lock freeze. The LVGL
// task stops, its watchdog entry goes unfed, and the task watchdog panics and reboots the device.
static bool rc_cmd_hang(char *reply, size_t reply_size) {
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    // No unlock, on purpose
    return rc_reply(reply, reply_size, true, "@ok hang");
}


// Debug (ADR-011): `grav` gives "up" in the sensor's own frame from the newest game rotation vector, the level's
// raw cant and pitch in degrees (against the factory calibration, before the rotation term and the rifle zero), and
// the report count. Needs a tile that runs the game rotation vector (the level, the calibration)
static bool rc_cmd_grav(int argc, char **argv, char *reply, size_t reply_size) {
    if (argc != 1) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    float quat[4];
    float up[3];
    const uint32_t count = bno085_get_game_rotation_quaternion(bno085_dev, quat);
    if (count == 0) {
        return rc_reply(reply, reply_size, false, "@err no game rotation vector yet");
    }
    sensor_frame_up_from_quaternion(quat, up);
    return rc_reply(reply, reply_size, true, "@ok grav up=%.4f,%.4f,%.4f cant=%.2f pitch=%.2f n=%" PRIu32,
                    (double) up[0], (double) up[1], (double) up[2], (double) RAD_TO_DEG(sensor_roll_thread_unsafe),
                    (double) RAD_TO_DEG(sensor_pitch_thread_unsafe), count);
}


// Debug (ADR-014): `trace <s>` logs the level's cant and the count of game rotation reports every 50 ms for s
// seconds from the next wake, so what the level read right after a wake is in the log the host reads once Wi-Fi is
// back. RAM only, one trace at a time. While armed its timer wakes the chip 20 times a second
#define RC_TRACE_PERIOD_US (50 * 1000)
#define RC_TRACE_MAX_S 30
static esp_timer_handle_t rc_trace_timer = NULL;
static int64_t rc_trace_wake_us = 0;        // 0 until the wake is seen
static int64_t rc_trace_length_us = 0;
static bool rc_trace_seen_idle = false;

static void rc_trace_timer_cb(void *arg) {
    const bool idle = is_idle_mode_activated();
    if (rc_trace_wake_us == 0) {
        if (idle) {
            rc_trace_seen_idle = true;
            return;
        }
        if (!rc_trace_seen_idle) {
            return;
        }
        rc_trace_wake_us = esp_timer_get_time();
    }
    const int64_t since_us = esp_timer_get_time() - rc_trace_wake_us;
    float quat[4];
    const uint32_t count = bno085_get_game_rotation_quaternion(bno085_dev, quat);
    float accel[3];
    const uint32_t accel_count = bno085_get_accelerometer(bno085_dev, accel);
    float accel_cant = 0, accel_pitch = 0;
    const float g = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (accel_count != 0 && g > 0.1f) {
        const float up[3] = {accel[0] / g, accel[1] / g, accel[2] / g};
        sensor_frame_cant_pitch(up, &accel_cant, &accel_pitch);
    }
    ESP_LOGI(TAG, "wtrace t=%" PRId64 " cant=%.2f grv=%.2f acc=%.2f fix=%.2f pitch=%.2f n=%" PRIu32 " na=%" PRIu32
             " g=%.2f idle=%d", since_us / 1000, (double) RAD_TO_DEG(sensor_roll_thread_unsafe),
             (double) RAD_TO_DEG(level_grv_cant_rad), (double) RAD_TO_DEG(accel_cant),
             (double) RAD_TO_DEG(level_fix_rad), (double) RAD_TO_DEG(sensor_pitch_thread_unsafe), count, accel_count,
             (double) g, idle ? 1 : 0);
    if (since_us >= rc_trace_length_us) {
        esp_timer_stop(rc_trace_timer);
        ESP_LOGI(TAG, "wtrace end");
    }
}


static bool rc_cmd_trace(int argc, char **argv, char *reply, size_t reply_size) {
    // `trace <s>` from the next wake, `trace <s> now` from now
    long seconds = 0;
    const bool now = argc == 3 && strcmp(argv[2], "now") == 0;
    if ((argc != 2 && !now) || !rc_parse_long(argv[1], &seconds) || seconds < 1 || seconds > RC_TRACE_MAX_S) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    esp_timer_stop(rc_trace_timer);
    rc_trace_wake_us = now ? esp_timer_get_time() : 0;
    rc_trace_seen_idle = false;
    rc_trace_length_us = (int64_t) seconds * 1000000;
    ESP_LOGI(TAG, "wtrace armed cant=%.2f", (double) RAD_TO_DEG(sensor_roll_thread_unsafe));
    esp_err_t ret = esp_timer_start_periodic(rc_trace_timer, RC_TRACE_PERIOD_US);
    if (ret != ESP_OK) {
        return rc_reply(reply, reply_size, false, "@err %s", esp_err_to_name(ret));
    }
    return rc_reply(reply, reply_size, true, "@ok trace %ld", seconds);
}


// Trial (ADR-010 step 2): `tear [free|off]` shows or sets anti-tearing (the flush's wait for the panel's scan).
// RAM only: a reboot restores it
static bool rc_cmd_tear(int argc, char **argv, char *reply, size_t reply_size) {
    if (argc == 2 && strcmp(argv[1], "free") == 0) {
        display_flush_set_tear_free(true);
    }
    else if (argc == 2 && strcmp(argv[1], "off") == 0) {
        display_flush_set_tear_free(false);
    }
    else if (argc != 1) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    return rc_reply(reply, reply_size, true, "@ok tear %s", display_flush_get_tear_free() ? "free" : "off");
}


// Debug (ADR-009): `bno alwayson grv|la 0|1` sets whether the level's game rotation vector or linear acceleration
// keeps running while the hub sleeps. RAM only: the report's next enable (a tile change) sets its default again
static bool rc_cmd_bno(int argc, char **argv, char *reply, size_t reply_size) {
    // Experiments (ADR-014), RAM only: `bno accel <ms>` (0 off), `bno wake fresh|resume`, `bno levelfix on|off`
    if (argc == 3 && strcmp(argv[1], "accel") == 0) {
        long ms = 0;
        if (!rc_parse_long(argv[2], &ms) || ms < 0 || ms > 1000) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        esp_err_t ret = bno085_enable_accelerometer_report(bno085_dev, (uint32_t) ms);
        return rc_reply(reply, reply_size, ret == ESP_OK, "@%s bno accel %ld", ret == ESP_OK ? "ok" : "err", ms);
    }
    if (argc == 3 && strcmp(argv[1], "wake") == 0 &&
        (strcmp(argv[2], "fresh") == 0 || strcmp(argv[2], "resume") == 0)) {
        bno085_dev->grv_fresh_on_wake = strcmp(argv[2], "fresh") == 0;
        return rc_reply(reply, reply_size, true, "@ok bno wake %s", argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "levelfix") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        level_wake_fix_enabled = strcmp(argv[2], "on") == 0;
        return rc_reply(reply, reply_size, true, "@ok bno levelfix %s", argv[2]);
    }
    if (argc != 4 || strcmp(argv[1], "alwayson") != 0 || (strcmp(argv[3], "0") != 0 && strcmp(argv[3], "1") != 0)) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    sh2_SensorId_t id;
    if (strcmp(argv[2], "grv") == 0) {
        id = SH2_GAME_ROTATION_VECTOR;
    }
    else if (strcmp(argv[2], "la") == 0) {
        id = SH2_LINEAR_ACCELERATION;
    }
    else {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    esp_err_t ret = bno085_set_report_always_on(bno085_dev, id, argv[3][0] == '1');
    if (ret != ESP_OK) {
        return rc_reply(reply, reply_size, false, "@err %s", esp_err_to_name(ret));
    }
    return rc_reply(reply, reply_size, true, "@ok bno alwayson %s %s", argv[2], argv[3]);
}


// Debug (ADR-009): `top [ms]` logs each task's CPU share over the window (default 2000 ms, at most 4000, inside the
// runner's reply timeout); verification builds only
static bool rc_cmd_top(int argc, char **argv, char *reply, size_t reply_size) {
    uint32_t window_ms = 2000;
    if (argc == 2) {
        window_ms = (uint32_t) strtoul(argv[1], NULL, 10);
    }
    if (argc > 2 || window_ms < 100 || window_ms > 4000) {
        return rc_reply(reply, reply_size, false, "@err args");
    }
    if (!mem_diag_cpu_report(window_ms)) {
        return rc_reply(reply, reply_size, false, "@err unsupported");
    }
    return rc_reply(reply, reply_size, true, "@ok top window_ms=%" PRIu32, window_ms);
}


// Debug: pulse the BNO085's reset line, so the sensor restarts as after an unexpected reset and the
// driver's recovery (re-enabling the saved reports) runs (ADR-005 B6).
static bool rc_cmd_bnoreset(char *reply, size_t reply_size) {
    esp_err_t ret = bno085_hardware_reset(bno085_dev);
    if (ret != ESP_OK) {
        return rc_reply(reply, reply_size, false, "@err %s", esp_err_to_name(ret));
    }
    return rc_reply(reply, reply_size, true, "@ok bnoreset");
}


static bool rc_execute_locked(char *line, char *reply, size_t reply_size) {
    char *argv[RC_MAX_ARGS] = {0};
    int argc = 0;
    char *save_ptr = NULL;

    for (char *token = strtok_r(line, " \t", &save_ptr); token != NULL; token = strtok_r(NULL, " \t", &save_ptr)) {
        if (argc == RC_MAX_ARGS) {
            argc++;  // too many arguments for any command
            break;
        }
        argv[argc++] = token;
    }

    if (argc == 0) {
        return rc_reply(reply, reply_size, false, "@err unknown");
    }

    const char *cmd = argv[0];
    bool is_observer = strcmp(cmd, "idle") == 0 || strcmp(cmd, "state") == 0 || strcmp(cmd, "top") == 0 ||
                       strcmp(cmd, "stats") == 0 || strcmp(cmd, "sleep") == 0 || strcmp(cmd, "help") == 0 ||
                       strcmp(cmd, "bt") == 0 || strcmp(cmd, "pmic") == 0 || strcmp(cmd, "snap") == 0;
    bool is_known = is_observer ||
                    strcmp(cmd, "tap") == 0 || strcmp(cmd, "swipe") == 0 || strcmp(cmd, "tile") == 0 ||
                    strcmp(cmd, "rot") == 0 || strcmp(cmd, "wifi") == 0 || strcmp(cmd, "save") == 0 ||
                    strcmp(cmd, "wake") == 0 || strcmp(cmd, "reboot") == 0 || strcmp(cmd, "hang") == 0 ||
                    strcmp(cmd, "bnoreset") == 0 || strcmp(cmd, "outline") == 0 || strcmp(cmd, "kestrel") == 0 ||
                    strcmp(cmd, "settings") == 0 || strcmp(cmd, "ota") == 0 || strcmp(cmd, "bno") == 0 ||
                    strcmp(cmd, "top") == 0 || strcmp(cmd, "tear") == 0 || strcmp(cmd, "grav") == 0 ||
                    strcmp(cmd, "trace") == 0;

    if (!is_known) {
        return rc_reply(reply, reply_size, false, "@err unknown %s", cmd);
    }

    // A scripted session keeps the device awake the way a person does; observing it does not
    if (!is_observer) {
        update_low_power_mode_last_activity_event();
    }

    if (argc > RC_MAX_ARGS) {
        return rc_reply(reply, reply_size, false, "@err args");
    }

    if (strcmp(cmd, "help") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_reply(reply, reply_size, true, "@ok help " RC_HELP_COMMANDS);
    }
    if (strcmp(cmd, "state") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_state(reply, reply_size);
    }
    if (strcmp(cmd, "stats") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_stats(reply, reply_size);
    }
    if (strcmp(cmd, "sleep") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_sleep(reply, reply_size);
    }
    if (strcmp(cmd, "bt") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_bt(reply, reply_size);
    }
    if (strcmp(cmd, "outline") == 0) {
        return rc_cmd_outline(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "snap") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_snap(reply, reply_size);
    }
    if (strcmp(cmd, "pmic") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_pmic(reply, reply_size);
    }
    if (strcmp(cmd, "idle") == 0) {
        return rc_cmd_idle(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "wake") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_wake(reply, reply_size);
    }
    if (strcmp(cmd, "reboot") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_reboot(reply, reply_size);
    }
    if (strcmp(cmd, "hang") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_hang(reply, reply_size);
    }
    if (strcmp(cmd, "bnoreset") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_bnoreset(reply, reply_size);
    }
    if (strcmp(cmd, "tap") == 0) {
        return rc_cmd_tap(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "swipe") == 0) {
        return rc_cmd_swipe(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "tile") == 0) {
        return rc_cmd_tile(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "rot") == 0) {
        return rc_cmd_rot(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "settings") == 0) {
        return rc_cmd_settings(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "wifi") == 0) {
        return rc_cmd_wifi(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "kestrel") == 0) {
        return rc_cmd_kestrel(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "bno") == 0) {
        return rc_cmd_bno(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "top") == 0) {
        return rc_cmd_top(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "tear") == 0) {
        return rc_cmd_tear(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "grav") == 0) {
        return rc_cmd_grav(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "trace") == 0) {
        return rc_cmd_trace(argc, argv, reply, reply_size);
    }
    if (strcmp(cmd, "ota") == 0) {
        return rc_cmd_ota(argc, argv, reply, reply_size);
    }
    return rc_cmd_save(argc, argv, reply, reply_size);
}


bool remote_control_execute(const char *line, char *reply, size_t reply_size, remote_control_write_fn write,
                            void *write_ctx) {
    if (reply == NULL || reply_size == 0) {
        return false;
    }
    if (line == NULL || rc_execute_mutex == NULL) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }

    // Tokenising writes into the line, so work on a copy
    char line_copy[REMOTE_CONTROL_LINE_MAX + 1];
    if (strlen(line) > REMOTE_CONTROL_LINE_MAX) {
        return rc_reply(reply, reply_size, false, "@err too_long");
    }
    strlcpy(line_copy, line, sizeof(line_copy));

    // One command at a time, whichever transport it comes from
    xSemaphoreTake(rc_execute_mutex, portMAX_DELAY);
    rc_write = write;
    rc_write_ctx = write_ctx;
    bool ok = rc_execute_locked(line_copy, reply, reply_size);
    rc_write = NULL;
    rc_write_ctx = NULL;
    xSemaphoreGive(rc_execute_mutex);

    return ok;
}


esp_err_t remote_control_init(void) {
    rc_execute_mutex = xSemaphoreCreateMutex();
    if (rc_execute_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create rc_execute_mutex");
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t reboot_timer_args = {
        .callback = rc_reboot_timer_cb,
        .name = "rc_reboot",
    };
    esp_err_t ret = esp_timer_create(&reboot_timer_args, &rc_reboot_timer);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create reboot timer: %s", esp_err_to_name(ret));

    const esp_timer_create_args_t idle_wake_timer_args = {
        .callback = rc_idle_wake_timer_cb,
        .name = "rc_idle_wake",
    };
    ret = esp_timer_create(&idle_wake_timer_args, &rc_idle_wake_timer);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create idle wake timer: %s", esp_err_to_name(ret));

    const esp_timer_create_args_t trace_timer_args = {
        .callback = rc_trace_timer_cb,
        .name = "rc_trace",
    };
    ret = esp_timer_create(&trace_timer_args, &rc_trace_timer);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create trace timer: %s", esp_err_to_name(ret));

    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "Failed to take the LVGL lock");
        return ESP_ERR_TIMEOUT;
    }

    // Virtual pointer for `tap` and `swipe`; its read timer only runs while a gesture is queued
    rc_pointer = lv_indev_create();
    if (rc_pointer) {
        lv_indev_set_type(rc_pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(rc_pointer, rc_pointer_read_cb);
        lv_timer_pause(lv_indev_get_read_timer(rc_pointer));
    }
    lvgl_port_unlock();

    if (rc_pointer == NULL) {
        ESP_LOGE(TAG, "Failed to create the virtual pointer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Remote control initialized");

    return ESP_OK;
}

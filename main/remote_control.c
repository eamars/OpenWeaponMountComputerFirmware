#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "lvgl.h"

#include "remote_control.h"
#include "app_cfg.h"
#include "common.h"
#include "bno085.h"
#include "low_power_mode.h"
#include "pmic_axp2101.h"
#include "ui_bridge.h"
#include "system_config.h"
#include "wifi.h"
#include "wifi_config.h"
#include "app.h"
#include "main_tileview.h"

#define TAG "RemoteControl"
#define NVS_NAMESPACE "RC"

// Every LVGL call from a command waits at most this long for the lock, then replies `@err busy`
#define RC_LVGL_LOCK_TIMEOUT_MS 1000

// Gestures: a gesture not consumed by LVGL within its duration plus this margin is dropped
#define RC_GESTURE_MARGIN_MS 1000
#define RC_GESTURE_POLL_MS 10
#define RC_TAP_PRESS_MS 100
#define RC_SWIPE_STEPS 6
#define RC_SWIPE_STEP_MS 25         // 6 steps over 150 ms
#define RC_SWIPE_DISTANCE_PCT 60
#define RC_POINT_QUEUE_LEN (RC_SWIPE_STEPS + 2)  // press, moves, release

#define RC_REBOOT_DELAY_US (100 * 1000)

#define RC_MAX_ARGS 4

#define RC_BT_DEPTH 24
#define RC_HELP_COMMANDS "help state stats bt pmic tap swipe tile rot wifi save idle wake reboot hang bnoreset"


typedef struct {
    int32_t x;          // logical coordinates, as LVGL sees them at the current rotation
    int32_t y;
    bool pressed;
    uint32_t hold_ms;   // how long this point stays current before the next one
} rc_point_t;


HEAPS_CAPS_ATTR remote_control_config_t remote_control_config;
const remote_control_config_t remote_control_config_default = {
    .serial_enable = true,  // on while the firmware is in development; flip before a release build
};


static SemaphoreHandle_t rc_execute_mutex = NULL;
static esp_timer_handle_t rc_reboot_timer = NULL;

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
    return load_config(NVS_NAMESPACE, &remote_control_config, &remote_control_config_default, sizeof(remote_control_config));
}


esp_err_t save_remote_control_config() {
    return save_config(NVS_NAMESPACE, &remote_control_config, sizeof(remote_control_config));
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
    if (argc != 3 || !rc_parse_long(argv[1], &x) || !rc_parse_long(argv[2], &y)) {
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
        { .x = x, .y = y, .pressed = true, .hold_ms = RC_TAP_PRESS_MS },
        { .x = x, .y = y, .pressed = false, .hold_ms = 0 },
    };
    rc_pointer_start(points, sizeof(points) / sizeof(points[0]));
    lvgl_port_unlock();

    if (!rc_pointer_wait(RC_TAP_PRESS_MS)) {
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


static bool rc_cmd_idle(char *reply, size_t reply_size) {
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    low_power_mode_request_idle();
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true, "@ok idle");
}


static bool rc_cmd_wake(char *reply, size_t reply_size) {
    if (!lvgl_port_lock(RC_LVGL_LOCK_TIMEOUT_MS)) {
        return rc_reply(reply, reply_size, false, "@err busy");
    }
    // Every existing caller wakes only from idle; waking an active device would rerun the exit path
    if (is_idle_mode_activated()) {
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
    int32_t icon = lv_subject_get_int(&ui_battery_level);
    const axp2101_status_t *shown = (const axp2101_status_t *) lv_subject_get_pointer(&ui_pmic_status);
    unsigned shown_vbat = shown->vbatt_voltage_mv;
    lvgl_port_unlock();

    return rc_reply(reply, reply_size, true,
                    "@ok pmic valid=%d soc=%d vbat=%u vbus=%u vsys=%u usb=%d chg=%d icon=%ld shown_vbat=%u",
                    status.valid ? 1 : 0, status.battery_percentage, (unsigned) status.vbatt_voltage_mv,
                    (unsigned) status.vbus_voltage_mv, (unsigned) status.vsys_voltage_mv,
                    status.is_usb_connected ? 1 : 0, status.charge_status, (long) icon, shown_vbat);
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
    bool is_observer = strcmp(cmd, "idle") == 0 || strcmp(cmd, "state") == 0 ||
                       strcmp(cmd, "stats") == 0 || strcmp(cmd, "help") == 0 ||
                       strcmp(cmd, "bt") == 0 || strcmp(cmd, "pmic") == 0;
    bool is_known = is_observer ||
                    strcmp(cmd, "tap") == 0 || strcmp(cmd, "swipe") == 0 || strcmp(cmd, "tile") == 0 ||
                    strcmp(cmd, "rot") == 0 || strcmp(cmd, "wifi") == 0 || strcmp(cmd, "save") == 0 ||
                    strcmp(cmd, "wake") == 0 || strcmp(cmd, "reboot") == 0 || strcmp(cmd, "hang") == 0 ||
                    strcmp(cmd, "bnoreset") == 0;

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
    if (strcmp(cmd, "bt") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_bt(reply, reply_size);
    }
    if (strcmp(cmd, "pmic") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_pmic(reply, reply_size);
    }
    if (strcmp(cmd, "idle") == 0) {
        if (argc != 1) {
            return rc_reply(reply, reply_size, false, "@err args");
        }
        return rc_cmd_idle(reply, reply_size);
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
    if (strcmp(cmd, "wifi") == 0) {
        return rc_cmd_wifi(argc, argv, reply, reply_size);
    }
    return rc_cmd_save(argc, argv, reply, reply_size);
}


bool remote_control_execute(const char *line, char *reply, size_t reply_size) {
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
    bool ok = rc_execute_locked(line_copy, reply, reply_size);
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

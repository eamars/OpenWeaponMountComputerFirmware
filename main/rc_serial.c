#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "rc_serial.h"
#include "remote_control.h"
#include "app_cfg.h"

#define TAG "RCSerial"

// rc_serial_stop() waits this long for the task to reach its stop point.
// Longest command path: LVGL lock wait (1 s) or a gesture plus its margin (~1.2 s).
#define RC_SERIAL_STOP_TIMEOUT_MS 5000
#define RC_SERIAL_STOP_POLL_MS 10

// Large enough for `stats` with the per-task stack report
#define RC_SERIAL_REPLY_SIZE 1024

#define RC_SERIAL_READ_CHUNK 32


static TaskHandle_t rc_serial_task_handle = NULL;
static volatile bool rc_serial_stop_requested = false;
static volatile bool rc_serial_parked = false;

HEAPS_CAPS_ATTR static char rc_serial_line[REMOTE_CONTROL_LINE_MAX + 1];
HEAPS_CAPS_ATTR static char rc_serial_reply[RC_SERIAL_REPLY_SIZE];


static void rc_serial_print_reply() {
    // One printf per reply, so the line is not interleaved with log output
    printf("%s\n", rc_serial_reply);
    fflush(stdout);
}


// The only place the task stops: no LVGL lock, no stdout lock, no command half done
static void rc_serial_park_if_requested() {
    if (rc_serial_stop_requested) {
        rc_serial_parked = true;
        vTaskSuspend(NULL);  // rc_serial_stop() deletes the task from here
    }
}


static void rc_serial_task(void *p) {
    char chunk[RC_SERIAL_READ_CHUNK];
    size_t line_len = 0;
    bool discarding = false;  // dropping the rest of an over-long line until its line ending

    while (1) {
        rc_serial_park_if_requested();

        // Default console read without a driver: returns at once, -1 (EAGAIN or no host) when nothing is waiting
        ssize_t received;
        while ((received = read(STDIN_FILENO, chunk, sizeof(chunk))) > 0) {
            for (ssize_t i = 0; i < received; i++) {
                char c = chunk[i];

                // CR, LF and CRLF all end a line; the empty line between CR and LF is ignored
                if (c == '\r' || c == '\n') {
                    if (!discarding && line_len > 0) {
                        rc_serial_line[line_len] = '\0';

                        rc_serial_park_if_requested();
                        remote_control_execute(rc_serial_line, rc_serial_reply, sizeof(rc_serial_reply));
                        rc_serial_print_reply();
                    }
                    line_len = 0;
                    discarding = false;
                    continue;
                }

                if (discarding) {
                    continue;
                }

                if (line_len == REMOTE_CONTROL_LINE_MAX) {
                    discarding = true;
                    line_len = 0;
                    snprintf(rc_serial_reply, sizeof(rc_serial_reply), "@err too_long");
                    rc_serial_print_reply();
                    continue;
                }

                rc_serial_line[line_len++] = c;
            }

            rc_serial_park_if_requested();
        }

        vTaskDelay(pdMS_TO_TICKS(RC_SERIAL_POLL_PERIOD_MS));
    }
}


esp_err_t rc_serial_start(void) {
    if (rc_serial_task_handle != NULL) {
        if (!rc_serial_stop_requested) {
            return ESP_OK;
        }

        // An earlier stop timed out; finish it before starting again
        rc_serial_stop();
        if (rc_serial_task_handle != NULL) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    rc_serial_stop_requested = false;
    rc_serial_parked = false;

    BaseType_t rtos_return = xTaskCreateWithCaps(
        rc_serial_task,
        "rc_serial",
        RC_SERIAL_TASK_STACK,
        NULL,
        RC_SERIAL_TASK_PRIORITY,
        &rc_serial_task_handle,
        HEAPS_CAPS_ALLOC_DEFAULT_FLAGS
    );
    if (rtos_return != pdPASS) {
        rc_serial_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to allocate memory for rc_serial_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Serial remote control started");

    return ESP_OK;
}


void rc_serial_stop(void) {
    if (rc_serial_task_handle == NULL) {
        return;
    }

    // Let the task finish its command and stop where it holds no lock; never delete it mid-command
    rc_serial_stop_requested = true;
    uint32_t waited_ms = 0;
    while (!rc_serial_parked && waited_ms < RC_SERIAL_STOP_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(RC_SERIAL_STOP_POLL_MS));
        waited_ms += RC_SERIAL_STOP_POLL_MS;
    }

    if (!rc_serial_parked) {
        // Leave the request set: the task parks when its command ends, and the next stop or start deletes it
        ESP_LOGE(TAG, "rc_serial did not stop within %d ms", RC_SERIAL_STOP_TIMEOUT_MS);
        return;
    }

    vTaskDeleteWithCaps(rc_serial_task_handle);
    rc_serial_task_handle = NULL;
    rc_serial_stop_requested = false;
    rc_serial_parked = false;

    ESP_LOGI(TAG, "Serial remote control stopped");
}


bool rc_serial_is_running(void) {
    return rc_serial_task_handle != NULL && !rc_serial_stop_requested;
}

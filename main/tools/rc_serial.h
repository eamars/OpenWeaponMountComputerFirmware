#ifndef RC_SERIAL_H_
#define RC_SERIAL_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC_SERIAL_TASK_STACK 4096
#define RC_SERIAL_TASK_PRIORITY 3
// Longest wait for input before the task looks for a stop request; well under rc_serial_stop()'s 2 s timeout
#define RC_SERIAL_READ_WAIT_MS 500


/**
 * @brief Start the serial remote control: one task reading command lines from the console. Idempotent.
 */
esp_err_t rc_serial_start(void);

/**
 * @brief Stop the serial remote control and delete its task. Idempotent.
 * Safe from LVGL callbacks: the task stops at a point where it holds no lock, after finishing the
 * command it is executing and printing its reply.
 */
void rc_serial_stop(void);

bool rc_serial_is_running(void);

#ifdef __cplusplus
}
#endif

#endif  // RC_SERIAL_H_

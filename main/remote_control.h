#ifndef REMOTE_CONTROL_H_
#define REMOTE_CONTROL_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Longest command line accepted, without the line ending
#define REMOTE_CONTROL_LINE_MAX 128


typedef struct {
    bool serial_enable;
} remote_control_config_t;


esp_err_t load_remote_control_config();
esp_err_t save_remote_control_config();

/**
 * @brief Create the virtual pointer used by `tap` and `swipe`. Call after lvgl_display_init().
 */
esp_err_t remote_control_init(void);

/**
 * @brief Execute one command line (no newline) and write one reply line (no newline) into reply.
 * Thread-safe; may block up to the command's time. Must not be called from the LVGL task.
 * @return true for an `@ok` reply, false for an `@err` reply.
 */
bool remote_control_execute(const char *line, char *reply, size_t reply_size);

#ifdef __cplusplus
}
#endif

#endif  // REMOTE_CONTROL_H_

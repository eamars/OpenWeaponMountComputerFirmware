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
    bool serial_enable;     // over USB
    bool wifi_enable;       // over Wi-Fi, on its own (ADR-013). Appended: older settings load with it off
} remote_control_config_t;


esp_err_t load_remote_control_config();
esp_err_t save_remote_control_config();

/**
 * @brief Create the virtual pointer used by `tap` and `swipe`. Call after lvgl_display_init().
 */
esp_err_t remote_control_init(void);

/**
 * @brief How a transport takes the lines a command sends before its reply (the snap's data): one line, no line
 * ending. Called from the task executing the command.
 */
typedef void (*remote_control_write_fn)(const char *line, void *ctx);

/**
 * @brief Execute one command line (no newline) and write one reply line (no newline) into reply. Lines the command
 * sends before its reply go to `write` (NULL drops them).
 * Thread-safe; may block up to the command's time. Must not be called from the LVGL task.
 * @return true for an `@ok` reply, false for an `@err` reply.
 */
bool remote_control_execute(const char *line, char *reply, size_t reply_size, remote_control_write_fn write,
                            void *write_ctx);

// Shared state, defined in remote_control.c
extern remote_control_config_t remote_control_config;
extern const remote_control_config_t remote_control_config_default;

#ifdef __cplusplus
}
#endif

#endif  // REMOTE_CONTROL_H_

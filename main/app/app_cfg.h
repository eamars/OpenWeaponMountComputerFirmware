#ifndef APP_CFG_H
#define APP_CFG_H

// The app's build-time settings: memory placement, which parts are fitted, task sizes and periods, profiling.
// The board's pins and panel are in board_cfg.h.

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "board_cfg.h"


// By default the memory will be allocated to PSRAM
#define HEAPS_CAPS_ALLOC_DEFAULT_FLAGS MALLOC_CAP_SPIRAM

// By default the heaps static memory will be allocated to PSRAM
#define HEAPS_CAPS_ATTR EXT_RAM_BSS_ATTR

// Parts fitted (each module builds its part only when set)
#define USE_BNO085 1
#define USE_BUZZER 1
#define USE_PMIC 1
#define USE_EXT_BUTTON 1

// Other software configurations
#define SENSOR_EVENT_POLLER_TASK_STACK 3072
#define SENSOR_EVENT_POLLER_TASK_PRIORITY 5
#define ACCELERATION_EVENT_POLLER_TASK_STACK 3072
#define ACCELERATION_EVENT_POLLER_TASK_PRIORITY 4

#define SENSOR_GAME_ROTATION_VECTOR_REPORT_PERIOD_MS 20
#define SENSOR_LINEAR_ACCELERATION_REPORT_PERIOD_MS 50
#define SENSOR_ACCELEROMETER_REPORT_PERIOD_MS 20     // the level's correction after a wake (ADR-014)


#define LOW_POWER_MODE_MONITOR_TASK_STACK 4096
#define LOW_POWER_MODE_MONITOR_TASK_PRIORITY 3
#define LOW_POWER_MODE_MONITOR_TASK_PERIOD_MS 1000

#define SENSOR_STABILITY_DETECTOR_POLLER_TASK_STACK 3072
#define SENSOR_STABILITY_DETECTOR_POLLER_TASK_PRIORITY 10
#define SENSOR_STABILITY_DETECTOR_REPORT_PERIOD_MS 100

#define LVGL_UNLOCK_WAIT_TIME_MS 1

// USB Serial/JTAG console driver buffers (internal RAM)
#define CONSOLE_TX_BUFFER_SIZE 4096
#define CONSOLE_RX_BUFFER_SIZE 256

#define DISPLAY_PROFILING_ENABLED 0
#define DISPLAY_PROFILING_REPORT_PERIOD_MS 1000

// Wake profiling (ADR-009): one `wake_prof:` line a second after each wake from idle (cause, the wake path's steps,
// the first frame, the BNO085's first reports)
#define WAKE_PROFILING_ENABLED 0


#endif // APP_CFG_H

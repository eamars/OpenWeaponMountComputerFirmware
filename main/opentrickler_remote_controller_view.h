#ifndef OPENTRICKLER_REMOTE_CONTROLLER_VIEW_H_
#define OPENTRICKLER_REMOTE_CONTROLLER_VIEW_H_

#include <stdbool.h>
#include "lvgl.h"


#define OPENTRICKLER_REST_POLLER_TASK_STACK 4096
#define OPENTRICKLER_REST_POLLER_TASK_PRIORITY 4
#define OPENTRICKLER_MAX_DISCOVER_COUNT 5
#define OPENTRICKLER_REST_POLLER_TASK_PERIOD_MS 500
#define OPENTRICKLER_REST_BUFFER_BYTES 160


void create_opentrickler_remote_controller_view(lv_obj_t * parent);
void enable_opentrickler_remote_controller_mode(bool enable);

#endif  // OPENTRICKLER_REMOTE_CONTROLLER_VIEW_H_
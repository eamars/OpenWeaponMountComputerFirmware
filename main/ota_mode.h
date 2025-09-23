#ifndef OTA_MODE_H_
#define OTA_MODE_H_

#include "lvgl.h"


#define OTA_POLLER_TASK_STACK 4096
#define OTA_POLLER_TASK_PRIORITY 4

void create_ota_mode_view(lv_obj_t * parent);
void enter_ota_mode(bool enable);
void update_ota_mode_progress(int progress);


#endif  // OTA_MODE_H_
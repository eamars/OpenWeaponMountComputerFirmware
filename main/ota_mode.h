#ifndef OTA_MODE_H_
#define OTA_MODE_H_

#include <stdint.h>

#include "lvgl.h"

typedef enum {
    OTA_IMPORTANCE_NORMAL = 0,
    OTA_IMPORTANCE_CRITICAL = 1
} ota_importance_e;


typedef struct {
    bool initialized;
    int manifest_version;
    char fw_version[16];
    char fw_build_hash[16];
    char fw_path[128];
    char fw_note[256];
    int port;
    bool ignore_version;
    ota_importance_e importance;
} ota_manifest_t;


#define OTA_POLLER_TASK_STACK 4096
#define OTA_POLLER_TASK_PRIORITY 4

#define OTA_UPDATE_TASK_STACK 4096
#define OTA_UPDATE_TASK_PRIORITY 7

void create_ota_mode_view(lv_obj_t * parent);
void create_ota_prompt_view(lv_obj_t * parent);
void enter_ota_mode(bool enable);
void update_ota_mode_progress(int progress);


#endif  // OTA_MODE_H_
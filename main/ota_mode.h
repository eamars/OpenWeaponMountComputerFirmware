#ifndef OTA_MODE_H_
#define OTA_MODE_H_

#include <stdint.h>

#include "lvgl.h"
#include "settings_ui.h"

typedef enum {
    OTA_IMPORTANCE_NORMAL = 0,
    OTA_IMPORTANCE_CRITICAL = 1
} ota_importance_e;

typedef enum {
    OTA_PACKAGE_TYPE_FIRMWARE = 0,
    OTA_PACKAGE_TYPE_DATA = 1,
    OTA_PACKAGE_TYPE_PARTITION_TABLE = 2,
    OTA_PACKAGE_TYPE_BOOTLOADER = 3,
} ota_package_type_e;


typedef struct {
    bool initialized;
    char *host;
    int manifest_version;
    char version[32];
    char path[128];
    char note[256];
    int port;
    bool ignore_version;
    ota_package_type_e type;
    ota_importance_e importance;
} ota_manifest_t;

#define OTA_MANIFEST_PORT 8080
#define OTA_HTTP_TIMEOUT 20 * 1000  // 20s

#define OTA_POLLER_TASK_STACK 4096
#define OTA_POLLER_TASK_PRIORITY 4

#define OTA_UPDATE_TASK_STACK 4096
#define OTA_UPDATE_TASK_PRIORITY 7


#ifdef __cplusplus
extern "C" {
#endif


void create_ota_mode_view(lv_obj_t * parent);
void create_ota_prompt_view(lv_obj_t * parent);
void enter_ota_mode(bool enable);
/** @brief True while the OTA tile is shown (an update may be downloading); any task */
bool ota_mode_is_active(void);
void update_ota_mode_progress(int progress);
/** @brief The hub's "Update available" row, hidden until a newer firmware is found (ADR-008) */
lv_obj_t * create_settings_update_row(settings_page_t * hub);


#ifdef __cplusplus
}
#endif

#endif  // OTA_MODE_H_
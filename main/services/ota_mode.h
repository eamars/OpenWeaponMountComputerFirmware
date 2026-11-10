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
    const char *host;       // where `path` is downloaded from when there is no `url`
    int manifest_version;
    char version[32];
    char path[128];
    char url[256];          // the image's address, when the manifest gives one; empty otherwise
    char note[1024];        // the release note, cut beyond this
    int port;
    bool ignore_version;
    ota_package_type_e type;
    ota_importance_e importance;
} ota_manifest_t;

#define OTA_MANIFEST_PORT 8080
#define OTA_HTTP_TIMEOUT 20 * 1000  // 20s
#define OTA_HTTP_HEADER_BUFFER_SIZE 2048    // GitHub's redirects carry addresses of about 900 bytes
#define OTA_MANIFEST_MAX_SIZE 4096

// The local host (scripts/ota_host.py) is asked first; the repository's latest published release next (ADR-015)
#define OTA_LOCAL_HOST "owmc_update.local"
#define OTA_LOCAL_MANIFEST_URL "http://" OTA_LOCAL_HOST ":8080/p1/manifest.json"
#define OTA_GITHUB_REPOSITORY "eamars/OpenWeaponMountComputerFirmware"
#define OTA_GITHUB_MANIFEST_URL "https://github.com/" OTA_GITHUB_REPOSITORY "/releases/latest/download/manifest.json"

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
/** @brief The hub's "Update available" row, hidden until a newer firmware is found (ADR-008) */
lv_obj_t * create_settings_update_row(settings_page_t * hub);

// Test hooks for the remote control (ADR-008): the offer and the OTA tile with mock data, without a server.
// LVGL lock held. The mock's server does not exist, so installing it fails
void ota_mode_test_mock(bool critical);
void ota_mode_test_offer(bool show);
void ota_mode_test_offer_end(void);
bool ota_mode_test_view(const char * state);    // connecting | downloading | done | failed


#ifdef __cplusplus
}
#endif

#endif  // OTA_MODE_H_
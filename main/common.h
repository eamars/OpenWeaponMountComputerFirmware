#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"


uint32_t crc32_wrapper(void * data, size_t length, size_t offset);

 /**
  * Wrap an angle in radians to the range [-π, π].
  */
float wrap_angle(float rad);


typedef struct {
    int major;
    int minor;
    int patch;
} version_t;
esp_err_t parse_git_describe_version(const char *describe, version_t * version);

/**
 * @brief Compare two versions.
 * Returns:
 *   -1 if self < other
 *    0 if self == other
 *    1 if self > other
 */
int compare_version(version_t * self, version_t * other);

/** 
 * @brief Read configuration from NVS. If the data doesn't exist or corrupted (likely due to the update of the file structure), the loaded config will be
 *      filled with default configuration. 
 */
#ifndef NVS_CFG_KEY
    #define NVS_KEY_NAME "cfg"
#endif  // NVS_CFG_KEY
esp_err_t load_config(const char *ns, void *cfg, const void *default_cfg, size_t size);


/**
 * @brief Save configuration to NVS. 
 */

esp_err_t save_config(const char *ns, void *cfg, size_t size);

#endif // COMMON_H_
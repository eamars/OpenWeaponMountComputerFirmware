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

#endif // COMMON_H_
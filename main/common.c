#include <math.h>
#include <string.h>
#include "common.h"

#include "esp_crc.h"

#define CRC32_POLYNOMIAL 0xEDB88320

uint32_t crc32_wrapper(void * data, size_t length, size_t offset) {
    uint8_t * data_start_addr = (uint8_t *) data + offset;
    size_t crc_length = length - offset;

    return esp_crc32_le(CRC32_POLYNOMIAL, data_start_addr, crc_length);
}



float wrap_angle(float rad) {
    rad = fmodf(rad + M_PI, 2 * M_PI);
    if (rad < 0) rad += 2 * M_PI;
    return rad - M_PI;
}


esp_err_t parse_git_describe_version(const char *describe, version_t * version) {
    // captures format like v1.0.0-dirty or v1.0.0
    sscanf(describe, "v%d.%d.%d", &version->major, &version->minor, &version->patch);
    
    return ESP_OK;
}

int compare_version(version_t *self, version_t *other) {
    if (self->major != other->major) return (self->major > other->major) ? 1 : -1;
    if (self->minor != other->minor) return (self->minor > other->minor) ? 1 : -1;
    if (self->patch != other->patch) return (self->patch > other->patch) ? 1 : -1;

    return 0; // versions are equal
}
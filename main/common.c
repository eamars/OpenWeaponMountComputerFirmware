#include <math.h>
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

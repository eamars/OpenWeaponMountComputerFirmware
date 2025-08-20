#ifndef COMMON_H_
#define COMMON_H_

#include <stdint.h>
#include <stddef.h>


uint32_t crc32_wrapper(void * data, size_t length, size_t offset);

 /**
  * Wrap an angle in radians to the range [-π, π].
  */
float wrap_angle(float rad);

#endif // COMMON_H_
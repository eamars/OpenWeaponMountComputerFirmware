#ifndef SH2_HAL_I2C_H_
#define SH2_HAL_I2C_H_

#include <stdint.h>

#include "sh2_hal.h"

#define BNO085_I2C_WRITE_TIMEOUT_MS 100
#define BNO085_I2C_ADDRESS 0x4A // Default I2C address for BNO085


int bno085_hal_i2c_open(sh2_Hal_t *self);
void bno085_hal_i2c_close(sh2_Hal_t *self);
int bno085_hal_i2c_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
int bno085_hal_i2c_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);

#endif  // SH2_HAL_I2C_H_
#ifndef SH2_HAL_SPI_H_
#define SH2_HAL_SPI_H_

#include <stdint.h>
#include "sh2_hal.h"


int bno085_hal_spi_open(sh2_Hal_t *self);
void bno085_hal_spi_close(sh2_Hal_t *self);
int bno085_hal_spi_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
int bno085_hal_spi_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);


#endif  // SH2_HAL_SPI_H_
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bno085.h"

#include "esp_log.h"
#include "esp_check.h"


#define TAG "BNO085_HAL_SPI"


esp_err_t wait_for_h_intn(uint32_t timeout_ms) {

    return ESP_OK;
}

int bno085_hal_spi_open(sh2_Hal_t *self) {
    // TODO: PS0 (GPIO7) needed to be asserted high before and after the hardware reset (GPIO8), until the first assertion of INT (GPIO5)
    return 0;
} 
void bno085_hal_spi_close(sh2_Hal_t *self) {

}
int bno085_hal_spi_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    return 0;
}
int bno085_hal_spi_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    return 0;
}
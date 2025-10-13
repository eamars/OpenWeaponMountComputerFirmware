#ifndef BNO085_PRIVATE_H_

#include "driver/gpio.h"
#include "esp_err.h"
#include "bno085.h"


#define BNO085_HARD_RESET_DELAY_MS 10
#define BNO085_SOFT_RESET_DELAY_MS 300


typedef enum {
    SENSOR_INTERRUPT_EVENT_BIT = (1 << 0),
    SENSOR_INIT_SUCCESS_EVENT_BIT = (1 << 1),
} sensor_event_control_bit_e;


esp_err_t _bno085_ctx_init(bno085_ctx_t *ctx, gpio_num_t interrupt_pin, gpio_num_t reset_pin, gpio_num_t boot_pin, gpio_num_t ps0_wake_pin);
esp_err_t _bno085_sh2_init(bno085_ctx_t *ctx);

void _bno085_disable_interrupt(bno085_ctx_t *ctx);
void _bno085_enable_interrupt(bno085_ctx_t *ctx);
esp_err_t _bno085_wait_for_interrupt(bno085_ctx_t *ctx);

#endif // BNO085_PRIVATE_H_
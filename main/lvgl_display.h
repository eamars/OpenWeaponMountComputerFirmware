#ifndef LVGL_DISPLAY_H_
#define LVGL_DISPLAY_H_

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_display_init(i2c_master_bus_handle_t tp_i2c_handle);


esp_err_t lvgl_display_wait_for_ready(uint32_t block_wait_ms);
bool lvgl_display_is_ready(); 


#ifdef __cplusplus
}
#endif


#endif  // LVGL_DISPLAY_H_
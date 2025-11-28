#include "usb.h"
#include "esp_err.h"
#include "esp_log.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"

#define TAG "USB"

esp_err_t usb_init() {
    ESP_LOGI(TAG, "USB initialization started");

    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    return ESP_OK;
}
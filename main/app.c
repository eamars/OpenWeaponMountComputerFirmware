/* FreeRTOS Real Time Stats Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_pm.h"

#include "driver/i2c_master.h"

#include "nvs_flash.h"

#include "main_tileview.h"
#include "app_cfg.h"
#include "bno085.h"
#include "system_config.h"
#include "sensor_config.h"
#include "bsp.h"
#include "wifi.h"
#include "wifi_provision.h"
#include "pmic_axp2101.h"
#include "usb.h"
#include "buzzer.h"
#include "lvgl_display.h"

#define TAG "App"


bno085_ctx_t * bno085_dev;
axp2101_ctx_t * axp2101_dev;
extern system_config_t system_config;
extern sensor_config_t sensor_config;


void mem_monitor_task(void *pvParameters) {
    while (1) {
        // Heap info
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "Free heap: %u kbytes | Min free heap: %u kbytes",
                (unsigned)free_heap / 1024, (unsigned)min_free_heap / 1024);

        // Optional: Internal RAM only
        ESP_LOGI(TAG, "Free internal heap: %u kbytes",
                (unsigned)esp_get_free_internal_heap_size() / 1024);


        ESP_LOGI(TAG, "---------------------------");
        vTaskDelay(pdMS_TO_TICKS(2000)); // Every 2 seconds
    }
}

i2c_master_bus_handle_t i2c0_master_init() {
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = (i2c_port_num_t) I2C0_PORT_NUM;
    i2c_mst_config.scl_io_num = I2C0_MASTER_SCL;
    i2c_mst_config.sda_io_num = I2C0_MASTER_SDA;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));
    return i2c_bus_handle;
}

i2c_master_bus_handle_t i2c1_master_init() {
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = (i2c_port_num_t) I2C1_PORT_NUM;
    i2c_mst_config.scl_io_num = I2C1_MASTER_SCL;
    i2c_mst_config.sda_io_num = I2C1_MASTER_SDA;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));
    return i2c_bus_handle;
}


esp_err_t storage_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

void app_main(void)
{
    esp_err_t ret;

    // Create task to monitor memory usage
    xTaskCreate(mem_monitor_task, "mem_monitor_task", 4096, NULL, 3, NULL);

    // Start event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Install interrupt service (so each module don't need to run again)
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    //Initialize NVS storage
    ESP_ERROR_CHECK(storage_init());

    // Read system configurations
    ESP_ERROR_CHECK(load_system_config());
    ESP_ERROR_CHECK(load_sensor_config());

    // Set buzzer
    ESP_ERROR_CHECK(buzzer_init());

    // Initialize I2C
    i2c_master_bus_handle_t i2c0_bus_handle;
    i2c_master_bus_handle_t i2c1_bus_handle;

    // Initialize USB
    // ESP_ERROR_CHECK(usb_init());

    // Initialize shared I2C bus
    i2c0_bus_handle = i2c0_master_init();
    i2c1_bus_handle = i2c1_master_init();

#if USE_PMIC
    // Initialize PMIC
    axp2101_dev = heap_caps_malloc(sizeof(axp2101_ctx_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    ESP_ERROR_CHECK(axp2101_init(axp2101_dev, i2c0_bus_handle, PMIC_AXP2101_INT_PIN));

    ESP_LOGI(TAG, "PMIC AXP2101 initialized");

#endif  // USE_PMIC


#if USE_BNO085
    // Initialize BNO085 sensor
    bno085_i2c_ctx_t * bno085_i2c_dev = heap_caps_malloc(sizeof(bno085_i2c_ctx_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);    
    if (bno085_i2c_dev == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for BNO085 I2C device");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    memset(bno085_i2c_dev, 0, sizeof(bno085_i2c_ctx_t));
    ESP_ERROR_CHECK(bno085_init_i2c(bno085_i2c_dev, i2c1_bus_handle, BNO085_INT_PIN, BNO085_RESET_PIN, BNO085_BOOT_PIN));
    bno085_dev = (bno085_ctx_t *) bno085_i2c_dev;
#endif  // USE_BNO085

    // Initialize Display
    ESP_ERROR_CHECK(lvgl_display_init(i2c0_bus_handle));

    // Initialize WiFi and related calls
    ESP_ERROR_CHECK(wifi_init());

    // Enable ESP32 power management module (automatically adjust CPU frequency based on RTOS scheduler)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // Beep the buzzer to indicate the success of initialization
    buzzer_run(100, 50, 2, false);
}


/**
 * Moves LVGL memory to PSRAM
 */

void lv_mem_init(void) {
    // do nothing
}

void lv_mem_deinit(void) {
    // do nothing
}

void *lv_malloc_core(size_t size) {
    return heap_caps_malloc(size, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
}

void *lv_realloc_core(void *p, size_t new_size) {
    return heap_caps_realloc(p, new_size, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
}

void lv_free_core(void *p)
{
	heap_caps_free(p);
}

// void esp_task_wdt_isr_user_handler(void) {
//     // Reboot
//     esp_restart();
// }
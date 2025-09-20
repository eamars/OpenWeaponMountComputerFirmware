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

#include "esp_check.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "driver/i2c_master.h"

#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "main_tileview.h"
#include "app_cfg.h"
#include "bno085.h"
#include "system_config.h"
#include "sensor_config.h"
#include "bsp.h"
#include "wifi_event.h"
#include "wifi_provision.h"

#define TAG "App"

static bno085_i2c_ctx_t bno085_i2c_dev;
bno085_ctx_t * bno085_dev;
extern system_config_t system_config;
extern sensor_config_t sensor_config;

// Initialize SPI touch screen and I2C display
esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;

// LVGL touch input device
lv_indev_t *lvgl_touch_handle = NULL;  // allow the low power module to inject callback


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

#if CONFIG_IDF_TARGET_ESP32C6
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon); // Fill the monitor struct

        ESP_LOGI(TAG, "LVGL free: %u kbytes | LVGL total: %u kbytes",
                mon.free_size / 1024, mon.total_size / 1024);
#endif  // CONFIG_IDF_TARGET_ESP32C6

        ESP_LOGI(TAG, "---------------------------");
        vTaskDelay(pdMS_TO_TICKS(2000)); // Every 2 seconds
    }
}

i2c_master_bus_handle_t i2c_master_init() {
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = (i2c_port_num_t) I2C_PORT_NUM;
    i2c_mst_config.scl_io_num = I2C_MASTER_SCL;
    i2c_mst_config.sda_io_num = I2C_MASTER_SDA;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));
    return i2c_bus_handle;
}


esp_err_t spi3_master_init() {
    spi_bus_config_t buscfg = {
        .miso_io_num = SPI3_MISO,
        .mosi_io_num = SPI3_MOSI,
        .sclk_io_num = SPI3_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
    };

    // Initialize SPI3
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST,&buscfg, SPI_DMA_CH_AUTO));

    return ESP_OK;
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

    // Initialize WiFi stack
    ESP_ERROR_CHECK(esp_netif_init());
    // Register Wifi events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Initialize Wifi including netif with default configuration
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure the provision manager
    wifi_prov_mgr_config_t config = {
        .wifi_prov_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // Check if the provisioning is already done
    bool is_provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&is_provisioned));

    if (!is_provisioned) {
        // Not provisioned, we will start the provisioning service
        ESP_LOGI(TAG, "Starting WiFi SoftAP provisioning");

        char * service_name = "PROV_OWMC";

        /* What is the security level that we want (0, 1, 2):
         *      - WIFI_PROV_SECURITY_0 is simply plain text communication.
         *      - WIFI_PROV_SECURITY_1 is secure communication which consists of secure handshake
         *          using X25519 key exchange and proof of possession (pop) and AES-CTR
         *          for encryption/decryption of messages.
         *      - WIFI_PROV_SECURITY_2 SRP6a based authentication and key exchange
         *        + AES-GCM encryption/decryption of messages
         */
        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        const char * pop = "PROV_1234"; // Proof of possession for security level 1 and 2

        /* This is the structure for passing security parameters
         * for the protocomm security 1.
         */
        wifi_prov_security1_params_t *sec_params = pop;

        const char *username  = NULL;
        /* What is the service key (could be NULL)
         * This translates to :
         *     - Wi-Fi password when scheme is wifi_prov_scheme_softap
         *          (Minimum expected length: 8, maximum 64 for WPA2-PSK)
         *     - simply ignored when scheme is wifi_prov_scheme_ble
         */
        const char *service_key = NULL;

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, service_key));

        
    }
    else {
        ESP_LOGI(TAG, "Already provisioned, starting WiFi STA");
        // Already provisioned, we can start the WiFi directly
        wifi_prov_mgr_deinit(); // Deinitialize the manager as we don't need it anymore

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    // Initialize I2C
    i2c_master_bus_handle_t i2c_bus_handle = i2c_master_init();
    
    // Initialize display modules
    ESP_ERROR_CHECK(display_init(&io_handle, &panel_handle, 100));
    ESP_ERROR_CHECK(touchscreen_init(&touch_handle, i2c_bus_handle, DISP_H_RES_PIXEL, DISP_V_RES_PIXEL, DISP_ROTATION));

    // Initialize BNO085 sensor
    ESP_ERROR_CHECK(bno085_init_i2c(&bno085_i2c_dev, i2c_bus_handle, BNO085_INT_PIN));
    bno085_dev = (bno085_ctx_t *) &bno085_i2c_dev;

    // Initialize LVGL
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    
    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    ESP_ERROR_CHECK(ret);

    // Add display to LVGL
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = DISP_H_RES_PIXEL * DISP_V_RES_PIXEL / 4,
        .double_buffer = true,
        .hres = DISP_H_RES_PIXEL,
        .vres = DISP_V_RES_PIXEL,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false
        },
#if CONFIG_IDF_TARGET_ESP32S3
        .trans_size = 8192,
#endif  // CONFIG_IDF_TARGET_ESP32S3
        .flags = {
            .swap_bytes = true,
            .sw_rotate = true,
#if CONFIG_IDF_TARGET_ESP32S3
            .buff_spiram = true,
            .buff_dma = false,
#else
            .buff_spiram = false,
            .buff_dma = true,
#endif  // CONFIG_TARGET_ESP32S3
        }
    };

    lv_display_t *lvgl_disp = NULL;
    // lv_indev_t *lvgl_touch_indev = NULL;
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // Add touch input to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp, 
        .handle = touch_handle
    };
    lvgl_touch_handle = lvgl_port_add_touch(&touch_cfg);

    // Create LVGL application
    if (lvgl_port_lock(0)) {
        create_main_tileview(lv_screen_active());
        lv_display_set_rotation(lvgl_disp, system_config.rotation);
        lvgl_port_unlock();
    }
}

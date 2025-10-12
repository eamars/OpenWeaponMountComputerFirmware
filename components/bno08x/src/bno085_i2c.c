#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bno085.h"
#include "bno085_private.h"
#include "driver/i2c_master.h"

#include "esp_log.h"
#include "esp_check.h"

#include "sh2_err.h"


#define TAG "BNO085_I2C"
#define BNO085_I2C_WRITE_TIMEOUT_MS 100
#define BNO085_I2C_ADDRESS 0x4A // Default I2C address for BNO085


int bno085_hal_i2c_open(sh2_Hal_t *self);
void bno085_hal_i2c_close(sh2_Hal_t *self);
int bno085_hal_i2c_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
int bno085_hal_i2c_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);


int i2c_soft_reset(bno085_i2c_ctx_t *ctx) {
    ESP_LOGI(TAG, "Sending soft reset to BNO085");
    // Send softreset packet
    uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
    int attempts = 5;
    for (; attempts >= 0; attempts -= 1) {
        if (i2c_master_transmit(ctx->dev_handle, softreset_pkt, sizeof(softreset_pkt), BNO085_I2C_WRITE_TIMEOUT_MS) == ESP_OK) {
            break;
        }
        ESP_LOGI(TAG, "Failed to send soft reset, will retry %d", attempts);
        vTaskDelay(pdMS_TO_TICKS(BNO085_SOFT_RESET_DELAY_MS));
    }
    if (attempts == 0) {
        ESP_LOGI(TAG, "Failed to send soft reset, will quit");
        return -1;
    }

    ESP_LOGI(TAG, "Soft reset sent successfully, waiting for device to reset");

    vTaskDelay(pdMS_TO_TICKS(BNO085_SOFT_RESET_DELAY_MS));
    return 0;
}

int bno085_hal_i2c_open(sh2_Hal_t *self) {
    // ESP_LOGI(TAG, "i2c_open() called");

    // Cast self back to the context object
    bno085_i2c_ctx_t * ctx = (bno085_i2c_ctx_t *) self;

    // Send softreset packet
    int ret = i2c_soft_reset(ctx);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to send soft reset");
        return ret;
    }

    ESP_LOGI(TAG, "i2c_open() complete");

    return 0;
}


void bno085_hal_i2c_close(sh2_Hal_t *self) {
    ESP_LOGI(TAG, "i2c_close() called");

    // nothing need to be done
}


int bno085_hal_i2c_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    // ESP_LOGI(TAG, "i2c_read() called with len: %d", len);
    esp_err_t err;

    // Cast self back to the context object
    bno085_i2c_ctx_t * ctx = (bno085_i2c_ctx_t *) self;

    // Read header (4 bytes)
    uint8_t headers[4];
    err = i2c_master_receive(ctx->dev_handle, headers, 4, BNO085_I2C_WRITE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read headers: %s", esp_err_to_name(err));
        
        // Send a soft reset
        i2c_soft_reset(ctx);
        return 0;
    }

    uint16_t packet_size = ((uint16_t)headers[0] + ((uint16_t)headers[1] << 8)) & ~0x8000;

    // Check the buffer size
    if (len < packet_size) {
        return 0;
    }
    else if (packet_size > 0) {
        // Read remaining packet
        err = i2c_master_receive(ctx->dev_handle, pBuffer, packet_size, BNO085_I2C_WRITE_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read packet: %s", esp_err_to_name(err));
        
            // Send a soft reset
            i2c_soft_reset(ctx); 
            return 0;
        }
    }
    else {
        // ESP_LOGW(TAG, "No data");
    }

    return packet_size;
}



int bno085_hal_i2c_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    esp_err_t err;
    // ESP_LOGI(TAG, "i2c_write() called");

    // Cast self back to the context object
    bno085_i2c_ctx_t * ctx = (bno085_i2c_ctx_t *) self;

    err = i2c_master_transmit(ctx->dev_handle, pBuffer, len, BNO085_I2C_WRITE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write data: %s", esp_err_to_name(err));
        return 0;
    }

    // ESP_LOGI(TAG, "i2c_write() write %d bytes", len);
    // for (int i = 0; i < len; i++) {
    //     printf("%02x ", pBuffer[i]);
    // }
    // printf("\n");

    return len;
}

esp_err_t bno085_init_i2c(bno085_i2c_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin) {
    // Initialize configuration
    ESP_RETURN_ON_ERROR(_bno085_ctx_init(&ctx->parent, interrupt_pin, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC), TAG, "Failed to initialize ctx object");

    // Configure I2C
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BNO085_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };

    // Send an probe command to verify if the device is available
    int attempt = 5;
    for (; attempt > 0; attempt -= 1) {
        if (i2c_master_probe(i2c_bus_handle, BNO085_I2C_ADDRESS, BNO085_I2C_WRITE_TIMEOUT_MS) == ESP_OK) {
            ESP_LOGI(TAG, "BNO085 i2c slave device detected");
            break;
        }
        ESP_LOGW(TAG, "Retry in 10ms");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (attempt == 0) {
        ESP_LOGE(TAG, "Failed to detect BNO085 i2c slave device");
        return ESP_FAIL;
    }

    // Add I2C slave to the master
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &ctx->dev_handle));


    // Assign HAL functions
    ctx->parent._HAL.open = bno085_hal_i2c_open;
    ctx->parent._HAL.close = bno085_hal_i2c_close;
    ctx->parent._HAL.read = bno085_hal_i2c_read;
    ctx->parent._HAL.write = bno085_hal_i2c_write;
    
    // Initialize SH2
    ESP_RETURN_ON_ERROR(_bno085_sh2_init(&ctx->parent), TAG, "Failed to initialize SH2 Interface");

    // Set initialization success
    xEventGroupSetBits(ctx->parent.sensor_event_control, SENSOR_INIT_SUCCESS_EVENT_BIT);

    return ESP_OK;
}

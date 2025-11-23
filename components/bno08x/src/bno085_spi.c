#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bno085.h"
#include "bno085_private.h"

#include "esp_log.h"
#include "esp_check.h"

#define TAG "BNO085_SPI"


int bno085_hal_spi_open(sh2_Hal_t *self);
void bno085_hal_spi_close(sh2_Hal_t *self);
int bno085_hal_spi_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
int bno085_hal_spi_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);


void bno085_spi_hard_reset(bno085_ctx_t *ctx) {
    if (ctx->reset_pin != GPIO_NUM_NC) {
        // Make sure the BOOTN, PS0/WAKE is high before and after the reset
        if (ctx->ps0_wake_pin != GPIO_NUM_NC) {
            // Set ps0_wake pin to high (to assert SPI mode)
            ESP_LOGI(TAG, "Setting PS0/WAKE pin high for SPI mode");
            gpio_set_level(ctx->ps0_wake_pin, 1);
        }

        if (ctx->boot_pin != GPIO_NUM_NC) {
            // Set boot pin to high (normal mode)
            ESP_LOGI(TAG, "Setting BOOTN pin high for normal mode");
            gpio_set_level(ctx->boot_pin, 1);
        }

#if BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
        gpio_set_level(((bno085_spi_ctx_t *) ctx)->spi_cs_pin, 1);  // Set CS high to de-assert
#endif

        // Disable interrupt
        _bno085_disable_interrupt(ctx);

        // Hold reset to low
        gpio_set_level(ctx->reset_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(BNO085_HARD_RESET_DELAY_MS));

        // Set reset pin to high
        gpio_set_level(ctx->reset_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(90));  // As per Figure 6-9

        // Enable interrupt
        _bno085_enable_interrupt(ctx);

        ESP_LOGI(TAG, "BNO085 hard reset complete");


    }
}

int bno085_hal_spi_open(sh2_Hal_t *self) {
    if (_bno085_wait_for_interrupt((bno085_ctx_t *) self) != ESP_OK) {
        bno085_spi_hard_reset((bno085_ctx_t *) self);
    }

    return 0;
} 
void bno085_hal_spi_close(sh2_Hal_t *self) {
    // do nothing
}
int bno085_hal_spi_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    bno085_spi_ctx_t * ctx = (bno085_spi_ctx_t *) self;
    esp_err_t err;

    if (_bno085_wait_for_interrupt((bno085_ctx_t *) self) != ESP_OK) {
        ESP_LOGW(TAG, "spi_read() timeout waiting for interrupt");
        return 0;
    }

#if BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
    // Assert chip select for full duplex transfer
    gpio_set_level(((bno085_spi_ctx_t *) ctx)->spi_cs_pin, 0);  // assert CS
#endif

    // Read SH2 header
    uint8_t tx_headers[4] = {0};
    uint8_t rx_headers[4] = {0};

    spi_transaction_t transaction = {
        .tx_buffer = tx_headers,
        .length = sizeof(tx_headers) * 8,  // in bits

        .rx_buffer = rx_headers,
        .rxlength = sizeof(rx_headers) * 8, // in bits

        .flags = 0,
    };

    err = spi_device_polling_transmit(ctx->dev_handle, &transaction);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read headers: %s", esp_err_to_name(err));
        return 0;
    }

    // Parse the return header to determine the packet size
    uint16_t packet_size = ((uint16_t)rx_headers[0] + ((uint16_t)rx_headers[1] << 8)) & ~0x8000;

    ESP_LOGI(TAG, "spi_read() received header: %02x %02x %02x %02x, packet size: %d", rx_headers[0], rx_headers[1], rx_headers[2], rx_headers[3], packet_size);

    // Check the buffer size
    if (len < packet_size) {
        return 0;
    }
    else if (packet_size > 0) {
        // Read remaining packet
        transaction.tx_buffer = NULL;  // No MOSI for the reminder of the packet
        transaction.length = packet_size * 8;

        transaction.rx_buffer = pBuffer;
        transaction.rxlength = packet_size * 8; // in bits

        err = spi_device_polling_transmit(ctx->dev_handle, &transaction);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read packet: %s", esp_err_to_name(err));
            packet_size = 0;
        }

        // ESP_LOGI(TAG, "spi_read() read %d bytes", packet_size);
        // for (int i = 0; i < packet_size; i++) {
        //     printf("%02x ", pBuffer[i]);
        // }
        // printf("\n");
    }
    else {
        // No data
    }

#if BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
    // Terminate full duplex transfer
    gpio_set_level(((bno085_spi_ctx_t *) ctx)->spi_cs_pin, 1);  // de-assert CS
#endif
    

    return packet_size;
}
int bno085_hal_spi_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    bno085_spi_ctx_t * ctx = (bno085_spi_ctx_t *) self;
    esp_err_t err;

    if (_bno085_wait_for_interrupt((bno085_ctx_t *) self) != ESP_OK) {
        ESP_LOGW(TAG, "spi_write() timeout waiting for interrupt");
        return 0;
    }
    
    ESP_LOGI(TAG, "spi_write() write %d bytes", len);
    for (int i = 0; i < len; i++) {
        printf("%02x ", pBuffer[i]);
    }
    printf("\n"); 

    ESP_LOGI(TAG, "spi_write() writing %d bytes", len);

#if BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
    // Assert chip select for full duplex transfer
    gpio_set_level(((bno085_spi_ctx_t *) ctx)->spi_cs_pin, 0);  // assert CS
#endif

    spi_transaction_t transaction = {
        .tx_buffer = pBuffer,
        .length = len * 8,  // in bits

        .rx_buffer = NULL,
        .rxlength = 0,

        .flags = 0,
    };

    err = spi_device_polling_transmit(ctx->dev_handle, &transaction);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write data: %s", esp_err_to_name(err));
        len = 0;
    }

#if BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
    // Terminate full duplex transfer
    gpio_set_level(((bno085_spi_ctx_t *) ctx)->spi_cs_pin, 1);  // de-assert CS
#endif

    return len;
}

esp_err_t bno085_init_spi(bno085_spi_ctx_t *ctx, spi_host_device_t spi_host, gpio_num_t spi_cs_pin, gpio_num_t interrupt_pin, gpio_num_t reset_pin, gpio_num_t boot_pin, gpio_num_t ps0_wake_pin)
{
    // Initialize configuration
    ESP_RETURN_ON_ERROR(_bno085_ctx_init(&ctx->parent, interrupt_pin, reset_pin, boot_pin, ps0_wake_pin), TAG, "Failed to initialize ctx object");

    // Set ps0/wake pin to high by default
    if (ctx->parent.ps0_wake_pin != GPIO_NUM_NC) {
        // Set ps0_wake pin to high (to assert SPI mode)
        gpio_set_level(ctx->parent.ps0_wake_pin, 1);
    }

    // Configure CS as output pin
    ctx->spi_cs_pin = spi_cs_pin;
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << ctx->spi_cs_pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(ctx->spi_cs_pin, 1);  // de-assert CS

    ESP_LOGI(TAG, "Configured spi_cs pin on pin %d", ctx->spi_cs_pin);

    // Configure SPI slave device
    spi_device_interface_config_t dev_cfg = {
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .clock_speed_hz = 3000000UL,  // Maximum 3Mhz as per Figure 6-6
        .address_bits = 0,
        .command_bits = 0,
#if BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
        .spics_io_num = GPIO_NUM_NC,  // Don't use SPI driver to control CS
#else
        .spics_io_num = ctx->spi_cs_pin,
#endif  // BNO085_USE_SOFTWARE_CONTROLLED_CS_PIN
        .queue_size = 5,
        .mode = 0x3,    // CPOL=1, CPHA=1, MODE=SPI_MODE3
    };

    // Initialize the slave device
    ESP_RETURN_ON_ERROR(spi_bus_add_device(spi_host, &dev_cfg, &ctx->dev_handle), TAG, "Failed to initialize SPI slave device");

    // Assign HAL functions
    ctx->parent._HAL.open = bno085_hal_spi_open;
    ctx->parent._HAL.close = bno085_hal_spi_close;
    ctx->parent._HAL.read = bno085_hal_spi_read;
    ctx->parent._HAL.write = bno085_hal_spi_write;

    // Initialize SH2
    ESP_RETURN_ON_ERROR(_bno085_sh2_init(&ctx->parent), TAG, "Failed to initialize SH2 Interface");

    return ESP_OK;
}

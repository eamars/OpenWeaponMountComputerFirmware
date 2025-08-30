#include <math.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_task.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#include "bno085.h"
#include "sh2_err.h"


#define TAG "BNO085"
#define BNO085_I2C_ADDRESS 0x4A // Default I2C address for BNO085
#define BNO085_I2C_WRITE_TIMEOUT_MS 100
#define HARD_RESET_DELAY_MS 1000
#define SOFT_RESET_DELAY_MS 300


// Forward declaration
float q_to_roll_sf(float dqw, float dqx, float dqy, float dqz);
float q_to_pitch_sf(float dqw, float dqx, float dqy, float dqz);
float q_to_yaw_sf(float dqw, float dqx, float dqy, float dqz);


uint32_t get_time_us(sh2_Hal_t *self) {
    uint32_t time_us = esp_timer_get_time() & 0xFFFFFFFFul;

    return time_us;
}


static void disable_interrupt(bno085_ctx_t *ctx) {
    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        gpio_intr_disable(ctx->interrupt_pin);
    }
}

static void enable_interrupt(bno085_ctx_t *ctx) {
    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        gpio_intr_enable(ctx->interrupt_pin);
    }
}

static void sh2_sensor_callback(void *cookie, sh2_SensorEvent_t *event) {
    // Cast cookie back to the context
    bno085_ctx_t * ctx = (bno085_ctx_t *) cookie;

    // Decode event into the value
    sh2_SensorValue_t sensor_value;
    if (sh2_decodeSensorEvent(&sensor_value, event) == SH2_ERR) {
        ESP_LOGI(TAG, "sh2_decodeSensorEventd failed");
        return;
    }

    // Read sensor type and send it to the corresponding 
    sensor_report_config_t * target_report_config = &ctx->enabled_sensor_report_list[sensor_value.sensorId];
    if (target_report_config->sensor_value_queue == NULL) {
        ESP_LOGE(TAG, "Sensor value queue is not initialized for sensor ID %d", sensor_value.sensorId);
        return;
    }

    // Send it to the corresponding queue
    // ESP_LOGI(TAG, "Event REceived %p", sensor_value.sensorId);
    xQueueOverwrite(target_report_config->sensor_value_queue, &sensor_value);
}


void bno085_hard_reset(bno085_ctx_t *ctx) {
    if (ctx->reset_pin != GPIO_NUM_NC) {
        disable_interrupt(ctx);
        gpio_set_level(ctx->reset_pin, 0);  // set to low (active low)
        vTaskDelay(pdMS_TO_TICKS(HARD_RESET_DELAY_MS));
        gpio_set_level(ctx->reset_pin, 1);  // set to high
        vTaskDelay(pdMS_TO_TICKS(HARD_RESET_DELAY_MS));
        enable_interrupt(ctx);

        ESP_LOGI(TAG, "BNO085 Resetted");
    }
    else {
        ESP_LOGI(TAG, "BNO085 Reset pin not configured, skipping hard reset");
    }
}


int bno085_soft_reset(bno085_ctx_t *ctx) {
    ESP_LOGI(TAG, "Sending soft reset to BNO085");
    // Send softreset packet
    uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
    int attempts = 5;
    for (; attempts >= 0; attempts -= 1) {
        if (i2c_master_transmit(ctx->dev_handle, softreset_pkt, sizeof(softreset_pkt), BNO085_I2C_WRITE_TIMEOUT_MS) == ESP_OK) {
            break;
        }
        ESP_LOGI(TAG, "Failed to send soft reset, will retry %d", attempts);
        vTaskDelay(pdMS_TO_TICKS(SOFT_RESET_DELAY_MS));
    }
    if (attempts == 0) {
        ESP_LOGI(TAG, "Failed to send soft reset, will quit");
        return -1;
    }

    ESP_LOGI(TAG, "Soft reset sent successfully, waiting for device to reset");

    vTaskDelay(pdMS_TO_TICKS(SOFT_RESET_DELAY_MS));
    return 0;
}


int i2c_open(sh2_Hal_t *self) {
    // ESP_LOGI(TAG, "i2c_open() called");

    // Cast self back to the context object
    bno085_ctx_t * ctx = (bno085_ctx_t *) self;

    // Send softreset packet
    int ret = bno085_soft_reset(ctx);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to send soft reset");
        return ret;
    }

    ESP_LOGI(TAG, "i2c_open() complete");

    return 0;
}


void i2c_close(sh2_Hal_t *self) {
    ESP_LOGI(TAG, "i2c_close() called");

    // nothing need to be done
}


int i2c_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    // ESP_LOGI(TAG, "i2c_read() called with len: %d", len);
    esp_err_t err;

    // Cast self back to the context object
    bno085_ctx_t * ctx = (bno085_ctx_t *) self;

    // Read header (4 bytes)
    uint8_t headers[4];
    err = i2c_master_receive(ctx->dev_handle, headers, 4, BNO085_I2C_WRITE_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read headers: %s", esp_err_to_name(err));
        
        // Send a soft reset
        bno085_soft_reset(ctx);
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
            bno085_soft_reset(ctx); 
            return 0;
        }
    }
    else {
        // ESP_LOGW(TAG, "No data");
    }

    return packet_size;
}



int i2c_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    esp_err_t err;
    // ESP_LOGI(TAG, "i2c_write() called");

    // Cast self back to the context object
    bno085_ctx_t * ctx = (bno085_ctx_t *) self;

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


/**
 * @brief Interrupt handler for the BNO085 sensor
 */
void IRAM_ATTR bno085_interrupt_handler(void *arg) {
    bno085_ctx_t *ctx = (bno085_ctx_t *) arg;

    // Permit the sensor poller to run
    if (ctx->sensor_poller_task_handle) {
        vTaskNotifyGiveFromISR(ctx->sensor_poller_task_handle, 0);
    }   
}

void sensor_poller_task(void *self) {
    while (1) {
        // Wait until the interrupt happens
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        sh2_service();
    }
}


esp_err_t sh2_enable_report(sh2_SensorId_t sensor_id, uint32_t interval_ms) {
    static sh2_SensorConfig_t config = {
        .changeSensitivityEnabled = false,
        .wakeupEnabled = false,
        .changeSensitivityRelative = false,
        .alwaysOnEnabled = false,
        .changeSensitivity = 0,
        .batchInterval_us = 0,
        .sensorSpecific = 0,
    };

    config.reportInterval_us = interval_ms * 1000;

    int status = sh2_setSensorConfig(sensor_id, &config);

    if (status != SH2_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}


static void sh2_event_callback(void *cookie, sh2_AsyncEvent_t *pEvent) {
    // Cast cookie back to the context
    bno085_ctx_t * ctx = (bno085_ctx_t *) cookie;

    // If we see a reset, set a flag so that sensors will be reconfigured.
    switch (pEvent->eventId) {
        case SH2_RESET: {
            ESP_LOGW(TAG, "BNO085 Reset Unexpectly");
        
            // Go over saved configurations
            for (uint8_t i = 0; i < SH2_MAX_SENSOR_EVENT_LEN; i += 1) {
                // Re-enable reports
                if (ctx->enabled_sensor_report_list[i].interval_ms != 0) {
                    sh2_enable_report(i, ctx->enabled_sensor_report_list[i].interval_ms);
                }
            }
            break;
        }
        case SH2_SHTP_EVENT: {
            // ESP_LOGI(TAG, "EventHandler  id:SHTP, %d\n", pEvent->shtpEvent);
            break;
        }
        case SH2_GET_FEATURE_RESP: {
            // ESP_LOGI(TAG, "EventHandler Sensor Config, %d\n", pEvent->sh2SensorConfigResp.sensorId);
            break;
        }
        default: {
            ESP_LOGW(TAG, "EventHandler, unknown event Id: %d\n", pEvent->eventId);
        }
    }

}


esp_err_t bno085_init_i2c(bno085_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, int interrupt_pin) {
    // Initialize configuration
    memset(ctx, 0x0, sizeof(bno085_ctx_t));
    ctx->reset_pin = GPIO_NUM_NC;  // No reset pin configured by default
    ctx->interrupt_pin = GPIO_NUM_NC;  // no interrupt pin configured before initialized

    // Configure Interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << interrupt_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // enable internal pull up to avoid floating state during BNO085 chip reset
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // Trigger on falling edge (active low)
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Setup interrupt handler
    ESP_ERROR_CHECK(gpio_isr_handler_add(interrupt_pin, bno085_interrupt_handler, (void *) ctx));

    // Record
    ctx->interrupt_pin = interrupt_pin;

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
    ctx->_HAL.open = i2c_open;
    ctx->_HAL.close = i2c_close;
    ctx->_HAL.read = i2c_read;
    ctx->_HAL.write = i2c_write;
    ctx->_HAL.getTimeUs = get_time_us;

    
    // Open SH2 interface
    int status;
    status = sh2_open((sh2_Hal_t *) ctx, sh2_event_callback, (void *) ctx);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to run sh2_open(): %d", status);
        return ESP_FAIL;
    }

    sh2_ProductIds_t prodIds;
    memset(&prodIds, 0, sizeof(prodIds));
    status = sh2_getProdIds(&prodIds);
    if (status != SH2_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ProdIds Read");


    // Register sensor callback
    if (sh2_setSensorCallback(sh2_sensor_callback, (void *) ctx)) {
        return ESP_FAIL;
    }

    // Create task to process event
    BaseType_t rtos_return = xTaskCreate(
        sensor_poller_task, 
        "sensor_poller",
        BNO085_SENSOR_POLLER_TASK_STACK,
        (void *) ctx, 
        BNO085_SENSOR_POLLER_TASK_PRIORITY,
        &ctx->sensor_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor_poller");
        return ESP_FAIL;
    }


    return ESP_OK;
}

esp_err_t bno085_enable_report(bno085_ctx_t *ctx, sh2_SensorId_t sensor_id, uint32_t interval_ms) {
    // look for a slot to save the config
    sensor_report_config_t * target_report_config = &ctx->enabled_sensor_report_list[sensor_id];

    // enable report
    ESP_LOGI(TAG, "Enabling Report 0x%x with interval %dms", sensor_id, interval_ms);

    // Create queue if not created already
    if (target_report_config->sensor_value_queue == NULL) {
        target_report_config->sensor_value_queue = xQueueCreate(BNO085_EVENT_QUEUE_DEPTH, sizeof(sh2_SensorValue_t));
        if (target_report_config->sensor_value_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create queue for sensor report");
            return ESP_FAIL;
        }
    }

    // Fill other house keeping information
    target_report_config->interval_ms = interval_ms;

    // Enable report at the sensor
    return sh2_enable_report(sensor_id, interval_ms);
}


esp_err_t bno085_enable_game_rotation_vector_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    return bno085_enable_report(ctx, SH2_GAME_ROTATION_VECTOR, interval_ms);
}


esp_err_t bno085_enable_linear_acceleration_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    return bno085_enable_report(ctx, SH2_LINEAR_ACCELERATION, interval_ms);   
}

esp_err_t bno085_enable_stability_classification_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    return bno085_enable_report(ctx, SH2_STABILITY_CLASSIFIER, interval_ms);
}


esp_err_t bno085_wait_for_game_rotation_vector_roll_pitch(bno085_ctx_t *ctx, float *roll, float *pitch, bool block_wait) {
    TickType_t wait_ticks;
    if (block_wait) {
        wait_ticks = portMAX_DELAY;
    }
    else {
        wait_ticks = 0;
    }

    sh2_SensorValue_t sensor_value;

    // Wait for the queue from the corresponding report
    if (xQueueReceive(ctx->enabled_sensor_report_list[SH2_GAME_ROTATION_VECTOR].sensor_value_queue, &sensor_value, wait_ticks) != pdPASS) {
        // ESP_LOGE(TAG, "Failed to receive game rotation vector report");
        return ESP_FAIL;
    }

    // Decode sensor event
    if (sensor_value.sensorId == SH2_GAME_ROTATION_VECTOR) {
        float r, i, j, k;
        r = sensor_value.un.gameRotationVector.real;
        i = sensor_value.un.gameRotationVector.i;
        j = sensor_value.un.gameRotationVector.j;
        k = sensor_value.un.gameRotationVector.k;

        *pitch = q_to_pitch_sf(r, i, j, k);
        *roll = q_to_roll_sf(r, i, j, k);
    }

    return ESP_OK;
}

esp_err_t bno085_wait_for_linear_acceleration_report(bno085_ctx_t *ctx, float *x, float *y, float *z, bool block_wait) {
    TickType_t wait_ticks;
    if (block_wait) {
        wait_ticks = portMAX_DELAY;
    }
    else {
        wait_ticks = 0;
    }

    sh2_SensorValue_t sensor_value;

    // Wait for the queue from the corresponding report
    if (xQueueReceive(ctx->enabled_sensor_report_list[SH2_LINEAR_ACCELERATION].sensor_value_queue, &sensor_value, wait_ticks) != pdPASS) {
        // ESP_LOGE(TAG, "Failed to receive linear acceleration report");
        return ESP_FAIL;
    }

    // Decode sensor event
    if (sensor_value.sensorId == SH2_LINEAR_ACCELERATION) {
        *x = sensor_value.un.linearAcceleration.x;
        *y = sensor_value.un.linearAcceleration.y;
        *z = sensor_value.un.linearAcceleration.z;
    }

    return ESP_OK;
}


esp_err_t bno085_wait_for_stability_classification_report(bno085_ctx_t *ctx, uint8_t * classification, bool block_wait) {
    TickType_t wait_ticks;
    if (block_wait) {
        wait_ticks = portMAX_DELAY;
    }
    else {
        wait_ticks = 0;
    }

    sh2_SensorValue_t sensor_value;

    // Wait for the queue from the corresponding report
    if (xQueueReceive(ctx->enabled_sensor_report_list[SH2_STABILITY_CLASSIFIER].sensor_value_queue, &sensor_value, wait_ticks) != pdPASS) {
        // ESP_LOGE(TAG, "Failed to receive stability classification report");
        return ESP_FAIL;
    }

    // Decode sensor event
    if (sensor_value.sensorId == SH2_STABILITY_CLASSIFIER) {
        *classification = sensor_value.un.stabilityClassifier.classification;
    }

    return ESP_OK;
}


float q_to_roll_sf(float dqw, float dqx, float dqy, float dqz) {
    float norm = sqrt(dqw*dqw + dqx*dqx + dqy*dqy + dqz*dqz);
	dqw = dqw/norm;
	dqx = dqx/norm;
	dqy = dqy/norm;
	dqz = dqz/norm;

	float ysqr = dqy * dqy;

	// roll (x-axis rotation)
	float t0 = +2.0 * (dqw * dqx + dqy * dqz);
	float t1 = +1.0 - 2.0 * (dqx * dqx + ysqr);
	float roll = atan2(t0, t1);

	return (roll);
}


float q_to_pitch_sf(float dqw, float dqx, float dqy, float dqz) {
	float norm = sqrt(dqw*dqw + dqx*dqx + dqy*dqy + dqz*dqz);
	dqw = dqw/norm;
	dqx = dqx/norm;
	dqy = dqy/norm;
	dqz = dqz/norm;

	//float ysqr = dqy * dqy;

	// pitch (y-axis rotation)
	float t2 = +2.0 * (dqw * dqy - dqz * dqx);
	t2 = t2 > 1.0 ? 1.0 : t2;
	t2 = t2 < -1.0 ? -1.0 : t2;
	float pitch = asin(t2);

	return (pitch);
}
float q_to_yaw_sf(float dqw, float dqx, float dqy, float dqz) {
    float norm = sqrt(dqw*dqw + dqx*dqx + dqy*dqy + dqz*dqz);
	dqw = dqw/norm;
	dqx = dqx/norm;
	dqy = dqy/norm;
	dqz = dqz/norm;

	float ysqr = dqy * dqy;

	// yaw (z-axis rotation)
	float t3 = +2.0 * (dqw * dqz + dqx * dqy);
	float t4 = +1.0 - 2.0 * (ysqr + dqz * dqz);
	float yaw = atan2(t3, t4);

	return (yaw);
}
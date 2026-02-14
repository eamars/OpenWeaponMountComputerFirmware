#include <math.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_task.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "bno085.h"
#include "bno085_private.h"
#include "sh2_err.h"


#define TAG "BNO085"

// Forward declaration
float q_to_roll_sf(float dqw, float dqx, float dqy, float dqz);
float q_to_pitch_sf(float dqw, float dqx, float dqy, float dqz);
float q_to_yaw_sf(float dqw, float dqx, float dqy, float dqz);

static inline esp_err_t create_sensor_event_group(bno085_ctx_t *ctx) {
    // If not created, then create the event group. This function may be called before the `bno085_init()`. 
    if (ctx->sensor_event_control == NULL) {
        ctx->sensor_event_control = xEventGroupCreate();
        if (ctx->sensor_event_control == NULL) {
            ESP_LOGE(TAG, "Failed to create sensor_event_control");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}


esp_err_t _bno085_wait_for_interrupt(bno085_ctx_t *ctx) {
    if (xEventGroupWaitBits(ctx->sensor_event_control, SENSOR_INTERRUPT_EVENT_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(500)) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

uint32_t get_time_us(sh2_Hal_t *self) {
    uint32_t time_us = esp_timer_get_time() & 0xFFFFFFFFul;

    return time_us;
}


void _bno085_disable_interrupt(bno085_ctx_t *ctx) {
    ESP_ERROR_CHECK(create_sensor_event_group(ctx));

    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        // Clear event bit prior to the disable
        xEventGroupClearBits(ctx->sensor_event_control, SENSOR_INTERRUPT_EVENT_BIT);
        gpio_intr_disable(ctx->interrupt_pin);
    }
}

void _bno085_enable_interrupt(bno085_ctx_t *ctx) {
    ESP_ERROR_CHECK(create_sensor_event_group(ctx));

    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        // Clear event bit prior to the enable
        xEventGroupClearBits(ctx->sensor_event_control, SENSOR_INTERRUPT_EVENT_BIT);
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
    // ESP_LOGI(TAG, "Event Received %p", sensor_value.sensorId);

    xQueueOverwrite(target_report_config->sensor_value_queue, &sensor_value);
}


/**
 * @brief Interrupt handler for the BNO085 sensor
 */
void IRAM_ATTR bno085_interrupt_handler(void *arg) {
    bno085_ctx_t *ctx = (bno085_ctx_t *) arg;

    // Allow the consumer to unblock
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xEventGroupSetBitsFromISR(ctx->sensor_event_control, SENSOR_INTERRUPT_EVENT_BIT, &xHigherPriorityTaskWoken) != pdFAIL) {
        // Yield a context switch if needed
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void sensor_poller_task(void *self) {
    bno085_ctx_t *ctx = (bno085_ctx_t *) self;

    ESP_ERROR_CHECK(create_sensor_event_group(ctx));

    while (1) {
        // Wait until the interrupt happens
        if (_bno085_wait_for_interrupt(ctx) == ESP_OK) {
            sh2_service();
        }
    }
}


esp_err_t sh2_enable_report(sh2_SensorId_t sensor_id, sh2_SensorConfig_t *config) {
    int status = sh2_setSensorConfig(sensor_id, config);

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
                if (ctx->enabled_sensor_report_list[i].config.reportInterval_us != 0) {
                    ESP_LOGI(TAG, "Re-enabling report for sensor ID %d with interval %d us", i, ctx->enabled_sensor_report_list[i].config.reportInterval_us);
                    sh2_enable_report(i, &ctx->enabled_sensor_report_list[i].config);
                }
            }
            break;
        }
        case SH2_SHTP_EVENT: {
            // ESP_LOGI(TAG, "EventHandler id:SHTP, %d", pEvent->shtpEvent);
            break;
        }
        case SH2_GET_FEATURE_RESP: {
            ESP_LOGI(TAG, "EventHandler Sensor Config, %d", pEvent->sh2SensorConfigResp.sensorId);
            break;
        }
        default: {
            ESP_LOGW(TAG, "EventHandler, unknown event Id: %d", pEvent->eventId);
        }
    }

}


esp_err_t _bno085_ctx_init(bno085_ctx_t *ctx, gpio_num_t interrupt_pin, gpio_num_t reset_pin, gpio_num_t boot_pin, gpio_num_t ps0_wake_pin) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize the context structure
    memset(ctx, 0, sizeof(bno085_ctx_t));

    // Reset all pin assignments
    ctx->interrupt_pin = interrupt_pin;
    ctx->reset_pin = reset_pin;
    ctx->boot_pin = boot_pin;
    ctx->ps0_wake_pin = ps0_wake_pin;

    // Create sensor event group if not created before
    ESP_ERROR_CHECK(create_sensor_event_group(ctx));

    // Configure interrupt
    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << ctx->interrupt_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,  // enable internal pull up to avoid floating state during BNO085 chip reset
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE, // Trigger on falling edge (active low)
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        // Setup interrupt handler
        ESP_ERROR_CHECK(gpio_isr_handler_add(ctx->interrupt_pin, bno085_interrupt_handler, (void *) ctx));

        ESP_LOGI(TAG, "Configured interrupt on pin %d", ctx->interrupt_pin);
    }

    // Configure reset pin
    if (ctx->reset_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << ctx->reset_pin),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        // Set reset pin to high
        gpio_set_level(ctx->reset_pin, 1);

        ESP_LOGI(TAG, "Configured reset on pin %d", ctx->reset_pin);
    }

    // Configure boot pin
    if (ctx->boot_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << ctx->boot_pin),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        // Set boot pin to high (normal mode)
        gpio_set_level(ctx->boot_pin, 1);

        ESP_LOGI(TAG, "Configured boot on pin %d", ctx->boot_pin);
    }

    // Configure ps0_wake pin
    if (ctx->ps0_wake_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << ctx->ps0_wake_pin),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        ESP_LOGI(TAG, "Configured ps0_wake on pin %d", ctx->ps0_wake_pin);
    }

    return ESP_OK;
}


esp_err_t _bno085_sh2_init(bno085_ctx_t *ctx) {
    ctx->_HAL.getTimeUs = get_time_us;

    // Assume other HAL functions are already assigned
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


esp_err_t bno085_configure_report(bno085_ctx_t *ctx, sh2_SensorId_t sensor_id, sh2_SensorConfig_t *config) {
    // look for a slot to save the config
    sensor_report_config_t * target_report_config = &ctx->enabled_sensor_report_list[sensor_id];

    // Create queue if not created already
    if (target_report_config->sensor_value_queue == NULL) {
        target_report_config->sensor_value_queue = xQueueCreate(BNO085_EVENT_QUEUE_DEPTH, sizeof(sh2_SensorValue_t));
        if (target_report_config->sensor_value_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create queue for sensor report");
            return ESP_FAIL;
        }
    }

    // Copy the configuration to the target report config
    memcpy(&target_report_config->config, config, sizeof(sh2_SensorConfig_t));

    // Enable report at the sensor
    return sh2_enable_report(sensor_id, &target_report_config->config);
}


esp_err_t bno085_enable_game_rotation_vector_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    sh2_SensorConfig_t config = {
        .changeSensitivityEnabled = false,
        .wakeupEnabled = false,
        .changeSensitivityRelative = false,
        .alwaysOnEnabled = false,
        .changeSensitivity = 0,
        .batchInterval_us = 0,
        .sensorSpecific = 0,
    };

    config.reportInterval_us = interval_ms * 1000;

    return bno085_configure_report(ctx, SH2_GAME_ROTATION_VECTOR, &config);
}


esp_err_t bno085_enable_linear_acceleration_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    sh2_SensorConfig_t config = {
        .changeSensitivityEnabled = false,
        .wakeupEnabled = false,
        .changeSensitivityRelative = false,
        .alwaysOnEnabled = false,
        .changeSensitivity = 0,
        .batchInterval_us = 0,
        .sensorSpecific = 0,
    };

    config.reportInterval_us = interval_ms * 1000;

    return bno085_configure_report(ctx, SH2_LINEAR_ACCELERATION, &config);
}

esp_err_t bno085_enable_stability_classification_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    sh2_SensorConfig_t config = {
        .changeSensitivityEnabled = false,
        .wakeupEnabled = false,
        .changeSensitivityRelative = false,
        .alwaysOnEnabled = false,
        .changeSensitivity = 0,
        .batchInterval_us = 0,
        .sensorSpecific = 0,
    };

    config.reportInterval_us = interval_ms * 1000;

    return bno085_configure_report(ctx, SH2_STABILITY_CLASSIFIER, &config);
}

esp_err_t bno085_enable_rotation_vector_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    sh2_SensorConfig_t config = {
        .changeSensitivityEnabled = false,
        .wakeupEnabled = false,
        .changeSensitivityRelative = false,
        .alwaysOnEnabled = false,
        .changeSensitivity = 0,
        .batchInterval_us = 0,
        .sensorSpecific = 0,
    };

    config.reportInterval_us = interval_ms * 1000;

    return bno085_configure_report(ctx, SH2_ROTATION_VECTOR, &config);
}


esp_err_t bno085_enable_stability_detector_report(bno085_ctx_t *ctx, uint32_t interval_ms) {
    sh2_SensorConfig_t config = {   
        .changeSensitivityEnabled = true,
        .wakeupEnabled = false,
        .changeSensitivityRelative = false,
        .alwaysOnEnabled = false,
        .changeSensitivity = 0,
        .batchInterval_us = 0,
        .sensorSpecific = 0,
    };

    config.reportInterval_us = interval_ms * 1000;
    return bno085_configure_report(ctx, SH2_STABILITY_DETECTOR, &config);
}


esp_err_t bno085_wait_for_game_rotation_vector_roll_pitch_yaw(bno085_ctx_t *ctx, float *roll, float *pitch, float *yaw, bool block_wait) {
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
        if (roll) {
            *roll = q_to_roll_sf(
                sensor_value.un.gameRotationVector.real,
                sensor_value.un.gameRotationVector.i,
                sensor_value.un.gameRotationVector.j,
                sensor_value.un.gameRotationVector.k
            );
        }

        if (pitch) {
            *pitch = q_to_pitch_sf(
                sensor_value.un.gameRotationVector.real,
                sensor_value.un.gameRotationVector.i,
                sensor_value.un.gameRotationVector.j,
                sensor_value.un.gameRotationVector.k
            );
        }

        if (yaw) {
            *yaw = q_to_yaw_sf(
                sensor_value.un.gameRotationVector.real,
                sensor_value.un.gameRotationVector.i,
                sensor_value.un.gameRotationVector.j,
                sensor_value.un.gameRotationVector.k
            );
        }

        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t bno085_wait_for_rotation_vector_roll_pitch_yaw(bno085_ctx_t * ctx, float * roll, float * pitch, float * yaw, float * accuracy, bool block_wait) {
    TickType_t wait_ticks;
    if (block_wait) {
        wait_ticks = portMAX_DELAY;
    }
    else {
        wait_ticks = 0;
    }

    sh2_SensorValue_t sensor_value;

    // Wait for the queue from the corresponding report
    if (xQueueReceive(ctx->enabled_sensor_report_list[SH2_ROTATION_VECTOR].sensor_value_queue, &sensor_value, wait_ticks) != pdPASS) {
        // ESP_LOGE(TAG, "Failed to receive game rotation vector report");
        return ESP_FAIL;
    }

    // Decode sensor envent
    if (sensor_value.sensorId == SH2_ROTATION_VECTOR) {
        if (roll) {
            *roll = q_to_roll_sf(
                sensor_value.un.rotationVector.real, 
                sensor_value.un.rotationVector.i, 
                sensor_value.un.rotationVector.j, 
                sensor_value.un.rotationVector.k
            );
        }

        if (pitch) {
            *pitch = q_to_pitch_sf(
                sensor_value.un.rotationVector.real, 
                sensor_value.un.rotationVector.i, 
                sensor_value.un.rotationVector.j, 
                sensor_value.un.rotationVector.k
            );
        }

        if (yaw) {
            *yaw = q_to_yaw_sf(
                sensor_value.un.rotationVector.real, 
                sensor_value.un.rotationVector.i, 
                sensor_value.un.rotationVector.j, 
                sensor_value.un.rotationVector.k
            );
        }

        if (accuracy) {
            *accuracy = sensor_value.un.rotationVector.accuracy;
        }

        return ESP_OK;
    }

    return ESP_FAIL;
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

        return ESP_OK;
    }

    return ESP_FAIL;
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
        return ESP_OK;
    }

    return ESP_FAIL;
}


esp_err_t bno085_wait_for_stability_detector_report(bno085_ctx_t *ctx, uint16_t *stability, bool block_wait) {
    TickType_t wait_ticks;
    if (block_wait) {
        wait_ticks = portMAX_DELAY;
    }
    else {
        wait_ticks = 0;
    }

    sh2_SensorValue_t sensor_value;

    // Wait for the queue from the corresponding report
    if (xQueueReceive(ctx->enabled_sensor_report_list[SH2_STABILITY_DETECTOR].sensor_value_queue, &sensor_value, wait_ticks) != pdPASS) {
        // ESP_LOGE(TAG, "Failed to receive stability detector report");
        return ESP_FAIL;
    }

    // Decode sensor event
    if (sensor_value.sensorId == SH2_STABILITY_DETECTOR) {
        *stability = sensor_value.un.stabilityDetector.stability;
        return ESP_OK;
    }

    return ESP_FAIL;
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
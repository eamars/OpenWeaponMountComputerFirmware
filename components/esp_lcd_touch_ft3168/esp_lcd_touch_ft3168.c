#include "esp_lcd_touch_ft3168.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_check.h"

#define TAG "FT3168"

// The touch controller shares I2C0 with the PMIC, and the driver also waits for the bus lock with this timeout.
// Touch reads run in the LVGL task, so a finite wait turns a stuck bus into a missed touch, not a hung display
// (ADR-005 C3). A transfer takes well under 1 ms at 300 kHz
#define FT3168_I2C_TIMEOUT_MS 100


esp_err_t esp_lcd_touch_ft3168_read_data(esp_lcd_touch_handle_t ctx) {
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read touch data from the FT3168
    uint8_t write_cmd;
    uint8_t touch_points = 0;

    // Read the number of touch points (max 2)
    write_cmd = 0x02;

    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(
        (i2c_master_dev_handle_t) ctx->config.driver_data,
        &write_cmd, 1,
        &touch_points, 1,
        FT3168_I2C_TIMEOUT_MS), TAG, "Failed to read touch points");

    if (touch_points == 0) {
        return ESP_OK;
    }

    touch_points = (touch_points > 2 ? 2 : touch_points);  // make sure we are not reading more than needed

    // Read every point first, then publish them together: a failed read leaves no point count with stale
    // coordinates behind
    uint16_t xs[2];
    uint16_t ys[2];
    uint8_t read_buf[4];

    for (int i = 0; i < touch_points; i++) {
        write_cmd = 0x03 + i * 6;

        ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(
            (i2c_master_dev_handle_t) ctx->config.driver_data,
            &write_cmd, 1,
            read_buf, 4,
            FT3168_I2C_TIMEOUT_MS), TAG, "Failed to read touch coordinates %d", i);

        ys[i] = (((uint16_t)read_buf[0] & 0x0f)<<8) | (uint16_t)read_buf[1];
        xs[i] = (((uint16_t)read_buf[2] & 0x0f)<<8) | (uint16_t)read_buf[3];
    }

    portENTER_CRITICAL(&ctx->data.lock);
    for (int i = 0; i < touch_points; i++) {
        ctx->data.coords[i].x = xs[i];
        ctx->data.coords[i].y = ys[i];
    }
    ctx->data.points = touch_points;
    portEXIT_CRITICAL(&ctx->data.lock);

    return ESP_OK;
}


bool esp_lcd_touch_ft3168_get_xy(esp_lcd_touch_handle_t ctx, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num) {
    assert(ctx != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&ctx->data.lock);

    /* Count of points */
    *point_num = (ctx->data.points > max_point_num ? max_point_num : ctx->data.points);

    for (size_t i = 0; i < *point_num; i++)
    {
        x[i] = ctx->data.coords[i].x;
        y[i] = ctx->data.coords[i].y;

        if (strength)
        {
            strength[i] = ctx->data.coords[i].strength;
        }
    }

    /* Invalidate */
    ctx->data.points = 0;

    portEXIT_CRITICAL(&ctx->data.lock);

    return (*point_num > 0);
}


esp_err_t esp_lcd_touch_ft3168_del(esp_lcd_touch_handle_t ctx) {
    assert(ctx != NULL);

    // Reset GPIO pin settings
    if (ctx->config.int_gpio_num != GPIO_NUM_NC)
    {
        gpio_reset_pin(ctx->config.int_gpio_num);
        if (ctx->config.interrupt_callback)
        {
            esp_lcd_touch_register_interrupt_callback(ctx, NULL);
        }
    }
    if (ctx->config.rst_gpio_num != GPIO_NUM_NC)
    {
        gpio_reset_pin(ctx->config.rst_gpio_num);
    }

    // Free resources
    free(ctx);

    return ESP_OK;
}

esp_err_t esp_lcd_touch_new_ft3168(esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch) {

    esp_lcd_touch_handle_t touch_handle = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);

    if (!touch_handle) {
        return ESP_ERR_NO_MEM;
    }

    // Save config first: the reset below reads the pins from it (read from the zeroed handle, the reset
    // pulsed GPIO0, the external button, and never reset the controller; ADR-005 B16)
    memcpy(&touch_handle->config, config, sizeof(esp_lcd_touch_config_t));

    // Prepare pin for touch controller reset
    if (touch_handle->config.rst_gpio_num != GPIO_NUM_NC)
    {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(touch_handle->config.rst_gpio_num)
        };
        esp_err_t ret = gpio_config(&rst_gpio_config);
        if (ret != ESP_OK) {
            free(touch_handle);
            ESP_RETURN_ON_ERROR(ret, TAG, "Reset GPIO config failed");
        }

        // Perform a reset
        gpio_set_level(touch_handle->config.rst_gpio_num, touch_handle->config.levels.reset);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(touch_handle->config.rst_gpio_num, !touch_handle->config.levels.reset);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Initialize touch screen
    int retry = 5;
    uint8_t write_buf[2] = {0x0, 0x0};  // write 0x0 to addr 0x0 to switch to working mode

    while (retry > 0) {
        esp_err_t ret = i2c_master_transmit((i2c_master_dev_handle_t) config->driver_data, write_buf, 2,
                                            FT3168_I2C_TIMEOUT_MS);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to write to FT3168: %s, %d", esp_err_to_name(ret), retry);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        else {
            ESP_LOGI(TAG, "Successfully wrote to FT3168");
            break;
        }

        retry -= 1;
    }

    if (retry == 0) {
        ESP_LOGE(TAG, "Failed to initialize FT3168 after multiple attempts");
        free(touch_handle);
        ESP_RETURN_ON_ERROR(ESP_FAIL, TAG, "Unable to establish communication with FT3168");
    }

    ESP_LOGI(TAG, "FT3168 initialized successfully");

    // Assign callbacks
    touch_handle->read_data = esp_lcd_touch_ft3168_read_data;
    touch_handle->get_xy = esp_lcd_touch_ft3168_get_xy;
    touch_handle->del = esp_lcd_touch_ft3168_del;
    touch_handle->set_swap_xy = NULL;
    touch_handle->get_swap_xy = NULL;

    /* Mutex */
    touch_handle->data.lock.owner = portMUX_FREE_VAL;

    // Prepare pin for touch interrupt
    if (touch_handle->config.int_gpio_num != GPIO_NUM_NC)
    {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (touch_handle->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(touch_handle->config.int_gpio_num)
        };
        
        esp_err_t ret = gpio_config(&int_gpio_config);
        if (ret != ESP_OK) {
            free(touch_handle);
            ESP_RETURN_ON_ERROR(ret, TAG, "Interrupt GPIO config failed");
        }

        /* Register interrupt callback */
        if (touch_handle->config.interrupt_callback)
        {
            esp_lcd_touch_register_interrupt_callback(touch_handle, touch_handle->config.interrupt_callback);
        }
    }

    *out_touch = touch_handle;

    return ESP_OK;
}
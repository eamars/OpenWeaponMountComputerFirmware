#include "esp_lcd_touch_ft3168.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_check.h"

#define TAG "FT3168"


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
        -1), TAG, "Failed to read touch points");

    if (touch_points == 0) {
        return ESP_OK;
    }

    touch_points = (touch_points > 2 ? 2 : touch_points);  // make sure we are not reading more than needed

    // Enter critical section to protect data
    portENTER_CRITICAL(&ctx->data.lock);
    ctx->data.points = touch_points;
    portEXIT_CRITICAL(&ctx->data.lock);

    uint8_t read_buf[4];

    // Find all coordinates
    for (int i = 0; i < touch_points; i++) {
        // Read all coordinates
        write_cmd = 0x03 + i * 6;
        
        ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(
            (i2c_master_dev_handle_t) ctx->config.driver_data, 
            &write_cmd, 1,
            read_buf, 4,
            -1), TAG, "Failed to read touch coordinates %d", i);

        // Write data to the handle
        portENTER_CRITICAL(&ctx->data.lock);
        ctx->data.coords[i].y = (((uint16_t)read_buf[0] & 0x0f)<<8) | (uint16_t)read_buf[1];
        ctx->data.coords[i].x = (((uint16_t)read_buf[2] & 0x0f)<<8) | (uint16_t)read_buf[3];
        portEXIT_CRITICAL(&ctx->data.lock);

        ESP_LOGI(TAG, "Touch %d: x=%d, y=%d", i, ctx->data.coords[i].x, ctx->data.coords[i].y);
    }
    
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

    // Free resources
    free(ctx);

    return ESP_OK;
}

esp_err_t esp_lcd_touch_new_ft3168(esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch) {

    esp_lcd_touch_handle_t touch_handle = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);

    if (!touch_handle) {
        return ESP_ERR_NO_MEM;
    }

    // Initialize touch screen
    uint8_t write_buf[2] = {0x0, 0x0};  // write 0x0 to addr 0x0 to switch to working mode
    ESP_RETURN_ON_ERROR(i2c_master_transmit(config->driver_data, write_buf, 2, -1), TAG, "Failed to initialize touch screen");

    ESP_LOGI(TAG, "FT3168 initialized successfully");

    // Assign callbacks
    touch_handle->read_data = esp_lcd_touch_ft3168_read_data;
    touch_handle->get_xy = esp_lcd_touch_ft3168_get_xy;
    touch_handle->del = esp_lcd_touch_ft3168_del;
    touch_handle->set_swap_xy = NULL;
    touch_handle->get_swap_xy = NULL;

    /* Mutex */
    touch_handle->data.lock.owner = portMUX_FREE_VAL;

    /* Save config */
    memcpy(&touch_handle->config, config, sizeof(esp_lcd_touch_config_t));

    *out_touch = touch_handle;

    return ESP_OK;
}
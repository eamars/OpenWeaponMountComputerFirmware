#include "ota_mode.h"
#include "low_power_mode.h"
#include "wifi.h"
#include "main_tileview.h"
#include "app_cfg.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "json_parser.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#define TAG "OTAMode"

static lv_obj_t * progress_label;
static lv_obj_t * progress_bar;

extern lv_obj_t * tile_ota_mode_view;
extern lv_obj_t * main_tileview;

static TaskHandle_t ota_poller_task_handle;
static lv_obj_t * last_tile = NULL;  // last tile before entering the OTA mode


const char * ota_sources [] = {
    "owmc_update.local",
    "http://owmc_update.vfnz.pro",
    NULL,
};

const char * manifest_endpoint = "/manifest.json";




esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void ota_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA started");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_REVISION:
                ESP_LOGI(TAG, "Verifying chip revision of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGD(TAG, "Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA finish");
                break;
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGI(TAG, "OTA abort");
                break;
        }
    }
}


esp_err_t apply_ota_from_source(const char * ota_source) {
    esp_err_t ret;
    char * manifest_json_raw = NULL;

    ESP_LOGI(TAG, "Fetching manifest from %s", ota_source);

    esp_http_client_config_t http_config = {
        .host = ota_source, 
        .path = manifest_endpoint,
        .port = 8080,
        .event_handler = http_client_event_handler,
    };
    esp_http_client_handle_t client_handle = esp_http_client_init(&http_config);

    // GET
    esp_http_client_set_method(client_handle, HTTP_METHOD_GET);
    ret = esp_http_client_open(client_handle, 0);
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to open HTTP connection");

    int manifest_length = (int) esp_http_client_fetch_headers(client_handle);
    if (manifest_length < 0) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to read manifest length: %d", manifest_length);
    }
    else {
        // success
        ESP_LOGI(TAG, "Manifest read successful. Content length: %d", manifest_length);
    }

    // Allocate memory to read 
    manifest_json_raw = heap_caps_calloc(1, manifest_length + 1, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (!manifest_json_raw) {
        ret = ESP_ERR_NO_MEM;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to allocate memory for manifest_json_raw");
    }
    int byte_read = esp_http_client_read_response(client_handle, manifest_json_raw, manifest_length);
    ESP_LOGI(TAG, "Read %d bytes: %s", byte_read, manifest_json_raw);

    // Decode by json parser
    /* Example JSON file: 
        {
            "version": "1.2.3",
            "path": "/1.2.3/firmware.bin"
        }
    */
    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, manifest_json_raw, byte_read) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to decode json string: %s", manifest_json_raw);
    }

    char version_str_buffer[32];
    if (json_obj_get_string(&jctx, "version", version_str_buffer, sizeof(version_str_buffer)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract version string: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "Remote firmware version: %s", version_str_buffer);
    }

    char firmware_path_buffer[64];
    if (json_obj_get_string(&jctx, "path", firmware_path_buffer, sizeof(firmware_path_buffer)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract firwmare path string: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "Remote firmware path: %s", firmware_path_buffer);
    }

    // Log the OTA information. The rest will be handled in the OTA config view

    // // Now we have the path to the firmware, then we can feed the path to the OTA updater
    // esp_http_client_cleanup(client_handle);

    // // Restart the client handle to point to the new location
    // http_config.path = firmware_path_buffer;
    // client_handle = esp_http_client_init(&http_config);

    // // GET
    // esp_http_client_set_method(client_handle, HTTP_METHOD_GET);
    // ret = esp_http_client_open(client_handle, 0);
    // ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to open HTTP connection");

    // // TODO: Get OTA to start

    // // FIXME: automatically enter OTA Mode
    // if (lvgl_port_lock(0)) {
    //     // Record the last tile
    //     last_tile = lv_tileview_get_tile_active(main_tileview);

    //     // Shift to low power tileview
    //     lv_tileview_set_tile(main_tileview, tile_ota_mode_view, LV_ANIM_OFF);
    //     lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
    //     lvgl_port_unlock();
    // }

    ret = ESP_OK;

finally:
    esp_http_client_cleanup(client_handle);

    if (manifest_json_raw) {
        heap_caps_free(manifest_json_raw);
    }

    return ret;
}

void ota_poller_task(void *p) {
    // Wait for wifi to be connected
    while (true) {
        esp_err_t ret = wifi_wait_for_sta_connected(1000);
        if (ret == ESP_OK) {
            break;
        }
    }

    ESP_LOGI(TAG, "Network connected, will poll OTA source");

    for (int idx = 0; ota_sources[idx] != NULL; idx += 1) {
        esp_err_t ret = apply_ota_from_source(ota_sources[idx]);
        if (ret == ESP_OK) {
            break;
        }
    }

    vTaskDelete(NULL);   // safely remove this task
 }


void create_ota_mode_view(lv_obj_t * parent) {
    // Draw a container to allow vertical stacking
    lv_obj_t * container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, 
        LV_FLEX_ALIGN_CENTER, 
        LV_FLEX_ALIGN_CENTER, 
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);


    // Put Title Label
    lv_obj_t * title_label = lv_label_create(container);
    lv_label_set_text(title_label, "OTA Update");

    // Put progress label
    progress_label = lv_label_create(container);
    lv_label_set_text(progress_label, "Progress: 0%");

    // Put progress bar
    progress_bar = lv_bar_create(container);
    lv_obj_set_size(progress_bar, lv_pct(80), 20);

    // Create the task to poll for OTA update
    BaseType_t rtos_return;
    rtos_return = xTaskCreate(
        ota_poller_task, 
        "ota_poller",
        OTA_POLLER_TASK_STACK,
        NULL, 
        OTA_POLLER_TASK_PRIORITY,
        &ota_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for ota_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}

void update_ota_mode_progress(int progress) {
    if (progress_label && progress_bar) {
        lv_label_set_text_fmt(progress_label, "Progress: %d", progress);
        lv_bar_set_value(progress_bar, progress, LV_ANIM_OFF);
    }
}

void enter_ota_mode(bool enable) {
    ESP_LOGI(TAG, "OTA Mode %s", enable ? "enabled" : "disabled");

    if (enable) {
        prevent_low_power_mode_enter(true);
    }
    else {
        prevent_low_power_mode_enter(false);
    }
}
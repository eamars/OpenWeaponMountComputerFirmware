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
#include "esp_task_wdt.h"
#include "json_parser.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


#define TAG "OTAMode"

typedef enum {
    OTA_EVENT_MANIFEST_READY = (1 << 0),
    OTA_EVENT_OTA_COMPLETE = (1 << 1),
    OTA_EVENT_USER_CONFIRMED = (1 << 2),
    OTA_EVENT_IS_POWERED_BY_USB = (1 << 3),
} ota_event_e;
static EventGroupHandle_t ota_event_group = NULL;

static lv_obj_t * progress_label;
static lv_obj_t * progress_bar;

extern lv_obj_t * tile_ota_mode_view;
extern lv_obj_t * main_tileview;
extern lv_obj_t * default_tile;

static TaskHandle_t ota_poller_task_handle;
static TaskHandle_t ota_update_task_handle;

static lv_obj_t * last_tile = NULL;  // last tile before entering the OTA mode
static lv_obj_t * ota_description_label;
static lv_obj_t * ota_prompt_view;


HEAPS_CAPS_ATTR ota_manifest_t ota_manifest;


const char * ota_sources [] = {
    "owmc_update.local",
    "http://owmc_update.vfnz.pro",
    NULL,
};

// End point for PCB revision 1
const char * manifest_endpoint = "/p1/manifest.json";


static inline esp_err_t create_ota_event_group() {
    // If not created, then create the event group. This function may be called before the `wifi_init()`. 
    if (ota_event_group == NULL) {
        ota_event_group = xEventGroupCreate();
        if (ota_event_group == NULL) {
            ESP_LOGE(TAG, "Failed to create ota_event_group");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

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


 void set_ota_prompt_view_visibility(bool is_visible) {
    if (is_visible) {
        // Shift to OTA view
        lv_obj_clear_flag(ota_prompt_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ota_prompt_view, LV_OBJ_FLAG_HIDDEN);
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
        .timeout_ms = 20 * 1000,  // longer timeout for slow response servers
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

    // Read manifest_version
    if (json_obj_get_int(&jctx, "manifest_version", &ota_manifest.manifest_version)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract manifest_version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "manifest_version: %d", ota_manifest.manifest_version);
    }

    // Read fw_version
    if (json_obj_get_string(&jctx, "fw_version", ota_manifest.fw_version, sizeof(ota_manifest.fw_version)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract fw_version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "fw_version: %s", ota_manifest.fw_version);
    }

    // Read fw_build_hash
    if (json_obj_get_string(&jctx, "fw_build_hash", ota_manifest.fw_build_hash, sizeof(ota_manifest.fw_build_hash)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract fw_build_hash: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "fw_build_hash: %s", ota_manifest.fw_build_hash);
    }

    // Read fw_path
    if (json_obj_get_string(&jctx, "fw_path", ota_manifest.fw_path, sizeof(ota_manifest.fw_path)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract fw_path: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "fw_path: %s", ota_manifest.fw_path);
    }

    // Read fw_note
    if (json_obj_get_string(&jctx, "fw_note", ota_manifest.fw_note, sizeof(ota_manifest.fw_note)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract fw_note: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "fw_note: %s", ota_manifest.fw_note);
    }

    // Read port number
    if (json_obj_get_int(&jctx, "port", &ota_manifest.port)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract port: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "port: %d", ota_manifest.port);
    }

    // Read ignore version flag
    if (json_obj_get_bool(&jctx, "ignore_version", &ota_manifest.ignore_version)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract ignore_version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "ignore_version: %d", ota_manifest.ignore_version);
    }

    // Read importance
    if (json_obj_get_int(&jctx, "importance", (int *) &ota_manifest.importance)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract importance: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "importance: %d", ota_manifest.importance);
    }


    ota_manifest.initialized = true;

    // Update the description field
    lv_label_set_text_fmt(ota_description_label, 
        "New Firmware #ff0000 %s # Available\n"
        "It is recommended to update immediately\n"
        "Release Note:\n"
        "------\n"
        "%s",
        ota_manifest.fw_version, 
        ota_manifest.fw_note
    );


    if (ota_manifest.importance > OTA_IMPORTANCE_NORMAL) {
        if (lvgl_port_lock(0)) {
            set_ota_prompt_view_visibility(true);
            lvgl_port_unlock();
        }
    }

    // We're done here
    // The rest will be handled within the OTA mode

    ret = ESP_OK;

finally:
    esp_http_client_cleanup(client_handle);

    if (manifest_json_raw) {
        heap_caps_free(manifest_json_raw);
    }

    return ret;
}

void ota_poller_task(void *p) {
    // Create OTA event group
    ESP_ERROR_CHECK(create_ota_event_group());

    // Uninitialize the OTA manifest
    memset(&ota_manifest, 0x0, sizeof(ota_manifest));
    ota_manifest.initialized = false;

    // Wait for wifi to be connected
    while (true) {
        esp_err_t ret = wifi_wait_for_sta_connected(1000);
        if (ret == ESP_OK) {
            // Manifest download ready
            xEventGroupSetBits(ota_event_group, OTA_EVENT_MANIFEST_READY);
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

    ESP_LOGI(TAG, "Manifest downloaded, waiting for OTA update");

    vTaskDelete(NULL);   // safely remove this task
 }


 void ota_update_task(void *p) {
    // Create OTA event group
    ESP_ERROR_CHECK(create_ota_event_group());

    // Disable task watchdog to ensure the OTA is smooth
    // Also in case the network is down and network stack is deleted. 
    esp_task_wdt_delete(NULL);

    // Wait for manifest to be downloaded
    xEventGroupWaitBits(ota_event_group, OTA_EVENT_MANIFEST_READY | OTA_EVENT_USER_CONFIRMED, pdFALSE, pdTRUE, portMAX_DELAY);

    // Shift to OTA view
    last_tile = lv_tileview_get_tile_active(main_tileview);
    lv_tileview_set_tile(main_tileview, tile_ota_mode_view, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);


    // TODO: DO the update here
    vTaskDelete(NULL);   // safely remove this task

 }

 static void on_ota_cancel_button_pressed(lv_event_t *e) {
    ESP_LOGI(TAG, "Cancel OTA");
    set_ota_prompt_view_visibility(false);
 }

 static void on_ota_accept_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Start Upgrade pressed");
    set_ota_prompt_view_visibility(false);

    xEventGroupSetBits(ota_event_group, OTA_EVENT_USER_CONFIRMED);
 }


void create_ota_mode_view(lv_obj_t * parent) {
    create_ota_prompt_view(lv_screen_active());

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

    // Create the task to perform OTA update
    rtos_return = xTaskCreate(
        ota_update_task, 
        "ota_updater",
        OTA_UPDATE_TASK_STACK,
        NULL, 
        OTA_UPDATE_TASK_PRIORITY,
        &ota_update_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for ota_update_task");
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


void create_ota_prompt_view(lv_obj_t * parent) {
    ota_prompt_view = lv_obj_create(parent);
    lv_obj_set_size(ota_prompt_view, lv_pct(100), lv_pct(100));
    // set hide by default
    set_ota_prompt_view_visibility(false);
    
    lv_obj_set_scroll_dir(ota_prompt_view, LV_DIR_VER);  // only vertical scroll
    lv_obj_set_style_pad_all(ota_prompt_view, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(ota_prompt_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ota_prompt_view,
                    LV_FLEX_ALIGN_START,  // main axis (row) center
                    LV_FLEX_ALIGN_CENTER,  // cross axis center
                    LV_FLEX_ALIGN_CENTER); // track cross axis center

    // Add cancel button at top
    lv_obj_t * ota_cancel_button = lv_button_create(ota_prompt_view);
    lv_obj_add_event_cb(ota_cancel_button, on_ota_cancel_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * ota_cancel_button_label = lv_label_create(ota_cancel_button);
    lv_label_set_text(ota_cancel_button_label, "Cancel");
    lv_obj_center(ota_cancel_button_label);
    lv_obj_set_style_text_font(ota_cancel_button_label, &lv_font_montserrat_20, LV_PART_MAIN);

    // Add update description
    ota_description_label = lv_label_create(ota_prompt_view);
    lv_obj_set_width(ota_description_label, lv_pct(100));
    lv_label_set_recolor(ota_description_label, true);  // allow inline colour annotation
    lv_label_set_long_mode(ota_description_label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(ota_description_label, "OTA Not Ready");

    // Add accept button at bottom
    lv_obj_t * ota_accept_button = lv_button_create(ota_prompt_view);
    lv_obj_set_style_bg_color(ota_accept_button, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ota_accept_button, on_ota_accept_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * ota_accept_button_label = lv_label_create(ota_accept_button);
    lv_label_set_text(ota_accept_button_label, "Start Upgrade");
    lv_obj_center(ota_accept_button_label);
    lv_obj_set_style_text_font(ota_accept_button_label, &lv_font_montserrat_20, LV_PART_MAIN);
}

#include "ota_mode.h"
#include "low_power_mode.h"
#include "wifi.h"
#include "main_tileview.h"
#include "app_cfg.h"
#include "common.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "json_parser.h"
#include "esp_system.h"
#include "esp_partition.h"

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
static lv_obj_t * ota_title_label;
static lv_obj_t * reboot_button;
static lv_obj_t * menu_ota_upgrade_button;

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

static esp_err_t http_client_init_cb(esp_http_client_handle_t http_client) {
    return ESP_OK;
}


 void set_ota_prompt_view_visibility(bool is_visible) {
    if (is_visible) {
        // Shift to OTA view
        lv_obj_remove_flag(ota_prompt_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ota_prompt_view, LV_OBJ_FLAG_HIDDEN);
    }
 }

esp_err_t fetch_manifest_from_source(const char * ota_source) {
    esp_err_t ret;
    char * manifest_json_raw = NULL;

    ESP_LOGI(TAG, "Fetching manifest from %s", ota_source);

    esp_http_client_config_t http_config = {
        .host = ota_source, 
        .path = manifest_endpoint,
        .port = OTA_MANIFEST_PORT,
        .event_handler = http_client_event_handler,
        .timeout_ms = OTA_HTTP_TIMEOUT,  // longer timeout for slow response servers
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
    if (json_obj_get_string(&jctx, "version", ota_manifest.version, sizeof(ota_manifest.version)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract version: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "version: %s", ota_manifest.version);
    }

    // Read fw_path
    if (json_obj_get_string(&jctx, "path", ota_manifest.path, sizeof(ota_manifest.path)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract path: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "path: %s", ota_manifest.path);
    }

    // Read fw_note
    if (json_obj_get_string(&jctx, "note", ota_manifest.note, sizeof(ota_manifest.note)) != OS_SUCCESS) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract fw_note: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "note: %s", ota_manifest.note);
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

    // Read type
    if (json_obj_get_int(&jctx, "type", (int *) &ota_manifest.type)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract type: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "type: %d", ota_manifest.type);
    }

    // Read importance
    if (json_obj_get_int(&jctx, "importance", (int *) &ota_manifest.importance)) {
        ret = ESP_FAIL;
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract importance: %s", manifest_json_raw);
    }
    else {
        ESP_LOGI(TAG, "importance: %d", ota_manifest.importance);
    }

    // Copy other attributes
    ota_manifest.host = (char *) ota_source;
    ota_manifest.initialized = true;

    // if ignore_version is false then we can compare the version, populate update only if the OTA server has newer version
    if (!ota_manifest.ignore_version) {
        // Check the current version
        const esp_app_desc_t * app_desc = esp_app_get_description();
        ESP_LOGI(TAG, "Running version: %s", app_desc->version);

        version_t self_version;
        parse_git_describe_version(app_desc->version, &self_version);

        // Read the target version
        version_t other_version;
        parse_git_describe_version(ota_manifest.version, &other_version);

        // Compare
        if (compare_version(&self_version, &other_version) >= 0) {
            ESP_LOGI(TAG, "Current running version %s is newer than the target version %s, will skip OTA", app_desc->version, ota_manifest.version);
            // don't update
            ret = ESP_OK;
            goto finally;
        }
    }

    // Update the description field
    lv_label_set_text_fmt(ota_description_label, 
        "Firmware #ff0000 %s # Available.\n"
        "Release Note:\n"
        "------\n"
        "%s",
        ota_manifest.version, 
        ota_manifest.note
    );

    
    // Make OTA upgrade button visible
    lv_obj_remove_flag(menu_ota_upgrade_button, LV_OBJ_FLAG_HIDDEN);

    // If important, then also prompt directly to the user
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
        esp_err_t ret = fetch_manifest_from_source(ota_sources[idx]);
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


    // -------------------------------------
    esp_err_t ret;

    // Register event
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, ota_event_handler, NULL));

    esp_http_client_config_t http_config = {
        .host = ota_manifest.host,
        .path = ota_manifest.path,
        .port = ota_manifest.port,
        .event_handler = http_client_event_handler,
        .keep_alive_enable = true,
        .timeout_ms = OTA_HTTP_TIMEOUT,
    };

    // Determine the target partition
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "OTA will be written to partition %s", ota_partition->label);

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = http_client_init_cb,
        .partition = {
            .staging = ota_partition,
            .final = NULL,  // use staging partition
            .finalize_with_copy = false,
        },
    };

    if (lvgl_port_lock(0)) {
        lv_label_set_text(ota_title_label, "Locating Firmware");
        lvgl_port_unlock();
    }
    esp_https_ota_handle_t https_ota_handle = NULL;
    ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        if (lvgl_port_lock(0)) {
            lv_label_set_text(ota_title_label, "OTA Failed - Unable to download firmware");
            lvgl_port_unlock();
        }

        ESP_GOTO_ON_ERROR(ret, finally, TAG, "esp_https_ota_begin failed");
    }
    else {
        if (lvgl_port_lock(0)) {
            lv_label_set_text(ota_title_label, "Locating Firmware -- done");
            lvgl_port_unlock();
        }
    }

    // Read application size
    int ota_size = esp_https_ota_get_image_size(https_ota_handle);
    ESP_LOGI(TAG, "OTA Size: %d", ota_size);

    // Download the new application header
    if (lvgl_port_lock(0)) {
        lv_label_set_text(ota_title_label, "Downloading Header");
        lvgl_port_unlock();
    }
    esp_app_desc_t app_desc;
    memset(&app_desc, 0x0, sizeof(app_desc));
    ret = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (ret != ESP_OK) {
        if (lvgl_port_lock(0)) {
            lv_label_set_text(ota_title_label, "OTA Failed - Unable to download application header");
            lvgl_port_unlock();
        }
        ESP_GOTO_ON_ERROR(ret, finally, TAG, "esp_https_ota_get_img_desc failed");
    }
    else {
        if (lvgl_port_lock(0)) {
            lv_label_set_text(ota_title_label, "Downloading Header -- done");
            lvgl_port_unlock();
        }
    }

    // We don't need to verify the target app header, 
    ESP_LOGI(TAG, "Declared image version: %s, actual image version: %s", ota_manifest.version, app_desc.version);

    if (lvgl_port_lock(0)) {
        lv_label_set_text(ota_title_label, "Applying Update");
        lv_label_set_text_fmt(progress_label, "Progress: 0/%d", ota_size);
        lvgl_port_unlock();
    }

    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break; 
        }

        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        int ota_read_size = esp_https_ota_get_image_len_read(https_ota_handle);
        // ESP_LOGI(TAG, "OTA Read size: %d", ota_read_size);

        int percentage = ota_read_size * 100.0 / ota_size;
        if (lvgl_port_lock(0)) {
            // Update progress text and bar
            lv_label_set_text_fmt(progress_label, "Progress: %dK/%dK", ota_read_size/1024, ota_size/1024);
            lv_bar_set_value(progress_bar, percentage, LV_ANIM_OFF);
            lvgl_port_unlock();
        }

        // Check for the progres
        if (esp_https_ota_is_complete_data_received(https_ota_handle)) {
            // Data receive complete
            ret = esp_https_ota_finish(https_ota_handle);
            if (ret != ESP_OK) {
                if (lvgl_port_lock(0)) {
                    lv_label_set_text_fmt(ota_title_label, "OTA Failed - %d", ret);
                    lvgl_port_unlock();
                }
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "esp_https_ota_finish failed");
            }
            else {
                if (lvgl_port_lock(0)) {
                    lv_label_set_text(ota_title_label, "OTA Success");
                    lvgl_port_unlock();
                }

                // Set the next boot partition
                ret = esp_ota_set_boot_partition(ota_partition);
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to set boot partition");
                
                break;
            }
        }
        else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

finally:
    ESP_LOGI(TAG, "OTA Complete");
    // Display reboot button
    if (lvgl_port_lock(0)) {
        lv_obj_remove_flag(reboot_button, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }

    vTaskDelete(NULL);   // safely remove this task

 }

 static void on_ota_cancel_button_pressed(lv_event_t *e) {
    ESP_LOGI(TAG, "Cancel OTA");
    set_ota_prompt_view_visibility(false);
 }

 static void on_ota_accept_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Start Upgrade pressed");
    set_ota_prompt_view_visibility(false);

    // Shift to OTA view
    last_tile = lv_tileview_get_tile_active(main_tileview);
    lv_tileview_set_tile(main_tileview, tile_ota_mode_view, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);

    xEventGroupSetBits(ota_event_group, OTA_EVENT_USER_CONFIRMED);
 }

 static void on_reboot_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
 }

 static void on_menu_ota_upgrade_button_pressed(lv_event_t *e) {
    set_ota_prompt_view_visibility(true);
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
    ota_title_label = lv_label_create(container);
    lv_obj_set_width(ota_title_label, lv_pct(100));
    lv_obj_set_align(ota_title_label, LV_ALIGN_CENTER);
    lv_label_set_long_mode(ota_title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(ota_title_label, "OTA Update");

    // Put progress label
    progress_label = lv_label_create(container);
    lv_obj_set_align(progress_label, LV_ALIGN_CENTER);
    lv_label_set_text(progress_label, "Progress: 0/0");
    lv_obj_set_width(progress_label, lv_pct(100));
    lv_label_set_long_mode(progress_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    // Put progress bar
    progress_bar = lv_bar_create(container);
    lv_obj_set_size(progress_bar, lv_pct(80), 20);

    // Put Exit and Reboot button
    reboot_button = lv_button_create(container);
    lv_obj_add_flag(reboot_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(reboot_button, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(reboot_button, on_reboot_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * reboot_button_label = lv_label_create(reboot_button);
    lv_label_set_text(reboot_button_label, "Reboot");
    lv_obj_center(reboot_button_label);
    lv_obj_set_style_text_font(reboot_button_label, &lv_font_montserrat_20, LV_PART_MAIN);

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


lv_obj_t * create_menu_ota_upgrade_button(lv_obj_t * parent) {
    menu_ota_upgrade_button = lv_button_create(parent);
    lv_obj_add_event_cb(menu_ota_upgrade_button, on_menu_ota_upgrade_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);
    // lv_obj_set_width(menu_ota_upgrade_button, lv_pct(80));
    // Set invisible by default
    lv_obj_add_flag(menu_ota_upgrade_button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * menu_ota_upgrade_button_label = lv_label_create(menu_ota_upgrade_button);
    // lv_label_set_long_mode(menu_ota_upgrade_button_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(menu_ota_upgrade_button_label, "Update Available");
    lv_obj_center(menu_ota_upgrade_button_label);
    // lv_obj_set_style_text_font(menu_ota_upgrade_button_label, &lv_font_montserrat_20, LV_PART_MAIN);

    return menu_ota_upgrade_button;
}
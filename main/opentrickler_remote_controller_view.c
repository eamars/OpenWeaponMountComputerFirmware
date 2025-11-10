#include <string.h>
#include <arpa/inet.h>

#include "opentrickler_remote_controller_view.h"
#include "wifi.h"
#include "app_cfg.h"
#include "system_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_task_wdt.h"
#include "esp_err.h"
#include "esp_check.h"
#include "mdns.h"
#include "esp_http_client.h"
#include "json_parser.h"
#include "esp_lvgl_port.h"

#include "lvgl.h"

#define TAG "OpenTricklerRemoteControl"


// Copied from OpenTrickler firmware
// TODO: Clone opentrickler controller as sub module
typedef enum {
    CHARGE_MODE_EXIT = 0,
    CHARGE_MODE_WAIT_FOR_ZERO = 1,
    CHARGE_MODE_WAIT_FOR_COMPLETE = 2,
    CHARGE_MODE_WAIT_FOR_CUP_REMOVAL = 3,
    CHARGE_MODE_WAIT_FOR_CUP_RETURN = 4,
} charge_mode_state_t;


TaskHandle_t opentrickler_rest_poller_task_handle;

typedef enum {
    OPENTRICKLER_REST_POLLER_TASK_RUN = (1 << 0),
    OPENTRICKLER_REST_POLLER_SERVER_SELECTED = (1 << 1),
} opentrickler_rest_poller_task_control_bit_e;
static EventGroupHandle_t opentrickler_rest_poller_task_control;
const char * opentrickler_service_name = "_opentrickler";
const char * opentrickler_service_protocol = "_tcp";


// Cache of opentrickler addresses
typedef struct {
    bool is_valid;
    char address[64];
} opentrickler_addr_t;
HEAPS_CAPS_ATTR opentrickler_addr_t discovered_opentrickler[OPENTRICKLER_MAX_DISCOVER_COUNT];

typedef struct {
    char address[64];
    char version[32];
    // other modes to be populated
} opentrickler_server_t;
HEAPS_CAPS_ATTR opentrickler_server_t opentrickler_server;

extern system_config_t system_config;

// LVGL objects
lv_obj_t * load_weight_arc;
lv_obj_t * load_weight_label;
lv_obj_t * powder_profile_label;
lv_obj_t * charge_time_secs_label;
lv_obj_t * center_button;


esp_err_t find_opentrickler_mdns_service() {
    // Reset the discovered opentrickler
    memset(discovered_opentrickler, 0x0, sizeof(discovered_opentrickler));

    ESP_LOGI(TAG, "Query PTR: %s.%s.local", opentrickler_service_name, opentrickler_service_protocol);

    mdns_result_t * mdns_result = NULL;
    esp_err_t ret = mdns_query_ptr(opentrickler_service_name, opentrickler_service_protocol, 30000, 20, &mdns_result);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run mdns_query_ptr: %s", esp_err_to_name(ret));
        return ret;
    }

    // If no result
    if (!mdns_result) {
        ESP_LOGW(TAG, "No opentrickler mDNS service found");
    }
    else {
        // There is at least one server available, then start to identify the service
        mdns_result_t *r = mdns_result;
        for (size_t idx = 0; idx < OPENTRICKLER_MAX_DISCOVER_COUNT; idx += 1) {
            if (!r) {
                break;
            }

            ESP_LOGI(TAG, "Discovered %s", r->hostname);
            // IP address is already discovered, then store the address
            mdns_ip_addr_t *a = r->addr;
            discovered_opentrickler[idx].address[0] = 0;  // unset address
            while (a) {
                if (a->addr.type == IPADDR_TYPE_V4) {
                    snprintf(discovered_opentrickler[idx].address,  sizeof(discovered_opentrickler[idx].address), IPSTR, IP2STR(&(a->addr.u_addr.ip4)));
                    break;
                }
                a = a->next;
            }

            if (discovered_opentrickler[idx].address[0] == 0) {
                // If the addr is not available then use hostname instead
                strncpy(discovered_opentrickler[idx].address, r->hostname, sizeof(discovered_opentrickler[idx].address));
            }

            ESP_LOGI(TAG, "  Address: %s", discovered_opentrickler[idx].address);
            discovered_opentrickler[idx].is_valid = true;

            // Move to next one
            r = r->next;
        }
    }


    mdns_query_results_free(mdns_result);

    return ESP_OK;
}

static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    static size_t output_len = 0;

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
            output_len = 0;
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            // Assume chunked transfer encoding (actually not)
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (evt->user_data) {
                    // Copy the data to the buffer
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                    output_len += evt->data_len;
                }
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}


static void opentrickler_rest_poller_task(void *p) {
    esp_err_t ret;

    // First disable the watchdog timer
    // The task is expectd to be blocked indefinitely if wifi is not connected
    esp_task_wdt_delete(NULL);

    // Wait for wifi to be connected
    while (true) {
        ret = wifi_wait_for_sta_connected(1000);
        if (ret == ESP_OK) {
            break;
        }
    }

    // Wifi is connected, will first check for mDNS service
    ret = find_opentrickler_mdns_service();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to run mDNS discover");
    }

    // Check if there is any opentrickler being discovered
    size_t discovered_count = 0;
    for (; discovered_count < OPENTRICKLER_MAX_DISCOVER_COUNT; discovered_count += 1) {
        if (!discovered_opentrickler[discovered_count].is_valid) {
            break;
        }
    }
    if (discovered_count == 0) {
        ESP_LOGW(TAG, "No OpenTrickler service discovered");
        // Stop the thread
    }
    else {
        // indicate the opentrickler is selected
        // FIXME: Prompt user to select the server, or load from NVS
        memcpy(opentrickler_server.address, discovered_opentrickler[0].address, sizeof(opentrickler_server.address));
        xEventGroupSetBits(opentrickler_rest_poller_task_control, OPENTRICKLER_REST_POLLER_SERVER_SELECTED);
    }

    // Maximum buffer length is known, there is no need to dynamically allocate
    char * charge_mode_state_json_raw = heap_caps_calloc(1, OPENTRICKLER_REST_BUFFER_BYTES, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (!charge_mode_state_json_raw) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Failed to allocate memory for charge_mode_state_json_raw");
        ESP_ERROR_CHECK(ret);
    }

    // Start the polling loop
    TickType_t last_poll_tick = 0;

    while (true) {
        // Wait for bits to be asserted
        xEventGroupWaitBits(
            opentrickler_rest_poller_task_control, 
            OPENTRICKLER_REST_POLLER_TASK_RUN | OPENTRICKLER_REST_POLLER_SERVER_SELECTED, 
            pdFALSE, 
            pdTRUE,
            portMAX_DELAY
        );

        esp_http_client_handle_t client = NULL;

        // Once allow to run, will keep running until the run bit is cleared. This will
        // 1. Create HTTP connection to the server
        // 2. Poll the server at the configured rate
        esp_http_client_config_t config = {
            .host = opentrickler_server.address,
            .path = "/rest/charge_mode_state",
            .port = 80,
            .event_handler = http_client_event_handler,
            .user_data = charge_mode_state_json_raw,
            .timeout_ms = 5000,
        };
        client = esp_http_client_init(&config);
       
        // Start the polling loop
        last_poll_tick = xTaskGetTickCount();
        while (xEventGroupGetBits(opentrickler_rest_poller_task_control) & (OPENTRICKLER_REST_POLLER_TASK_RUN | OPENTRICKLER_REST_POLLER_SERVER_SELECTED)) {
            // Start HTTP request
            ret = esp_http_client_perform(client);
            ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));

            // Handle chunked transfer (no content length)

            if (esp_http_client_get_status_code(client) != 200) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "HTTP request failed with status code: %d", esp_http_client_get_status_code(client));
            }

            // Decode by json parser
            jparse_ctx_t jctx;
            if (json_parse_start(&jctx, charge_mode_state_json_raw, OPENTRICKLER_REST_BUFFER_BYTES) != OS_SUCCESS) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to decode json string: %s", charge_mode_state_json_raw);
            }

            // All variables
            float target_charge_weight;             // s0
            char current_charge_weight_str[16];     // s1
            charge_mode_state_t charge_mode_state;  // s2
            uint32_t charge_mode_event;             // s3
            char profile_name_str[16];              // s4
            char elapsed_time_str[16];              // s5

            // Read target_charge_weight
            if (json_obj_get_float(&jctx, "s0", &target_charge_weight)) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract s0: %s", charge_mode_state_json_raw);
            }

            // Read current_charge_weight
            if (json_obj_get_string(&jctx, "s1", current_charge_weight_str, sizeof(current_charge_weight_str))) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract s1: %s", charge_mode_state_json_raw);
            }

            // Read charge_mode_state
            if (json_obj_get_int(&jctx, "s2", (int *) &charge_mode_state)) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract s2: %s", charge_mode_state_json_raw);
            }

            // Read charge_mode_event
            if (json_obj_get_int(&jctx, "s3", (int *)&charge_mode_event)) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract s3: %s", charge_mode_state_json_raw);
            }

            // Read profile_name
            if (json_obj_get_string(&jctx, "s4", profile_name_str, sizeof(profile_name_str))) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract s4: %s", charge_mode_state_json_raw);
            }

            // Read clapsed_time
            if (json_obj_get_string(&jctx, "s5", elapsed_time_str, sizeof(elapsed_time_str))) {
                ret = ESP_FAIL;
                ESP_GOTO_ON_ERROR(ret, finally, TAG, "Failed to extract s5: %s", charge_mode_state_json_raw);
            }

            // Update the UI
            if (lvgl_port_lock(0)) {
                // Update current weight label
                lv_label_set_text(load_weight_label, current_charge_weight_str);

                // Update profile name
                lv_label_set_text(powder_profile_label, profile_name_str);

                // Update elapsed time
                lv_label_set_text(charge_time_secs_label, elapsed_time_str);

                lvgl_port_unlock();
            }

            vTaskDelayUntil(&last_poll_tick, pdMS_TO_TICKS(OPENTRICKLER_REST_POLLER_TASK_PERIOD_MS));
        }

finally:
        if (client) {
            esp_http_client_cleanup(client);
        }
    }

    if (charge_mode_state_json_raw) {
        heap_caps_free(charge_mode_state_json_raw);
        charge_mode_state_json_raw = NULL;
    }
}


void set_rotation_opentrickler_remote_controller_view(lv_display_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        // Portrait
        lv_obj_align(center_button, LV_ALIGN_CENTER, 0, 5);  // put to the middle
        lv_obj_align(powder_profile_label, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_align(charge_time_secs_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    else {
        // Landscape
        lv_obj_align(center_button, LV_ALIGN_RIGHT_MID, -5, 0);  // put to the middle
        lv_obj_align(powder_profile_label, LV_ALIGN_TOP_LEFT, 10, 0);
        lv_obj_align(charge_time_secs_label, LV_ALIGN_BOTTOM_LEFT, 10, 0);  
    }
}


static void rotation_event_callback(lv_event_t * e) {
    set_rotation_opentrickler_remote_controller_view(system_config.rotation);
}

void create_opentrickler_remote_controller_view(lv_obj_t * parent) {
    // Initialize UI
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);  // set background colour to black
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    // Calculate the size of center button/arc
    lv_coord_t w = lv_obj_get_content_width(parent);
    lv_coord_t h = lv_obj_get_content_height(parent);
    lv_coord_t shorter = (lv_coord_t) (LV_MIN(w, h) * 0.9);

    // Set the center label (actually an button)
    center_button = lv_button_create(parent);
    lv_obj_set_style_radius(center_button, LV_RADIUS_CIRCLE, LV_PART_MAIN); // Make it fully round
    lv_obj_set_style_bg_opa(center_button, LV_OPA_TRANSP, LV_PART_MAIN);  // transparent
    lv_obj_set_style_border_width(center_button, 0, LV_PART_MAIN);  // hide border
    lv_obj_set_style_shadow_width(center_button, 0, LV_PART_MAIN);  // hide shadow
    lv_obj_set_size(center_button, shorter, shorter);


    // TODO: Add short and long press callback

    // The center gauge (arc)
    load_weight_arc = lv_arc_create(center_button);
    lv_obj_set_style_arc_color(load_weight_arc, lv_palette_main(LV_PALETTE_INDIGO), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(load_weight_arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_remove_style(load_weight_arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
    lv_obj_remove_flag(load_weight_arc, LV_OBJ_FLAG_CLICKABLE);  /*To not allow adjusting by click*/
    lv_obj_align(load_weight_arc, LV_ALIGN_CENTER, 0, 0);  // put to the middle
    lv_obj_set_size(load_weight_arc, lv_pct(100), lv_pct(100));

    lv_arc_set_rotation(load_weight_arc, 270);
    lv_arc_set_bg_angles(load_weight_arc, 0, 360);
    lv_arc_set_range(load_weight_arc, 0, 100);  // percentage

    // Weight Label 
    load_weight_label = lv_label_create(center_button);
    lv_obj_set_style_text_color(load_weight_label, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_text_font(load_weight_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(load_weight_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(load_weight_label, "0");

    // Powder profile
    powder_profile_label = lv_label_create(parent);
    lv_obj_set_style_text_color(powder_profile_label, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_text_font(powder_profile_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(powder_profile_label, "Unknown Profile");

    // Charge time
    charge_time_secs_label = lv_label_create(parent);
    lv_obj_set_style_text_color(charge_time_secs_label, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_text_font(charge_time_secs_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(charge_time_secs_label, "-1.00 s");

    set_rotation_opentrickler_remote_controller_view(system_config.rotation);

    // Add rotation event to the callback
    lv_obj_add_event_cb(parent, rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);

    // Initialize task control
    opentrickler_rest_poller_task_control = xEventGroupCreate();
    if (opentrickler_rest_poller_task_control == NULL) {
        ESP_LOGE(TAG, "Failed to create opentrickler_rest_poller_task_control");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // Initialize poller
    BaseType_t rtos_return = xTaskCreate(
        opentrickler_rest_poller_task, 
        "OT_poller",
        OPENTRICKLER_REST_POLLER_TASK_STACK,
        NULL,
        OPENTRICKLER_REST_POLLER_TASK_PRIORITY,
        &opentrickler_rest_poller_task_handle
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate memory for opentrickler_rest_poller_task");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}


void enable_opentrickler_remote_controller_mode(bool enable) {
    if (enable) {
        xEventGroupSetBits(opentrickler_rest_poller_task_control, OPENTRICKLER_REST_POLLER_TASK_RUN);
    }
    else {
        xEventGroupClearBits(opentrickler_rest_poller_task_control, OPENTRICKLER_REST_POLLER_TASK_RUN);
    }
}

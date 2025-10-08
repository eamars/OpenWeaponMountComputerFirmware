#include <string.h>
#include <arpa/inet.h>

#include "opentrickler_remote_controller_view.h"
#include "wifi.h"
#include "app_cfg.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_task_wdt.h"
#include "esp_err.h"
#include "esp_check.h"
#include "mdns.h"

#define TAG "OpenTricklerRemoteControl"





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
        // xEventGroupSetBits(opentrickler_rest_poller_task_control, OPENTRICKLER_REST_POLLER_SERVER_SELECTED);
    }

    // Start the polling loop
    TickType_t last_poll_tick;

    while (true) {
        // Wait for bits to be asserted
        xEventGroupWaitBits(
            opentrickler_rest_poller_task_control, 
            OPENTRICKLER_REST_POLLER_TASK_RUN | OPENTRICKLER_REST_POLLER_SERVER_SELECTED, 
            pdFALSE, 
            pdTRUE,
            portMAX_DELAY
        );

        last_poll_tick = xTaskGetTickCount();
        while (xEventGroupGetBits(opentrickler_rest_poller_task_control) & (OPENTRICKLER_REST_POLLER_TASK_RUN | OPENTRICKLER_REST_POLLER_SERVER_SELECTED)) {
            ESP_LOGI(TAG, "Poll server");
        }
    }
}

void create_opentrickler_remote_controller_view(lv_obj_t * parent) {
    // Initialize UI

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

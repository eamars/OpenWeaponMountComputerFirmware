#include "ui_bridge.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "config_view.h"
#include "pmic_axp2101.h"
#include "pmic_config.h"
#include "wifi.h"

#define TAG "UIBridge"

lv_subject_t ui_wifi_state;
lv_subject_t ui_wifi_enabled;
lv_subject_t ui_wifi_expired;
lv_subject_t ui_wifi_provisioned;
lv_subject_t ui_battery_level;
lv_subject_t ui_pmic_status;

// The bridge's copy of the PMIC readings, which ui_pmic_status points to; changed only with the LVGL lock held
static axp2101_status_t ui_pmic_status_copy;

extern lv_obj_t * msg_box;


// LVGL lock held. Reads the service's current status rather than the event's, so the newest is always shown
static void ui_bridge_show_wifi_status() {
    wifi_status_t status = wifi_get_status();

    // Each set notifies the bound widgets only when the value changes
    lv_subject_set_int(&ui_wifi_state, status.state);
    lv_subject_set_int(&ui_wifi_enabled, status.enabled);
    lv_subject_set_int(&ui_wifi_expired, status.expired);
    lv_subject_set_int(&ui_wifi_provisioned, status.provisioned);
}


// System event task
static void ui_bridge_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    lvgl_port_lock(0);

    if (event_id == WIFI_SERVICE_EVENT_STATUS_CHANGED) {
        ui_bridge_show_wifi_status();
    }
    else if (event_id == WIFI_SERVICE_EVENT_PROVISION_RESET && msg_box != NULL) {
        esp_err_t ret = *(esp_err_t *) event_data;
        // A failure (e.g. Wi-Fi already shut down by the expiry) is reported, not fatal (ADR-005 B10)
        update_info_msg_box(msg_box, ret == ESP_OK ?
            "WiFi Provisioning reset. Please re-provision the device." :
            "WiFi Provisioning reset failed. Power cycle the device and try again.");
    }

    lvgl_port_unlock();
}


// 101 shows the USB icon: on USB power, and before the first reading (or with no PMIC), as before
static int32_t ui_bridge_battery_level(const axp2101_status_t * status) {
    return (!status->valid || status->is_usb_connected) ? 101 : status->battery_percentage;
}


// LVGL lock held
static void ui_bridge_show_pmic_status() {
    axp2101_status_t status = pmic_get_status();

    lv_subject_set_int(&ui_battery_level, ui_bridge_battery_level(&status));
    if (memcmp(&status, &ui_pmic_status_copy, sizeof(status)) != 0) {
        ui_pmic_status_copy = status;
        lv_subject_notify(&ui_pmic_status);  // same pointer, new contents
    }
}


// System event task
static void ui_bridge_pmic_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    lvgl_port_lock(0);

    if (event_id == PMIC_SERVICE_EVENT_STATUS_CHANGED) {
        ui_bridge_show_pmic_status();
    }
    else if (event_id == PMIC_SERVICE_EVENT_POWER_KEY_LONG_PRESS) {
        power_menu_make_visible();
    }

    lvgl_port_unlock();
}


void ui_bridge_init() {
    wifi_status_t status = wifi_get_status();
    lv_subject_init_int(&ui_wifi_state, status.state);
    lv_subject_init_int(&ui_wifi_enabled, status.enabled);
    lv_subject_init_int(&ui_wifi_expired, status.expired);
    lv_subject_init_int(&ui_wifi_provisioned, status.provisioned);

    ui_pmic_status_copy = pmic_get_status();
    lv_subject_init_int(&ui_battery_level, ui_bridge_battery_level(&ui_pmic_status_copy));
    lv_subject_init_pointer(&ui_pmic_status, &ui_pmic_status_copy);

    // A change between the reads above and these registrations is still shown: the handlers read the status anew
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_SERVICE_EVENT, ESP_EVENT_ANY_ID, ui_bridge_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PMIC_SERVICE_EVENT, ESP_EVENT_ANY_ID, ui_bridge_pmic_event_handler, NULL));

    ESP_LOGI(TAG, "UI bridge ready, Wi-Fi status: %s", wifi_state_name(status.state));
}

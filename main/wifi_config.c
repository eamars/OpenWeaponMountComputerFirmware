#include <string.h>

#include "wifi_config.h"
#include "wifi.h"
#include "config_view.h"
#include "wifi_provision.h"

#include "esp_wifi.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "nvs.h"
#include "esp_crc.h"

#include "common.h"
#include "app_cfg.h"

#define TAG "WiFiConfig"
#define NVS_NAMESPACE "WIFI"

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_SOFTAP   "softap"


extern wireless_state_e wireless_state;
extern wireless_provision_state_t wifi_provision_state;
extern wifi_user_config_t wifi_user_config;
extern const wifi_user_config_t default_wifi_user_config;
HEAPS_CAPS_ATTR static char wifi_status_str[64];
static lv_obj_t * qr_container;
static lv_obj_t * qr_code;
static lv_obj_t * reset_provision_button;
static lv_obj_t * wifi_status_label = NULL;
static lv_obj_t * wifi_enable_switch = NULL;
extern EventGroupHandle_t wireless_event_group;
extern lv_obj_t * msg_box;

extern esp_err_t save_wifi_user_config();
extern esp_err_t load_wifi_user_config();


void wifi_config_update_status(const char * state_str) {
    if (wifi_status_label) {
        snprintf(wifi_status_str, sizeof(wifi_status_str), "Status: %s", state_str);

        // Force redraw
        if (lvgl_port_lock(0)) {
            lv_label_set_text_static(wifi_status_label, wifi_status_str);
            lvgl_port_unlock();
        }
    }
}


static void toggle_enable_wifi(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (*state) {
        esp_wifi_start();
    }
    else {
        esp_wifi_stop();
    }
}


static void on_reset_provision_button_pressed(lv_event_t * e) {
    wifi_provision_reset();
    update_info_msg_box(msg_box, "WiFi Provisioning reset. Please re-provision the device.");
}



void on_qr_code_button_pressed(lv_event_t * e) {
    if (qr_code) {
        // Set QR Code size based on the condition
        // Note: QR code has to be scaled before setting the data
        lv_coord_t box_w = lv_obj_get_content_width(qr_container);
        lv_coord_t box_h = lv_obj_get_content_height(qr_container);
        ESP_LOGI(TAG, "QR container size: %d x %d", box_w, box_h);
        lv_qrcode_set_size(qr_code, LV_MIN(box_w, box_h));  // leave some margin

        // Configure QR code
        char payload[150];
        memset(payload, 0, sizeof(payload));

        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                    ",\"pop\":\"%s\",\"transport\":\"%s\"}",
                PROV_QR_VERSION, wifi_provision_state.service_name, wifi_provision_state.pop, PROV_TRANSPORT_SOFTAP);
        lv_qrcode_update(qr_code, payload, strlen(payload));
    }

    // Set display
    lv_obj_clear_flag(qr_container, LV_OBJ_FLAG_HIDDEN);
}


void on_qr_code_clicked(lv_event_t * e) {
    // hide
    lv_obj_add_flag(qr_container, LV_OBJ_FLAG_HIDDEN);
}


void wifi_config_disable_enable_interface() {
    if (lvgl_port_lock(0)) {
        // Then disable the enable wifi toggle switch
        lv_obj_remove_state(wifi_enable_switch, LV_STATE_CHECKED);
        lv_obj_add_state(wifi_enable_switch, LV_STATE_DISABLED);
        lvgl_port_unlock();
    }
}


void wifi_config_disable_provision_interface(wireless_state_e reason) {
    if (lvgl_port_lock(0)) {
        // Delete QR code first
        if (qr_code) {
            lv_obj_delete(qr_code);
            qr_code = NULL;
        }

        if (reason == WIRELESS_STATE_PROVISION_EXPIRE) {
            // Disable the button to reset provisioning state
            lv_obj_add_state(reset_provision_button, LV_STATE_DISABLED);

            lv_obj_t * reason_label = lv_label_create(qr_container);
            lv_obj_set_width(reason_label, lv_pct(80));
            lv_label_set_long_mode(reason_label, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_add_flag(reason_label, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(reason_label, on_qr_code_clicked, LV_EVENT_CLICKED, NULL);

            lv_label_set_text(reason_label, "Provision Expired. Power cycle the device to restart provision process.");
        }
        else if (reason == WIRELESS_STATE_PROVISIONED) {
            lv_obj_t * reason_label = lv_label_create(qr_container);
            lv_obj_set_width(reason_label, lv_pct(80));
            lv_label_set_long_mode(reason_label, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_add_flag(reason_label, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(reason_label, on_qr_code_clicked, LV_EVENT_CLICKED, NULL);

            lv_label_set_text(reason_label, "Provision Complete. Use Reset Provision button and power cycle the device to restart provision process.");
        }

        lvgl_port_unlock();
    }
}


esp_err_t save_wifi_user_config() {
    return save_config(NVS_NAMESPACE, &wifi_user_config, sizeof(wifi_user_config));
}



esp_err_t load_wifi_user_config() {
    return load_config(NVS_NAMESPACE, &wifi_user_config, &default_wifi_user_config, sizeof(wifi_user_config));
}



static void on_save_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(save_wifi_user_config());

    update_info_msg_box(msg_box, "Configuration Saved");
}

static void on_reload_button_pressed(lv_event_t * e) {
    ESP_ERROR_CHECK(load_wifi_user_config());

    update_info_msg_box(msg_box, "Previous Configuration Reloaded");

    // TODO: Update current displayed values
}

static void on_reset_button_pressed(lv_event_t * e) {
    // Initialize with default values
    memcpy(&wifi_user_config, &default_wifi_user_config, sizeof(wifi_user_config));

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");

    // TODO: Update current display values
}


lv_obj_t * create_wifi_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    // Load configuration
    ESP_ERROR_CHECK(load_wifi_user_config());


    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Wifi status label
    snprintf(wifi_status_str, sizeof(wifi_status_str), "Status: %s", "Disconnected");  // TODO: load actual wifi status
    wifi_status_label = create_config_label_static(sub_page_config_view, wifi_status_str);


    // Enable Wifi
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable WiFi");
    wifi_enable_switch = create_switch(container, &wifi_user_config.wifi_enable, toggle_enable_wifi);

    // Reset Wifi provision
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Reset Provision");
    reset_provision_button = create_single_button(container, LV_SYMBOL_WARNING, on_reset_provision_button_pressed);

    // Create qr msg box
    qr_container = lv_obj_create(parent);
    lv_obj_set_size(qr_container, lv_pct(100), lv_pct(100));

    // Remove padding, border, and scroll to allow full expansion
    lv_obj_set_style_pad_all(qr_container, 0, 0);
    lv_obj_set_style_border_width(qr_container, 0, 0);
    lv_obj_clear_flag(qr_container, LV_OBJ_FLAG_SCROLLABLE);

    // Enable flex so the child (QR) can expand
    lv_obj_set_flex_flow(qr_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(qr_container,
                          LV_FLEX_ALIGN_CENTER,  /* main axis */
                          LV_FLEX_ALIGN_CENTER,  /* cross axis */
                          LV_FLEX_ALIGN_CENTER); /* track cross axis */

    // Create Wifi provision QR code
    qr_code = lv_qrcode_create(qr_container);

    lv_color_t bg_color = lv_color_darken(lv_color_white(), 0);
    lv_color_t fg_color = lv_color_darken(lv_color_black(), 0);

    lv_qrcode_set_dark_color(qr_code, fg_color);
    lv_qrcode_set_light_color(qr_code, bg_color);

    lv_obj_set_style_border_color(qr_code, bg_color, 0);
    lv_obj_set_style_border_width(qr_code, 5, 0);

    lv_obj_add_flag(qr_code, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(qr_code, on_qr_code_clicked, LV_EVENT_CLICKED, NULL);

    // set hidden
    lv_obj_add_flag(qr_container, LV_OBJ_FLAG_HIDDEN);
    
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Provision QR Code");
    config_item = create_single_button(container, LV_SYMBOL_IMAGE, on_qr_code_button_pressed);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    config_item = create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);


    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "WiFi Config");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);


    return sub_page_config_view;
}
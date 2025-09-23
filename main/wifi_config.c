#include <string.h>

#include "wifi_config.h"
#include "wifi.h"
#include "config_view.h"
#include "wifi_provision.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "app_cfg.h"

#define TAG "WiFiConfig"
#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_SOFTAP   "softap"


extern wireless_state_e wireless_state;
extern wireless_provision_state_t wifi_provision_state;
extern wifi_user_config_t wifi_config;
HEAPS_CAPS_ATTR static char wifi_status_str[64];
static lv_obj_t * qr_container;
static lv_obj_t * qr_code;
static lv_obj_t * reset_provision_button;


static void toggle_enable_wifi(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);
}


static void on_reset_provision_button_pressed(lv_event_t * e) {
    wifi_provision_reset();
    update_info_msg_box("WiFi Provisioning reset. Please re-provision the device.");
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


void wifi_config_disable_provision_interface(wireless_state_e reason) {
    if (lvgl_port_lock(0)) {
        // Delete QR code first
        lv_obj_delete(qr_code);
        qr_code = NULL;

        // Disable the button to reset provisioning state
        lv_obj_add_state(reset_provision_button, LV_STATE_DISABLED);


        if (reason == WIRELESS_STATE_PROVISION_EXPIRE) {
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


lv_obj_t * create_wifi_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * container;
    lv_obj_t * config_item;

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Enable Wifi
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable WiFi");
    bool temp_wifi_enable = false;  // TODO: load actual wifi state
    config_item = create_switch(container, &temp_wifi_enable, toggle_enable_wifi);

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

    // Wifi status label
    snprintf(wifi_status_str, sizeof(wifi_status_str), "Status: %s", "Disconnected");  // TODO: load actual wifi status
    lv_obj_t * wifi_status_label = create_config_label_static(sub_page_config_view, wifi_status_str);

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
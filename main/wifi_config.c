#include <string.h>

#include "wifi_config.h"
#include "wifi.h"
#include "config_view.h"
#include "wifi_provision.h"
#include "ui_bridge.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "common.h"
#include "app_cfg.h"

#define TAG "WiFiConfig"

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_SOFTAP   "softap"

// The Wi-Fi settings page. It shows the Wi-Fi service through the UI bridge's subjects and asks the service for
// changes; it never calls esp_wifi_* (which it used to do with the LVGL lock held) (ADR-005 step 3)

HEAPS_CAPS_ATTR static char wifi_status_str[64];
static lv_obj_t * qr_container;
static lv_obj_t * qr_code;
static lv_obj_t * provision_note = NULL;
static lv_obj_t * reset_provision_button;
static bool wifi_enable_switch_initial;


static void toggle_enable_wifi(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    wifi_request_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}


static void on_reset_provision_button_pressed(lv_event_t * e) {
    // The result comes back through the UI bridge, which shows it in the message box
    wifi_request_reset_provisioning();
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


// ---- Observers (LVGL context): the widgets follow the Wi-Fi service ----------------------------------------------

static void wifi_status_label_observer_cb(lv_observer_t * observer, lv_subject_t * subject) {
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "Status: %s",
                          wifi_state_name((wireless_state_e) lv_subject_get_int(subject)));
}


// Bound to the enabled and expired subjects. Setting the state does not fire the switch's VALUE_CHANGED.
// After the expiry the radio is off, so the switch shows off; switching it on starts a new Wi-Fi window
static void wifi_enable_switch_observer_cb(lv_observer_t * observer, lv_subject_t * subject) {
    lv_obj_t * sw = lv_observer_get_target_obj(observer);

    if (lv_subject_get_int(&ui_wifi_enabled) && !lv_subject_get_int(&ui_wifi_expired)) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    else {
        lv_obj_remove_state(sw, LV_STATE_CHECKED);
    }
}


static void create_provision_qr_code() {
    qr_code = lv_qrcode_create(qr_container);

    lv_color_t bg_color = lv_color_darken(lv_color_white(), 0);
    lv_color_t fg_color = lv_color_darken(lv_color_black(), 0);

    lv_qrcode_set_dark_color(qr_code, fg_color);
    lv_qrcode_set_light_color(qr_code, bg_color);

    lv_obj_set_style_border_color(qr_code, bg_color, 0);
    lv_obj_set_style_border_width(qr_code, 5, 0);

    lv_obj_add_flag(qr_code, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(qr_code, on_qr_code_clicked, LV_EVENT_CLICKED, NULL);
}


// Bound to the provisioned and expired subjects: the QR code while the unit waits for credentials (rebuilt after
// a provisioning reset; its data, with the new proof of possession, is set when it is opened), a note otherwise
static void wifi_provision_panel_observer_cb(lv_observer_t * observer, lv_subject_t * subject) {
    bool provisioned = lv_subject_get_int(&ui_wifi_provisioned);
    bool expired = lv_subject_get_int(&ui_wifi_expired);

    if (!provisioned && !expired) {
        if (provision_note) {
            lv_obj_delete(provision_note);
            provision_note = NULL;
        }
        if (qr_code == NULL) {
            create_provision_qr_code();
        }
        return;
    }

    if (qr_code) {
        lv_obj_delete(qr_code);
        qr_code = NULL;
    }

    if (provision_note == NULL) {
        provision_note = lv_label_create(qr_container);
        lv_obj_set_width(provision_note, lv_pct(80));
        lv_label_set_long_mode(provision_note, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_add_flag(provision_note, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(provision_note, on_qr_code_clicked, LV_EVENT_CLICKED, NULL);
    }

    lv_label_set_text(provision_note, provisioned ?
        "Provision Complete. Use the Reset Provision button to provision again." :
        "Provision Expired. Switch WiFi on to provision again.");
}


static void on_save_button_pressed(lv_event_t * e) {
    update_info_msg_box_result(msg_box, save_wifi_user_config(), "Configuration Saved", "Save failed");
}

static void on_reload_button_pressed(lv_event_t * e) {
    update_info_msg_box_result(msg_box, load_wifi_user_config(), "Previous Configuration Reloaded", "Reload failed");

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

    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Wifi status label (the observer below sets the text)
    snprintf(wifi_status_str, sizeof(wifi_status_str), "Status: %s", "");
    lv_obj_t * wifi_status_label = create_config_label_static(sub_page_config_view, wifi_status_str);


    // Enable Wifi
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Enable WiFi");
    wifi_enable_switch_initial = lv_subject_get_int(&ui_wifi_enabled);
    lv_obj_t * wifi_enable_switch = create_switch(container, &wifi_enable_switch_initial, toggle_enable_wifi);

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

    // The QR code or the note is made by the provisioning observer below
    qr_code = NULL;

    // set hidden
    lv_obj_add_flag(qr_container, LV_OBJ_FLAG_HIDDEN);

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Provision QR Code");
    config_item = create_single_button(container, LV_SYMBOL_IMAGE, on_qr_code_button_pressed);

    // Save Reload
    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    config_item = create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);


    // Bind the widgets to the Wi-Fi service; each observer runs once now with the current value
    lv_subject_add_observer_obj(&ui_wifi_state, wifi_status_label_observer_cb, wifi_status_label, NULL);
    lv_subject_add_observer_obj(&ui_wifi_enabled, wifi_enable_switch_observer_cb, wifi_enable_switch, NULL);
    lv_subject_add_observer_obj(&ui_wifi_expired, wifi_enable_switch_observer_cb, wifi_enable_switch, NULL);
    lv_subject_add_observer_obj(&ui_wifi_provisioned, wifi_provision_panel_observer_cb, qr_container, NULL);
    lv_subject_add_observer_obj(&ui_wifi_expired, wifi_provision_panel_observer_cb, qr_container, NULL);


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

#include <stdio.h>
#include <string.h>

#include "wifi_page.h"
#include "wifi.h"
#include "wifi_provision.h"
#include "ui_bridge.h"
#include "settings_ui.h"

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

// The Wi-Fi settings page (ADR-008). It shows the Wi-Fi service through the UI bridge's subjects and asks the
// service for changes; it never calls esp_wifi_* (which it used to do with the LVGL lock held) (ADR-005 step 3)

static settings_page_t * qr_page = NULL;
static lv_obj_t * qr_container = NULL;
static lv_obj_t * qr_code = NULL;
static lv_obj_t * provision_note = NULL;


static void create_provision_qr_code() {
    qr_code = lv_qrcode_create(qr_container);

    lv_color_t bg_color = lv_color_white();
    lv_color_t fg_color = lv_color_black();

    lv_qrcode_set_dark_color(qr_code, fg_color);
    lv_qrcode_set_light_color(qr_code, bg_color);

    lv_obj_set_style_border_color(qr_code, bg_color, 0);
    lv_obj_set_style_border_width(qr_code, 5, 0);
}


// Bound to the provisioned and expired subjects: the QR code while the unit waits for credentials (rebuilt after
// a provisioning reset; its data, with the new proof of possession, is set when it is opened), a note otherwise
// (the page's row is shown only while the code is useful; the note is for a change while the page is open)
static void wifi_provision_panel_observer_cb(lv_observer_t * observer, lv_subject_t * subject) {
    bool provisioned = lv_subject_get_int(ui_wifi_provisioned);
    bool expired = lv_subject_get_int(ui_wifi_expired);

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
        lv_obj_set_width(provision_note, lv_pct(100));
        lv_label_set_long_mode(provision_note, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(provision_note, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(provision_note, &lv_font_montserrat_24, LV_PART_MAIN);
    }

    lv_label_set_text(provision_note, provisioned ?
        "The unit has its Wi-Fi network. Forget network to set it up again." :
        "Set-up time is over. Switch Wi-Fi on to set it up again.");
}


// The QR code for the provisioning app, as large as the page allows
static void open_qr_page(void * ctx) {
    settings_ui_open(qr_page);
    if (qr_code) {
        lv_obj_update_layout(qr_container);
        int32_t side = LV_MIN(lv_obj_get_content_width(qr_container), lv_obj_get_content_height(qr_container));
        lv_qrcode_set_size(qr_code, side - 10);     // the code is scaled before its data is set

        char payload[150];
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, wifi_provision_state.service_name, wifi_provision_state.pop, PROV_TRANSPORT_SOFTAP);
        lv_qrcode_update(qr_code, payload, strlen(payload));
    }
}


static void wifi_status_text(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Status: %s", wifi_state_name((wireless_state_e) lv_subject_get_int(ui_wifi_state)));
}


// What a host connects to (ADR-013): the name, which stays, and the address, which the router may change
static void wifi_name_text(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Network name\n%s.local", wifi_get_hostname());     // as the Kestrel page shows the HUD name
}


static void wifi_address_text(char * buf, size_t size, void * ctx) {
    char address[16];
    snprintf(buf, size, "Address\n%s", wifi_get_address(address, sizeof(address)) ? address : "-");
}


// Only while connected: there is no address otherwise, and the name is reached through it
static bool connected_shown(void) {
    return lv_subject_get_int(ui_wifi_state) == WIRELESS_STATE_CONNECTED;
}


// After the expiry the radio is off, so Wi-Fi shows off; switching it on starts a new Wi-Fi window
static bool get_wifi_on(void * ctx) {
    return lv_subject_get_int(ui_wifi_enabled) && !lv_subject_get_int(ui_wifi_expired);
}


static void set_wifi_on(bool on, void * ctx) {
    wifi_request_enable(on);
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_wifi_user_config());
}


// The result comes back through the service: the status row shows it
static void forget_network(void * ctx) {
    wifi_request_reset_provisioning();
}


// Set-up only while the unit waits for its network with the radio on (the set-up network runs); Forget only once
// it has one. Shown otherwise, each did nothing
static bool setup_shown(void) {
    return !lv_subject_get_int(ui_wifi_provisioned) && get_wifi_on(NULL);
}


static bool forget_shown(void) {
    return lv_subject_get_int(ui_wifi_provisioned);
}


void wifi_settings_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "%s", wifi_state_name((wireless_state_e) lv_subject_get_int(ui_wifi_state)));
}


settings_page_t * create_wifi_settings_page(void) {
    // The QR code's page: the code or a note, filling the page under its header
    qr_page = settings_page_create("Set-up QR code");
    qr_container = lv_obj_create(settings_page_body(qr_page));
    lv_obj_remove_style_all(qr_container);
    lv_obj_set_size(qr_container, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollable(qr_container, false);
    lv_obj_set_flex_flow(qr_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(qr_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_subject_add_observer_obj(ui_wifi_provisioned, wifi_provision_panel_observer_cb, qr_container, NULL);
    lv_subject_add_observer_obj(ui_wifi_expired, wifi_provision_panel_observer_cb, qr_container, NULL);

    // What you tap at the top, what you read in the middle, what cannot be undone at the bottom (owner, 2026-11-09)
    settings_page_t * page = settings_page_create("Wi-Fi");
    settings_row_toggle(page, "Wi-Fi", get_wifi_on, set_wifi_on, NULL);
    settings_row_action(page, NULL, "Set-up QR code", NULL, NULL, open_qr_page, NULL, NULL, false);
    settings_row_shown_when(page, setup_shown);
    settings_row_status(page, wifi_status_text, NULL);
    settings_row_status(page, wifi_name_text, NULL);
    settings_row_shown_when(page, connected_shown);
    settings_row_status(page, wifi_address_text, NULL);
    settings_row_shown_when(page, connected_shown);
    // The page's only setting is Wi-Fi on or off, so it has no Reset: Forget network is its reset
    settings_row_action(page, NULL, "Forget network", NULL, NULL, forget_network,
                        "Forget the Wi-Fi network? The unit waits to be set up again.", "Forget", true);
    settings_row_shown_when(page, forget_shown);
    return page;
}

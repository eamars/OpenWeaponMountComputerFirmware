#include "wifi_config.h"

#include "config_view.h"
#include "wifi_provision.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#define TAG "WiFiConfig"


static char wifi_status_str[64];

static void toggle_enable_wifi(lv_event_t *e) {
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool * state = lv_event_get_user_data(e);
    *state = lv_obj_has_state(sw, LV_STATE_CHECKED);
}


static void on_reset_provision_button_pressed(lv_event_t * e) {
    wifi_provision_reset();
    update_info_msg_box("WiFi Provisioning reset. Please re-provision the device.");
}

lv_obj_t * create_reset_provision_button(lv_obj_t * container, lv_event_cb_t reset_provision_cb) {
    lv_obj_t * reset_provision_button = lv_btn_create(container);

    // Save/reload Styling
    // lv_obj_add_flag(reset_provision_button, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_style_bg_image_src(reset_provision_button, LV_SYMBOL_WARNING, 0);
    lv_obj_set_width(reset_provision_button, lv_pct(40));
    lv_obj_set_height(reset_provision_button, 36);  // TODO: Find a better way to read the height from other widgets
    lv_obj_add_event_cb(reset_provision_button, reset_provision_cb, LV_EVENT_SINGLE_CLICKED, NULL);

    return container;
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
    config_item = create_reset_provision_button(container, on_reset_provision_button_pressed);

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
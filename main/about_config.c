#include "about_config.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "app_cfg.h"
#include "pmic_axp2101.h"

#define TAG "AboutConfig"


// FIXME: Make it generic (this is imported from ota_mode.c)
void on_reboot_button_pressed(lv_event_t * e);

 void on_power_off_button_pressed(lv_event_t * e) {
    ESP_LOGI(TAG, "Shutting down");

    pmic_power_off();
 }

lv_obj_t * create_about_config_line_item(lv_obj_t * parent, const char * name, const char * value) {
    lv_obj_t * container = lv_menu_cont_create(parent);

    lv_obj_t * label = lv_label_create(container);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_text_fmt(label, "%s: %s", name, value);

    // Set colour
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(container, lv_color_white(), 0);
    // lv_obj_set_style_bg_color(container, lv_palette_main(LV_PALETTE_YELLOW), 0);

    // Other styling
    lv_obj_set_width(container, lv_pct(100));  // extend to full width
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    
    return label;
}

lv_obj_t * create_about_config_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * sub_page_config_view = lv_menu_page_create(parent, NULL);

    // Add a reboot button
    lv_obj_t * reboot_button = lv_button_create(sub_page_config_view);
    lv_obj_set_style_bg_color(reboot_button, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(reboot_button, LV_PCT(80), LV_PCT(20));
    lv_obj_add_event_cb(reboot_button, on_reboot_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * reboot_button_label = lv_label_create(reboot_button);
    lv_label_set_text(reboot_button_label, "Reboot");
    lv_obj_center(reboot_button_label);
    lv_obj_set_style_text_font(reboot_button_label, &lv_font_montserrat_20, LV_PART_MAIN);

    // Add a power off button
#if USE_PMIC
    lv_obj_t * power_off_button = lv_button_create(sub_page_config_view);
    lv_obj_set_style_bg_color(power_off_button, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(power_off_button, LV_PCT(80), LV_PCT(20));
    lv_obj_add_event_cb(power_off_button, on_power_off_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    lv_obj_t * power_off_button_label = lv_label_create(power_off_button);
    lv_label_set_text(power_off_button_label, "Power Off");
    lv_obj_center(power_off_button_label);
    lv_obj_set_style_text_font(power_off_button_label, &lv_font_montserrat_20, LV_PART_MAIN);
#endif  // USE_PMIC

    // Get current app version information
    const esp_app_desc_t * app_desc = esp_app_get_description();
    const esp_partition_t * running = esp_ota_get_running_partition();

    // Current Firmware Information
    create_about_config_line_item(sub_page_config_view, "Partition", running->label);
    create_about_config_line_item(sub_page_config_view, "Version", app_desc->version);
    create_about_config_line_item(sub_page_config_view, "ESP-IDF", app_desc->idf_ver);
    create_about_config_line_item(sub_page_config_view, "Build", app_desc->date);

    lv_menu_separator_create(sub_page_config_view);

    // OTA information
    esp_partition_iterator_t it;
    const esp_partition_t* part;
    esp_app_desc_t ota_app_desc;

    // OTA0
    it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (it) {
        part = esp_partition_get(it);
        esp_ota_get_partition_description(part, &ota_app_desc);
        create_about_config_line_item(sub_page_config_view, "OTA0 Ver", ota_app_desc.version);
        esp_partition_iterator_release(it);
    }
    else {
        create_about_config_line_item(sub_page_config_view, "Partition0", "Not Found");
    }

    // OTA1
    it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (it) {
        part = esp_partition_get(it);
        esp_ota_get_partition_description(part, &ota_app_desc);
        create_about_config_line_item(sub_page_config_view, "OTA1 Ver", ota_app_desc.version);
        esp_partition_iterator_release(it);
    }
    else {
        create_about_config_line_item(sub_page_config_view, "Partition1", "Not Found");
    }


    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "About");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);

    return sub_page_config_view;
}
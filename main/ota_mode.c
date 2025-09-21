#include "ota_mode.h"
#include "low_power_mode.h"

#include "esp_lvgl_port.h"
#include "esp_log.h"


#define TAG "OTAMode"

static lv_obj_t * progress_label;
static lv_obj_t * progress_bar;

extern lv_obj_t * tile_ota_mode_view;
extern lv_obj_t * main_tileview;
extern lv_obj_t * last_tile;
extern lv_obj_t * default_tile;

void create_ota_mode_view(lv_obj_t * parent) {
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
    lv_obj_t * title_label = lv_label_create(container);
    lv_label_set_text(title_label, "OTA Update");

    // Put progress label
    progress_label = lv_label_create(container);
    lv_label_set_text(progress_label, "Progress: 0%");

    // Put progress bar
    progress_bar = lv_bar_create(container);
    lv_obj_set_size(progress_bar, lv_pct(80), 20);
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
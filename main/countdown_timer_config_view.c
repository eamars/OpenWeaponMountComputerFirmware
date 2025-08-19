#include "countdown_timer_config_view.h"
#include "countdown_timer.h"
#include "esp_log.h"
#include "system_config.h"

#define TAG "CountdownTimerConfigView"

const char * roller_minute = "0m\n1m\n2m\n3m\n4m\n5m\n6m\n7m\n8m\n9m\n10m";
const char * roller_second = "0s\n1s\n2s\n3s\n4s\n5s\n6s\n7s\n8s\n9s\n10s\n11s\n12s\n13s\n14s\n15s\n16s\n17s\n18s\n19s\n20s\n21s\n22s\n23s\n24s\n25s\n26s\n27s\n28s\n29s\n30s\n31s\n32s\n33s\n34s\n35s\n36s\n37s\n38s\n39s\n40s\n41s\n42s\n43s\n44s\n45s\n46s\n47s\n48s\n49s\n50s\n51s\n52s\n53s\n54s\n55s\n56s\n57s\n58s\n59s";


typedef struct {
    uint8_t minute;
    uint8_t second;
} timer_config_t;

typedef struct {
    int idx;
    lv_obj_t * preset_button;
    lv_obj_t * preset_label;
    timer_config_t timer_config;
} preset_t;

preset_t presets[2];
preset_t * selected_preset = NULL;
lv_obj_t * minute_roller = NULL;
lv_obj_t * second_roller = NULL;
lv_obj_t * parent_container = NULL;
lv_obj_t * top_container;
lv_obj_t * bottom_container;
extern countdown_timer_t countdown_timer;
extern system_config_t system_config;


static void update_label_with_timer_config(lv_obj_t * label, timer_config_t * config) {
    lv_label_set_text_fmt(label, "%d:%02d", config->minute, config->second);
}

static void enable_roller(lv_obj_t *roller, bool enabled) {
    if (enabled) {
        lv_obj_clear_state(roller, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(roller, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_SELECTED);

    } else {
        lv_obj_set_state(roller, LV_STATE_DISABLED, true);
        lv_obj_set_style_bg_color(roller, lv_palette_main(LV_PALETTE_GREY), LV_PART_SELECTED);
    }
}

static void preset_button_pressed_cb(lv_event_t * e) {
    lv_obj_t * preset_button = lv_event_get_target(e);
    preset_t * preset = lv_event_get_user_data(e);
    int total_presets = sizeof(presets) / sizeof(presets[0]);

    // Manually toggle the checked state to prevent other type of clicks other than the registered action
    if (lv_obj_has_state(preset_button, LV_STATE_CHECKED)) {
        // If the button is checked, then uncheck
        lv_obj_clear_state(preset_button, LV_STATE_CHECKED);
    }
    else {
        lv_obj_add_state(preset_button, LV_STATE_CHECKED);

        // If the current button is checked, then uncheck other buttons
        for (int i = 0; i < total_presets; i++) {
            if (i != preset->idx) {
                lv_obj_clear_state(presets[i].preset_button, LV_STATE_CHECKED);
            }
        }

        // Send it to the configurator
        lv_roller_set_selected(minute_roller, preset->timer_config.minute, LV_ANIM_ON);
        lv_roller_set_selected(second_roller, preset->timer_config.second, LV_ANIM_ON);

        // Register the selected preset
        selected_preset = preset;
    }

    // If no preset is selected, reset the rollers and disable the play button
    bool no_preset_selected = true;
    for (int i = 0; i < total_presets; i++) {
        if (lv_obj_get_state(presets[i].preset_button) & LV_STATE_CHECKED) {
            no_preset_selected = false;
            break;
        }
    }

    if (no_preset_selected) {
        lv_roller_set_selected(minute_roller, 0, LV_ANIM_ON);
        lv_roller_set_selected(second_roller, 0, LV_ANIM_ON);
        enable_roller(minute_roller, false);
        enable_roller(second_roller, false);

        // Reset the selected preset
        selected_preset = NULL;

        // Disable main widgets as well
        enable_countdown_timer_widget(false);
    }
    else{
        // Re enable the rollers
        enable_roller(minute_roller, true);
        enable_roller(second_roller, true);

        // Set the timer to the widget
        countdown_timer_update_time(&countdown_timer, (selected_preset->timer_config.minute * 60 + selected_preset->timer_config.second) * 1000);

        // Force the timer to reset
        countdown_timer_start(&countdown_timer);

        // Enable visibility
        enable_countdown_timer_widget(true);
    }
}


static void roller_update_event_cb(lv_event_t * e) {
    lv_obj_t * roller = lv_event_get_target(e);

    if (selected_preset) {
        // Generate current countdown time
        selected_preset->timer_config.minute = lv_roller_get_selected(minute_roller);
        selected_preset->timer_config.second = lv_roller_get_selected(second_roller);
        update_label_with_timer_config(selected_preset->preset_label, &selected_preset->timer_config);

        // If the preset is selected then the counter object needed to be updated with the new time
        countdown_timer_update_time(&countdown_timer, (selected_preset->timer_config.minute * 60 + selected_preset->timer_config.second) * 1000);
    }
}


void set_rotation_countdown_timer_config_view(lv_display_rotation_t display_rotation) {
    if (display_rotation == LV_DISPLAY_ROTATION_0 || display_rotation == LV_DISPLAY_ROTATION_180) {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(top_container, lv_pct(100), lv_pct(38));
        lv_obj_set_size(bottom_container, lv_pct(100), lv_pct(60));
    }
    else {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(top_container, lv_pct(50), lv_pct(100));
        lv_obj_set_size(bottom_container, lv_pct(48), lv_pct(100));
    }
}


void create_countdown_timer_config_view(lv_obj_t * parent) {
    memset(presets, 0, sizeof(presets));

    // Create parent container to house everything
    parent_container = lv_obj_create(parent);

    lv_obj_set_style_border_width(parent_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(parent_container, 2, 0);
    lv_obj_set_size(parent_container, lv_pct(100), lv_pct(100));

    lv_obj_set_scroll_dir(parent_container, LV_DIR_NONE);  // no scroll

    // Create two container to house each
    top_container = lv_obj_create(parent_container);
    bottom_container = lv_obj_create(parent_container);

    lv_obj_set_style_pad_all(top_container, 1, 0);
    lv_obj_set_style_border_width(top_container, 0, LV_PART_MAIN);

    // lv_obj_set_style_bg_color(top_container, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_bg_opa(top_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(top_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_container,
                      LV_FLEX_ALIGN_CENTER,  // main axis (row) center
                      LV_FLEX_ALIGN_CENTER,  // cross axis center
                      LV_FLEX_ALIGN_CENTER); // track cross axis center

    lv_obj_set_style_pad_all(bottom_container, 1, 0);
    lv_obj_set_style_border_width(bottom_container, 0, LV_PART_MAIN);

    // lv_obj_set_style_bg_color(bottom_container, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
    lv_obj_set_style_bg_opa(bottom_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(bottom_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottom_container,
                      LV_FLEX_ALIGN_CENTER,  // main axis (row) center
                      LV_FLEX_ALIGN_CENTER,  // cross axis center
                      LV_FLEX_ALIGN_CENTER); // track cross axis center

    set_rotation_countdown_timer_config_view(system_config.rotation);

    // Add roller to the top half for both minute and second
    minute_roller = lv_roller_create(top_container);
    lv_obj_set_size(minute_roller, lv_pct(45), lv_pct(100));
    lv_roller_set_options(minute_roller, roller_minute, LV_ROLLER_MODE_INFINITE);
    lv_obj_add_event_cb(minute_roller, roller_update_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_text_font(minute_roller, &lv_font_montserrat_20, 0);
    enable_roller(minute_roller, false);

    second_roller = lv_roller_create(top_container);
    lv_obj_set_size(second_roller, lv_pct(45), lv_pct(100));
    lv_roller_set_options(second_roller, roller_second, LV_ROLLER_MODE_INFINITE);
    lv_obj_add_event_cb(second_roller, roller_update_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_text_font(second_roller, &lv_font_montserrat_20, 0);
    enable_roller(second_roller, false);

    // Add two presets under the second container
    // Preset 1
    presets[0].idx = 0;
    presets[0].timer_config.minute = 2;
    presets[0].timer_config.second = 0;
    presets[0].preset_button = lv_btn_create(bottom_container);
    lv_obj_set_size(presets[0].preset_button, lv_pct(100), lv_pct(48));
    // lv_obj_set_style_bg_color(presets[0].preset_button, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
    lv_obj_remove_flag(presets[0].preset_button, LV_OBJ_FLAG_CHECKABLE);  // manual check
    lv_obj_add_event_cb(presets[0].preset_button, preset_button_pressed_cb, LV_EVENT_SHORT_CLICKED, &presets[0]);
    // Specify the colour for checked and unchecked
    lv_obj_set_style_bg_color(presets[0].preset_button, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(presets[0].preset_button, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);

    presets[0].preset_label = lv_label_create(presets[0].preset_button);
    update_label_with_timer_config(presets[0].preset_label, &presets[0].timer_config);
    lv_obj_center(presets[0].preset_label);
    lv_obj_set_style_text_font(presets[0].preset_label, &lv_font_montserrat_32, LV_PART_MAIN);

    // Preset 2
    presets[1].idx = 1;
    presets[1].timer_config.minute = 1;
    presets[1].timer_config.second = 30;
    presets[1].preset_button = lv_btn_create(bottom_container);
    lv_obj_set_size(presets[1].preset_button, lv_pct(100), lv_pct(48));
    // lv_obj_set_style_bg_color(presets[1].preset_button, lv_palette_main(LV_PALETTE_LIME), 0);
    lv_obj_remove_flag(presets[1].preset_button, LV_OBJ_FLAG_CHECKABLE);  // manual check
    lv_obj_add_event_cb(presets[1].preset_button, preset_button_pressed_cb, LV_EVENT_SHORT_CLICKED, &presets[1]);
    // Specify the colour for checked and unchecked
    lv_obj_set_style_bg_color(presets[1].preset_button, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(presets[1].preset_button, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);


    presets[1].preset_label = lv_label_create(presets[1].preset_button);
    update_label_with_timer_config(presets[1].preset_label, &presets[1].timer_config);
    lv_obj_center(presets[1].preset_label);
    lv_obj_set_style_text_font(presets[1].preset_label, &lv_font_montserrat_32, LV_PART_MAIN);

    // Set tile layout based on the screen rotation
    set_rotation_countdown_timer_config_view(system_config.rotation);
}


void countdown_timer_rotation_event_callback(lv_event_t * e) {
    set_rotation_countdown_timer_config_view(system_config.rotation);
}

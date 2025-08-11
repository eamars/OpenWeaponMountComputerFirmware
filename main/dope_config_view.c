#include "dope_config_view.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#define TAG "DopeConfigView"

// Major roller from 0 to 30
const char * major_roller_options = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30";
// Minor roller from 0 to 9
const char * minor_roller_options = ".0\n.1\n.2\n.3\n.4\n.5\n.6\n.7\n.8\n.9";

typedef struct {
    int idx;
    int dope_10;
    lv_obj_t * dope_item;
    lv_obj_t * dope_item_icon;
    lv_obj_t * dope_item_menu_label;
    bool enable;
} dope_data_t;

dope_data_t all_dope_data[DOPE_CONFIG_MAX_DOPE_ITEM] = {0};
lv_obj_t * dope_item_settings = NULL;
lv_obj_t * dope_item_settings_major_roller = NULL;
lv_obj_t * dope_item_settings_minor_roller = NULL;
lv_obj_t * dope_item_settings_enable_switch = NULL;
int current_selected_dope_item = 0;


void set_dope_item_settings_visibility(bool is_visible) {
    if (is_visible) {
        lv_obj_clear_flag(dope_item_settings, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(dope_item_settings, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_dope_item_settings(dope_data_t *data) {
    // Set title
    lv_obj_t *title = lv_msgbox_get_title(dope_item_settings);
    lv_label_set_text_fmt(title, "Target %c", data->idx + 'A');

    // Update the rollers with the current dope values
    lv_roller_set_selected(dope_item_settings_major_roller, data->dope_10 / 10, LV_ANIM_OFF);
    lv_roller_set_selected(dope_item_settings_minor_roller, data->dope_10 % 10, LV_ANIM_OFF);
    
    if (data->enable) {
        lv_obj_add_state(dope_item_settings_enable_switch, LV_STATE_CHECKED);
        lv_label_set_text(all_dope_data[data->idx].dope_item_icon, LV_SYMBOL_EYE_OPEN);
    } else {
        lv_obj_clear_state(dope_item_settings_enable_switch, LV_STATE_CHECKED);
        lv_label_set_text(all_dope_data[data->idx].dope_item_icon, LV_SYMBOL_EYE_CLOSE);
    }
}



static void apply_dope_item_settings(lv_event_t * e) {
    // ESP_LOGI(TAG, "Applying settings for dope item %d", data->idx);
    dope_data_t *data = &all_dope_data[current_selected_dope_item];

    // Read user input
    int dope_major_decimal = (int) lv_roller_get_selected(dope_item_settings_major_roller);
    int dope_minor_decimal = (int) lv_roller_get_selected(dope_item_settings_minor_roller);

    data->dope_10 = dope_major_decimal * 10 + dope_minor_decimal;
    data->enable = lv_obj_has_state(dope_item_settings_enable_switch, LV_STATE_CHECKED);

    // Update dope item title
    lv_label_set_text_fmt(data->dope_item_menu_label, "%d.%d", dope_major_decimal, dope_minor_decimal);

    // Update icon
    // lv_obj_t *icon_label = lv_obj_get_child(data->dope_item, 0);

    if (data->enable) {
        lv_obj_add_state(data->dope_item, LV_STATE_CHECKED);
        lv_label_set_text(data->dope_item_icon, LV_SYMBOL_EYE_OPEN);
    } else {
        lv_obj_clear_state(data->dope_item, LV_STATE_CHECKED);
        lv_label_set_text(data->dope_item_icon, LV_SYMBOL_EYE_CLOSE);
    }

    set_dope_item_settings_visibility(false);
}


static void open_edit_window(lv_event_t * e) {
    lv_obj_t * dope_item = lv_event_get_target(e);
    dope_data_t * dope_data = lv_obj_get_user_data(dope_item);

    // Update current selection
    current_selected_dope_item = dope_data->idx;

    // Populate the settings page and show it
    set_dope_item_settings(&all_dope_data[current_selected_dope_item]);
    set_dope_item_settings_visibility(true);
}


static void close_edit_window(lv_event_t * e) {
    set_dope_item_settings_visibility(false);
}

void create_dope_config_view(lv_obj_t * parent) {
    lv_obj_t * dope_list = lv_list_create(parent);
    memset(all_dope_data, 0, sizeof(all_dope_data));
    current_selected_dope_item = 0;

    // Set styling
    lv_obj_set_size(dope_list, lv_pct(100), lv_pct(100));
    lv_obj_center(dope_list);

    // Create dope items
    for (int i = 0; i < DOPE_CONFIG_MAX_DOPE_ITEM; i += 1) {
        // TODO: Implement load from EEPROM
        all_dope_data[i].idx = i;
        all_dope_data[i].dope_10 = 0; // Default value
        all_dope_data[i].enable = false;

        all_dope_data[i].dope_item = lv_list_add_button(dope_list, NULL, NULL);
        lv_obj_remove_flag(all_dope_data[i].dope_item, LV_OBJ_FLAG_CHECKABLE);  // Button is not checkable by the user, but controlled by the underlying state
        lv_obj_set_style_bg_color(all_dope_data[i].dope_item, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(all_dope_data[i].dope_item, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);

        // Set icon
        all_dope_data[i].dope_item_icon = lv_label_create(all_dope_data[i].dope_item);
        // lv_obj_align(all_dope_data[i].dope_item_icon, LV_ALIGN_LEFT_MID, 0, 0);

        // Set name and default
        lv_obj_t * target_idx_label = lv_label_create(all_dope_data[i].dope_item);
        lv_obj_set_style_text_font(target_idx_label, &lv_font_montserrat_20, 0);
        lv_label_set_text_fmt(target_idx_label, "%c", all_dope_data[i].idx + 'A');
        // lv_obj_align(target_idx_label, LV_ALIGN_LEFT_MID, 0, 0);

        all_dope_data[i].dope_item_menu_label = lv_label_create(all_dope_data[i].dope_item);
        lv_obj_set_style_text_font(all_dope_data[i].dope_item_menu_label, &lv_font_montserrat_20, 0);
        lv_label_set_text_fmt(all_dope_data[i].dope_item_menu_label, "%d.%d", all_dope_data[i].dope_10 / 10, all_dope_data[i].dope_10 % 10);
        // lv_obj_align(all_dope_data[i].dope_item_menu_label, LV_ALIGN_RIGHT_MID, 0, 0);

        // Set state
        if (all_dope_data[i].enable) {
            lv_obj_add_state(all_dope_data[i].dope_item, LV_STATE_CHECKED);
            lv_label_set_text(all_dope_data[i].dope_item_icon, LV_SYMBOL_EYE_OPEN);
        }
        else {
            lv_obj_clear_state(all_dope_data[i].dope_item, LV_STATE_CHECKED);
            lv_label_set_text(all_dope_data[i].dope_item_icon, LV_SYMBOL_EYE_CLOSE);
        }

        lv_obj_add_event_cb(all_dope_data[i].dope_item, open_edit_window, LV_EVENT_CLICKED, NULL);
        lv_obj_set_user_data(all_dope_data[i].dope_item, &all_dope_data[i]);
    }

    // Create per item settings
    dope_item_settings = lv_msgbox_create(parent);

    // Set styling
    lv_obj_set_size(dope_item_settings, lv_pct(100), lv_pct(100));
    lv_obj_center(dope_item_settings);

    // Add placheolder for title
    lv_msgbox_add_title(dope_item_settings, NULL);

    // main container
    // lv_obj_t * container = lv_obj_create(dope_item_settings);
    // lv_obj_set_size(container, lv_pct(100), lv_pct(85));
    // lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // // Remove left and right padding
    // lv_obj_set_style_pad_left(container, 0, 0);
    // lv_obj_set_style_pad_right(container, 0, 0);

    // A container for the rollers
    lv_obj_t * top_container = lv_obj_create(dope_item_settings);
    lv_obj_set_size(top_container, lv_pct(100), lv_pct(50));
    // lv_obj_set_style_bg_color(top_container, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(top_container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(top_container, 0, 0);  // remove all paddings for container

    dope_item_settings_major_roller = lv_roller_create(top_container);
    lv_roller_set_options(dope_item_settings_major_roller, major_roller_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_size(dope_item_settings_major_roller, lv_pct(45), lv_pct(100));
    lv_obj_set_style_text_font(dope_item_settings_major_roller, &lv_font_montserrat_32, 0);
    lv_obj_align(dope_item_settings_major_roller, LV_ALIGN_LEFT_MID, 0, 0);

    dope_item_settings_minor_roller = lv_roller_create(top_container);
    lv_roller_set_options(dope_item_settings_minor_roller, minor_roller_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_size(dope_item_settings_minor_roller, lv_pct(45), lv_pct(100));
    lv_obj_set_style_text_font(dope_item_settings_minor_roller, &lv_font_montserrat_32, 0);
    lv_obj_align(dope_item_settings_minor_roller, LV_ALIGN_RIGHT_MID, 0, 0);

    
    // A container for top widget group including a label and a switch
    lv_obj_t * mid_container = lv_obj_create(dope_item_settings);
    lv_obj_set_size(mid_container, lv_pct(100), lv_pct(20));
    // lv_obj_set_style_bg_color(mid_container, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(mid_container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(mid_container, 0, 0);  // remove all paddings for container

    lv_obj_t * enable_label = lv_label_create(mid_container);
    lv_label_set_text_static(enable_label, "Enable");
    lv_obj_set_style_text_font(enable_label, &lv_font_montserrat_20, 0);
    lv_obj_align(enable_label, LV_ALIGN_LEFT_MID, 0, 0);

    dope_item_settings_enable_switch = lv_switch_create(mid_container);
    lv_obj_set_size(dope_item_settings_enable_switch, lv_pct(40), lv_pct(60));
    lv_obj_align(dope_item_settings_enable_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    // Add Save and cancel button to the bottom container
    lv_obj_t * dope_item_settings_apply_button = lv_msgbox_add_footer_button(dope_item_settings, "Apply");
    lv_obj_set_flex_grow(dope_item_settings_apply_button, 1);

    lv_obj_t * dope_item_settings_cancel_button = lv_msgbox_add_footer_button(dope_item_settings, "Cancel");
    lv_obj_add_event_cb(dope_item_settings_cancel_button, close_edit_window, LV_EVENT_CLICKED, NULL);
    lv_obj_set_flex_grow(dope_item_settings_cancel_button, 1);

    // Set callback to the apply button
    lv_obj_add_event_cb(dope_item_settings_apply_button, apply_dope_item_settings, LV_EVENT_CLICKED, NULL);

    // Set hide by default
    set_dope_item_settings_visibility(false);
}

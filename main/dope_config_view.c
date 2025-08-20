#include "dope_config_view.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_check.h"
#include "nvs.h"
#include "system_config.h"
#include "common.h"

#define TAG "DopeConfigView"
#define DOPE_CONFIG_NAMESPACE "DOPE"

// Major roller from 0 to 30
const char * major_roller_options = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30";
// Minor roller from 0 to 9
const char * minor_roller_options = ".0\n.1\n.2\n.3\n.4\n.5\n.6\n.7\n.8\n.9";

extern system_config_t system_config;



// Data structure used to store and read dope information
typedef struct {
    uint32_t crc32;
    int dope10;
    bool enable;
} nvs_dope_data_t;

typedef struct {
    int idx;
    int dope_10;
    char target_identifier[4];
    char dope_label_text[8];
    lv_obj_t * dope_item;
    lv_obj_t * dope_item_icon;
    lv_obj_t * dope_item_menu_label;
    lv_obj_t * dope_card_view;
    bool enable;
} dope_data_t;

// dope_data_t all_dope_data[DOPE_CONFIG_MAX_DOPE_ITEM] = {0};
dope_data_t * all_dope_data = NULL;
lv_obj_t * dope_item_settings = NULL;
lv_obj_t * dope_item_settings_major_roller = NULL;
lv_obj_t * dope_item_settings_minor_roller = NULL;
lv_obj_t * dope_item_settings_enable_switch = NULL;
int current_selected_dope_item = 0;

// View to be created showing enabled dope item
lv_obj_t * dope_card_list = NULL;

// Dope settings
static lv_obj_t * parent_container = NULL;
static lv_obj_t * top_container = NULL;
static lv_obj_t * bottom_container = NULL;
static lv_obj_t * nested_button_container = NULL;

// Message box to show the confirm of saved data
static lv_obj_t * msg_box;
static lv_obj_t * msg_box_label;

// Forward declaration
lv_obj_t * create_dope_card(lv_obj_t *parent, dope_data_t *dope_data);
esp_err_t save_dope_config();
esp_err_t load_dope_config();


const nvs_dope_data_t nvs_dope_data_default = {
    .dope10 = 0,
    .enable = false
};


static void on_msg_box_ok_button_clicked(lv_event_t *e) {
    // hide
    lv_obj_add_flag(msg_box, LV_OBJ_FLAG_HIDDEN);
}


static void create_info_msg_box(lv_obj_t *parent) {
    // Create a message box to be called by its content
    msg_box = lv_msgbox_create(parent);
    lv_obj_set_size(msg_box, lv_pct(100), lv_pct(40));
    msg_box_label = lv_msgbox_add_text(msg_box, "This is a message box");
    lv_obj_t * btn = lv_msgbox_add_footer_button(msg_box, "OK");
    lv_obj_add_event_cb(btn, on_msg_box_ok_button_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_align(msg_box, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(msg_box, 1, 0);

    // Set hidden
    lv_obj_add_flag(msg_box, LV_OBJ_FLAG_HIDDEN);
}

static void update_info_msg_box(const char * text) {
    lv_label_set_text(msg_box_label, text);

    lv_obj_clear_flag(msg_box, LV_OBJ_FLAG_HIDDEN);
}

static void on_save_button_pressed(lv_event_t * e) {
    // Save the list to FLASH
    ESP_ERROR_CHECK(save_dope_config());

    // Display the message box
    update_info_msg_box("DOPE Saved");
}


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
    lv_label_set_text_fmt(title, "Target %s", data->target_identifier);

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

    // Update dope label (under multiple views due the sharing memory)
    snprintf(data->dope_label_text, sizeof(data->dope_label_text), "%d.%d", dope_major_decimal, dope_minor_decimal);

    if (data->enable) {
        lv_obj_add_state(data->dope_item, LV_STATE_CHECKED);
        lv_label_set_text(data->dope_item_icon, LV_SYMBOL_EYE_OPEN);

        // Update the visibility of the dope card too
        lv_obj_clear_flag(data->dope_card_view, LV_OBJ_FLAG_HIDDEN);

        // show the top level card list
        lv_obj_clear_flag(dope_card_list, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_state(data->dope_item, LV_STATE_CHECKED);
        lv_label_set_text(data->dope_item_icon, LV_SYMBOL_EYE_CLOSE);

        // Update the visibility of the dope card too
        lv_obj_add_flag(data->dope_card_view, LV_OBJ_FLAG_HIDDEN);

        // If no card is visible then disable the dope_card_list too
        bool is_all_card_disabled = true;
        for (int i = 0; i < DOPE_CONFIG_MAX_DOPE_ITEM; i++) {
            if (all_dope_data[i].enable) {
                is_all_card_disabled = false;
                break;
            }
        }
        if (is_all_card_disabled) {
            lv_obj_add_flag(dope_card_list, LV_OBJ_FLAG_HIDDEN);
        }
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



esp_err_t load_dope_config() {
    esp_err_t err;

    // Read configuration from NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(DOPE_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", DOPE_CONFIG_NAMESPACE);

    // Read each dope item configuration
    for (int i = 0; i < DOPE_CONFIG_MAX_DOPE_ITEM; i += 1) {
        nvs_dope_data_t dope_data = {0};
        size_t required_size = sizeof(dope_data);

        char key[NVS_KEY_NAME_MAX_SIZE - 1];
        memset(key, 0, sizeof(key));
        snprintf(key, NVS_KEY_NAME_MAX_SIZE - 1, "D%d", i);
        err = nvs_get_blob(handle, key, &dope_data, &required_size);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // If not found, use default values
            ESP_LOGI(TAG, "Dope item %d not found in NVS, using default values", i);
            memcpy(&dope_data, &nvs_dope_data_default, sizeof(nvs_dope_data_default));

            // Calculate CRC
            dope_data.crc32 = crc32_wrapper(&dope_data, sizeof(dope_data), sizeof(dope_data.crc32));

            // Write back to NVS
            ESP_RETURN_ON_ERROR(nvs_set_blob(handle, key, &dope_data, required_size), TAG, "Failed to write NVS blob");
            ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");

        } else if (err != ESP_OK) {
            ESP_RETURN_ON_ERROR(err, TAG, "Failed to read NVS blob");
        }

        // Verify CRC
        uint32_t crc32 = crc32_wrapper(&dope_data, sizeof(dope_data), sizeof(dope_data.crc32));

        if (crc32 != dope_data.crc32) {
            ESP_LOGW(TAG, "Dope item %d CRC mismatch, will use default values. Expected %p, got %p", i, dope_data.crc32, crc32);
            memcpy(&dope_data, &nvs_dope_data_default, sizeof(nvs_dope_data_default));
        }
        else {
            // Apply the loaded configuration
            all_dope_data[i].idx = i;
            all_dope_data[i].dope_10 = dope_data.dope10;
            all_dope_data[i].enable = dope_data.enable;
        }
    }

    nvs_close(handle);
    return ESP_OK;
}


esp_err_t save_dope_config() {
    esp_err_t err;

    // Write configuration to NVS
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(DOPE_CONFIG_NAMESPACE, NVS_READWRITE, &handle), TAG, "Failed to open NVS namespace %s", DOPE_CONFIG_NAMESPACE);

    // Write each dope item configuration
    for (int i = 0; i < DOPE_CONFIG_MAX_DOPE_ITEM; i += 1) {
        nvs_dope_data_t dope_data = {0};
        size_t required_size = sizeof(dope_data);

        // Populate dope_data from all_dope_data
        dope_data.dope10 = all_dope_data[i].dope_10;
        dope_data.enable = all_dope_data[i].enable;

        // Calculate CRC
        dope_data.crc32 = crc32_wrapper(&dope_data, sizeof(dope_data), sizeof(dope_data.crc32));

        char key[NVS_KEY_NAME_MAX_SIZE - 1];
        memset(key, 0, sizeof(key));
        snprintf(key, NVS_KEY_NAME_MAX_SIZE - 1, "D%d", i);
        
        ESP_RETURN_ON_ERROR(nvs_set_blob(handle, key, &dope_data, required_size), TAG, "Failed to write NVS blob");
    }

    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "Failed to commit NVS changes");

    nvs_close(handle);

    return ESP_OK;
}


void set_rotation_dope_card_config_view(lv_display_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(top_container, lv_pct(100), lv_pct(50));
        lv_obj_set_size(bottom_container, lv_pct(100), lv_pct(45));
    }
    else {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(top_container, lv_pct(50), lv_pct(100));
        lv_obj_set_size(bottom_container, lv_pct(50), lv_pct(100));
    }
}


void create_dope_config_msgbox(lv_obj_t * parent) {
    
    // Create per item settings
    dope_item_settings = lv_msgbox_create(parent);
    lv_obj_t * close_button = lv_msgbox_add_close_button(dope_item_settings);

    // Remove the old event
    lv_obj_remove_event(close_button, 0);
    // Add the new event
    lv_obj_add_event_cb(close_button, close_edit_window, LV_EVENT_CLICKED, NULL);


    // Add placheolder for title
    lv_msgbox_add_title(dope_item_settings, NULL);

    // Set styling
    lv_obj_set_size(dope_item_settings, lv_pct(100), lv_pct(100));
    lv_obj_center(dope_item_settings);
    lv_obj_set_scroll_dir(dope_item_settings, LV_DIR_NONE);  // no scroll

    // Fill the full msg box
    parent_container = lv_obj_create(dope_item_settings);
    lv_obj_set_size(parent_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(parent_container, 2, LV_PART_MAIN);
    lv_obj_center(parent_container);

    // lv_obj_set_style_bg_opa(parent_container, LV_OPA_COVER, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(parent_container, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

    lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent_container,
                      LV_FLEX_ALIGN_START,  // main axis (row) center
                      LV_FLEX_ALIGN_CENTER,  // cross axis center
                      LV_FLEX_ALIGN_CENTER); // track cross axis center

    // Set styling
    lv_obj_set_scroll_dir(parent_container, LV_DIR_NONE);  // no scroll


    // A container for the rollers
    top_container = lv_obj_create(parent_container);

    lv_obj_set_style_pad_all(top_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_bg_opa(top_container, LV_OPA_COVER, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(top_container, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);

    // Object within will grow horizontally
    lv_obj_set_flex_flow(top_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_container,
                      LV_FLEX_ALIGN_CENTER,  // main axis (row) center
                      LV_FLEX_ALIGN_CENTER,  // cross axis center
                      LV_FLEX_ALIGN_CENTER); // track cross axis center

    // A container for the enable label and apply/cancel buttons
    bottom_container = lv_obj_create(parent_container);

    lv_obj_set_style_pad_all(bottom_container, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_bg_opa(bottom_container, LV_OPA_COVER, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(bottom_container, lv_palette_main(LV_PALETTE_YELLOW), 0);

    // Object within will grow vertically
    lv_obj_set_flex_flow(bottom_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottom_container,
                      LV_FLEX_ALIGN_START,  // main axis (column) center
                      LV_FLEX_ALIGN_CENTER,  // cross axis center
                      LV_FLEX_ALIGN_CENTER); // track cross axis center

    
    // Add objects to top container
    dope_item_settings_major_roller = lv_roller_create(top_container);
    lv_roller_set_options(dope_item_settings_major_roller, major_roller_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_size(dope_item_settings_major_roller, lv_pct(45), lv_pct(100));
    lv_obj_set_style_text_font(dope_item_settings_major_roller, &lv_font_montserrat_32, 0);

    dope_item_settings_minor_roller = lv_roller_create(top_container);
    lv_roller_set_options(dope_item_settings_minor_roller, minor_roller_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_set_size(dope_item_settings_minor_roller, lv_pct(45), lv_pct(100));
    lv_obj_set_style_text_font(dope_item_settings_minor_roller, &lv_font_montserrat_32, 0);
    
    // A container for top widget group including a label and a switch
    lv_obj_t * enable_option_container = lv_obj_create(bottom_container);
    // lv_obj_set_align(enable_option_container, LV_ALIGN_TOP_MID);
    lv_obj_set_size(enable_option_container, lv_pct(100), lv_pct(30));
    lv_obj_set_flex_flow(enable_option_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_option_container,
                      LV_FLEX_ALIGN_SPACE_BETWEEN,  // main axis (row) center
                      LV_FLEX_ALIGN_CENTER,  // cross axis center
                      LV_FLEX_ALIGN_CENTER); // track cross axis center

    lv_obj_set_style_pad_all(enable_option_container, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(enable_option_container, 0, LV_PART_MAIN);

    lv_obj_t * enable_label = lv_label_create(enable_option_container);
    lv_label_set_text_static(enable_label, "Enable");
    lv_obj_set_style_text_font(enable_label, &lv_font_montserrat_20, 0);

    dope_item_settings_enable_switch = lv_switch_create(enable_option_container);
    lv_obj_set_width(dope_item_settings_enable_switch, lv_pct(40));
    lv_switch_set_orientation(dope_item_settings_enable_switch, LV_SWITCH_ORIENTATION_HORIZONTAL);


    // Add Save and cancel button to the bottom container
    lv_obj_t * dope_item_settings_apply_button = lv_button_create(bottom_container);
    lv_obj_t * apply_label = lv_label_create(dope_item_settings_apply_button);
    lv_obj_set_style_text_font(apply_label, &lv_font_montserrat_20, 0);
    lv_label_set_text(apply_label, "Apply");

    // Set rotation for the dope card config view
    set_rotation_dope_card_config_view(system_config.rotation);

    // Set callback to the apply button
    lv_obj_add_event_cb(dope_item_settings_apply_button, apply_dope_item_settings, LV_EVENT_CLICKED, NULL);

    // Set hide by default
    set_dope_item_settings_visibility(false);
}


void create_dope_config_view(lv_obj_t * parent) {
    // Allocate memory for dope data
    all_dope_data = heap_caps_calloc(DOPE_CONFIG_MAX_DOPE_ITEM, sizeof(dope_data_t), MALLOC_CAP_DEFAULT);
    if (!all_dope_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for dope data");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Read from NVS
    ESP_ERROR_CHECK(load_dope_config());

    lv_obj_t * dope_list = lv_list_create(parent);
    current_selected_dope_item = 0;
    
    // Set styling
    lv_obj_set_size(dope_list, lv_pct(100), lv_pct(100));
    lv_obj_center(dope_list);

    bool is_all_card_disabled = true;

    // Create dope items
    for (int i = 0; i < DOPE_CONFIG_MAX_DOPE_ITEM; i += 1) {
        // The data is already pre-populated by load_dope_config();

        all_dope_data[i].dope_item = lv_list_add_button(dope_list, NULL, NULL);
        lv_obj_remove_flag(all_dope_data[i].dope_item, LV_OBJ_FLAG_CHECKABLE);  // Button is not checkable by the user, but controlled by the underlying state
        lv_obj_set_style_bg_color(all_dope_data[i].dope_item, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(all_dope_data[i].dope_item, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);

        // Set icon
        all_dope_data[i].dope_item_icon = lv_label_create(all_dope_data[i].dope_item);
            // lv_obj_align(all_dope_data[i].dope_item_icon, LV_ALIGN_LEFT_MID, 0, 0);

        // Set name and default
        snprintf(all_dope_data[i].target_identifier, sizeof(all_dope_data[i].target_identifier), "%c", 'A' + i);
        lv_obj_t * target_idx_label = lv_label_create(all_dope_data[i].dope_item);
        lv_obj_set_style_text_font(target_idx_label, &lv_font_montserrat_20, 0);
        lv_label_set_text_static(target_idx_label, all_dope_data[i].target_identifier);
        // lv_obj_align(target_idx_label, LV_ALIGN_LEFT_MID, 0, 0);

        all_dope_data[i].dope_item_menu_label = lv_label_create(all_dope_data[i].dope_item);
        lv_obj_set_style_text_font(all_dope_data[i].dope_item_menu_label, &lv_font_montserrat_20, 0);
        snprintf(all_dope_data[i].dope_label_text, sizeof(all_dope_data[i].dope_label_text), "%d.%d", (int8_t) (all_dope_data[i].dope_10 / 10),  (int8_t) (all_dope_data[i].dope_10 % 10));
        lv_label_set_text_static(all_dope_data[i].dope_item_menu_label, all_dope_data[i].dope_label_text);
        // lv_obj_align(all_dope_data[i].dope_item_menu_label, LV_ALIGN_RIGHT_MID, 0, 0);

        // create corresponding dope card at the digital level view screen
        all_dope_data[i].dope_card_view = create_dope_card(dope_card_list, &all_dope_data[i]);
        // Set default based on the information
        if (all_dope_data[i].enable) {
            is_all_card_disabled = false;
            lv_obj_add_state(all_dope_data[i].dope_item, LV_STATE_CHECKED);
            lv_label_set_text(all_dope_data[i].dope_item_icon, LV_SYMBOL_EYE_OPEN);
            lv_obj_clear_flag(all_dope_data[i].dope_card_view, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_state(all_dope_data[i].dope_item, LV_STATE_CHECKED);
            lv_label_set_text(all_dope_data[i].dope_item_icon, LV_SYMBOL_EYE_CLOSE);
            lv_obj_add_flag(all_dope_data[i].dope_card_view, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_add_event_cb(all_dope_data[i].dope_item, open_edit_window, LV_EVENT_CLICKED, NULL);
        lv_obj_set_user_data(all_dope_data[i].dope_item, &all_dope_data[i]);
    }

    // Set the default state of the dope card view on the digital level view
    if ((dope_card_list != NULL) && is_all_card_disabled) {
        lv_obj_add_flag(dope_card_list, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(dope_card_list, LV_OBJ_FLAG_HIDDEN);
    }

    // Append a button to the end of list to save to FLASH
    lv_obj_t * save_button = lv_btn_create(dope_list);
    lv_obj_t * label = lv_label_create(save_button);
    lv_obj_center(label);
    lv_label_set_text(label, LV_SYMBOL_SAVE " Save");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_size(save_button, lv_pct(100), lv_pct(30));
    lv_obj_add_event_cb(save_button, on_save_button_pressed, LV_EVENT_SINGLE_CLICKED, NULL);

    // Create a message box to display information and set to hidden by default
    create_info_msg_box(parent);

    // Create dope configuration dialog
    create_dope_config_msgbox(parent);
}



lv_obj_t * create_dope_card(lv_obj_t *parent, dope_data_t *dope_data) {

    lv_obj_t * column_layout = lv_obj_create(parent);

    lv_obj_set_size(column_layout, 70, 55);

    lv_obj_set_style_pad_top(column_layout, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(column_layout, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(column_layout, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(column_layout, 0, LV_PART_MAIN);

    // set transparent background and border
    lv_obj_set_style_bg_opa(column_layout, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(column_layout, 0, LV_PART_MAIN);

    // Disable scroll 
    lv_obj_remove_flag(column_layout, LV_OBJ_FLAG_SCROLLABLE);

    // Add target identifier
    lv_obj_t *target_identifer_label = lv_label_create(column_layout);
    lv_label_set_text_static(target_identifer_label, dope_data->target_identifier);
    lv_obj_set_style_text_font(target_identifer_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(target_identifer_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(target_identifer_label);
    lv_obj_align(target_identifer_label, LV_ALIGN_TOP_MID, 0, 0);


    lv_obj_t *dope_label = lv_label_create(column_layout);
    lv_label_set_text_static(dope_label, dope_data->dope_label_text);
    lv_obj_set_style_text_color(dope_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(dope_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(dope_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Set pointer to the label as the user data
    lv_obj_set_user_data(column_layout, dope_label);

    return column_layout;
}


void set_rotation_dope_card_list(lv_display_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        // For 0 and 180 degrees, use the default layout
        lv_obj_set_flex_flow(dope_card_list, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(dope_card_list, lv_pct(100), 80);  // a fixed height object
        lv_obj_set_scroll_dir(dope_card_list, LV_DIR_HOR);  // only horizontal scroll
        lv_obj_set_flex_align(dope_card_list,
                LV_FLEX_ALIGN_START,  // main axis (row) center
                LV_FLEX_ALIGN_CENTER,  // cross axis center
                LV_FLEX_ALIGN_CENTER); // track cross axis center
        lv_obj_align(dope_card_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
        // // For 90 and 270 degrees, switch to column layout
        // lv_obj_set_flex_flow(dope_card_list, LV_FLEX_FLOW_COLUMN);
        // lv_obj_set_size(dope_card_list, 80, lv_pct(100));  // a fixed width object
        // lv_obj_set_scroll_dir(dope_card_list, LV_DIR_VER);  // only vertical scroll
        // lv_obj_set_flex_align(dope_card_list,
        //         LV_FLEX_ALIGN_START,  // main axis (column) center
        //         LV_FLEX_ALIGN_CENTER,  // cross axis center
        //         LV_FLEX_ALIGN_CENTER); // track cross axis center
        // lv_obj_align(dope_card_list, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_set_flex_flow(dope_card_list, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(dope_card_list, lv_pct(100), 80);  // a fixed height object
        lv_obj_set_scroll_dir(dope_card_list, LV_DIR_HOR);  // only horizontal scroll
        lv_obj_set_flex_align(dope_card_list,
                LV_FLEX_ALIGN_START,  // main axis (row) center
                LV_FLEX_ALIGN_CENTER,  // cross axis center
                LV_FLEX_ALIGN_CENTER); // track cross axis center
        lv_obj_align(dope_card_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}


lv_obj_t * create_dope_card_list_widget(lv_obj_t * parent) {
    if (!dope_card_list) {
        dope_card_list = lv_obj_create(parent);
    }

    set_rotation_dope_card_list(system_config.rotation);

    lv_obj_update_snap(dope_card_list, LV_ANIM_ON);
    lv_obj_set_scroll_snap_x(dope_card_list, LV_SCROLL_SNAP_CENTER); // optional snap
    lv_obj_set_scrollbar_mode(dope_card_list, LV_SCROLLBAR_MODE_OFF);  // hide scrollbar
    lv_obj_send_event(dope_card_list, LV_EVENT_SCROLL, NULL);  // focus on the first item

    // set transparent background and border
    lv_obj_set_style_bg_opa(dope_card_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dope_card_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_bg_opa(dope_card_list, LV_OPA_COVER, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(dope_card_list, lv_palette_main(LV_PALETTE_YELLOW), 0);

    // Set invisible by default
    lv_obj_add_flag(dope_card_list, LV_OBJ_FLAG_HIDDEN);


    return dope_card_list;
}


void dope_config_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_dope_card_config_view(system_config.rotation);
}
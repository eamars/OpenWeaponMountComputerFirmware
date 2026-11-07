#include "countdown_timer_config_view.h"
#include "countdown_timer.h"
#include "system_config.h"
#include "app_cfg.h"
#include "esp_log.h"
#include "nvs.h"
#include "common.h"
#include "esp_check.h"
#include "config_view.h"
#include "digital_level_view.h"
#include "display_shape.h"
#include "main_tileview.h"

#include <string.h>


#define TAG "CountdownTimerConfigView"
#define NVS_NAMESPACE "CT"

// Layout of the tile and the editor (ADR-006): an 8 px margin, 20 px between targets, corners concentric with
// the panel's
#define TIMER_TILE_GAP_PX 20
#define TIMER_EDITOR_TIME_ROW_PX 96     // portrait: the row that shows the time

#define TIMER_PRESET_MAX_S (10 * 60)

// Holding minus or plus repeats one step every long-press repeat (100 ms); after this many repeats, each repeat
// moves this many steps
#define TIMER_EDITOR_FAST_AFTER_REPEATS 10
#define TIMER_EDITOR_FAST_STEPS 3


HEAPS_CAPS_ATTR countdown_timer_config_t countdown_timer_config;

const countdown_timer_config_t countdown_timer_config_default = {
    // The two common PRS par times and the squad's walk-through (ADR-006)
    .preset_s = {90, 120, 60},
    .step_s = 5,
    .start_on_first_shot = true,
};

// The step choices, as the settings page lists them
static const uint8_t timer_step_options_s[] = {5, 10, 15};
static const char * timer_step_options = "5 s\n10 s\n15 s";


typedef struct {
    lv_obj_t * button;
    lv_obj_t * label;
} timer_preset_widget_t;

static lv_obj_t * parent_container = NULL;
static timer_preset_widget_t preset_widgets[TIMER_PRESET_COUNT];
static int selected_preset = -1;    // the preset the timer is armed with; -1: none, the timer is hidden

static lv_obj_t * editor = NULL;
static lv_obj_t * editor_time_label = NULL;
static lv_obj_t * editor_minus_button = NULL;
static lv_obj_t * editor_plus_button = NULL;
static lv_obj_t * editor_done_button = NULL;
static lv_obj_t * editor_minus_label = NULL;
static lv_obj_t * editor_plus_label = NULL;
static int editor_preset = -1;      // the preset being edited; -1: the editor is closed
static uint16_t editor_value_s = 0;
static uint32_t editor_repeats = 0;

static lv_obj_t * timer_config_page = NULL;


static void format_time(char * buf, size_t size, uint16_t seconds) {
    snprintf(buf, size, "%u:%02u", (unsigned) (seconds / 60), (unsigned) (seconds % 60));
}


esp_err_t save_countdown_timer_config() {
    return save_config(NVS_NAMESPACE, &countdown_timer_config, sizeof(countdown_timer_config));
}


esp_err_t load_countdown_timer_config() {
    esp_err_t ret = load_config(NVS_NAMESPACE, &countdown_timer_config, &countdown_timer_config_default,
                                sizeof(countdown_timer_config));

    // A step that is not one of the choices (never saved by this firmware) falls back to the default
    bool step_known = false;
    for (size_t i = 0; i < sizeof(timer_step_options_s); i++) {
        step_known |= countdown_timer_config.step_s == timer_step_options_s[i];
    }
    if (!step_known) {
        countdown_timer_config.step_s = countdown_timer_config_default.step_s;
    }

    return ret;
}


bool countdown_timer_start_on_first_shot() {
    return countdown_timer_config.start_on_first_shot;
}


/*
----------------------------------------------------
The tile: three presets
----------------------------------------------------
*/

// LVGL lock held. Each preset shows its time; the armed one is checked
static void refresh_preset_widgets() {
    for (int i = 0; i < TIMER_PRESET_COUNT; i++) {
        if (preset_widgets[i].label == NULL) {
            continue;
        }
        char text[8];
        format_time(text, sizeof(text), countdown_timer_config.preset_s[i]);
        lv_label_set_text(preset_widgets[i].label, text);

        if (i == selected_preset) {
            lv_obj_add_state(preset_widgets[i].button, LV_STATE_CHECKED);
        }
        else {
            lv_obj_remove_state(preset_widgets[i].button, LV_STATE_CHECKED);
        }
    }
}


// Arm the timer with a preset: the time is loaded and held until a tap on the timer or the first shot starts it
static void arm_preset(int preset) {
    selected_preset = preset;
    countdown_timer_update_time(&countdown_timer, countdown_timer_config.preset_s[preset] * 1000);
    enable_countdown_timer_widget(true);
    refresh_preset_widgets();
}


static void disarm_timer() {
    selected_preset = -1;
    countdown_timer_pause(&countdown_timer);
    enable_countdown_timer_widget(false);
    refresh_preset_widgets();
}


static void preset_clicked_cb(lv_event_t * e) {
    int preset = (int) (intptr_t) lv_event_get_user_data(e);

    if (preset == selected_preset) {
        // Tapping the armed preset again stops and hides the timer
        disarm_timer();
        return;
    }

    arm_preset(preset);

    // Back to the level, where the timer shows; at the beep a tap on it, or the first shot, starts it
    lv_tileview_set_tile(main_tileview, default_tile, LV_ANIM_ON);
}


static void open_editor(int preset);

static void preset_long_pressed_cb(lv_event_t * e) {
    open_editor((int) (intptr_t) lv_event_get_user_data(e));
}


static lv_obj_t * create_timer_button(lv_obj_t * parent, lv_palette_t checked_palette) {
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_set_style_radius(button, display_concentric_radius(DISPLAY_MARGIN_PX), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_palette_main(checked_palette), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_PRESSED);
    return button;
}


static lv_obj_t * create_timer_label(lv_obj_t * parent, const lv_font_t * font) {
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_center(label);
    return label;
}


/*
----------------------------------------------------
The editor: minus and plus one step, and Done
----------------------------------------------------
*/

static void editor_show_value() {
    char text[8];
    format_time(text, sizeof(text), editor_value_s);
    lv_label_set_text(editor_time_label, text);
}


static void editor_show_step() {
    lv_label_set_text_fmt(editor_minus_label, "-%u s", (unsigned) countdown_timer_config.step_s);
    lv_label_set_text_fmt(editor_plus_label, "+%u s", (unsigned) countdown_timer_config.step_s);
}


// One step up or down; a time off the step snaps to it first (1:37 with 10 s: plus 1:40, minus 1:30)
static void editor_step(int direction, int steps) {
    uint32_t step = countdown_timer_config.step_s;
    int32_t value = editor_value_s;

    for (int i = 0; i < steps; i++) {
        if (direction > 0) {
            value = (value / step + 1) * step;
        }
        else {
            value = ((value + step - 1) / step - 1) * step;
        }
    }

    if (value < (int32_t) step) {
        value = step;
    }
    if (value > TIMER_PRESET_MAX_S) {
        value = TIMER_PRESET_MAX_S;
    }

    editor_value_s = (uint16_t) value;
    editor_show_value();
}


static void editor_step_event_cb(lv_event_t * e) {
    int direction = (int) (intptr_t) lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        editor_repeats = 0;
        editor_step(direction, 1);
    }
    else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        editor_repeats++;
        editor_step(direction, editor_repeats > TIMER_EDITOR_FAST_AFTER_REPEATS ? TIMER_EDITOR_FAST_STEPS : 1);
    }
}


// Close the editor, keeping the edit: Done, and leaving the tile. Saves only when the time changed
static void close_editor() {
    if (editor_preset < 0) {
        return;
    }

    int preset = editor_preset;
    editor_preset = -1;
    lv_obj_add_flag(editor, LV_OBJ_FLAG_HIDDEN);

    if (editor_value_s == countdown_timer_config.preset_s[preset]) {
        return;
    }

    countdown_timer_config.preset_s[preset] = editor_value_s;
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_countdown_timer_config());

    // An armed timer takes the new time, unless it is running
    if (preset == selected_preset && get_countdown_timer_state(&countdown_timer) != COUNTDOWN_TIMER_RUN) {
        countdown_timer_update_time(&countdown_timer, editor_value_s * 1000);
    }
    refresh_preset_widgets();
}


static void editor_done_cb(lv_event_t * e) {
    close_editor();
}


static void open_editor(int preset) {
    editor_preset = preset;
    editor_value_s = countdown_timer_config.preset_s[preset];
    editor_show_value();
    editor_show_step();
    lv_obj_remove_flag(editor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(editor);
}


static void create_editor(lv_obj_t * parent) {
    editor = lv_obj_create(parent);
    lv_obj_remove_style_all(editor);
    lv_obj_set_size(editor, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(editor, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(editor, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(editor, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(editor, TIMER_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(editor, TIMER_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_remove_flag(editor, LV_OBJ_FLAG_SCROLLABLE);
    // A drag on the editor's buttons must not swipe the tile away
    lv_obj_remove_flag(editor, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(editor, LV_OBJ_FLAG_CLICKABLE);  // taps between the buttons stay in the editor
    lv_obj_add_flag(editor, LV_OBJ_FLAG_HIDDEN);

    editor_time_label = create_timer_label(editor, &lv_font_montserrat_48);

    editor_minus_button = create_timer_button(editor, LV_PALETTE_ORANGE);
    editor_minus_label = create_timer_label(editor_minus_button, &lv_font_montserrat_32);
    lv_obj_add_event_cb(editor_minus_button, editor_step_event_cb, LV_EVENT_PRESSED, (void *) (intptr_t) -1);
    lv_obj_add_event_cb(editor_minus_button, editor_step_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *) (intptr_t) -1);

    editor_plus_button = create_timer_button(editor, LV_PALETTE_ORANGE);
    editor_plus_label = create_timer_label(editor_plus_button, &lv_font_montserrat_32);
    lv_obj_add_event_cb(editor_plus_button, editor_step_event_cb, LV_EVENT_PRESSED, (void *) (intptr_t) 1);
    lv_obj_add_event_cb(editor_plus_button, editor_step_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *) (intptr_t) 1);

    editor_done_button = create_timer_button(editor, LV_PALETTE_ORANGE);
    lv_obj_set_style_bg_color(editor_done_button, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), LV_PART_MAIN);
    lv_obj_t * done_label = create_timer_label(editor_done_button, &lv_font_montserrat_32);
    lv_label_set_text(done_label, "Done");
    lv_obj_add_event_cb(editor_done_button, editor_done_cb, LV_EVENT_SHORT_CLICKED, NULL);
}


/*
----------------------------------------------------
Layout for the rotation
----------------------------------------------------
*/

void set_rotation_countdown_timer_config_view(lv_display_rotation_t display_rotation) {
    bool portrait = display_rotation == LV_DISPLAY_ROTATION_0 || display_rotation == LV_DISPLAY_ROTATION_180;

    // The presets fill the tile in one line: stacked in portrait, side by side in landscape
    lv_obj_set_flex_flow(parent_container, portrait ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);

    // The editor: portrait, the time across the top, minus and plus side by side, Done across the bottom;
    // landscape, the time over Done on the left, plus over minus on the right
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {TIMER_EDITOR_TIME_ROW_PX, LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (portrait) {
        lv_obj_set_grid_dsc_array(editor, portrait_cols, portrait_rows);
        lv_obj_set_grid_cell(editor_time_label, LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(editor_minus_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(editor_plus_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(editor_done_button, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 2, 1);
    }
    else {
        lv_obj_set_grid_dsc_array(editor, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(editor_time_label, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(editor_done_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(editor_plus_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(editor_minus_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
}


void countdown_timer_rotation_event_callback(lv_event_t * e) {
    set_rotation_countdown_timer_config_view(system_config.rotation);
}


// The settings are read at start-up (app_main), so the tile and the settings page show the same presets
void create_countdown_timer_config_view(lv_obj_t * parent) {
    memset(preset_widgets, 0, sizeof(preset_widgets));

    parent_container = lv_obj_create(parent);
    lv_obj_remove_style_all(parent_container);
    lv_obj_set_size(parent_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(parent_container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(parent_container, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(parent_container, TIMER_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(parent_container, TIMER_TILE_GAP_PX, LV_PART_MAIN);
    lv_obj_remove_flag(parent_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < TIMER_PRESET_COUNT; i++) {
        lv_obj_t * button = create_timer_button(parent_container, LV_PALETTE_ORANGE);
        // Each preset takes an equal share of what the margins and gaps leave, in either direction
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_size(button, lv_pct(100), lv_pct(100));
        lv_obj_add_event_cb(button, preset_clicked_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
        lv_obj_add_event_cb(button, preset_long_pressed_cb, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) i);

        preset_widgets[i].button = button;
        preset_widgets[i].label = create_timer_label(button, &lv_font_montserrat_40);
    }

    create_editor(parent);

    refresh_preset_widgets();
    set_rotation_countdown_timer_config_view(system_config.rotation);
}


void enable_countdown_timer_config_view(bool enable) {
    if (!enable) {
        // Leaving the tile keeps an edit in progress, as Done does; nothing else is saved here (ADR-006)
        close_editor();
    }
}


/*
----------------------------------------------------
Settings page: Timer
----------------------------------------------------
*/

static void update_step_event_cb(lv_event_t * e) {
    lv_obj_t * dropdown = lv_event_get_target_obj(e);
    uint32_t selected = lv_dropdown_get_selected(dropdown);
    if (selected < sizeof(timer_step_options_s)) {
        countdown_timer_config.step_s = timer_step_options_s[selected];
    }
}


static uint32_t step_option_index() {
    for (uint32_t i = 0; i < sizeof(timer_step_options_s); i++) {
        if (timer_step_options_s[i] == countdown_timer_config.step_s) {
            return i;
        }
    }
    return 0;
}


static void on_save_button_pressed(lv_event_t * e) {
    update_info_msg_box_result(msg_box, save_countdown_timer_config(), "Configuration Saved", "Save failed");
}


static void on_reload_button_pressed(lv_event_t * e) {
    // Nothing to apply: the step and the first-shot start are read when used; the presets show on the tile
    esp_err_t ret = load_countdown_timer_config();
    refresh_preset_widgets();
    config_page_rebuild(timer_config_page);

    update_info_msg_box_result(msg_box, ret, "Previous Configuration Reloaded", "Reload failed");
}


static void on_reset_button_pressed(lv_event_t * e) {
    memcpy(&countdown_timer_config, &countdown_timer_config_default, sizeof(countdown_timer_config));
    refresh_preset_widgets();
    config_page_rebuild(timer_config_page);

    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");
}


// The page's widgets, built from the settings; config_page_rebuild() runs it again after Reload and Reset
static void fill_timer_config_page(lv_obj_t * sub_page_config_view) {
    lv_obj_t * container;

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Editor Step");
    create_dropdown_list(container, timer_step_options, step_option_index(), update_step_event_cb, NULL);

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Start on First Shot");
    create_switch(container, &countdown_timer_config.start_on_first_shot, NULL);

    container = create_menu_container_with_text(sub_page_config_view, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);
}


lv_obj_t * create_countdown_timer_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    lv_obj_t * sub_page_config_view = create_config_page(parent, fill_timer_config_page);
    timer_config_page = sub_page_config_view;

    // Add to the menu
    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);

    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "Timer");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);

    lv_menu_set_load_page_event(parent, cont, sub_page_config_view);

    return sub_page_config_view;
}

#include "countdown_timer_config_view.h"
#include "countdown_timer.h"
#include "system_config.h"
#include "app_cfg.h"
#include "esp_log.h"
#include "nvs.h"
#include "common.h"
#include "esp_check.h"
#include "digital_level_view.h"
#include "display_shape.h"
#include "main_tileview.h"
#include "value_editor.h"
#include "settings_ui.h"

#include <string.h>


#define TAG "CountdownTimerConfigView"
#define NVS_NAMESPACE "CT"

// Layout of the tile (ADR-006): an 8 px margin, 20 px between targets, corners concentric with the panel's
#define TIMER_TILE_GAP_PX 20

#define TIMER_PRESET_MAX_S (10 * 60)

// Holding minus or plus: after a second, each repeat moves this many steps
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


typedef struct {
    lv_obj_t * button;
    lv_obj_t * label;
} timer_preset_widget_t;

static lv_obj_t * parent_container = NULL;
static timer_preset_widget_t preset_widgets[TIMER_PRESET_COUNT];
static int selected_preset = -1;    // the preset the timer is armed with; -1: none, the timer is hidden

static value_editor_t * editor = NULL;


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


/*
----------------------------------------------------
The editor (value_editor): minus and plus one step, and Done
----------------------------------------------------
*/

static void format_editor_time(char * buf, size_t size, int32_t value) {
    format_time(buf, size, (uint16_t) value);
}


static void format_editor_step(char * buf, size_t size, int direction, int32_t step) {
    snprintf(buf, size, "%c%u s", direction < 0 ? '-' : '+', (unsigned) step);
}


// Done, and leaving the tile, keep the edit. Saves only when the time changed
static void editor_closed(int32_t value, void * ctx) {
    int preset = (int) (intptr_t) ctx;

    if (value == countdown_timer_config.preset_s[preset]) {
        return;
    }

    countdown_timer_config.preset_s[preset] = (uint16_t) value;
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_countdown_timer_config());

    // An armed timer takes the new time, unless it is running
    if (preset == selected_preset && get_countdown_timer_state(&countdown_timer) != COUNTDOWN_TIMER_RUN) {
        countdown_timer_update_time(&countdown_timer, value * 1000);
    }
    refresh_preset_widgets();
}


static void open_editor(int preset) {
    const value_editor_params_t params = {
        .title = NULL,
        .value = countdown_timer_config.preset_s[preset],
        .min = countdown_timer_config.step_s,
        .max = TIMER_PRESET_MAX_S,
        .step = countdown_timer_config.step_s,
        .fast_steps = TIMER_EDITOR_FAST_STEPS,
        .format_value = format_editor_time,
        .format_step = format_editor_step,
        .closed = editor_closed,
        .ctx = (void *) (intptr_t) preset,
    };
    value_editor_open(editor, &params);
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

    value_editor_set_rotation(editor, display_rotation);
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
        lv_obj_t * button = value_editor_create_button(parent_container, LV_PALETTE_ORANGE);
        // Each preset takes an equal share of what the margins and gaps leave, in either direction
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_size(button, lv_pct(100), lv_pct(100));
        lv_obj_add_event_cb(button, preset_clicked_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
        lv_obj_add_event_cb(button, preset_long_pressed_cb, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) i);

        preset_widgets[i].button = button;
        preset_widgets[i].label = value_editor_create_label(button, &lv_font_montserrat_40);
    }

    editor = value_editor_create(parent);
    ESP_ERROR_CHECK(editor == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    refresh_preset_widgets();
    set_rotation_countdown_timer_config_view(system_config.rotation);
}


void enable_countdown_timer_config_view(bool enable) {
    if (!enable) {
        // Leaving the tile keeps an edit in progress, as Done does; nothing else is saved here (ADR-006)
        value_editor_close(editor);
    }
}


/*
----------------------------------------------------
Settings page: Timer (ADR-008: each change stored when made)
----------------------------------------------------
*/

static const char * const step_option_names[] = {"5 s", "10 s", "15 s"};


static int step_option_index() {
    for (int i = 0; i < (int) sizeof(timer_step_options_s); i++) {
        if (timer_step_options_s[i] == countdown_timer_config.step_s) {
            return i;
        }
    }
    return 0;
}


static int get_step(void * ctx) {
    return step_option_index();
}


static void set_step(int index, void * ctx) {
    countdown_timer_config.step_s = timer_step_options_s[index];
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_countdown_timer_config());
}


static bool get_first_shot(void * ctx) {
    return countdown_timer_config.start_on_first_shot;
}


static void set_first_shot(bool on, void * ctx) {
    countdown_timer_config.start_on_first_shot = on;
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_countdown_timer_config());
}


// The step, the first-shot start and the three presets
static void reset_timer(void * ctx) {
    memcpy(&countdown_timer_config, &countdown_timer_config_default, sizeof(countdown_timer_config));
    refresh_preset_widgets();
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_countdown_timer_config());
}


void countdown_timer_settings_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "%u s step, first shot %s", (unsigned) countdown_timer_config.step_s,
             countdown_timer_config.start_on_first_shot ? "on" : "off");
}


settings_page_t * create_countdown_timer_settings_page(void) {
    settings_page_t * page = settings_page_create("Timer");
    settings_row_choice(page, "Editor step", step_option_names, sizeof(step_option_names) / sizeof(step_option_names[0]),
                        get_step, set_step, NULL);
    settings_row_toggle(page, "Start on first shot", get_first_shot, set_first_shot, NULL);
    settings_row_reset(page, "Reset Timer", "Reset the Timer settings and presets?", reset_timer, NULL);
    return page;
}

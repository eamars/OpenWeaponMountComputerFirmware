#include <limits.h>
#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_macros.h"

#include "digital_level_view.h"
#include "settings_ui.h"
#include "app_cfg.h"
#include "bno085.h"
#include "esp_lvgl_port.h"
#include "countdown_timer.h"
#include "dope_view.h"
#include "level_band.h"
#include "level_card.h"
#include "display_shape.h"
#include "common.h"
#include "system_config.h"
#include "digital_level_view_controller.h"
#include "digital_level_view_type_1.h"
#include "digital_level_view_type_2.h"
#include "digital_level_view_type_3.h"
#include "main_tileview.h"
#include "timer_view.h"


#define TAG "DigitalLevelView"
#define NVS_NAMESPACE "DLV"



HEAPS_CAPS_ATTR digital_level_view_config_t digital_level_view_config;
const digital_level_view_config_t digital_level_view_config_default = {
    .roll_display_gain = 1.0f,
    .pitch_display_gain = 0.1f, // Default gain of 1/10
    .delta_level_threshold = 1.0f, // Default level threshold
    .user_roll_rad_offset = 0.0,       // Default to no offset
    .colour_left_tilt_indicator = LV_PALETTE_LIGHT_BLUE,
    .colour_right_tilt_indicator = LV_PALETTE_AMBER,
    .colour_horizontal_level_indicator = LV_PALETTE_LIGHT_GREEN,
    .colour_foreground = LV_PALETTE_BLACK,
    .level_style = DIGITAL_LEVEL_VIEW_TYPE_1,
    .foreground = LEVEL_FOREGROUND_MATCH,
};

static lv_obj_t * tilt_angle_label = NULL;
static lv_obj_t * tilt_angle_button = NULL;
static lv_obj_t * parent_container = NULL;
static lv_obj_t * overlay_container = NULL;

HEAPS_CAPS_ATTR countdown_timer_t countdown_timer;
digital_level_view_t * current_digital_level_view;


// NOT threadsafe copy of roll and pitch to be shared within the module


// Forward declaration
esp_err_t load_digital_level_view_config();
esp_err_t save_digital_level_view_config();


// A short message over the level ("Hold to set zero"), gone after a moment
#define HINT_SHOW_MS 1500
static lv_obj_t * hint_label = NULL;
static lv_timer_t * hint_timer = NULL;

static void hint_timeout_cb(lv_timer_t * timer) {
    lv_obj_set_hidden(hint_label, true);
    lv_timer_pause(timer);
}

static void show_hint(const char * text) {
    lv_label_set_text(hint_label, text);
    lv_obj_set_hidden(hint_label, false);
    lv_obj_move_foreground(hint_label);
    lv_timer_reset(hint_timer);
    lv_timer_resume(hint_timer);
}

static void create_hint(lv_obj_t * parent) {
    hint_label = lv_label_create(parent);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(hint_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hint_label, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(hint_label, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(hint_label, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(hint_label, 12, LV_PART_MAIN);
    lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(hint_label, true);

    hint_timer = lv_timer_create(hint_timeout_cb, HINT_SHOW_MS, NULL);
    lv_timer_set_auto_delete(hint_timer, false);
    lv_timer_set_repeat_count(hint_timer, -1);
    lv_timer_pause(hint_timer);
}


// The tilt box (ADR-007): a tap says how to zero; holding it sets the zero. A touch on the clock cannot move it
void tilt_angle_button_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED) {
        show_hint("Hold to set zero");
        return;
    }

    // Take a snapshot of current roll and use that as offset (take account of the screen rotation). The zero is a
    // calibration (the rifle's mount), so it is saved
    digital_level_view_config.user_roll_rad_offset = -1 * (sensor_roll_thread_unsafe - system_config.rotation * M_PI_2);
    ESP_LOGI(TAG, "user_roll_rad_offset := %f", digital_level_view_config.user_roll_rad_offset);

    esp_err_t ret = save_digital_level_view_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save the level zero: %s", esp_err_to_name(ret));
    }
    show_hint(ret == ESP_OK ? "Zero set" : "Zero set, not saved");
}


static void overlay_clicked_cb(lv_event_t * e) {
    if (get_countdown_timer_widget_enabled() && get_countdown_timer_state(&countdown_timer) == COUNTDOWN_TIMER_PAUSE) {
        countdown_timer_continue(&countdown_timer);
    }
}

void digital_level_view_clear_zero() {
    // A new factory calibration changes what the raw cant counts from, so the rifle zero taken against the old one
    // no longer holds (ADR-011). Saved with the level's settings
    digital_level_view_config.user_roll_rad_offset = 0.0f;
    esp_err_t ret = save_digital_level_view_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save the cleared level zero: %s", esp_err_to_name(ret));
    }
}


// Degrees the label shows; INT_MIN when the label is new and still shows "--"
static int tilt_angle_label_deg = INT_MIN;
static lv_obj_t * tilt_sign_label = NULL;

void update_roll_deg_indicator(float roll_rad) {
    // Setting the text redraws the label, so set it only when the shown number changes. The digits stay centred
    // and the minus sign hangs to their left, so the number does not jump when it turns negative (ADR-007)
    const int deg = (int) roundf(RAD_TO_DEG(roll_rad));
    if (deg != tilt_angle_label_deg) {
        lv_label_set_text_fmt(tilt_angle_label, "%d", deg < 0 ? -deg : deg);
        lv_label_set_text(tilt_sign_label, deg < 0 ? "-" : "");
        lv_obj_update_layout(tilt_angle_label);
        lv_obj_align_to(tilt_sign_label, tilt_angle_label, LV_ALIGN_OUT_LEFT_MID, -2, 0);
        tilt_angle_label_deg = deg;
    }
}


void set_rotation_roll_deg_indicator(lv_display_rotation_t display_rotation){
    // Centred at the top in both rotations (ADR-007)
    lv_obj_align(tilt_angle_button, LV_ALIGN_TOP_MID, 0, DISPLAY_MARGIN_PX);
}

void update_digital_level_view(float roll_rad, float pitch_rad)
 {
    // Update the tilt angle label
    update_roll_deg_indicator(roll_rad);;

    // Update background widget
    if (current_digital_level_view != NULL) {
        current_digital_level_view->update_callback(roll_rad, pitch_rad);
    }
}


void create_roll_deg_indicator(lv_obj_t * parent) {
    // Create button
    static lv_style_t btn_style;  // NOTE: "static" is required to hold the style within the memory
    lv_style_init(&btn_style);
    lv_style_set_bg_opa(&btn_style, LV_OPA_TRANSP);
    lv_style_set_border_width(&btn_style, 2);
    lv_style_set_border_color(&btn_style, lv_color_white());
    lv_style_set_shadow_width(&btn_style, 0);

    tilt_angle_button = lv_btn_create(parent);
    lv_obj_add_style(tilt_angle_button, &btn_style, LV_PART_MAIN);
    lv_obj_set_width(tilt_angle_button, 80);
    lv_obj_add_event_cb(tilt_angle_button, tilt_angle_button_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(tilt_angle_button, tilt_angle_button_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    set_rotation_roll_deg_indicator(system_config.rotation);

    // Create label on the button
    tilt_angle_label = lv_label_create(tilt_angle_button);
    lv_label_set_text(tilt_angle_label, "--");
    tilt_angle_label_deg = INT_MIN;
    lv_obj_set_style_text_color(tilt_angle_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(tilt_angle_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(tilt_angle_label, LV_ALIGN_CENTER, 0, 0);

    tilt_sign_label = lv_label_create(tilt_angle_button);
    lv_label_set_text(tilt_sign_label, "");
    lv_obj_set_style_text_color(tilt_sign_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(tilt_sign_label, &lv_font_montserrat_32, LV_PART_MAIN);
}


/*
----------------------------------------------------
The foreground (ADR-017): what is shown over the background, beside the tilt box. Built when it is chosen and
deleted when another is, as the backgrounds are
----------------------------------------------------
*/

typedef struct {
    void (*constructor)(lv_obj_t * overlay);
    void (*destructor)(void);
    void (*set_rotation)(lv_display_rotation_t rotation);
    // The level tile's neighbours this foreground has a use for: LV_DIR_TOP the timer tile, LV_DIR_BOTTOM the dope
    // tile. The others are not offered
    lv_dir_t tiles;
} level_foreground_t;


static void create_match_foreground(lv_obj_t * overlay) {
    level_band_create(overlay, &countdown_timer);
}


static void delete_match_foreground(void) {
    // The band is the timer's only display, so an armed timer goes with it
    countdown_timer_view_disarm();
    level_band_delete();
}


static void create_no_foreground(lv_obj_t * overlay) {
    ESP_UNUSED(overlay);
}


static void delete_no_foreground(void) {
}


static void set_rotation_no_foreground(lv_display_rotation_t rotation) {
    ESP_UNUSED(rotation);
}


static const level_foreground_t level_foregrounds[LEVEL_FOREGROUND_COUNT] = {
    [LEVEL_FOREGROUND_MATCH] = {
        .constructor = create_match_foreground,
        .destructor = delete_match_foreground,
        .set_rotation = level_band_set_rotation,
        .tiles = LV_DIR_VER,
    },
    [LEVEL_FOREGROUND_OFF] = {
        .constructor = create_no_foreground,
        .destructor = delete_no_foreground,
        .set_rotation = set_rotation_no_foreground,
        .tiles = LV_DIR_NONE,
    },
    // The card as rows, no timer (ADR-018): the dope tile for entry and the Kestrel pull, not the timer tile
    [LEVEL_FOREGROUND_DOPE] = {
        .constructor = level_card_create,
        .destructor = level_card_delete,
        .set_rotation = level_card_set_rotation,
        .tiles = LV_DIR_BOTTOM,
    },
};

static const level_foreground_t * current_foreground = NULL;


// A value this firmware does not know (saved by a later one) shows the default
static const level_foreground_t * select_level_foreground(int foreground) {
    if (foreground < 0 || foreground >= LEVEL_FOREGROUND_COUNT) {
        foreground = digital_level_view_config_default.foreground;
    }
    return &level_foregrounds[foreground];
}


// LVGL lock held. The first call builds the start-up foreground
static void show_level_foreground(const level_foreground_t * wanted) {
    if (wanted == current_foreground) {
        return;
    }
    if (current_foreground != NULL) {
        current_foreground->destructor();
    }
    current_foreground = wanted;
    current_foreground->constructor(overlay_container);

    // Right to the settings always; up and down only to a tile this foreground uses
    main_tileview_set_tile_dir(parent_container, LV_DIR_RIGHT | current_foreground->tiles);
}


void create_digital_level_overlay(lv_obj_t *parent)
{
    overlay_container = lv_obj_create(parent);
    lv_obj_set_style_border_width(overlay_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay_container, 0, 0);
    lv_obj_set_size(overlay_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(overlay_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_center(overlay_container);

    create_roll_deg_indicator(overlay_container);
    countdown_timer_setup(&countdown_timer, level_band_timer_update_cb, NULL);
    show_level_foreground(select_level_foreground(digital_level_view_config.foreground));
    create_hint(overlay_container);

    // While the timer is armed or paused, a tap anywhere on the level (not the band or the tilt box) starts it:
    // the largest target on the screen for the moment of the beep (ADR-007)
    lv_obj_add_event_cb(overlay_container, overlay_clicked_cb, LV_EVENT_SHORT_CLICKED, NULL);
}


/*
----------------------------------------------------
The background "Off" (ADR-017): a black screen under the tilt box and the foreground
----------------------------------------------------
*/

static lv_obj_t * create_no_background(lv_obj_t * parent);
static void delete_no_background(lv_obj_t * container);
static void update_no_background(float roll_rad, float pitch_rad);

static digital_level_view_t no_background_context = {
    .constructor = create_no_background,
    .destructor = delete_no_background,
    .update_callback = update_no_background,
};


static lv_obj_t * create_no_background(lv_obj_t * parent) {
    lv_obj_t * container = lv_obj_create(parent);
    no_background_context.container = container;
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(container, false);
    return container;
}


static void delete_no_background(lv_obj_t * container) {
    lv_obj_delete(container);
}


static void update_no_background(float roll_rad, float pitch_rad) {
    ESP_UNUSED(roll_rad);
    ESP_UNUSED(pitch_rad);
}


// A value this firmware does not know (saved by a later one) shows the default
digital_level_view_t * select_digital_level_view(digital_level_view_type_t level_style) {
    switch(level_style) {
        case DIGITAL_LEVEL_VIEW_TYPE_2:
            return &digital_level_view_type_2_context;
        case DIGITAL_LEVEL_VIEW_TYPE_3:
            return &digital_level_view_type_3_context;
        case DIGITAL_LEVEL_VIEW_OFF:
            return &no_background_context;
        case DIGITAL_LEVEL_VIEW_TYPE_1:
        default:
            return &digital_level_view_type_1_context;
    }
}

void create_digital_level_view(lv_obj_t *parent)
{
    parent_container = parent;

    // The settings are read at start-up (app_main)

    // Load digital level view widget
    current_digital_level_view = select_digital_level_view(digital_level_view_config.level_style);

    // Create widget
    current_digital_level_view->constructor(parent);

    // Create overlays
    create_digital_level_overlay(parent);

    // Initialize the digital level view controller tasks
    ESP_ERROR_CHECK(digital_level_view_controller_init());

    // Handle rotation change event
    lv_obj_add_event_cb(parent, digital_level_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);
}


/*
----------------------------------------------------
Settings page: Level (ADR-008: each change stored when made)
----------------------------------------------------
*/

// Show the background the settings ask for; the level reads its other settings live
static void apply_digital_level_view_style() {
    digital_level_view_t * wanted = select_digital_level_view(digital_level_view_config.level_style);
    if (wanted == current_digital_level_view) {
        return;
    }

    // LVGL task (an LVGL event), so the swap needs no lock
    current_digital_level_view->destructor(current_digital_level_view->container);
    current_digital_level_view = wanted;
    current_digital_level_view->constructor(parent_container);

    // Important! Move overlay to foreground
    lv_obj_move_foreground(overlay_container);
}


void digital_level_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_roll_deg_indicator(system_config.rotation);
    current_foreground->set_rotation(system_config.rotation);
}


bool digital_level_view_show_background(int background) {
    if (background < 0 || background >= DIGITAL_LEVEL_VIEW_TYPE_COUNT) {
        return false;
    }
    digital_level_view_config.level_style = (digital_level_view_type_t) background;
    apply_digital_level_view_style();
    return true;
}


bool digital_level_view_show_foreground(int foreground) {
    if (foreground < 0 || foreground >= LEVEL_FOREGROUND_COUNT) {
        return false;
    }
    digital_level_view_config.foreground = (uint8_t) foreground;
    show_level_foreground(select_level_foreground(foreground));
    return true;
}


// In the order of their stored values
static const char * const background_names[DIGITAL_LEVEL_VIEW_TYPE_COUNT] = {"Level", "Block", "Chevrons", "Off"};
// As the page lists them, Off last; the stored values are in the order they were added
static const char * const foreground_names[LEVEL_FOREGROUND_COUNT] = {"Match", "Dope", "Off"};
static const uint8_t foreground_values[LEVEL_FOREGROUND_COUNT] = {
    LEVEL_FOREGROUND_MATCH, LEVEL_FOREGROUND_DOPE, LEVEL_FOREGROUND_OFF,
};

// The colours a tilt or the level can show: LVGL's palette, and black (LV_PALETTE_BLACK, after the palette)
static const char * const colour_names[MAX_NUM_COLOURS] = {
    "Red", "Pink", "Purple", "Deep purple", "Indigo", "Blue", "Light blue", "Cyan", "Teal", "Green",
    "Light green", "Lime", "Yellow", "Amber", "Orange", "Deep orange", "Brown", "Blue grey", "Grey", "Black",
};


static lv_color_t colour_of(int index) {
    return index < LV_PALETTE_LAST ? lv_palette_main((lv_palette_t) index) : lv_color_black();
}


static void save_level(void) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_digital_level_view_config());
}


static int get_style(void * ctx) {
    return digital_level_view_config.level_style;
}


static void set_style(int index, void * ctx) {
    digital_level_view_config.level_style = (digital_level_view_type_t) index;
    apply_digital_level_view_style();
    save_level();
}


// The row the stored foreground is on; one this firmware does not know shows as the default
static int get_foreground(void * ctx) {
    int default_row = 0;
    for (int row = 0; row < LEVEL_FOREGROUND_COUNT; row++) {
        if (foreground_values[row] == digital_level_view_config.foreground) {
            return row;
        }
        if (foreground_values[row] == digital_level_view_config_default.foreground) {
            default_row = row;
        }
    }
    return default_row;
}


static void set_foreground(int index, void * ctx) {
    digital_level_view_config.foreground = foreground_values[index];
    show_level_foreground(select_level_foreground(digital_level_view_config.foreground));
    save_level();
}


// The gains and the threshold are kept as floats; the page edits them in tenths
static int32_t get_tenths(void * ctx) {
    return (int32_t) lroundf(*(float *) ctx * 10.0f);
}


static void set_tenths(int32_t value, void * ctx) {
    *(float *) ctx = value / 10.0f;
    save_level();
}


static void format_gain(char * buf, size_t size, int32_t tenths) {
    snprintf(buf, size, "%ld.%ld x", (long) (tenths / 10), (long) (tenths % 10));
}


static void format_degrees(char * buf, size_t size, int32_t tenths) {
    snprintf(buf, size, "%ld.%ld\xC2\xB0", (long) (tenths / 10), (long) (tenths % 10));
}


static void format_tenth_step(char * buf, size_t size, int direction, int32_t step) {
    snprintf(buf, size, "%c0.%ld", direction < 0 ? '-' : '+', (long) step);
}


// The display gains scale the Level style's line; the Block style does not use them
static bool gains_shown(void) {
    return digital_level_view_config.level_style == DIGITAL_LEVEL_VIEW_TYPE_1;
}


// The threshold and the colours belong to the backgrounds that show the level; "Off" uses none of them
static bool background_shown(void) {
    return digital_level_view_config.level_style != DIGITAL_LEVEL_VIEW_OFF;
}


static int get_colour(void * ctx) {
    return *(lv_palette_t *) ctx;
}


static void set_colour(int index, void * ctx) {
    *(lv_palette_t *) ctx = (lv_palette_t) index;
    save_level();
}


// Reset restores the preferences, not the calibration: the zero (the rifle's mounting offset) stays (owner, ADR-005);
// a new factory calibration clears it (ADR-011)
static void reset_level(void * ctx) {
    float zero_rad = digital_level_view_config.user_roll_rad_offset;
    memcpy(&digital_level_view_config, &digital_level_view_config_default, sizeof(digital_level_view_config));
    digital_level_view_config.user_roll_rad_offset = zero_rad;
    apply_digital_level_view_style();
    show_level_foreground(select_level_foreground(digital_level_view_config.foreground));
    save_level();
}


void digital_level_settings_summary(char * buf, size_t size, void * ctx) {
    int32_t tenths = (int32_t) lroundf(digital_level_view_config.delta_level_threshold * 10.0f);
    snprintf(buf, size, "%s, within %ld.%ld\xC2\xB0",
             background_names[digital_level_view_config.level_style % DIGITAL_LEVEL_VIEW_TYPE_COUNT],
             (long) (tenths / 10), (long) (tenths % 10));
}


settings_page_t * create_digital_level_settings_page(void) {
    static const settings_number_t roll_gain = {.min = 10, .max = 20, .step = 1, .fast_steps = 1,
                                                .format_value = format_gain, .format_step = format_tenth_step};
    static const settings_number_t pitch_gain = {.min = 1, .max = 10, .step = 1, .fast_steps = 1,
                                                 .format_value = format_gain, .format_step = format_tenth_step};
    static const settings_number_t threshold = {.min = 5, .max = 20, .step = 1, .fast_steps = 1,
                                                .format_value = format_degrees, .format_step = format_tenth_step};

    settings_page_t * page = settings_page_create("Level");
    settings_row_choice(page, "Background", background_names, DIGITAL_LEVEL_VIEW_TYPE_COUNT, get_style, set_style,
                        NULL);
    settings_row_choice(page, "Foreground", foreground_names, LEVEL_FOREGROUND_COUNT, get_foreground, set_foreground,
                        NULL);
    settings_row_number(page, "Roll display gain", &roll_gain, get_tenths, set_tenths,
                        &digital_level_view_config.roll_display_gain);
    settings_row_shown_when(page, gains_shown);
    settings_row_number(page, "Pitch display gain", &pitch_gain, get_tenths, set_tenths,
                        &digital_level_view_config.pitch_display_gain);
    settings_row_shown_when(page, gains_shown);
    settings_row_number(page, "Level threshold", &threshold, get_tenths, set_tenths,
                        &digital_level_view_config.delta_level_threshold);
    settings_row_shown_when(page, background_shown);
    settings_row_colour(page, "Left tilt colour", colour_names, MAX_NUM_COLOURS, colour_of, get_colour, set_colour,
                        &digital_level_view_config.colour_left_tilt_indicator);
    settings_row_shown_when(page, background_shown);
    settings_row_colour(page, "Right tilt colour", colour_names, MAX_NUM_COLOURS, colour_of, get_colour, set_colour,
                        &digital_level_view_config.colour_right_tilt_indicator);
    settings_row_shown_when(page, background_shown);
    settings_row_colour(page, "Levelled colour", colour_names, MAX_NUM_COLOURS, colour_of, get_colour, set_colour,
                        &digital_level_view_config.colour_horizontal_level_indicator);
    settings_row_shown_when(page, background_shown);
    settings_row_colour(page, "Foreground colour", colour_names, MAX_NUM_COLOURS, colour_of, get_colour, set_colour,
                        &digital_level_view_config.colour_foreground);
    settings_row_shown_when(page, background_shown);
    settings_row_reset(page, "Reset Level settings", "Reset the Level settings? The zero stays.", reset_level, NULL);
    return page;
}


esp_err_t load_digital_level_view_config() {
    // Settings saved before the foreground was added (ADR-017) load as they were, the zero included
    return load_config_grown(NVS_NAMESPACE, &digital_level_view_config, &digital_level_view_config_default,
                             sizeof(digital_level_view_config), offsetof(digital_level_view_config_t, foreground));
}

esp_err_t save_digital_level_view_config() {
    return save_config(NVS_NAMESPACE, &digital_level_view_config, sizeof(digital_level_view_config));
}

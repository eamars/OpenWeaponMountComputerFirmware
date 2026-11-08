#ifndef DIGITAL_LEVEL_VIEW_H
#define DIGITAL_LEVEL_VIEW_H


#include <lvgl.h>
#include "esp_err.h"
#include "settings_ui.h"
#include "digital_level_view_controller.h"
#include "countdown_timer.h"

#define MAX_NUM_COLOURS 20                  // the 19 colours of lv_palette_t, and black
#define LV_PALETTE_BLACK LV_PALETTE_LAST

typedef struct {
    uint32_t crc32;
    
    // Record the gain between the physical tilt angle and the displayed angle
    float roll_display_gain;
    float pitch_display_gain;

    // Threshold that determines the device is leveled
    float delta_level_threshold;

    // User defined roll offset to compensate for installation errors
    float user_roll_rad_offset;

    // Display colours for tilt conditions
    lv_palette_t colour_left_tilt_indicator;
    lv_palette_t colour_right_tilt_indicator;
    lv_palette_t colour_horizontal_level_indicator;
    lv_palette_t colour_foreground;

    // Unused: was "move the dope card on recoil", now the Dope page's (ADR-007). Kept so the saved block keeps its
    // size and the level's zero survives the update
    bool reserved_was_auto_move_dope_card;

    // Type of digital level view
    digital_level_view_type_t level_style;

} digital_level_view_config_t;


void create_digital_level_view(lv_obj_t *parent);
/** @brief The Level page of the settings (ADR-008) */
settings_page_t * create_digital_level_settings_page(void);
/** @brief One line for the hub */
void digital_level_settings_summary(char * buf, size_t size, void * ctx);

esp_err_t load_digital_level_view_config();
esp_err_t save_digital_level_view_config();

/**
 * @brief Call after a successful sensor tare: sets and saves the level's zero so the tare pose reads 0 at the
 * current screen rotation (ADR-005 B25).
 */
void digital_level_view_zero_after_tare();

void update_digital_level_view(float roll_rad, float pitch_rad);
// The angle in the top pill; redraws only when the whole-degree value changes. LVGL lock held
void update_roll_deg_indicator(float roll_rad);

void digital_level_view_rotation_event_callback(lv_event_t * e);

// Shared state, defined in digital_level_view.c
extern digital_level_view_config_t digital_level_view_config;
extern countdown_timer_t countdown_timer;

#endif // DIGITAL_LEVEL_VIEW_H
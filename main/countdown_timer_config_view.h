#ifndef COUNTDOWN_TIMER_CONFIG_VIEW_H_
#define COUNTDOWN_TIMER_CONFIG_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include "esp_err.h"

#define TIMER_PRESET_COUNT 3

// The timer's settings, saved together (NVS "CT"): the presets on the tile, and the Timer settings page (ADR-006)
typedef struct {
    uint16_t preset_s[TIMER_PRESET_COUNT];
    uint8_t step_s;             // the editor's step: 5, 10 or 15 s
    bool start_on_first_shot;   // the first shot starts an armed timer
} countdown_timer_config_t;

void create_countdown_timer_config_view(lv_obj_t * parent);
void enable_countdown_timer_config_view(bool enable);
void countdown_timer_rotation_event_callback(lv_event_t * e);

/** @brief The Timer page of the settings menu */
lv_obj_t * create_countdown_timer_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page);

esp_err_t load_countdown_timer_config();
esp_err_t save_countdown_timer_config();

/** @brief Whether the first shot starts an armed timer */
bool countdown_timer_start_on_first_shot();

// Shared state, defined in countdown_timer_config_view.c
extern countdown_timer_config_t countdown_timer_config;

#endif  // COUNTDOWN_TIMER_CONFIG_VIEW_H_

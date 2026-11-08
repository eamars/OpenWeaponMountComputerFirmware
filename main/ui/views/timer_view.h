#ifndef TIMER_VIEW_H_
#define TIMER_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include "esp_err.h"
#include "settings_ui.h"

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

/** @brief The Timer page of the settings (ADR-008) */
settings_page_t * create_countdown_timer_settings_page(void);
/** @brief One line for the hub: the step and the first-shot start */
void countdown_timer_settings_summary(char * buf, size_t size, void * ctx);

esp_err_t load_countdown_timer_config();
esp_err_t save_countdown_timer_config();

/** @brief Whether the first shot starts an armed timer */
bool countdown_timer_start_on_first_shot();

// Shared state, defined in timer_view.c
extern countdown_timer_config_t countdown_timer_config;

#endif  // TIMER_VIEW_H_

#ifndef DOPE_CONFIG_VIEW_H_
#define DOPE_CONFIG_VIEW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>
#include "esp_err.h"

// The dope card (ADR-007): 10 targets, A to J, as the Kestrel's Target Card. Holds are in mil x 10, signed:
// elevation + up / - down, wind + right / - left. Filled by hand on the dope tile, or (later) pulled from a Kestrel.

#define DOPE_TARGET_COUNT 10
#define DOPE_HOLD_NONE INT16_MIN            // a hold that is not known (the gust, until a Kestrel gives one)

#define DOPE_TARGET_SET (1 << 0)            // the target has an elevation; an empty target is not shown on the level
#define DOPE_TARGET_FROM_KESTREL (1 << 1)

typedef struct {
    int16_t elevation_x10;
    int16_t wind_x10;
    int16_t gust_x10;       // DOPE_HOLD_NONE when not known
    uint16_t range_m;       // 0 when not known
    uint8_t flags;
    uint8_t reserved;
} dope_target_t;

typedef struct {
    dope_target_t target[DOPE_TARGET_COUNT];
} dope_card_t;

extern dope_card_t dope_card;

esp_err_t load_dope_config();
esp_err_t save_dope_config();

// How targets are named, as the Kestrel's Target Card offers (same values as its designator setting)
typedef enum {
    DOPE_TARGET_NAMES_LETTERS = 0,  // A to J
    DOPE_TARGET_NAMES_NUMBERS = 1,  // 1 to 10
} dope_target_names_t;

// The Dope settings page
typedef struct {
    uint8_t target_names;           // dope_target_names_t
} dope_settings_t;

extern dope_settings_t dope_settings;

esp_err_t load_dope_settings();
esp_err_t save_dope_settings();

#define DOPE_TARGET_NAME_SIZE 12

/** @brief "A" or "1" for target 0, as the Target Names setting says */
void dope_target_name(char * buf, size_t size, int index);

/** @brief The Dope page in the settings menu */
lv_obj_t * create_dope_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page);

static inline bool dope_target_is_set(const dope_target_t * target) {
    return (target->flags & DOPE_TARGET_SET) != 0;
}

/** @brief "8.5" (up), "-0.3" (down) */
void dope_format_elevation(char * buf, size_t size, int32_t elevation_x10);
/** @brief "R0.4", "L0.4", "0.0" */
void dope_format_wind(char * buf, size_t size, int32_t wind_x10);

void create_dope_config_view(lv_obj_t * parent);
lv_obj_t * create_dope_card_list_widget(lv_obj_t * parent);

void set_rotation_dope_card_list(lv_display_rotation_t rotation);
void enable_dope_config_view(bool enable);
void dope_config_view_rotation_event_callback(lv_event_t * e);

#endif // DOPE_CONFIG_VIEW_H_

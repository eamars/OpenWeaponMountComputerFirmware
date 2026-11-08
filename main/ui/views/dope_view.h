#ifndef DOPE_VIEW_H_
#define DOPE_VIEW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>
#include "esp_err.h"
#include "settings_ui.h"

// The dope card (ADR-007): 10, 12 or 15 targets (the Card Size setting), named A, B, C... or 1, 2, 3... as the
// Kestrel's Target Card. Holds are in mil x 10, signed: elevation + up / - down, wind + right / - left. Filled by
// hand on the dope tile, or pulled from a Kestrel, whose Target Card holds 10: a pull fills only the first 10.

#define DOPE_TARGET_MAX 15                  // the card always stores this many; the setting says how many are used
#define DOPE_TARGET_COUNT_OLD 10            // the card as saved before the Card Size setting
#define DOPE_HOLD_NONE INT16_MIN            // a hold that is not known (the gust, until a Kestrel gives one)

#define DOPE_TARGET_SET (1 << 0)            // the target has an elevation; an empty target is not shown on the level
#define DOPE_TARGET_FROM_KESTREL (1 << 1)
// The side of a Kestrel wind or gust hold that rounded to 0.0 (0.02 L shows "L0.0"); a non-zero hold's sign wins
#define DOPE_TARGET_WIND_LEFT (1 << 2)
#define DOPE_TARGET_WIND_RIGHT (1 << 3)
#define DOPE_TARGET_GUST_LEFT (1 << 4)
#define DOPE_TARGET_GUST_RIGHT (1 << 5)

typedef struct {
    int16_t elevation_x10;
    int16_t wind_x10;
    int16_t gust_x10;       // DOPE_HOLD_NONE when not known
    uint16_t range_m;       // 0 when not known
    uint8_t flags;
    uint8_t reserved;
} dope_target_t;

typedef struct {
    dope_target_t target[DOPE_TARGET_MAX];  // appended to as it grew, so an older saved card still loads
} dope_card_t;

extern dope_card_t dope_card;

esp_err_t load_dope_config();
esp_err_t save_dope_config();

// How targets are named, as the Kestrel's Target Card offers (same values as its designator setting)
typedef enum {
    DOPE_TARGET_NAMES_LETTERS = 0,  // A, B, C...
    DOPE_TARGET_NAMES_NUMBERS = 1,  // 1, 2, 3...
} dope_target_names_t;

// The Dope settings page. Fields are only appended, so older saved settings still load
typedef struct {
    uint8_t target_names;           // dope_target_names_t
    bool next_target_on_recoil;     // the level shows the next target after a shot (off: unreliable with .22LR and
                                    // heavy rifles, owner 2026-11-07)
    uint8_t pull_count;             // how many Kestrel targets the last Pull took; the next starts from it
    uint8_t target_count;           // the card's size: 10, 12 (default: PRS stages, owner 2026-11-07) or 15
} dope_settings_t;

extern dope_settings_t dope_settings;

esp_err_t load_dope_settings();
esp_err_t save_dope_settings();

#define DOPE_TARGET_NAME_SIZE 12

/** @brief How many targets the card uses (the Card Size setting); targets past it are kept but not shown */
static inline int dope_target_count(void) {
    return dope_settings.target_count;
}

/** @brief "A" or "1" for target 0, as the Target Names setting says */
void dope_target_name(char * buf, size_t size, int index);

/** @brief The Dope page of the settings (ADR-008) */
settings_page_t * create_dope_settings_page(void);
/** @brief One line for the hub: the card size and the names */
void dope_settings_summary(char * buf, size_t size, void * ctx);

static inline bool dope_target_is_set(const dope_target_t * target) {
    return (target->flags & DOPE_TARGET_SET) != 0;
}

/** @brief "8.5" (up), "-0.3" (down) */
void dope_format_elevation(char * buf, size_t size, int32_t elevation_x10);
/** @brief A wind value: "R0.4", "L0.4", "0.0" */
void dope_format_wind(char * buf, size_t size, int32_t wind_x10);
/** @brief A target's wind, with the Kestrel's side when it rounded to 0.0: "R0.4", "L0.0", "0.0" */
void dope_format_target_wind(char * buf, size_t size, const dope_target_t * target);
/** @brief A target's gust after its wind: "0.7" on the wind's side, "R0.7" when the sides differ or the wind has none */
void dope_format_target_gust(char * buf, size_t size, const dope_target_t * target);

void create_dope_config_view(lv_obj_t * parent);
void enable_dope_config_view(bool enable);
void dope_config_view_rotation_event_callback(lv_event_t * e);

#endif // DOPE_VIEW_H_

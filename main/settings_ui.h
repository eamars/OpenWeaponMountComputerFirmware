#ifndef SETTINGS_UI_H_
#define SETTINGS_UI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

// The settings pages (ADR-008), in the design language of the other views (`embedded-ui-design` skill): black, a
// pinned header on every page (Back, the Wi-Fi and battery status, the title), full-width rows that lock to a grid
// (whole rows only, snapping to row edges), one action per row showing its value, a choice opening a full-screen
// list, a change stored when it is made, Reset asking once. Pages form a stack: the hub, a page, a choice list.
// LVGL lock held for every call.

typedef struct settings_page settings_page_t;

// The text a row shows under its label (its current value), worked out each time the row is shown
typedef void (*settings_text_fn)(char * buf, size_t size, void * ctx);

/** @brief Build the settings on the tile; `hub` is filled by the caller with settings_row_* rows */
settings_page_t * settings_ui_create(lv_obj_t * tile);

/** @brief Show the hub, once its rows and the pages are built */
void settings_ui_start(void);

/** @brief A page reached from a row, with Back */
settings_page_t * settings_page_create(const char * title);

/** @brief The page's area under its header, for a page that is not rows (the old settings menu) */
lv_obj_t * settings_page_body(settings_page_t * page);

/** @brief A row that opens another page; `value` may be NULL; `symbol` an LV_SYMBOL_* or NULL */
void settings_row_link(settings_page_t * page, const char * symbol, const char * label, settings_text_fn value,
                       void * ctx, settings_page_t * target);

/**
 * @brief A one-of-many setting: the row shows the current option; a tap opens a full-screen list of them. `set`
 * runs only when another option is picked, and stores the setting
 */
void settings_row_choice(settings_page_t * page, const char * label, const char * const * options, int count,
                         int (*get)(void * ctx), void (*set)(int index, void * ctx), void * ctx);

/** @brief An on/off setting: a tap on the row flips it; `set` stores it */
void settings_row_toggle(settings_page_t * page, const char * label, bool (*get)(void * ctx),
                         void (*set)(bool on, void * ctx), void * ctx);

/** @brief The page's Reset, in red: asks `question` on a full screen, then runs `reset` (which stores the defaults) */
void settings_row_reset(settings_page_t * page, const char * label, const char * question,
                        void (*reset)(void * ctx), void * ctx);

#endif  // SETTINGS_UI_H_

#ifndef SETTINGS_UI_H_
#define SETTINGS_UI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

// The settings pages (ADR-008), in the design language of the other views (`embedded-ui-design` skill): black, a
// pinned header on every page (Back, the Wi-Fi and battery status, the title), full-width rows that lock to a grid
// (whole rows only, snapping to row edges), one action per row showing its value, a choice opening a full-screen
// list, a number opening the shared value editor, a change stored when it is made, destructive actions asking once.
// Pages form a stack: the hub, a page, a list or an editor. LVGL lock held for every call.

typedef struct settings_page settings_page_t;

// The text a row shows under its label (its current value), worked out each time the row is shown and while it is
// on screen (twice a second), so live values (status, battery) follow
typedef void (*settings_text_fn)(char * buf, size_t size, void * ctx);

/** @brief Build the settings on the tile; returns the hub, which the caller fills with rows */
settings_page_t * settings_ui_create(lv_obj_t * tile);

/** @brief Show the hub, once its rows and the pages are built */
void settings_ui_start(void);

/** @brief A page reached from a row, with Back */
settings_page_t * settings_page_create(const char * title);

/** @brief The page's area under its header, for a page that is not rows */
lv_obj_t * settings_page_body(settings_page_t * page);

/** @brief Remove a page's rows (to build them again, as when a mode changes what the page shows) */
void settings_page_clear(settings_page_t * page);

/** @brief Lay the rows out again after rows were added, removed, shown or hidden */
void settings_page_relayout(settings_page_t * page);

/** @brief Open a page (on top of the current one) */
void settings_ui_open(settings_page_t * page);

/** @brief A row that opens another page; `value` may be NULL; `symbol` an LV_SYMBOL_* or NULL */
void settings_row_link(settings_page_t * page, const char * symbol, const char * label, settings_text_fn value,
                       void * ctx, settings_page_t * target);

/**
 * @brief A one-of-many setting: the row shows the current option; a tap opens a full-screen list of them. `set`
 * runs only when another option is picked, and stores the setting
 */
void settings_row_choice(settings_page_t * page, const char * label, const char * const * options, int count,
                         int (*get)(void * ctx), void (*set)(int index, void * ctx), void * ctx);

/** @brief As a choice, each option with a colour swatch (`colour` gives option i's colour) */
void settings_row_colour(settings_page_t * page, const char * label, const char * const * names, int count,
                         lv_color_t (*colour)(int index), int (*get)(void * ctx), void (*set)(int index, void * ctx),
                         void * ctx);

/** @brief An on/off setting: a tap on the row flips it; `set` stores it */
void settings_row_toggle(settings_page_t * page, const char * label, bool (*get)(void * ctx),
                         void (*set)(bool on, void * ctx), void * ctx);

typedef struct {
    int32_t min;
    int32_t max;
    int32_t step;
    int32_t fast_steps;
    void (*format_value)(char * buf, size_t size, int32_t value);
    void (*format_step)(char * buf, size_t size, int direction, int32_t step);   // NULL: "-1" / "+1"
} settings_number_t;

/**
 * @brief A number: the row shows it (`format_value`); a tap opens the shared value editor. `set` runs on Done
 * only when the value changed, and stores it
 */
void settings_row_number(settings_page_t * page, const char * label, const settings_number_t * number,
                         int32_t (*get)(void * ctx), void (*set)(int32_t value, void * ctx), void * ctx);

/**
 * @brief An action. With a `question`, it asks first on a full screen (`confirm` names the button, red when
 * `destructive`). Returns the row, to hide it (lv_obj_add_flag HIDDEN) and show it again; then
 * settings_page_relayout()
 */
lv_obj_t * settings_row_action(settings_page_t * page, const char * symbol, const char * label,
                               settings_text_fn value, void * ctx, void (*action)(void * ctx),
                               const char * question, const char * confirm, bool destructive);

/** @brief The page's Reset, in red: asks `question` on a full screen, then runs `reset` (which stores the defaults) */
void settings_row_reset(settings_page_t * page, const char * label, const char * question,
                        void (*reset)(void * ctx), void * ctx);

/** @brief Text to read, not a target: `text` gives it (up to three lines) */
void settings_row_status(settings_page_t * page, settings_text_fn text, void * ctx);

/**
 * @brief Show the row just added only while `shown` says so: a row that does nothing in the unit's present state
 * (set-up when the unit already has its network, a gain the chosen level style does not use) is not shown. Checked
 * when the page is shown, after a toggle, and twice a second; the page relays its rows when one comes or goes
 */
void settings_row_shown_when(settings_page_t * page, bool (*shown)(void));

/** @brief The row just added in orange, its icon and value in white: the one row that asks for attention (an update) */
void settings_row_highlight(settings_page_t * page);

/** @brief Whether the page is open (anywhere on the stack), and whether it is the one showing */
bool settings_ui_is_open(settings_page_t * page);
bool settings_ui_is_top(settings_page_t * page);

// Test hooks for the remote control (ADR-008): look at every page, list, editor and confirmation without touching
// the screen. They never change a setting: a row that would (a toggle, an action that does not ask) is refused, and
// back closes an editor or a confirmation without its action
void settings_ui_test_home(void);
bool settings_ui_test_open(const char * title);
bool settings_ui_test_row(int index);
void settings_ui_test_back(void);
void settings_ui_test_scroll(int index);
const char * settings_ui_test_title(void);

#endif  // SETTINGS_UI_H_

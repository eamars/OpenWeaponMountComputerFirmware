#ifndef VALUE_EDITOR_H_
#define VALUE_EDITOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

// One full-screen editor for a number: the value, minus, plus and Done (ADR-006 timer editor, shared in ADR-007).
// A press steps once; holding repeats every long-press repeat (100 ms) and, after a second, moves several steps per
// repeat. A value off the step snaps to it on the first press. Done, or the owner closing it (leaving the tile),
// keeps the edit and reports the value; the owner decides whether to save. Portrait: the value across the top,
// minus and plus side by side, Done across the bottom; landscape: the value over Done on the left, plus over minus
// on the right. All calls with the LVGL lock held.

typedef struct value_editor value_editor_t;

typedef struct {
    const char * title;     // small line over the value ("B  Elevation"); NULL for none
    int32_t value;
    int32_t min;
    int32_t max;
    int32_t step;
    int32_t fast_steps;     // steps per repeat once holding has gone on for a second
    // The value's text, and each button's text (direction -1 for minus, +1 for plus)
    void (*format_value)(char * buf, size_t size, int32_t value);
    void (*format_step)(char * buf, size_t size, int direction, int32_t step);
    // Called once when the editor closes, with the value shown
    void (*closed)(int32_t value, void * ctx);
    void * ctx;
} value_editor_params_t;

/** @brief Build the editor on `parent` (a tile), hidden. */
value_editor_t * value_editor_create(lv_obj_t * parent);

/** @brief Show the editor with these parameters (copied). */
void value_editor_open(value_editor_t * editor, const value_editor_params_t * params);

/** @brief Close the editor, keeping the edit: `closed` is called. Nothing happens when it is not open. */
void value_editor_close(value_editor_t * editor);

/** @brief Close the editor and drop the edit: `closed` is not called */
void value_editor_cancel(value_editor_t * editor);

bool value_editor_is_open(const value_editor_t * editor);

void value_editor_set_rotation(value_editor_t * editor, lv_display_rotation_t rotation);

/** @brief The rounded dark button the editor and the tiles share; `checked_palette` colours LV_STATE_CHECKED. */
lv_obj_t * value_editor_create_button(lv_obj_t * parent, lv_palette_t checked_palette);

/** @brief A white label, centred in its parent. */
lv_obj_t * value_editor_create_label(lv_obj_t * parent, const lv_font_t * font);

#endif  // VALUE_EDITOR_H_

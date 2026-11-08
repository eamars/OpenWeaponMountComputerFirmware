#include "value_editor.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "app_cfg.h"
#include "display_shape.h"


#define VALUE_EDITOR_GAP_PX 20
#define VALUE_EDITOR_VALUE_ROW_PX 96    // portrait: the row that shows the value

// Holding minus or plus repeats one step every long-press repeat (100 ms); after this many repeats, each repeat
// moves `fast_steps` steps
#define VALUE_EDITOR_FAST_AFTER_REPEATS 10


struct value_editor {
    lv_obj_t * screen;
    lv_obj_t * value_box;
    lv_obj_t * title_label;
    lv_obj_t * value_label;
    lv_obj_t * minus_button;
    lv_obj_t * minus_label;
    lv_obj_t * plus_button;
    lv_obj_t * plus_label;
    lv_obj_t * done_button;

    bool open;
    value_editor_params_t params;
    int32_t value;
    uint32_t repeats;
};


lv_obj_t * value_editor_create_button(lv_obj_t * parent, lv_palette_t checked_palette) {
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_set_style_radius(button, display_concentric_radius(DISPLAY_MARGIN_PX), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_palette_main(checked_palette), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_PRESSED);
    return button;
}


lv_obj_t * value_editor_create_label(lv_obj_t * parent, const lv_font_t * font) {
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_center(label);
    return label;
}


// Division rounding down, also for negative values (C's rounds toward zero)
static int32_t floor_div(int32_t a, int32_t b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}


static void show_value(value_editor_t * editor) {
    char text[16];
    editor->params.format_value(text, sizeof(text), editor->value);
    lv_label_set_text(editor->value_label, text);
}


// One or more steps up or down; a value off the step snaps to it first (1:37 with 10 s: plus 1:40, minus 1:30)
static void step(value_editor_t * editor, int direction, int32_t steps) {
    const int32_t step = editor->params.step;
    int32_t value = editor->value;

    for (int32_t i = 0; i < steps; i++) {
        if (direction > 0) {
            value = (floor_div(value, step) + 1) * step;
        }
        else {
            value = -(floor_div(-value, step) + 1) * step;
        }
    }

    if (value < editor->params.min) {
        value = editor->params.min;
    }
    if (value > editor->params.max) {
        value = editor->params.max;
    }

    editor->value = value;
    show_value(editor);
}


static void step_event_cb(lv_event_t * e) {
    lv_obj_t * button = lv_event_get_current_target_obj(e);
    value_editor_t * editor = lv_obj_get_user_data(button);
    int direction = button == editor->plus_button ? 1 : -1;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        editor->repeats = 0;
        step(editor, direction, 1);
    }
    else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        editor->repeats++;
        step(editor, direction, editor->repeats > VALUE_EDITOR_FAST_AFTER_REPEATS ? editor->params.fast_steps : 1);
    }
}


static void done_event_cb(lv_event_t * e) {
    value_editor_close(lv_event_get_user_data(e));
}


value_editor_t * value_editor_create(lv_obj_t * parent) {
    value_editor_t * editor = heap_caps_calloc(1, sizeof(value_editor_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (editor == NULL) {
        return NULL;
    }

    editor->screen = lv_obj_create(parent);
    lv_obj_t * screen = editor->screen;
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, VALUE_EDITOR_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(screen, VALUE_EDITOR_GAP_PX, LV_PART_MAIN);
    lv_obj_set_scrollable(screen, false);
    // A drag on the editor's buttons must not swipe the tile away
    lv_obj_set_scroll_chain_hor(screen, false);
    lv_obj_set_scroll_chain_ver(screen, false);
    lv_obj_set_clickable(screen, true);  // taps between the buttons stay in the editor
    lv_obj_set_hidden(screen, true);

    // The value, with an optional title over it
    editor->value_box = lv_obj_create(screen);
    lv_obj_remove_style_all(editor->value_box);
    lv_obj_set_flex_flow(editor->value_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(editor->value_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(editor->value_box, false);
    lv_obj_set_clickable(editor->value_box, false);
    editor->title_label = value_editor_create_label(editor->value_box, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(editor->title_label, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    editor->value_label = value_editor_create_label(editor->value_box, &lv_font_montserrat_48);

    editor->minus_button = value_editor_create_button(screen, LV_PALETTE_ORANGE);
    editor->minus_label = value_editor_create_label(editor->minus_button, &lv_font_montserrat_32);
    lv_obj_set_user_data(editor->minus_button, editor);
    lv_obj_add_event_cb(editor->minus_button, step_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(editor->minus_button, step_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    editor->plus_button = value_editor_create_button(screen, LV_PALETTE_ORANGE);
    editor->plus_label = value_editor_create_label(editor->plus_button, &lv_font_montserrat_32);
    lv_obj_set_user_data(editor->plus_button, editor);
    lv_obj_add_event_cb(editor->plus_button, step_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(editor->plus_button, step_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    editor->done_button = value_editor_create_button(screen, LV_PALETTE_ORANGE);
    lv_obj_set_style_bg_color(editor->done_button, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), LV_PART_MAIN);
    lv_obj_t * done_label = value_editor_create_label(editor->done_button, &lv_font_montserrat_32);
    lv_label_set_text(done_label, "Done");
    lv_obj_add_event_cb(editor->done_button, done_event_cb, LV_EVENT_SHORT_CLICKED, editor);

    return editor;
}


void value_editor_open(value_editor_t * editor, const value_editor_params_t * params) {
    editor->params = *params;
    editor->value = params->value;
    editor->open = true;

    if (params->title != NULL) {
        lv_label_set_text(editor->title_label, params->title);
        lv_obj_set_hidden(editor->title_label, false);
    }
    else {
        lv_obj_set_hidden(editor->title_label, true);
    }

    char text[16];
    params->format_step(text, sizeof(text), -1, params->step);
    lv_label_set_text(editor->minus_label, text);
    params->format_step(text, sizeof(text), 1, params->step);
    lv_label_set_text(editor->plus_label, text);
    show_value(editor);

    lv_obj_set_hidden(editor->screen, false);
    lv_obj_move_foreground(editor->screen);
}


void value_editor_close(value_editor_t * editor) {
    if (editor == NULL || !editor->open) {
        return;
    }
    editor->open = false;
    lv_obj_set_hidden(editor->screen, true);

    if (editor->params.closed != NULL) {
        editor->params.closed(editor->value, editor->params.ctx);
    }
}


void value_editor_cancel(value_editor_t * editor) {
    if (editor == NULL || !editor->open) {
        return;
    }
    editor->open = false;
    lv_obj_set_hidden(editor->screen, true);
}


bool value_editor_is_open(const value_editor_t * editor) {
    return editor != NULL && editor->open;
}


void value_editor_set_rotation(value_editor_t * editor, lv_display_rotation_t rotation) {
    bool portrait = rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180;

    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows[] = {VALUE_EDITOR_VALUE_ROW_PX, LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (portrait) {
        lv_obj_set_grid_dsc_array(editor->screen, portrait_cols, portrait_rows);
        lv_obj_set_grid_cell(editor->value_box, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(editor->minus_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(editor->plus_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(editor->done_button, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 2, 1);
    }
    else {
        lv_obj_set_grid_dsc_array(editor->screen, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(editor->value_box, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(editor->done_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(editor->plus_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
        lv_obj_set_grid_cell(editor->minus_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
}

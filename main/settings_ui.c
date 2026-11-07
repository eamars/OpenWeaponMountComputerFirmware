#include "settings_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "app_cfg.h"
#include "display_shape.h"
#include "ui_bridge.h"
#include "value_editor.h"
#include "wifi.h"

#define TAG "SettingsUI"

// Sizes (ADR-008, `embedded-ui-design`): an 8 px margin, 20 px between targets, the header pinned
#define SET_GAP_PX 20
#define SET_HEADER_PORTRAIT_PX 92           // across the top; 92 so three 96 px rows fill the rest exactly
#define SET_HEADER_LANDSCAPE_PX 122         // down the left
#define SET_BACK_LANDSCAPE_PX 96            // landscape: Back's height in the column
#define SET_ROW_PORTRAIT_PX 96              // portrait, three rows a screen (the owner's choice by hand, 2026-11-07)
#define SET_ROW_LANDSCAPE_PX 122
#define SET_ROW_PAD_PX 20                   // inside a row, left and right
#define SET_SWITCH_W_PX 72
#define SET_SWITCH_H_PX 40
#define SET_ROWS_MAX 16
#define SET_PAGES_MAX 16
#define SET_STACK_MAX 4
#define SET_RESET_COLOUR lv_palette_darken(LV_PALETTE_RED, 3)

typedef enum {
    ROW_LINK,
    ROW_CHOICE,
    ROW_TOGGLE,
    ROW_RESET,
    ROW_OPTION,     // one option on the choice list
} row_kind_t;

typedef struct row {
    row_kind_t kind;
    lv_obj_t * button;
    lv_obj_t * label;
    lv_obj_t * value_label;
    lv_obj_t * sw;
    const char * label_text;
    settings_text_fn value;
    void * ctx;
    settings_page_t * target;                   // ROW_LINK
    const char * const * options;               // ROW_CHOICE
    int count;
    int (*get_index)(void * ctx);
    void (*set_index)(int index, void * ctx);
    bool (*get_bool)(void * ctx);               // ROW_TOGGLE
    void (*set_bool)(bool on, void * ctx);
    const char * question;                      // ROW_RESET
    void (*reset)(void * ctx);
    struct row * source;                        // ROW_OPTION: the choice row it belongs to
    int index;
} row_t;

struct settings_page {
    lv_obj_t * obj;
    lv_obj_t * header;
    lv_obj_t * back;
    lv_obj_t * status;
    lv_obj_t * title;
    lv_obj_t * list;        // rows
    lv_obj_t * body;        // or a page of its own content
    row_t * rows[SET_ROWS_MAX];
    int row_count;
};

static lv_obj_t * root = NULL;
static settings_page_t * pages[SET_PAGES_MAX];
static int page_count = 0;
static settings_page_t * stack[SET_STACK_MAX];
static int depth = 0;

static settings_page_t * choice_page = NULL;    // one list, rebuilt for each choice
HEAPS_CAPS_ATTR static row_t option_rows[SET_ROWS_MAX];

static lv_obj_t * confirm_screen = NULL;
static lv_obj_t * confirm_label = NULL;
static lv_obj_t * confirm_reset_button = NULL;
static lv_obj_t * confirm_cancel_button = NULL;
static row_t * confirm_row = NULL;

static bool is_portrait(void) {
    return lv_obj_get_height(root) > lv_obj_get_width(root);
}


/*
----------------------------------------------------
Status: Wi-Fi and battery, on every page's header
----------------------------------------------------
*/

static void format_status(char * buf, size_t size) {
    int32_t wifi_state = lv_subject_get_int(&ui_wifi_state);
    int32_t level = lv_subject_get_int(&ui_battery_level);

    const char * wifi = "";
    if (wifi_state == WIRELESS_STATE_CONNECTED) {
        wifi = LV_SYMBOL_WIFI "  ";
    }
    else if (wifi_state == WIRELESS_STATE_PROVISIONING) {
        wifi = LV_SYMBOL_SHUFFLE "  ";
    }

    if (level < 0) {
        snprintf(buf, size, "%s" LV_SYMBOL_WARNING, wifi);
    }
    else if (level > 100) {
        snprintf(buf, size, "%s" LV_SYMBOL_USB, wifi);
    }
    else {
        const char * battery = level < 10 ? LV_SYMBOL_BATTERY_EMPTY : level < 40 ? LV_SYMBOL_BATTERY_1 :
                               level < 70 ? LV_SYMBOL_BATTERY_2 : level < 90 ? LV_SYMBOL_BATTERY_3 :
                               LV_SYMBOL_BATTERY_FULL;
        snprintf(buf, size, "%s%s %ld%%", wifi, battery, (long) level);
    }
}


static void status_observer_cb(lv_observer_t * observer, lv_subject_t * subject) {
    char text[40];
    format_status(text, sizeof(text));
    for (int i = 0; i < page_count; i++) {
        lv_label_set_text(pages[i]->status, text);
    }
}


/*
----------------------------------------------------
Layout: the header pinned, the rows locked to a grid
----------------------------------------------------
*/

// The list's height comes from the screen's size and the margins, not from the list: this runs in LVGL's
// size-change event, inside a layout pass, where the list's own size is not updated yet (a nested layout update
// is ignored)
static int32_t row_height_for(int32_t list_height, bool portrait) {
    int32_t min_row = portrait ? SET_ROW_PORTRAIT_PX : SET_ROW_LANDSCAPE_PX;
    return display_locked_row_height(list_height, min_row, SET_GAP_PX);
}


// After the layout pass: every list back onto a row edge
static void realign_lists_async(void * p) {
    for (int i = 0; i < page_count; i++) {
        lv_obj_update_layout(pages[i]->list);
        lv_obj_update_snap(pages[i]->list, LV_ANIM_OFF);
    }
}


static void layout_page(settings_page_t * page) {
    int32_t w = lv_obj_get_width(root);
    int32_t h = lv_obj_get_height(root);
    int32_t m = DISPLAY_MARGIN_PX;
    bool has_back = page->back != NULL;
    int32_t list_height;

    if (h > w) {
        // Portrait: the header across the top, Back on its left, status over the title on its right
        lv_obj_set_pos(page->header, m, m);
        lv_obj_set_size(page->header, w - 2 * m, SET_HEADER_PORTRAIT_PX);
        if (has_back) {
            lv_obj_set_pos(page->back, 0, 0);
            lv_obj_set_size(page->back, SET_HEADER_PORTRAIT_PX, SET_HEADER_PORTRAIT_PX);
        }
        int32_t text_w = w - 2 * m - (has_back ? SET_HEADER_PORTRAIT_PX : 0) - 2 * 12;
        lv_obj_set_width(page->title, text_w);
        lv_obj_set_style_text_align(page->title, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_align(page->status, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_width(page->status, text_w);
        lv_obj_align(page->status, LV_ALIGN_TOP_RIGHT, -12, 10);
        lv_obj_align(page->title, LV_ALIGN_BOTTOM_RIGHT, -12, -8);

        // The rows run to the screen's right edge so the scroll bar sits in the margin
        int32_t top = m + SET_HEADER_PORTRAIT_PX + SET_GAP_PX;
        list_height = h - top - m;
        lv_obj_set_pos(page->list, m, top);
        lv_obj_set_size(page->list, w - m, list_height);
        lv_obj_set_pos(page->body, m, top);
        lv_obj_set_size(page->body, w - 2 * m, h - top - m);
    }
    else {
        // Landscape: a column on the left, Back at its top, status and title under it
        lv_obj_set_pos(page->header, m, m);
        lv_obj_set_size(page->header, SET_HEADER_LANDSCAPE_PX, h - 2 * m);
        if (has_back) {
            lv_obj_set_pos(page->back, 0, 0);
            lv_obj_set_size(page->back, SET_HEADER_LANDSCAPE_PX, SET_BACK_LANDSCAPE_PX);
        }
        int32_t below = has_back ? SET_BACK_LANDSCAPE_PX : 0;
        lv_obj_set_width(page->title, SET_HEADER_LANDSCAPE_PX);
        lv_obj_set_width(page->status, SET_HEADER_LANDSCAPE_PX);
        lv_obj_set_style_text_align(page->title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(page->status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(page->status, LV_ALIGN_TOP_MID, 0, below + 16);
        lv_obj_align(page->title, LV_ALIGN_TOP_MID, 0, below + 48);

        int32_t left = m + SET_HEADER_LANDSCAPE_PX + SET_GAP_PX;
        list_height = h - 2 * m;
        lv_obj_set_pos(page->list, left, m);
        lv_obj_set_size(page->list, w - left, list_height);
        lv_obj_set_pos(page->body, left, m);
        lv_obj_set_size(page->body, w - left - m, h - 2 * m);
    }

    int32_t row_h = row_height_for(list_height, h > w);
    uint32_t children = lv_obj_get_child_count(page->list);
    for (uint32_t i = 0; i < children; i++) {
        lv_obj_set_height(lv_obj_get_child(page->list, i), row_h);
    }
}


static void layout_confirm(void) {
    static const int32_t portrait_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t portrait_rows_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t landscape_rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (is_portrait()) {
        lv_obj_set_grid_dsc_array(confirm_screen, portrait_cols, portrait_rows_dsc);
        lv_obj_set_grid_cell(confirm_label, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(confirm_reset_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(confirm_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    }
    else {
        lv_obj_set_grid_dsc_array(confirm_screen, landscape_cols, landscape_rows);
        lv_obj_set_grid_cell(confirm_label, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(confirm_cancel_button, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
        lv_obj_set_grid_cell(confirm_reset_button, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    }
}


static void layout_all(void) {
    for (int i = 0; i < page_count; i++) {
        layout_page(pages[i]);
    }
    layout_confirm();
    lv_async_call(realign_lists_async, NULL);
}


static void root_size_changed_cb(lv_event_t * e) {
    layout_all();
}


/*
----------------------------------------------------
Rows
----------------------------------------------------
*/

static void refresh_row(row_t * row) {
    char text[48] = "";
    switch (row->kind) {
    case ROW_LINK:
        if (row->value != NULL) {
            row->value(text, sizeof(text), row->ctx);
        }
        break;
    case ROW_CHOICE: {
        int index = row->get_index(row->ctx);
        snprintf(text, sizeof(text), "%s", index >= 0 && index < row->count ? row->options[index] : "");
        break;
    }
    case ROW_TOGGLE: {
        bool on = row->get_bool(row->ctx);
        snprintf(text, sizeof(text), "%s", on ? "On" : "Off");
        if (on) {
            lv_obj_add_state(row->sw, LV_STATE_CHECKED);
        }
        else {
            lv_obj_remove_state(row->sw, LV_STATE_CHECKED);
        }
        break;
    }
    default:
        break;
    }
    if (row->value_label != NULL) {
        lv_label_set_text(row->value_label, text);
        if (text[0] == '\0') {
            lv_obj_add_flag(row->value_label, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_remove_flag(row->value_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


static void refresh_page(settings_page_t * page) {
    for (int i = 0; i < page->row_count; i++) {
        refresh_row(page->rows[i]);
    }
}


static void show_top(void) {
    for (int i = 0; i < page_count; i++) {
        if (pages[i] == stack[depth - 1]) {
            lv_obj_remove_flag(pages[i]->obj, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(pages[i]->obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    settings_page_t * top = stack[depth - 1];
    refresh_page(top);
    layout_page(top);
    lv_async_call(realign_lists_async, NULL);
}


static void open_page(settings_page_t * page) {
    if (depth < SET_STACK_MAX) {
        stack[depth++] = page;
    }
    lv_obj_scroll_to_y(page->list, 0, LV_ANIM_OFF);
    show_top();
}


static void go_back(void) {
    if (depth > 1) {
        depth--;
    }
    show_top();
}


static void back_clicked_cb(lv_event_t * e) {
    go_back();
}


static void open_choice(row_t * source);
static void open_confirm(row_t * row);


static void row_clicked_cb(lv_event_t * e) {
    row_t * row = (row_t *) lv_event_get_user_data(e);
    switch (row->kind) {
    case ROW_LINK:
        if (row->target != NULL) {
            open_page(row->target);
        }
        break;
    case ROW_CHOICE:
        open_choice(row);
        break;
    case ROW_TOGGLE:
        row->set_bool(!row->get_bool(row->ctx), row->ctx);     // stores it
        refresh_row(row);
        break;
    case ROW_RESET:
        open_confirm(row);
        break;
    case ROW_OPTION:
        // Store only a change; either way back to the page, which shows the value
        if (row->index != row->source->get_index(row->source->ctx)) {
            row->source->set_index(row->index, row->source->ctx);
        }
        go_back();
        break;
    }
}


// A row: the whole row is the target; its label, and its value under it in orange
static row_t * add_row(settings_page_t * page, row_t * row, const char * symbol, const char * label) {
    row->button = value_editor_create_button(page->list, LV_PALETTE_ORANGE);
    lv_obj_set_width(row->button, lv_pct(100));
    lv_obj_set_style_pad_hor(row->button, SET_ROW_PAD_PX, LV_PART_MAIN);
    lv_obj_set_flex_flow(row->button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row->button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row->button, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(row->button, row_clicked_cb, LV_EVENT_SHORT_CLICKED, row);

    if (symbol != NULL) {
        lv_obj_t * icon = value_editor_create_label(row->button, &lv_font_montserrat_28);
        lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
        lv_label_set_text(icon, symbol);
    }

    lv_obj_t * column = lv_obj_create(row->button);
    lv_obj_remove_style_all(column);
    lv_obj_remove_flag(column, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 4, LV_PART_MAIN);

    row->label = value_editor_create_label(column, &lv_font_montserrat_24);
    lv_obj_set_width(row->label, lv_pct(100));
    lv_label_set_long_mode(row->label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(row->label, label);
    row->label_text = label;

    // A toggle's switch shows its state (its knob's side and colour), so its label gets the row's two lines
    if (row->kind != ROW_RESET && row->kind != ROW_OPTION && row->kind != ROW_TOGGLE) {
        row->value_label = value_editor_create_label(column, &lv_font_montserrat_20);
        lv_obj_set_width(row->value_label, lv_pct(100));
        lv_label_set_long_mode(row->value_label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(row->value_label, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    }

    if (page != choice_page && page->row_count < SET_ROWS_MAX) {
        page->rows[page->row_count++] = row;
    }
    return row;
}


static row_t * new_row(row_kind_t kind) {
    row_t * row = heap_caps_calloc(1, sizeof(row_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (row != NULL) {
        row->kind = kind;
    }
    return row;
}


void settings_row_link(settings_page_t * page, const char * symbol, const char * label, settings_text_fn value,
                       void * ctx, settings_page_t * target) {
    row_t * row = new_row(ROW_LINK);
    if (row == NULL) {
        return;
    }
    row->value = value;
    row->ctx = ctx;
    row->target = target;
    add_row(page, row, symbol, label);
}


void settings_row_choice(settings_page_t * page, const char * label, const char * const * options, int count,
                         int (*get)(void * ctx), void (*set)(int index, void * ctx), void * ctx) {
    row_t * row = new_row(ROW_CHOICE);
    if (row == NULL) {
        return;
    }
    row->options = options;
    row->count = count;
    row->get_index = get;
    row->set_index = set;
    row->ctx = ctx;
    add_row(page, row, NULL, label);
}


void settings_row_toggle(settings_page_t * page, const char * label, bool (*get)(void * ctx),
                         void (*set)(bool on, void * ctx), void * ctx) {
    row_t * row = new_row(ROW_TOGGLE);
    if (row == NULL) {
        return;
    }
    row->get_bool = get;
    row->set_bool = set;
    row->ctx = ctx;
    add_row(page, row, NULL, label);

    // The switch only shows the state: the row takes the tap
    row->sw = lv_switch_create(row->button);
    lv_obj_remove_flag(row->sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row->sw, SET_SWITCH_W_PX, SET_SWITCH_H_PX);
    lv_obj_set_style_bg_color(row->sw, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row->sw, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(row->sw, lv_color_white(), LV_PART_KNOB);
}


void settings_row_reset(settings_page_t * page, const char * label, const char * question,
                        void (*reset)(void * ctx), void * ctx) {
    row_t * row = new_row(ROW_RESET);
    if (row == NULL) {
        return;
    }
    row->question = question;
    row->reset = reset;
    row->ctx = ctx;
    add_row(page, row, NULL, label);
    lv_obj_set_style_bg_color(row->button, SET_RESET_COLOUR, LV_PART_MAIN);
}


/*
----------------------------------------------------
The choice list and the confirmation
----------------------------------------------------
*/

// A full-screen list of the options, titled with the setting; the current one ticked, in orange
static void open_choice(row_t * source) {
    lv_obj_clean(choice_page->list);
    lv_label_set_text(choice_page->title, source->label_text);
    int current = source->get_index(source->ctx);
    for (int i = 0; i < source->count && i < SET_ROWS_MAX; i++) {
        row_t * row = &option_rows[i];
        memset(row, 0, sizeof(*row));
        row->kind = ROW_OPTION;
        row->source = source;
        row->index = i;
        add_row(choice_page, row, i == current ? LV_SYMBOL_OK : NULL, source->options[i]);
        if (i == current) {
            lv_obj_set_style_bg_color(row->button, lv_palette_darken(LV_PALETTE_ORANGE, 2), LV_PART_MAIN);
        }
    }
    open_page(choice_page);
}


static void close_confirm(void) {
    lv_obj_add_flag(confirm_screen, LV_OBJ_FLAG_HIDDEN);
    confirm_row = NULL;
}


static void open_confirm(row_t * row) {
    confirm_row = row;
    lv_label_set_text(confirm_label, row->question);
    layout_confirm();
    lv_obj_remove_flag(confirm_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(confirm_screen);
}


static void confirm_reset_cb(lv_event_t * e) {
    row_t * row = confirm_row;
    close_confirm();
    if (row != NULL) {
        row->reset(row->ctx);   // stores the defaults
        refresh_page(stack[depth - 1]);
    }
}


static void confirm_cancel_cb(lv_event_t * e) {
    close_confirm();
}


static void create_confirm(void) {
    confirm_screen = lv_obj_create(root);
    lv_obj_remove_style_all(confirm_screen);
    lv_obj_set_size(confirm_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(confirm_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(confirm_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(confirm_screen, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(confirm_screen, SET_GAP_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_column(confirm_screen, SET_GAP_PX, LV_PART_MAIN);
    lv_obj_remove_flag(confirm_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(confirm_screen, LV_OBJ_FLAG_CLICKABLE);     // nothing behind it takes a tap
    lv_obj_add_flag(confirm_screen, LV_OBJ_FLAG_HIDDEN);

    confirm_label = value_editor_create_label(confirm_screen, &lv_font_montserrat_32);
    lv_label_set_long_mode(confirm_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(confirm_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    confirm_reset_button = value_editor_create_button(confirm_screen, LV_PALETTE_RED);
    lv_obj_set_style_bg_color(confirm_reset_button, lv_palette_darken(LV_PALETTE_RED, 2), LV_PART_MAIN);
    lv_label_set_text(value_editor_create_label(confirm_reset_button, &lv_font_montserrat_32), "Reset");
    lv_obj_add_event_cb(confirm_reset_button, confirm_reset_cb, LV_EVENT_SHORT_CLICKED, NULL);

    confirm_cancel_button = value_editor_create_button(confirm_screen, LV_PALETTE_GREY);
    lv_label_set_text(value_editor_create_label(confirm_cancel_button, &lv_font_montserrat_32), "Cancel");
    lv_obj_add_event_cb(confirm_cancel_button, confirm_cancel_cb, LV_EVENT_SHORT_CLICKED, NULL);
}


/*
----------------------------------------------------
Pages
----------------------------------------------------
*/

static settings_page_t * page_create(const char * title, bool with_back) {
    if (page_count >= SET_PAGES_MAX) {
        ESP_LOGE(TAG, "too many pages");
        return NULL;
    }
    settings_page_t * page = heap_caps_calloc(1, sizeof(settings_page_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (page == NULL) {
        return NULL;
    }

    page->obj = lv_obj_create(root);
    lv_obj_remove_style_all(page->obj);
    lv_obj_set_size(page->obj, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(page->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page->obj, LV_OBJ_FLAG_HIDDEN);

    page->header = lv_obj_create(page->obj);
    lv_obj_remove_style_all(page->header);
    lv_obj_remove_flag(page->header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    if (with_back) {
        page->back = value_editor_create_button(page->header, LV_PALETTE_ORANGE);
        lv_obj_t * arrow = value_editor_create_label(page->back, &lv_font_montserrat_40);
        lv_obj_set_style_text_color(arrow, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
        lv_label_set_text(arrow, LV_SYMBOL_LEFT);
        lv_obj_add_event_cb(page->back, back_clicked_cb, LV_EVENT_SHORT_CLICKED, NULL);
    }

    page->status = value_editor_create_label(page->header, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(page->status, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    char status[40];
    format_status(status, sizeof(status));
    lv_label_set_text(page->status, status);

    page->title = value_editor_create_label(page->header, &lv_font_montserrat_28);
    lv_obj_set_style_text_color(page->title, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_label_set_long_mode(page->title, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(page->title, title);

    // The rows: a column that snaps to row edges; the scroll bar in the right margin shows there is more
    page->list = lv_obj_create(page->obj);
    lv_obj_remove_style_all(page->list);
    lv_obj_set_style_pad_right(page->list, DISPLAY_MARGIN_PX, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page->list, SET_GAP_PX, LV_PART_MAIN);
    lv_obj_set_flex_flow(page->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page->list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(page->list, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(page->list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(page->list, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(page->list, 1, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(page->list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(page->list, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(page->list, LV_OPA_COVER, LV_PART_SCROLLBAR);

    page->body = lv_obj_create(page->obj);
    lv_obj_remove_style_all(page->body);
    lv_obj_remove_flag(page->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page->body, LV_OBJ_FLAG_HIDDEN);

    pages[page_count++] = page;
    return page;
}


settings_page_t * settings_page_create(const char * title) {
    return page_create(title, true);
}


lv_obj_t * settings_page_body(settings_page_t * page) {
    lv_obj_add_flag(page->list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page->body, LV_OBJ_FLAG_HIDDEN);
    return page->body;
}


settings_page_t * settings_ui_create(lv_obj_t * tile) {
    root = lv_obj_create(tile);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    settings_page_t * hub = page_create("Settings", false);
    choice_page = page_create("", true);
    create_confirm();

    lv_subject_add_observer_obj(&ui_wifi_state, status_observer_cb, root, NULL);
    lv_subject_add_observer_obj(&ui_battery_level, status_observer_cb, root, NULL);
    lv_obj_add_event_cb(root, root_size_changed_cb, LV_EVENT_SIZE_CHANGED, NULL);

    stack[0] = hub;
    depth = 1;
    return hub;
}


/** @brief Show the hub; the caller has added its rows */
void settings_ui_start(void) {
    layout_all();
    show_top();
}

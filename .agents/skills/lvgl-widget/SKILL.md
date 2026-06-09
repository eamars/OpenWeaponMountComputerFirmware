---
name: lvgl-widget
description: Select, implement, review, and debug LVGL 9.x widgets in this ESP-IDF firmware. Use this skill whenever writing or reviewing LVGL widget code, choosing between labels/buttons/dropdowns/sliders/arcs/bars/tables/tileviews/menus/textareas/switches/spinboxes, handling LVGL widget events or keys, managing label/static text lifetimes, styling widget parts/states, or checking widget-specific behavior. Apply it proactively for any main/*.c UI work that touches lv_*_create, lv_*_set_*, lv_obj_add_event_cb, LV_PART_*, LV_STATE_*, LV_EVENT_*, or enabled LVGL widgets, even when the user only asks for a small UI change.
---

# LVGL Widget

Use this skill to reason about LVGL widgets from the user interaction down to the concrete widget API, event contract, text/data lifetime, and enabled project configuration. For layout structure, rotation behavior, `LV_SIZE_CONTENT`, Flex/Grid, scroll direction, and overlap issues, also use the repository `lvgl-layout` skill. When editing C/C++ files, also use the repository `c-style` skill.

This project uses LVGL 9.x through `main/idf_component.yml` (`lvgl/lvgl: ^9.5.0`) with the common widget set enabled in `sdkconfig`, including labels, buttons, button matrices, arcs, bars, dropdowns, LEDs, lists, menus, rollers, sliders, spinboxes, switches, text areas, tables, tabviews, tileviews, windows, observer support, Flex, and Grid.

## First Pass

Start at the product behavior level before choosing or changing a widget:

- What is the user trying to see, select, edit, confirm, or navigate?
- Is the value read-only, momentary, toggleable, bounded numeric, text input, or one-of-many?
- Does the widget need touch only, encoder/key navigation, or both?
- Can its text/value update rapidly from a task or timer?
- Does it live in a tile, menu row, modal/prompt, status bar, or full-screen view?

Then inspect the local widget tree and callbacks:

```powershell
rg -n "lv_.*_create|lv_.*_set_|lv_obj_add_event_cb|lv_event_get_|LV_EVENT_|LV_PART_|LV_STATE_|lv_obj_set_user_data|lv_obj_get_user_data" main
```

Read the creator function, any event callbacks, and any update/enable functions that mutate the widget. Widget bugs often come from an API choice in the creator function being inconsistent with the later update path.

## Core LVGL Widget Model

All widgets are referenced as `lv_obj_t *`. Widget-specific constructors return the same base handle type, and generic `lv_obj_*` APIs still apply for size, style, flags, user data, events, and parent/child behavior.

Most widget docs are organized by:

- **Overview**: what the widget is for and what it creates internally.
- **Parts and Styles**: which `LV_PART_*` selectors can be styled.
- **Usage**: the widget-specific setter/getter APIs.
- **Events**: widget-specific events in addition to common object events.
- **Keys**: encoder/keypad behavior where supported.

Before inventing behavior, check the relevant widget page and the inherited base/common widget behavior. LVGL widgets often have built-in semantics, default group behavior, or lightweight virtual children that affect the right implementation.

## Widget Selection

Use the simplest widget with the right interaction contract:

- `lv_label`: read-only text, symbols, status text, headings, and values that are not directly edited.
- `lv_button` plus a child label or symbol: a momentary command, confirmation action, or checkable toggle with custom content.
- `lv_switch`: binary on/off setting when the current state should be visible without reading text.
- `lv_dropdown`: compact one-of-many selection where options can be hidden until interaction.
- `lv_roller`: one-of-many selection when the visible wheel affordance is useful and space allows.
- `lv_spinbox`: bounded numeric editing with digit control, often paired with plus/minus buttons.
- `lv_slider`: bounded numeric editing where approximate touch adjustment is acceptable.
- `lv_bar`: read-only progress, percentage, or range feedback.
- `lv_arc`: circular progress or knob-like value visualization; use carefully on small screens because it consumes stable square space.
- `lv_led`: small color/status indicator, not an event-emitting value control by itself.
- `lv_buttonmatrix`: many button-like choices in a memory-light grid; buttons are virtual, not child widgets.
- `lv_table`: rows/columns of text where cells do not need to be real child widgets.
- `lv_list`: vertical list made from buttons and labels when each row can be a simple command/text item.
- `lv_menu`: hierarchical configuration screens and menu pages; prefer local helper functions already present in `config_view.c`.
- `lv_textarea`: editable text or numeric-string input, including cursor, placeholder, password, accepted-character, and max-length support.
- `lv_tileview`: swipe navigation between screen-sized tiles.
- `lv_tabview`: tabbed organization when tabs are a better model than swipe-only tile navigation.
- `lv_msgbox` or a custom modal container: confirmation, warning, or short blocking prompt.

Prefer project helpers in `config_view.c` for configuration UI: menu containers, static labels, spinboxes, color picker, dropdowns, switches, save/reload/reset buttons, and info message boxes. Reusing those helpers keeps behavior consistent across settings pages.

## Text And Lifetime

Choose label/text APIs based on update frequency and lifetime:

```c
lv_label_set_text(label, "New text");
lv_label_set_text_fmt(label, "Value: %ld", value);
lv_label_set_text_static(label, persistent_buffer);
```

Use `lv_label_set_text` or `lv_label_set_text_fmt` for normal dynamic updates; LVGL copies the string. Use `lv_label_set_text_static` only when the buffer will remain valid for the widget lifetime or until replaced. String literals are safe for static text because they live in ROM.

For rapidly changing labels where text length changes often, prefer a persistent static buffer and `lv_label_set_text_static` to avoid repeated reallocations. Update only when the displayed value changes meaningfully.

Be careful with `LV_LABEL_LONG_MODE_DOTS`: LVGL edits the text buffer in place. Do not combine it with a ROM string passed through `lv_label_set_text_static`; use copied or writable text instead.

Constrained labels need both a width/grow policy and a long mode. Common local patterns:

```c
lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
lv_obj_set_flex_grow(label, 1);

lv_obj_set_width(label, lv_pct(100));
lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
```

## Events And User Data

Attach callbacks at the widget that owns the semantic event:

```c
lv_obj_add_event_cb(widget, callback, LV_EVENT_VALUE_CHANGED, user_data);
```

Inside callbacks, get objects and data deliberately:

```c
lv_obj_t * target = lv_event_get_target_obj(e);
void * user_data = lv_event_get_user_data(e);
lv_event_code_t code = lv_event_get_code(e);
```

Use `lv_obj_set_user_data` / `lv_obj_get_user_data` for object-associated state when that matches local patterns, such as tile enable callbacks or an LED's associated palette index. Keep the pointed-to data alive for at least as long as the object uses it.

Common widget event contracts:

- `lv_button`: no special value event unless made checkable; `LV_EVENT_VALUE_CHANGED` fires when checked state changes.
- `lv_dropdown`: `LV_EVENT_VALUE_CHANGED` fires when a new option is selected or the list opens/closes; `LV_EVENT_READY` on open and `LV_EVENT_CANCEL` on close.
- `lv_switch`: `LV_EVENT_VALUE_CHANGED` fires when the checked state changes.
- `lv_slider`: `LV_EVENT_VALUE_CHANGED` fires continuously while dragged or changed with keys; `LV_EVENT_RELEASED` fires once when released.
- `lv_spinbox`: `LV_EVENT_VALUE_CHANGED` fires when the numeric value changes.
- `lv_bar`: no special events or keys; treat it as read-only display unless you explicitly send your own event.
- `lv_table`: `LV_EVENT_VALUE_CHANGED` when a new cell is selected with keys.
- `lv_tileview`: `LV_EVENT_VALUE_CHANGED` after a new tile is displayed; use `lv_tileview_get_tile_active`.
- `lv_textarea`: `LV_EVENT_INSERT` before insertion, `LV_EVENT_VALUE_CHANGED` after content changes, and `LV_EVENT_READY` for Enter in one-line mode.
- `lv_label` and `lv_list`: no special events by default; labels are not clickable unless `LV_OBJ_FLAG_CLICKABLE` is added, and list button events come from the child buttons.

If a widget does not emit the semantic event you need, either send the event explicitly after changing its state, as this repo does for LED color picking, or attach the callback to the actual interactive child.

## Keys, Encoder, And Groups

Some widgets are added to the default group and support encoder/key editing by design. This matters if the target device uses buttons, encoder input, or keyboard navigation:

- Buttons are added to the default group and translate Enter into press/release events.
- Dropdowns are editable and support list-item selection via encoder/keyboard.
- Button matrices are editable; individual buttons are virtual and selected by index.
- Tables are editable and use arrow keys to select cells.
- Text areas process arrow keys and character input.
- Labels, lists, and tileviews do not process keys directly.

When adding or changing a control, preserve both touch behavior and any existing encoder/key behavior. Do not replace an editable control with a purely visual widget unless the interaction is intentionally removed.

## Parts, States, And Styling

Style the documented part/state selector instead of assuming the whole widget is a single rectangle:

```c
lv_obj_set_style_bg_color(button, lv_color_white(), LV_PART_MAIN);
lv_obj_add_style(slider, &knob_style, LV_PART_KNOB | LV_STATE_PRESSED);
```

Typical parts:

- `LV_PART_MAIN`: background/main body for most widgets.
- `LV_PART_INDICATOR`: progress fill, slider/bar indicator, dropdown symbol area, or similar indicator.
- `LV_PART_KNOB`: slider/arc knob where present.
- `LV_PART_ITEMS`: virtual cells/buttons/items in table and button matrix.
- `LV_PART_SCROLLBAR`: scrollbar for scrollable widgets.
- `LV_PART_SELECTED`: selected text/item/cell styling where supported.
- `LV_PART_CURSOR`: text area cursor.
- `LV_PART_TEXTAREA_PLACEHOLDER`: text area placeholder.

Use states for interaction appearance:

- `LV_STATE_PRESSED`, `LV_STATE_CHECKED`, `LV_STATE_FOCUSED`, `LV_STATE_DISABLED`, and combinations such as `LV_STATE_CHECKED | LV_STATE_PRESSED`.

Remember that the most recently added style has higher precedence, and local styles override normal styles. If a style property does not appear to work, check selector, state, part, add order, and inheritance before replacing the widget.

## Lightweight Widgets And Virtual Children

Some widgets do not create real child widgets for the pieces the user sees:

- `lv_buttonmatrix` buttons are virtual items controlled by a map and control flags.
- `lv_table` cells are virtual text cells, not real labels.
- `lv_dropdown` creates its list when opened; use `lv_dropdown_get_list` to style the list object after it exists.

Do not try to find or style non-existent children. Use the widget-specific map, cell, selected-index, list, and control APIs.

For `lv_buttonmatrix`, maps must end with `NULL` or `""`, `"\n"` creates a new row, button IDs ignore newline/terminator entries, and control maps must match the number of real buttons.

For `lv_table`, cells store text only; convert numbers to text first. Table text is copied, rows/columns can be created automatically, and setting smaller dimensions than intrinsic size makes the table scrollable.

For `lv_dropdown`, newline-separated options are copied by `lv_dropdown_set_options`; static options must remain alive and cannot be extended with `lv_dropdown_add_option`.

## Project Patterns

The firmware builds LVGL views directly in `main/*.c`:

- `create_*_view(parent)` creates widgets, styles them, attaches callbacks, and stores object pointers when later updates need them.
- Config pages are assembled through `lv_menu` and helper functions in `config_view.c`.
- `main_tileview.c` uses `lv_tileview_add_tile`, tile user data, and `LV_EVENT_VALUE_CHANGED` to enable/disable tile-specific behavior.
- Status/value labels often use static buffers declared with `HEAPS_CAPS_ATTR` and `lv_label_set_text_static`.
- Buttons often use `lv_obj_set_style_bg_image_src(button, LV_SYMBOL_..., 0)` for symbol-only controls.
- Several views update LVGL objects from task-like flows; preserve the project's `esp_lvgl_port` locking pattern when touching cross-task UI updates.

Prefer the modern LVGL 9 names already used in this repo, such as `lv_button_create`. If nearby code uses a compatibility alias like `lv_btn_create`, avoid broad churn unless the task is explicitly a modernization cleanup.

## Review Checklist

Before finalizing widget changes, check:

- The selected widget matches the user interaction, not just the visual shape.
- The needed widget is enabled in `sdkconfig`.
- Text lifetime is correct for `set_text`, `set_text_fmt`, `set_text_static`, dropdown static options, and table cell values.
- Rapid label updates avoid unnecessary allocation churn when practical.
- Long labels have a concrete width/grow policy and a long mode.
- Event callbacks listen to the widget that actually emits the event.
- Callback user data and object user data point to storage with sufficient lifetime.
- Checkable/toggle widgets update both LVGL state and backing config/state.
- Virtual child widgets are handled through widget-specific APIs, not child traversal.
- Parts/states selectors match the widget documentation.
- Encoder/key behavior is preserved when relevant.
- UI updates from FreeRTOS tasks use the repository LVGL lock pattern.
- Layout/rotation/scroll concerns have been checked with the `lvgl-layout` skill.
- Code follows the repository `c-style` skill when C/C++ files are edited.

## Source Basis

This skill is based on the official LVGL Open widget and common widget documentation:

- https://lvgl.io/docs/open/widgets
- https://lvgl.io/docs/open/widgets/button
- https://lvgl.io/docs/open/widgets/label
- https://lvgl.io/docs/open/widgets/buttonmatrix
- https://lvgl.io/docs/open/widgets/bar
- https://lvgl.io/docs/open/widgets/dropdown
- https://lvgl.io/docs/open/widgets/slider
- https://lvgl.io/docs/open/widgets/spinbox
- https://lvgl.io/docs/open/widgets/switch
- https://lvgl.io/docs/open/widgets/table
- https://lvgl.io/docs/open/widgets/textarea
- https://lvgl.io/docs/open/widgets/tileview
- https://lvgl.io/docs/open/common-widget-features/styles/overview
- https://lvgl.io/docs/open/common-widget-features/layouts/flex

The source docs describe the full built-in widget set, widget inheritance from the base object, widget parts and states, specific event/key behavior, text lifetime rules, lightweight virtual widgets, and memory/performance notes relevant to embedded LVGL firmware.

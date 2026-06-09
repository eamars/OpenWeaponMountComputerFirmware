---
name: lvgl-layout
description: Understand, review, and improve LVGL UI layout in this ESP-IDF firmware. Use this skill whenever working on LVGL views, create_*_view functions, set_rotation_* handlers, tileview/menu layouts, Flex/Grid containers, LV_SIZE_CONTENT/lv_pct sizing, scroll behavior, hidden/floating layout flags, or UI overlap/responsiveness issues. Apply it proactively when the user asks to make the UI layout better, cleaner, more rotation-friendly, or easier to maintain, even if they do not explicitly mention "layout".
---

# LVGL Layout

Use this skill to reason about LVGL layout from the parent container down through its children, then make focused changes that fit this firmware's direct C style.

This project uses LVGL 9.x through `main/idf_component.yml` (`lvgl/lvgl: ^9.5.0`) with `CONFIG_LV_USE_FLEX=y` and `CONFIG_LV_USE_GRID=y` in `sdkconfig`. Also apply the repository `c-style` skill when editing C/C++ files.

## First Pass

Start with the user-facing layout goal at a high level before editing:

- What screen or widget tree is affected?
- Which orientations need to work: 0/180, 90/270, or all rotations?
- Is the UI meant to fill a tile, scroll, overlay another view, or size to content?
- Which content can grow, wrap, scroll, or hide at runtime?

Then inspect the concrete LVGL tree:

```powershell
rg -n "create_.*view|set_rotation_|LV_EVENT_SIZE_CHANGED|lv_obj_set_(flex|grid|layout|size|width|height|align|pos)|LV_LAYOUT_|LV_FLEX_|LV_GRID_|LV_SIZE_CONTENT|lv_pct|LV_PCT|IGNORE_LAYOUT|FLOATING|HIDDEN|FLEX_IN_NEW_TRACK" main
```

Read the creator function, the rotation handler, and any callback that hides, deletes, moves, or resizes children. Layout bugs in LVGL often come from parent/child assumptions being split across these functions.

## Core LVGL Model

Layouts are set on a parent object and manage that parent's children. A layout can overwrite child `x`, `y`, and sometimes width or height. Treat manual child coordinates as secondary unless the child is intentionally outside the layout.

Use:

```c
lv_obj_set_layout(container, LV_LAYOUT_FLEX);
lv_obj_set_layout(container, LV_LAYOUT_GRID);
```

or the simple helpers:

```c
lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(container, main_place, cross_place, track_cross_place);
lv_obj_set_grid_dsc_array(container, column_dsc, row_dsc);
lv_obj_set_grid_cell(child, column_align, column_pos, column_span,
                     row_align, row_pos, row_span);
```

The Flex and Grid helper APIs make the parent a layout container if it is not already one.

## Choose The Layout

Use Flex when the structure is one-dimensional:

- toolbar, status bar, menu row, card list, button row
- vertical stack of screen sections
- wrapping row/column of same-kind controls
- one child should take the remaining space with `lv_obj_set_flex_grow`

Use Grid when the structure is two-dimensional:

- controls need stable rows and columns
- labels and inputs should line up across rows
- a child spans multiple rows or columns
- available space should be split predictably with `LV_GRID_FR(x)`

Use manual alignment only when layout is the wrong abstraction:

- a full-screen drawing/canvas object
- a label centered inside a fixed button/card
- an overlay, modal, badge, or object that should not consume layout space
- custom drawing where coordinates are calculated from `lv_obj_get_coords` or content size

When mixing manual alignment with a parent layout, add the right flag and explain why:

- `LV_OBJ_FLAG_IGNORE_LAYOUT`: parent layout ignores the child; normal coordinates apply.
- `LV_OBJ_FLAG_FLOATING`: like ignoring layout, and also ignored by `LV_SIZE_CONTENT` calculations.
- `LV_OBJ_FLAG_HIDDEN`: hidden children are ignored by layout calculations.
- `LV_OBJ_FLAG_FLEX_IN_NEW_TRACK`: Flex starts a new track before this child.

## Project Patterns

The firmware builds views imperatively in `main/*.c`:

- `create_*_view(parent)` creates objects, styles them, attaches callbacks, and returns or stores object pointers.
- `set_rotation_*` functions mutate layout flow, size, scroll direction, object order, and alignment for the current `system_config.rotation`.
- `LV_EVENT_SIZE_CHANGED` callbacks reapply rotation-dependent layout.
- Tile screens are created under `main_tileview.c`; many views need to fill their tile with `lv_pct(100)`.
- Config UI should reuse helpers in `config_view.c` where practical: menu rows, spinboxes, color picker, switches, save/reload/reset buttons, and info message boxes.

For rotation-aware screens, keep all rotation layout policy in one `set_rotation_*` function and call it both during creation and from the size-change callback. Avoid scattering orientation conditionals through child setup unless the child is itself a separate reusable widget.

Typical rotation shape:

```c
static void set_rotation_example_view(lv_display_rotation_t rotation) {
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(top_container, lv_pct(100), lv_pct(40));
        lv_obj_set_size(bottom_container, lv_pct(100), lv_pct(60));
    } else {
        lv_obj_set_flex_flow(parent_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(top_container, lv_pct(50), lv_pct(100));
        lv_obj_set_size(bottom_container, lv_pct(50), lv_pct(100));
    }
}

static void example_view_rotation_event_callback(lv_event_t * e) {
    set_rotation_example_view(system_config.rotation);
}
```

## Flex Guidance

For a Flex parent, set flow first, then alignment, then child sizes and grow:

```c
lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(container,
                      LV_FLEX_ALIGN_SPACE_BETWEEN,
                      LV_FLEX_ALIGN_CENTER,
                      LV_FLEX_ALIGN_CENTER);
```

Main axis follows the flow: row means horizontal, column means vertical. Cross axis is perpendicular. The third alignment argument distributes wrapped tracks, so it matters only when there can be multiple tracks.

Use `lv_obj_set_flex_grow(child, 1)` for the child that should take leftover main-axis space, such as labels in menu rows or the main menu beside a status bar. Grow behaves best when the parent has a concrete main-axis size. Be careful with grow inside `LV_SIZE_CONTENT` parents because content sizing and remaining-space distribution can interact in surprising ways.

Use container padding for gaps:

```c
lv_obj_set_style_pad_row(container, 4, LV_PART_MAIN);
lv_obj_set_style_pad_column(container, 8, LV_PART_MAIN);
```

Prefer `LV_FLEX_FLOW_ROW_WRAP` or `LV_FLEX_FLOW_COLUMN_WRAP` when wrapping is expected from available space. Use `LV_OBJ_FLAG_FLEX_IN_NEW_TRACK` for an intentional break in an otherwise sequential flow.

## Grid Guidance

Declare row and column descriptor arrays with a final `LV_GRID_TEMPLATE_LAST`. Keep descriptors alive for at least as long as the object uses them; use `static` arrays for normal screen layouts.

```c
static int32_t column_dsc[] = {
    LV_GRID_FR(1),
    LV_GRID_CONTENT,
    LV_GRID_TEMPLATE_LAST
};
static int32_t row_dsc[] = {
    LV_GRID_CONTENT,
    LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST
};

lv_obj_set_grid_dsc_array(container, column_dsc, row_dsc);
lv_obj_set_grid_cell(label,
                     LV_GRID_ALIGN_START, 0, 1,
                     LV_GRID_ALIGN_CENTER, 0, 1);
```

Grid children are not automatically assigned to cells. Every child that should participate in the grid needs `lv_obj_set_grid_cell`.

Use track sizes deliberately:

- pixel values for fixed touch targets or hardware-constrained areas
- `LV_GRID_CONTENT` when the track should fit its largest child
- `LV_GRID_FR(x)` when remaining space should be split proportionally

Sub-grid can make a wrapper use its parent's descriptors, but LVGL only resolves it one level deep and does not handle parent `LV_GRID_CONTENT` tracks through the sub-grid. Prefer simple explicit grids unless the wrapper is needed for styling or event handling.

## Size And Scroll Policy

Make container intent explicit:

- Full tile/page: `lv_obj_set_size(obj, lv_pct(100), lv_pct(100))`
- Content-height row: `lv_obj_set_height(obj, LV_SIZE_CONTENT)` with a concrete width
- Fill remaining Flex space: concrete parent size plus `lv_obj_set_flex_grow(child, 1)`
- Fixed control target: pixel height/width is acceptable for touch controls
- Repeated cards: stable pixel/card size plus a scroll direction when content can exceed the container

Disable scroll on fixed composition containers:

```c
lv_obj_set_scroll_dir(container, LV_DIR_NONE);
```

Enable only the intended direction for lists and strips:

```c
lv_obj_set_scroll_dir(card_list, LV_DIR_HOR);
lv_obj_set_scrollbar_mode(card_list, LV_SCROLLBAR_MODE_OFF);
```

For labels in constrained rows, set a width or allow grow, then set a long mode:

```c
lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
lv_obj_set_flex_grow(label, 1);
```

For multi-line text, use wrap and a concrete width:

```c
lv_obj_set_width(label, lv_pct(100));
lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
```

Avoid solving overlap by adding arbitrary `x/y` offsets inside a laid-out parent. First fix the parent layout, sizes, padding, grow, scroll direction, or hidden/floating flags.

## Review Checklist

Before finalizing LVGL layout changes, check:

- Parent layout choice matches the structure: Flex for row/column, Grid for 2D.
- Children do not rely on `lv_obj_align`, `lv_obj_set_pos`, or `lv_obj_center` while a parent layout is meant to control them, unless ignored/floating intentionally.
- Rotation function handles flow, child sizes, order, alignment, and scroll direction together.
- The view is initialized with the same layout policy used after `LV_EVENT_SIZE_CHANGED`.
- Hidden children can disappear without leaving gaps; floating overlays do not change content size.
- `LV_SIZE_CONTENT`, `lv_pct`, and flex grow are not fighting each other.
- Long labels have a width/grow policy and long mode.
- Fixed pixel sizes are justified by touch target, visual design, or hardware constraints.
- UI updates from FreeRTOS tasks use the project's LVGL lock pattern.
- Changes match existing local style and helper APIs.

## Source Basis

This skill is based on the official LVGL Open layout documentation:

- https://lvgl.io/docs/open/common-widget-features/layouts
- https://lvgl.io/docs/open/common-widget-features/layouts/overview
- https://lvgl.io/docs/open/common-widget-features/layouts/flex
- https://lvgl.io/docs/open/common-widget-features/layouts/grid

Those pages describe parent-assigned layouts, built-in Flex and Grid layouts, layout-affecting flags, Flex flow/alignment/grow/gaps, Grid descriptors/cells/FR units/sub-grid, and the relevant edge cases.

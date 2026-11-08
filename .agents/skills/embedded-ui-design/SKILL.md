---
name: embedded-ui-design
description: Design rules for small touchscreen UIs on this firmware's AMOLED panel (and small embedded touch screens generally), drawn from smart-watch platform guidelines (Wear OS, watchOS, Garmin) and this project's measured design language. Use before proposing, building or reviewing any LVGL view, settings page, dialog or control on the device; when sizing touch targets, choosing colours, fonts or corner radii; or when a UI is "too small", "too busy" or hard to use with one thumb.
---

# Embedded UI design (small touch screens)

The device: ESP32-S3, AMOLED 280 x 456 px, about 0.078 mm a pixel (about 21.8 x 35.6 mm active area), panel corners 30 px radius, used in portrait and landscape (the owner uses rotation 1), mounted on a rifle: outdoors, one thumb, possibly gloves, read at a glance during a timed stage. LVGL 9.

Unit conversion for watch guidance: one watchOS point (2/326 inch) and one Wear OS dp (192 dp across a 1.2 inch round screen) are each about 0.16 mm, about **2 px on this panel**. Any watch number in pt or dp is roughly twice that number in our pixels.

## Rules

1. **Black background, light content.** Every view, settings included, on pure black; white text; colour only where it means something (state, the one primary action). No large white or saturated areas.
   - Wear OS requires a black background for all apps (WO-V13) and has a dark theme only. Garmin: AMOLED devices use light text on black; dark-on-light suits only reflective MIP screens, not emissive AMOLED. Black also saves power on OLED and hides the bezel.
   - Exception by design: the level view's full-screen level colour is the level's message, not decoration.
2. **Touch targets at least 122 px (9.5 mm) on the short side; gaps of 20 px between them; 8 px from the screen edge.**
   - Wear OS minimum 48 x 48 dp (WO-V2), about 98 px. watchOS reportedly 44 x 44 pt default, 28 x 28 pt minimum (research agent; the Apple page did not load for checking). Thumb studies: 9.2 to 9.6 mm (Parhi et al. 2006). Cars: 76 dp with 23 dp gaps. Gloves: about 15 to 20 mm (flight-deck study; MIL-STD-1472 via secondary sources).
   - Our 122 px is above every watch minimum and at the thumb-study size. A full-width row (264 px, 20.6 mm) reaches the glove size in one direction; for glove use, trade width for height, not area.
   - Landscape fits exactly two 122 px rows in the 264 px height (122 + 20 + 122).
   - **Exception, full-width list rows in portrait: 96 px** (7.5 mm, Wear OS's 48 dp minimum), three a screen under a 92 px header. The owner chose it by hand over 2 rows of 154 px (2026-11-07): a full-width row has 2.3 times the area of a 122 px square (width traded for height). Buttons and cells stay at 122 px.
3. **One action per row, the whole row is the target.** Full-width rows, not a label with a small control at its end. A setting row shows its label and its current value; tapping the row acts.
   - Wear: toggle chips (whole chip toggles), split chips only when two actions are needed. Garmin: toggles for on/off, other choices open a list with the current value as subtext. AOSP settings: the value as a line under the title.
4. **No drop-downs, no spin boxes.** A choice opens a full-screen list of large rows (radio style, the current one marked); a number opens the shared `value_editor` (big value, minus and plus at least 122 px, hold to repeat, Done). Wear steppers and pickers are full-screen; watchOS shows a long picker as a button that opens the list; AOSP prefers a radio screen over a drop-down.
5. **Changes take effect when made; no Save, no Reload.** A toggle or choice applies and is stored at once (only if it changed: one write per change, never on open or leave). Watch platforms have no Save/Cancel for settings; NN/g: toggles take immediate effect. Wear WO-V5: no data lost on back navigation.
6. **Confirm only what destroys.** Reset, clear, forget: a full-screen yes/no question with two large buttons, the destructive one in red. Everything else needs no confirmation. (Garmin confirmations; Wear alerts used sparingly.)
7. **Shallow: a hub and one level down.** About 7 rows a page at most (Garmin), any setting within three to four taps (Garmin), minimal hierarchy depth (Apple). Rarely used or engineering settings (log level, sensor reports) go to the bottom or to a separate "Advanced" page, not among match settings.
8. **Two text sizes, no long scrolling labels.** Values large (montserrat 32 to 48, the digit fonts for numbers); labels montserrat 20 to 24; nothing under 20 px (watchOS minimum 12 pt reportedly, Wear 12 sp for essential text = about 24 px; our 20 px is the floor for secondary captions only). Medium weight. Text wraps or is shortened; never `LV_LABEL_LONG_MODE_SCROLL_CIRCULAR` (a moving label cannot be read at a glance).
9. **One corner rule.** Panels and buttons use `display_concentric_radius(margin)`: 30 px minus the margin, 22 px at the 8 px margin (concentric corners: the inner radius is the outer radius minus the padding, Apple WWDC25 session 356). Nothing may be cut by the panel's corners (Wear WO-V16): keep text out of the corner arcs (`display_corner_clearance`).
10. **Back is always one obvious move.** A visible back target of full size, plus swipe where the view allows it. Swipes are unreliable with gloves and wet hands (Garmin), so the visible target is the primary way. Wear: swipe-to-dismiss from almost every screen (WO-V3).
11. **Show where you are and that something happened.** A page title (it may scroll away with the list), a scroll indicator while scrolling (Wear WO-V8), the changed value visible on the row the moment it changes. Respond to a tap within 0.25 s; anything over 2 s shows progress (Android for Cars).
12. **Lists lock to a grid: never half a row** (owner, 2026-11-07). A scrolling list shows a whole number of rows: every row in it is one height, as many as fit (`rows = (viewport + gap) / (min_row + gap)`), stretched to fill the viewport exactly (`display_locked_row_height`), and the list snaps to row starts (`lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_START)`), so every scroll stops on row edges, the last one too. Row height therefore follows the orientation. With no half row hinting at more, the list shows that it scrolls (a scroll indicator, Wear WO-V8).
13. **Show a row only while it does something** (owner, 2026-11-07). A row that has no effect in the unit's present state is not shown, so the page never offers a dead target: set-up only while the unit waits to be set up, Forget only once it has a network, a gain only for the style that uses it, a trigger level only while its sensor report runs. `settings_row_shown_when(page, shown)` after the row: checked when the page shows, after a toggle and twice a second; the page relays its grid when a row comes or goes. Keep it to the row's own page: a row is not hidden because of a setting on another page (the owner would not find why it went).
14. **Glance budget: 2 seconds.** A match view must be read in one glance (Android for Cars, NHTSA's 2 s glance). The level stays the primary feature of the level view; every addition is checked against obstruction.

## This project's design language (measured, ADR-006 and ADR-007)

- Background `lv_color_black()`; inactive cells `lv_palette_darken(LV_PALETTE_GREY, 3)`; pressed `lv_palette_main(LV_PALETTE_GREY)`; primary or selected `LV_PALETTE_ORANGE` (main or darken 2); destructive `LV_PALETTE_RED` darken 2 to 3; captions `lv_palette_lighten(LV_PALETTE_GREY, 1)`; text white.
- Buttons and cells: `value_editor_create_button()` (radius `display_concentric_radius(DISPLAY_MARGIN_PX)`, no shadow) and `value_editor_create_label()`.
- Constants: `DISPLAY_MARGIN_PX` 8, `DISP_CORNER_RADIUS_PX` 30 (DO0164FMST05: R2.30 mm), gap 20 px, cell 122 px (`DOPE_CELL_PX`), editor value row 96 px.
- Fonts: montserrat 20 (captions), 32 (button labels, names), 40 to 48 (values), `font_dope_digits_64/84` (numbers read at a glance).
- Layout: grids per rotation (`set_rotation_*`), portrait stacks, landscape side by side; every UI is designed and shown in both orientations.
- Shared pieces to reuse, not copy: `value_editor` (numbers), the confirmation screen in `main/ui/views/dope_view.c` (clear), the timer tile's preset buttons.

## Relation to the other project skills

`lvgl-layout` and `lvgl-widget` cover the LVGL mechanics (Flex, Grid, rotation handlers, widget events, text lifetime); `c-style` the code. They point here for how a view looks and behaves. Settings pages are built with `settings_ui` (`main/ui/core/settings_ui.h`: pages, rows, choice lists, the value editor, confirmations), one file per page in `main/ui/settings/`; the old `lv_menu` settings and their helpers were removed in ADR-008. If any guidance still disagrees, this skill wins.

## Process

- Settle the design before building: proposals show **portrait and landscape**, sizes in px and mm, and say which rule each choice follows. Mock-ups go to the owner before code.
- Check on the unit, not on paper: capture with the remote control (`tools/remote_control/rc_run.py`, `snap`), in both rotations, and measure targets on the capture. Restore the owner's rotation (1) after tests.
- Remote-control taps act on whatever is on screen: check `state` (tile) before every tap, and never `swipe` on a page that cannot scroll (the swipe presses at the screen centre, and on a short page the release is a tap).
- Count NVS writes while testing: opening, leaving and scrolling must write nothing.
- Never chain blind taps on the owner's unit after a tap that can move or replace what is on screen (a toggle, Back, a choice): capture and look before the next tap, above all near Reset or Clear. A list that shifted by a row put Reset under a "flip it back" tap and reset the owner's Timer settings (ADR-008, 2026-11-07).
- LVGL: sizes read inside a size-change event are those before the current layout pass (a nested `lv_obj_update_layout` is ignored). Compute sizes from the screen and margins, or from the object's own size in its own event, and defer anything that needs the finished layout (`lv_obj_update_snap`) with `lv_async_call`.

## Sources

- Wear OS app quality (WO-V2 48 dp targets, WO-V3 swipe back, WO-V5 state, WO-V8 scroll bar, WO-V13 black background, WO-V14 12 sp text, WO-V16 shapes): https://developer.android.com/docs/quality-guidelines/wear-app-quality
- Wear OS buttons, lists, steppers, pickers, toggle chips, swipe-to-dismiss, colour: https://developer.android.com/design/ui/wear/guides/components/buttons, .../components/lists, .../components/steppers, .../components/pickers, https://developer.android.com/design/ui/wear/guides/m2-5/components/toggle-chips, .../components/swipe-to-dismiss, https://developer.android.com/design/ui/wear/guides/styles/color
- Apple HIG (accessibility, buttons, layout, pickers, typography, designing for watchOS): https://developer.apple.com/design/human-interface-guidelines/accessibility (numbers reported by the research agent, not checked: the page needs a browser)
- Apple concentric radius: https://developer.apple.com/videos/play/wwdc2025/356/
- Garmin Connect IQ UX (menus, confirmations, workflows and gloves, visual design and AMOLED): https://developer.garmin.com/connect-iq/user-experience-guidelines/
- AOSP settings guidelines: https://source.android.com/docs/core/settings/settings-guidelines
- Android for Cars design principles (2 s glance, 0.25 s response, 76 dp targets): https://docs.partner.android.com/drivingux/foundations/design-principles
- NN/g toggle switches: https://www.nngroup.com/articles/toggle-switch-guidelines/
- Parhi, Karlson, Bederson 2006, target size for one-handed thumb use: https://doi.org/10.1145/1152215.1152260
- Flight-deck touchscreen target sizes: https://eprints.nottingham.ac.uk/31399

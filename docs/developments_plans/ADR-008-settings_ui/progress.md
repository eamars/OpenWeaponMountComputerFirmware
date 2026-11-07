# ADR-008 progress

## Step 1: the frame, the hub, the Timer and Dope pages (2026-11-07)

The owner: "Let's build first and I will see if my finger will like larger or smaller items."

Built:

- **`settings_ui`** (`main/settings_ui.c/.h`): our own pages instead of `lv_menu`. A stack (hub, page, choice list); every page has the pinned header (Back, the Wi-Fi and battery status, the title in orange: across the top in portrait, a 122 px column on the left in landscape). Rows are full width and lock to a grid: all one height, as many as fit, stretched to fill the screen (`display_locked_row_height`), the list snapping to row starts; a thin scroll bar in the right margin. Row kinds so far: a link to a page (with an icon and a line saying what the page holds), a choice (opens a full-screen list, the current option ticked in orange; a tap stores a different option and goes back), a toggle (the whole row flips it; the switch shows the state), Reset (red, asks once on a full screen).
- **Rows a screen**: landscape 2 rows of 122 px; portrait 3 rows of 96 px (header 92 px) or 2 of 154 px, switched over the remote control (`rows 2|3`, memory only) for the owner to try both by hand.
- **Timer page**: Editor step, Start on first shot, Reset Timer (the step, the first-shot start and the presets). **Dope page**: Target names, Card size, Next target on recoil, Reset Dope settings (the targets stay). Each change is stored when it is made; no Save or Reload.
- **Hub**: Timer ("5 s step, first shot on"), Dope ("12 targets, 1, 2, 3"), More settings (the old menu, without Timer and Dope, under the new header until step 2).

On the unit (`rc_step1_look_*`, captures `s1b_*`): rows locked in both rotations (landscape 2 x 122 px, portrait 3 x 96 px, `rows 2` 2 x 154 px), the choice list, Back, More settings; opening and leaving pages wrote nothing.

Two faults found on the way and fixed:

- **Row heights from a stale size.** The first build sized the rows from the list's size inside LVGL's size-change event; inside a layout pass a nested layout update is ignored, so the size was the old one (hub rows 152 px in landscape, 122 px in portrait, half rows showing, a list left a row off its grid). Now the list's height comes from the screen's size and the margins, and the realignment to a row edge runs after the layout pass (`lv_async_call`). The dope tile's lock got the same deferred realignment.
- **Three-line toggle labels did not fit 96 px**: the On/Off text under a toggle repeated the switch; dropped.

**The owner's Timer settings were reset by the test.** In the first check, the list had shifted a row (the stale size above), so a scripted tap meant to flip Start on first shot back landed on Reset Timer, and the next scripted tap hit the confirmation's Reset: the Timer settings were stored at their defaults (step 5 s, first shot on, presets 1:30, 2:00, 1:00). Presets set by hand before need entering again. The skill's process now says: never chain blind taps after a tap that can move what is on screen; capture and look first, above all near Reset or Clear.

**Row size: the owner's choice by hand, 3 rows of 96 px in portrait** ("3 rows (96 px)"). The 2-row option and its remote-control switch are removed; portrait is fixed at 96 px.

Next: step 2 (the other pages).

# ADR-006: the timer view, and screen corners for every view

Status: **Draft** 2026-11-07, for the owner to decide.

## What we want

A countdown timer that is quick and sure to use on a small touch screen, mounted on a rifle: its settings tile laid out cleanly in every rotation, its widget on the level view never colliding with the dope strip, and every view placed with the panel's rounded corners in mind, by one rule rather than padding chosen by eye. No settings write when nothing changed.

## Owner's direction (2026-11-07)

1. The timer view comes first in the user-experience work; the dope view and settings usability follow.
2. The layout looks off, and the owner was not sure how LVGL layout works.
3. The AMOLED has rounded corners (see its datasheet). Padding at the edges is not how the design should handle them; treat the corners as a property of the screen that every view shares.
4. The timer widget overlaps the dope strip on the level view; propose something better.
5. The timer settings layout: propose a better one; usability comes first on a small touch screen.
6. Saving on swipe-away is a design issue: avoid unnecessary NVS writes.

## What we know (from the code, 2026-11-07)

The screen is 280 x 456 (portrait); the owner uses rotation 1, so the views below are 456 wide and 280 high.

**The timer widget on the level view** (`countdown_timer.c`, created in the level view's overlay): a 200 x 200 round button with the progress arc and a 48-pt label. Landscape: `LEFT_MID`, 20 px in, so it spans y 40 to 240. Portrait: centred. Tap pauses, resumes, or restarts an expired timer; long press restarts.

**The dope strip** (`dope_config_view.c`): 80 px high, the full width, aligned to the bottom (y 200 to 280 in landscape), cards 70 x 55. In landscape it overlaps the timer by 40 px: both are placed by hand on the same overlay, so nothing stops them meeting. In portrait they miss each other (timer y 128 to 328, strip y 376 to 456), by the numbers rather than by design.

**The angle readout**: an 80 px wide button, `TOP_MID` +20 in portrait, `RIGHT_MID` (-20, -20) in landscape.

**The timer settings tile** (`countdown_timer_config_view.c`, swipe up from the level): two rollers (minutes 0 to 10, seconds 0 to 59) above two preset buttons ("2:00" and "1:30"). The rollers stay greyed out until a preset is selected. Tapping a preset selects it and starts the timer straight away, and the widget appears on the level; tapping it again stops and hides the timer. Turning a roller edits the selected preset and changes the running time. The presets are saved when the tile is swiped away.

**Why the layout looks off**: LVGL's flex layout places children one after another and adds the parent's padding and a gap between children; both come from the theme unless the code sets them. A size in percent is a percent of the parent's content area, which is the parent minus its padding. The tile sets its two halves to 38 % + 60 % (portrait) and 50 % + 48 % (landscape) and the rollers to 45 % + 45 %, so the gaps decide whether things fit, and the leftover space lands unevenly. Flex `grow` gives each child its share of what is left after the gaps, so the parts always fill the screen exactly; that is the fix, not new percentages.

**Rounded corners**: no code knows the panel's corner radius; the views keep away from the corners with fixed offsets (20 px here and there). The panel is DWO's DO0164FMST05 (1.64", 280 x 456, ICNA3311 / CO5300 driver, which the firmware drives as the SH8601 it is compatible with; datasheet in the hardware repository, `pcb/datasheet/DO0164FMST02_05.pdf`). Its outline drawing (page 6) gives the pixel area (AMOLED A.A) as 21.84 x 35.57 mm with four corners of R2.30 mm. That is a 0.078 mm pixel pitch, so the corner radius is 29.5 px: **30 px**. The cover glass's visible area (22.34 x 36.06 mm) is a little larger than the pixel area, so the black print hides no pixels; the rounded pixel area is the limit. A point on the 45-degree diagonal clears a corner 9 px in from both edges; today's 20 px offsets are more than twice that.

**NVS on swipe-away**: ESP-IDF's NVS already compares a blob with the stored one and skips the flash write when they are equal (`nvs_storage.cpp`, `writeItem`: "it is cheaper to purposefully not write out new data"). So an unchanged swipe-away writes nothing to flash today; what it costs is a read and compare, and a "Configuration write successfully" log line that says otherwise. The countdown and dope tiles also save when they are switched off at boot.

**Screen capture**: LVGL's snapshot is already built in (`CONFIG_LV_USE_SNAPSHOT=y`) and unused.

## What we will do, and why

### Step 1: a screen capture over the remote control

A `snap` command copies what the screen shows (`lv_snapshot`, one 255 KB RGB565 frame in PSRAM, freed after) and streams it to the host, where the runner saves a PNG with the panel's corner outline drawn on it. Why first: every later step is a layout change, and today the only way to see one is the owner's eyes on the unit. With `snap`, each change is checked on the real render, at every rotation, before the owner looks, and the ADR keeps before and after pictures. Cost: a debug command and about 255 KB of PSRAM while it runs.

### Step 2: the corners, one rule for every view

- One number: the panel's corner radius `DISP_CORNER_RADIUS_PX` (30, from the datasheet), next to the resolution in `app_cfg.h`. The corners are the panel's, so they are the same at every rotation.
- One small helper, `display_shape.c`, answers the only question a view has: how far in must this object sit to clear a corner? For an object whose edge lies `d` px from one screen edge, near a corner, the clearance along the other edge is `R - sqrt(R^2 - (R - d)^2)` for `d < R`, and 0 beyond. Anything centred on an edge (top middle, left middle) needs nothing; only what reaches into a corner square is moved, and only by what that corner takes.
- The rules for a view: edge-centred objects use no offset; corner-anchored objects take their offset from the helper; full-width strips (the dope strip, list rows) pad their ends by the clearance at their depth, so their contents can still scroll under the corner but come to rest clear of it.
- A debug outline (the panel's rounded rectangle, drawn on LVGL's top layer, off by default) shows the safe shape on the real screen and in every `snap`.
- This ADR applies the rule to the timer tile and the level view's overlay; other views move to it in their own ADRs.

### Step 3: the level view's overlay laid out by LVGL, not by hand

The overlay becomes a flex column: a content area that grows to fill, and below it the dope strip at its 80 px. While the strip is hidden the content area takes the whole screen (LVGL leaves hidden children out of a layout). The timer and the angle readout sit in the content area (side by side in landscape, stacked in portrait), and the timer's diameter follows the content area's height, up to 200 px: about 180 px in landscape with the strip shown, 200 px without. They cannot overlap, because the layout gives each its own space.

The alternative considered: shrink the timer to a small pill in a corner while the strip shows. It frees more space, but the timer jumps between two looks during a stage, which is harder to read at a glance; the layout keeps one look and only scales it.

### Step 4: the timer settings tile, rebuilt for the thumb

Proposed (owner to decide, see below):
- The tile shows the presets as large buttons, each with its time. **Tap a preset: the timer starts and the view goes back to the level**, so starting a stage is swipe up, tap.
- **Long press a preset to edit it**: an editor with the time in large digits and four large buttons, minutes -1 / +1 and seconds -5 / +5 (holding repeats), and Done. Buttons replace the rollers: a roller needs a short, precise fling on a 280 px high screen, and is easy to miss with gloves or while holding the rifle; four fixed targets are not.
- Stopping and hiding the timer moves to the timer itself: a long press on the widget on the level view. Restarting a stage is a tap on its preset again. Today stopping is done by unselecting the preset in the tile, which is not discoverable.
- The tile is laid out with flex grow (step 3's reasoning), so it fills the screen exactly in every rotation.

### Step 5: save only what changed

Each tile keeps a copy of what it last loaded or saved and saves on swipe-away only when its settings differ. NVS would skip the identical write anyway; this removes the read and compare, and the log line that wrongly reports a write. The dope tile gets the same.

## How the device shows it works

- `snap` pictures at rotations 0 to 3 for the level view (strip shown and hidden, timer running) and the timer tile, with the corner outline: nothing crosses the outline, nothing overlaps.
- The owner uses the timer tile and the widget by hand.
- Swiping away an unchanged timer or dope tile logs no save; a changed one saves once.

## For the owner to decide

1. ~~The panel's corner radius~~: 30 px, from the panel datasheet the owner pointed to (2026-11-07; CO5300 is SH8601-compatible).
2. **Starting a preset returns to the level view** (recommended), or stays on the tile.
3. **Editing**: buttons with -1 / +1 min and -5 / +5 s (recommended), or keep the rollers (1 s steps). Is 5 s fine enough?
4. **Stopping the timer**: long press on the widget stops and hides it (recommended; restart is a tap on the preset), or keep long press as restart.
5. **Number of presets**: two as today, or three.
6. **The order**: the steps as above (capture first), or start with the timer tile.

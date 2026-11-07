# ADR-007 progress

## Step 1: the dope data, the tile and the value editor, by hand (2026-11-07)

Built:
- **The card** (`dope_config_view.h`): 10 targets A to J; each an elevation and a wind hold in mil x 10 (signed: up/down, right/left), an optional gust (none until a Kestrel gives one), a range (0: not known) and flags (set, from the Kestrel). 10 bytes a target. The saved block changed size, so `DOPE` started empty once. Read in `app_main` with the other settings.
- **The shared value editor** (`value_editor.c`): the ADR-006 timer editor taken out of the timer tile, unchanged in look and rules (value with an optional title, minus, plus, Done; a press steps once, holding repeats and speeds up after a second; a value off the step snaps to it; Done or leaving the tile keeps the edit and the owner of the value decides whether to save). The timer tile now uses it.
- **The dope tile**: one row per target, dark like the timer tile. Portrait: the letter (and range, when known) over an Elevation cell and a Wind cell, 122 px tall; landscape: the letter in a column on the left and the two cells beside it. A "Clear all" row ends the list.
  - Tap a cell: the editor, titled "A  Elevation" or "A  Wind"; 0.1 mil a press, 0.5 mil a repeat after a second of holding. Elevation shows U or D in the editor; wind passes through zero from L to R.
  - An empty target shows "-"; its wind cell is darker and does nothing until the target has an elevation. A new target starts from the nearest set target before it (elevation and wind), since targets mostly go further out.
  - Long press a target (its letter or a cell): "Clear target B?" with Clear and Cancel, full-screen, 122 px or larger buttons. Clear all asks the same way.
  - Every change saves when it is made: Done after a change, Clear. Leaving the tile closes an open editor (keeping the edit) and drops an unanswered question; nothing else is saved there.
- The level's strip shows the set targets' elevations as before, from the new card, until step 2 replaces it.

On the unit (`rc_dope_flow_20261107_161250.log`, landscape, PASS):
- A: elevation held and tapped to 1.3, Done: one `DOPE` save; wind three taps to R0.3, Done: one save.
- B: the editor opened at A's 1.3 (and the target took A's R0.3); five taps to 1.8, Done: one save.
- Done without a change: no save. Leaving to the level and back: no save (the level's strip showed A 1.3 and B 1.8).
- Long press on B: the confirmation; Cancel: no save, B kept.

Portrait (`rc_dope_portrait_20261107_161332.log`, PASS): tile, editor and confirmation captured; no saves.

The timer after its editor moved (`timer_flow.py`, PASS): Done after a change saves once; leaving the tile saves nothing; arming returns to the level; disarming stays on the tile; restored to 2:00 with one save.

Reboot (`rc_dope_reboot_20261107_161419.log`): a requested `reboot` (`RTC_SW_CPU_RST`); boot reads `DOPE` and writes nothing; the tile shows A and B as entered.

Seen on the captures and fixed before the runs above: the theme's disabled state lightened the inactive wind cell (now a plain darker colour), and in portrait the letter sat close to the top-left corner (now 12 px in).

Left on the unit for the owner's hands-on check: A 1.3 / R0.3 and B 1.8 / R0.3 ("Clear all" removes them).

### Target names, A to J or 1 to 10 (owner's request, 2026-11-07)

The Kestrel's Target Card names its targets either way (its designator setting, ICD §7.4 kind 2 +6: 0 letters, 1 numbers); the unit now offers the same. A **Dope** page in the settings menu, second after Timer: "Target Names", a dropdown with "A, B, C" (default) and "1, 2, 3", and Save, Reload, Reset as on the other pages. The choice shows at once on the tile, the editor's title, the clear confirmation and the level's strip; Save keeps it (`DOPECFG`, its own namespace, so the card is untouched). The setting uses the Kestrel's values, so Pull (step 4) can follow the Kestrel's choice.

On the unit, landscape (`rc_nav_*`, 17:00 to 17:05): "1, 2, 3" picked: the tile (both rotations), the editor ("1 Elevation") and the level's strip (both rotations) showed numbers; Save: one `DOPECFG` write; after the restart from flashing, still numbers; back to "A, B, C" and saved: one write. The unit is left on A, B, C. Not captured: target 10's row (the bottom of the list); "10" is two digits in the 64 px letter column.


## Step 2: the level view's band, timer bar and dope (2026-11-07)

Built:
- **The band** (`level_band.c`): from 20 px below the level's ticks to the bottom edge (portrait from y 252, landscape from y 164), full width, text 16 px in from the sides. It shows while a target is set or the timer is armed; the level alone otherwise. The level view itself is unchanged: full-screen colour field, fully the level colour when level.
  - First row: the timer bar (8 px, its length the time left): orange while armed or running, red in the last 10 s and at the end, grey while paused part-way. The bar keeps its place while the timer is hidden, so the dope does not move.
  - Second row: the target's name (orange), "next B" (and the range when known), and the time at the right.
  - Then the holds: the elevation in a digits-only Montserrat at 84 px in portrait and 64 px in landscape (`font_dope_digits_*.c`, from LVGL's `lv_font_conv` 1.5.3, 4 bpp; digit height about 61 and 47 px), U or D after it; the wind (montserrat 48 / 40) and the gust after it when known. Portrait: elevation over wind; landscape: side by side.
- **Taps**: while the timer is armed or paused, a tap anywhere on the level outside the band and the tilt box starts it. The time and the bar (16 px extra touch margin) start, pause or continue it and a long press re-arms it, as the old ring did; the hardware button works the time label. A tap on the band shows the next target, a long press the previous; the first and last target stay put. The current target is RAM only.
- **The timer's ring is gone**: `countdown_timer.c` keeps its task and states and shows them through the band.
- **The tilt box**: centred at the top in both rotations; the digits centred with the minus sign hung to their left. A tap shows "Hold to set zero" for 1.5 s; a long press sets the zero **and saves it** (before, a tap set it in RAM, lost at the next restart unless the Digital Level page was saved; the zero is a calibration, so it is saved as after a tare). Shows "Zero set".
- **Next Target on Recoil** moved from the Digital Level page to the Dope page, off by default. Its field in the level's saved block stays as an unused placeholder, so that block keeps its size and the level's zero survives the update; `DOPECFG` grew and started from its defaults.

On the unit (`rc_band_flow_20261107_171257.log`, PASS), both rotations: the card shown; a preset armed from the timer tile (back on the level, full orange bar, 1:30); a tap above the band started it (1:28); a tap on the time paused it (grey bar); a tap on the band showed B (0.8 U, R0.2), a long press A again; no `DOPE` or `DLV` write while stepping; a tap on the tilt box showed the hint; disarmed from the timer tile. The Dope page shows Target Names and Next Target on Recoil (off). Distances measured on the captures: the bar 28 px below the ticks in both rotations; nothing in the corners.

Not checked by script, for the owner's hands-on check: the full-screen level colour under the band (the unit lay on its side), the hold on the tilt box (it changes the saved zero), the hardware button on the time, and Next Target on Recoil with a real shot.

Found on the way: the card read A 0.3 / 0.0 with B empty after the commit, though the scripts had left A 1.3 / R0.3 and B 1.8 / R0.3; no script edited it in between, so it is taken to be the owner's hand check. B was entered again (0.8, R0.2) for the next/previous check.

Owner's hands-on check (2026-11-07): everything else works. One change asked for: no background behind the band ("the white text is already pouncing enough on all colours on the AMOLED screen"). Removed: the band is now only a layout and a touch area. Captures with the timer armed in both rotations (`nobg_timer_20261107_172112`): the bar and the time straight on the level; disarmed afterwards. The card was empty at that point, so the dope was not on the captures.
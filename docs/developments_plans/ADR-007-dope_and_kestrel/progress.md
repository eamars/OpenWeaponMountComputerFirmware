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

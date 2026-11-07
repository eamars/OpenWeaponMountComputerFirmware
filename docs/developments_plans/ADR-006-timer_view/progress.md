# ADR-006 progress

## Step 1: screen capture over the remote control (2026-11-07, `0275ae8`)

- `snap` copies the active screen with `lv_snapshot` (RGB565, one 255 KB buffer in PSRAM, freed after printing), run-length encodes it and prints base64 lines (`@sd`) between `@snap <w> <h> rgb565rle` and `@ok snap <w> <h> lines=<n>`.
- The copy is rendered in the LVGL task through `lv_async_call`: the first version rendered it in the serial task and overflowed its stack (`rc_send_20261107_135238.log`, a stack overflow reset). If LVGL is stopped (idle), the command times out and withdraws its request under the LVGL lock, so a late copy is never leaked.
- The runner: `rc_run.py send snap` or `Session.snap(path)` writes a PNG to `logs/rc/snaps/` with the pixels the panel cannot show (outside its 30 px corners) tinted red. Standard library only (no Pillow in the venv).
- `tap x y hold_ms` holds up to 1.5 s, for a long press.
- A landscape frame of the level view is 24 data lines and arrives in well under a second.
- The owner asked to consider a live mirror over serial or the network; a capture on demand covers layout checks and autonomous tests, so a live mirror is left as a later option if a test needs motion.

## Step 2: corners (2026-11-07, `929ba7d`)

`DISP_CORNER_RADIUS_PX` (30) in `app_cfg.h`; `display_shape.c` gives `display_concentric_radius(margin)` (22 px at the 8 px margin) and `display_corner_clearance(depth)`; `outline on|off` draws the panel's outline on the top layer.

## Step 3: the timer tile, its editor and the Timer settings page (2026-11-07, `38b9072`)

Correction to the plan, found in the code: tapping a preset never started the timer. `countdown_timer_start()` loads the time and parks it paused (armed); the countdown starts with a tap on the timer on the level view, or the first shot when that setting is on. That suits a PRS start better than the plan's wording: arm the preset during "make ready", then at the beep tap the large timer or just shoot. Kept, and the README says so.

Built:
- Three presets fill the tile, default 1:30, 2:00, 1:00 (the saved block changed size, so it starts from these). Black background like the other views (the old tile had the theme's white); 8 px margin, 20 px gaps, button corners 22 px, concentric with the panel's.
- Tap: arms the timer with the preset, the preset turns orange, the view goes back to the level. Tap the armed preset: the timer stops and hides; the view stays. Long press: the editor.
- Editor: the time, minus and plus one step (a press steps once, holding repeats every 100 ms and moves 3 steps per repeat after 1 s), Done. A grid per rotation. A time off the step snaps to it. Done, or leaving the tile, keeps the edit and saves only if the time changed; an armed timer takes the new time unless it is running.
- Timer page, first in the settings menu: Editor Step (5, 10, 15 s) and Start on First Shot (moved from Digital Level, which loses that field and starts from its defaults). Save, Reload, Reset as on the other pages; Reload and Reset also redraw the presets on the tile.
- The timer settings are read in `app_main` with the other settings, so the tile and the settings page can be created in any order.

Measured on the captures (`pictures/`): presets 264 x 133 px portrait (20.6 x 10.4 mm) and 133 x 264 px landscape, 20 px apart, 8 px from the edges; editor minus and plus 122 x 152 px portrait (9.5 x 11.9 mm) and 140 x 122 px landscape (10.9 x 9.5 mm). Nothing reaches the red corner areas.

Behaviour, on the unit, landscape (`rc_timer_flow_20261107_140015.log`, PASS):
- long press 2:00, plus twice: the editor shows 2:10; Done: one `CT` save; the tile shows 2:10;
- leaving and re-entering the tile twice: no save;
- tap 1:30: back on the level (`tile=2,1`), the timer shows 1:30; tap the timer: running (1:28 after 2 s);
- tap the armed 1:30 again: the timer hides, the view stays on the tile;
- 2:10 back to 2:00: one save.

Settings page (`rc_nav_*`, 14:02 to 14:04): step set to 10 s through the dropdown and saved (one `CT` save): the editor shows -10 s / +10 s; set back to 5 s and saved. The first label, "Start Timer on First Shot", was cut off on the page; shortened to "Start on First Shot".

## Step 4: save only what changed (2026-11-07, `bfa7d5a`)

The timer tile saves on Done after a change and on the settings page's Save, nothing else. The dope tile keeps a copy of what NVS holds and saves on swipe-away only after a change. Reboot (`rc_nav_*` 14:04): boot reads `DLV`, `CT` and `DOPE` and writes nothing (before, every boot wrote `CT` and `DOPE`); visiting the dope and timer tiles writes nothing.

## Checks

- `scenario state_change` PASS after steps 1 to 4 (`rc_state_change_20261107_140419.log`): no error or warning lines, no timer or dope saves, rotation back to 1.
- Not seen by the owner yet: the tile, the editor and the Timer page by hand; the hold-to-repeat feel; Start on First Shot with a real shot.
- Seen on the way, not in scope: the settings menu is the theme's light grey with square corners (settings usability, later); on the level view the timer still overlaps the dope strip in landscape (the level view's ADR).

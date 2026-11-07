# ADR-006: the timer tile, and screen corners for every view

Status: **Draft** 2026-11-07, for the owner to decide.

## What we want

A countdown timer tile that is quick and sure to use at a precision rifle match, on a small touch screen mounted on the rifle, in portrait and landscape alike; every view placed with the panel's rounded corners in mind, by one rule rather than padding chosen by eye; and no settings write when nothing changed.

## Owner's direction (2026-11-07)

1. The timer comes first in the user-experience work; the dope view and settings usability follow.
2. The layout looks off, and the owner was not sure how LVGL layout works.
3. The AMOLED has rounded corners (see its datasheet). Padding at the edges is not how the design should handle them; treat the corners as a property of the screen that every view shares.
4. The timer settings layout: propose a better one; usability comes first on a small touch screen. Each button must be physically large enough, and the design must follow how a timer is used in a PRS match (research it).
5. Saving on swipe-away is a design issue: avoid unnecessary NVS writes.
6. Every proposal shows portrait and landscape.
7. The editor's step is a setting: 5, 10 or 15 s (owner, 2026-11-07: "15 s step seems huge").
8. Consider where each setting belongs. Nothing important is saved yet, so an NVS reset of moved settings is accepted (owner, 2026-11-07).
9. The level view stays as it is in this ADR: its timer widget and the dope strip (which overlap in landscape) are to be designed together, as one system, later.

## What we know

### The screen (panel datasheet, 2026-11-07)

The panel is DWO's DO0164FMST05: 1.64", 280 x 456, ICNA3311 / CO5300 driver, which the firmware drives as the SH8601 it is compatible with (datasheet in the hardware repository, `pcb/datasheet/DO0164FMST02_05.pdf`). Its outline drawing (page 6) gives the pixel area (AMOLED A.A) as 21.84 x 35.57 mm with four corners of R2.30 mm:

- Pixel pitch 0.078 mm: **10 mm is 128 px**. The whole screen is 21.8 mm wide: two targets of about 10 mm fit side by side, three along the long side.
- Corner radius 29.5 px: **30 px**. A point on the 45-degree diagonal clears a corner 9 px in from both edges; the views' 20 px offsets today are more than twice that.
- The cover glass's visible area (22.34 x 36.06 mm) is a little larger than the pixel area, so the black print hides no pixels; the rounded pixel area is the limit.

### How a timer is used at a PRS match (research, 2026-11-07)

- Each stage has a par time, given in the match book with the course of fire. PRS rules ask match directors for "a nearly uniform par time for most stages"; in practice most stages are 90 to 120 s, 105 s happens, and longer stages are exceptions ([PRS 2026 rules](https://s3.us-east-1.amazonaws.com/impactscoring-uploads/rulebooks/prs_rules.pdf), [Precision Rifle Blog](https://precisionrifleblog.com/2024/02/20/new-upgrades-to-the-prs-barricade-skills-stage/), [American Rifleman](https://www.americanrifleman.org/content/american-rifleman-guide-precision-rifle-series), [Rifle Shooter](https://www.rifleshootermag.com/editorial/precision-rifle-a-beginners-guide/83624)). The skills stage used to be fixed at 90 s.
- The RO starts the shooter: "Load and/or make ready", "Stand by", "Engage", then within 1 to 3 s the beep of the RO's shot timer; "Time" when the par time runs out (PRS rules 3.3.6). The official time is the RO's; a shooter's own timer is for pacing, knowing how much is left at each position change. Timing out is a common new-shooter problem ([Snipershide](https://www.snipershide.com/shooting/posts/12238000), [Bullet Central](https://bulletcentral.com/blog/speed-and-timing-in-prs-train-with-a-timer)).
- At "Stand by" the shooter is behind the rifle, hands on it; at the beep they move or shoot at once. Starting a personal timer is a single movement at the beep, or hands-free (an Apple Watch PRS timer starts on a wrist shake for that reason, [Precision Match Timer](https://apps.apple.com/us/app/-/id1519610860)). This unit can already start on the first shot's recoil (level setting "Auto Start Countdown Timer on Recoil"), which starts late by the time to the first shot.
- Other timed moments: the squad's stage walk-through, at most one minute (rule 3.3.5); and practice, where shooters run stages against a countdown and shorten it, for example 180 s down to 90 s ([Outdoor Life](https://www.outdoorlife.com/at-home-precision-rifle-hunting-drills/)).
- So: a match runs on one or two par times, set once per stage or once per match, between stages when there is time; the start happens under stress, with one hand, at the beep; and the values are multiples of 15 s almost always.

### Touch target size (research, 2026-11-07)

Phone guidelines put a target at about 9 mm (Apple 44 pt, Material 48 dp). MIL-STD-1472 asks 15 mm bare-handed and 20 mm with gloves, with 3 mm between targets ([summary](https://specification.website/spec/accessibility/touch-target-size/)). On a 21.8 mm wide screen, 15 mm leaves room for one target per row. This ADR sets **9.5 mm (122 px) on the short side as the smallest target, above the phone guidelines, and 1.5 mm (20 px) between targets**, with fewer, larger targets wherever the screen allows; the action taken at the beep gets the largest targets on the tile (10.4 x 20.6 mm).

### The timer tile today (`countdown_timer_config_view.c`)

Swipe up from the level: two rollers (minutes 0 to 10, seconds 0 to 59) above two preset buttons ("2:00" and "1:30"). The rollers stay greyed out until a preset is selected. Tapping a preset selects it and starts the timer straight away; tapping it again stops and hides the timer. Turning a roller edits the selected preset and changes the running time. Rollers are 45 % of their half each, about 105 px wide in portrait (8 mm) and need a short vertical fling to land on one of 60 values.

**Why the layout looks off**: LVGL's flex layout places children one after another and adds the parent's padding and a gap between children; both come from the theme unless the code sets them. A size in percent is a percent of the parent's content area, which is the parent minus its padding. The tile sets its halves to 38 % + 60 % (portrait) and 50 % + 48 % (landscape) and the rollers to 45 % + 45 %, so the gaps decide whether things fit, and the leftover space lands unevenly. Flex `grow` gives each child its share of what is left after the gaps, so the parts always fill the screen exactly; that is the fix, not new percentages.

### NVS on swipe-away

ESP-IDF's NVS already compares a blob with the stored one and skips the flash write when they are equal (`nvs_storage.cpp`, `writeItem`: "it is cheaper to purposefully not write out new data"). So an unchanged swipe-away writes nothing to flash today; what it costs is a read and compare, and a "Configuration write successfully" log line that says otherwise. The countdown and dope tiles also save when they are switched off at boot.

### Screen capture

LVGL's snapshot is already built in (`CONFIG_LV_USE_SNAPSHOT=y`) and unused.

## What we will do, and why

### Step 1: a screen capture over the remote control

A `snap` command copies what the screen shows (`lv_snapshot`, one 255 KB RGB565 frame in PSRAM, freed after) and streams it to the host, where the runner saves a PNG with the panel's corner outline drawn on it. Why first: every later step is a layout change, and today the only way to see one is the owner's eyes on the unit. With `snap`, each change is checked on the real render, in portrait and landscape, before the owner looks, and the ADR keeps before and after pictures.

### Step 2: the corners, one rule for every view

- One number: `DISP_CORNER_RADIUS_PX` (30), next to the resolution in `app_cfg.h`. The corners are the panel's, so they are the same at every rotation.
- **Concentric corners**: a view keeps a margin `m` from the screen edge, and whatever sits in a corner gets a corner radius of `R - m`, so its outline runs parallel to the screen's corner and nothing is cut, without giving up the edges. This is how phones and watches place content in rounded screens. With an 8 px margin, corner buttons get a 22 px radius.
- For objects that are not rounded (text, icons) near a corner, one helper in `display_shape.c` gives how far in they must sit: for an edge `d` px from one screen edge, the clearance along the other is `R - sqrt(R^2 - (R - d)^2)` for `d < R`, and 0 beyond. Anything centred on an edge needs nothing.
- Full-width strips that scroll pad their ends by the clearance at their depth, so their contents rest clear of the corner but can scroll under it.
- A debug outline (the panel's rounded rectangle on LVGL's top layer, off by default) shows the shape on the real screen and in every `snap`.
- This ADR applies the rule to the timer tile; other views move to it in their own ADRs.

### Step 3: the timer tile, rebuilt for the thumb

Drawn in portrait (280 x 456) and landscape (456 x 280) in the proposal pictures; 8 px margin, 20 px gaps, concentric corners.

- **Three presets, as the whole tile.** Portrait: three full-width buttons stacked, 264 x 133 px (20.6 x 10.4 mm). Landscape: three side by side, 133 x 264 px. Each shows its time large. Defaults 1:30, 2:00 and 1:00 (the two common par times and the walk-through); the owner's current two values are kept as the first two.
- **Tap a preset: the timer starts and the view goes back to the level.** At "Stand by" the shooter has the tile up; at the beep, one tap on a 10 x 20 mm target starts the timer and shows the level with the time running. Tapping the running preset again stops and hides the timer, as today. The recoil start stays as it is.
- **Long press a preset to edit it.** The editor shows the time large and has three buttons: **minus and plus one step** (holding repeats, faster after a second), and **Done**. The step is a setting (below), 5 s by default. Portrait: the time on top (96 px), minus and plus side by side below (122 x 148 px each, 9.5 x 11.5 mm), Done across the bottom (264 x 156 px). Landscape: the time and Done on the left, plus above minus on the right (140 x 122 px each, 11 x 9.5 mm). Range one step to 10:00. Why two step buttons and no minute buttons: par times sit a few steps apart (1:30 to 2:00 is six taps at 5 s, two at 15 s, and holding repeats), so two large buttons beat four smaller ones or a roller. Rollers need a short, precise fling; fixed targets do not.
- **The step setting**: 5, 10 or 15 s, on a new Countdown Timer page in the settings menu (the same page pattern as the others: Save, Reload, Reset). A time that is not on the step (1:37 with a 10 s step) snaps on the first press: plus gives 1:40, minus 1:30. The step is saved with the presets (`CT`); that changes the block's size, so the loader treats the old block as missing and starts from the defaults once (accepted by the owner).
- Editing happens between stages, so a long press (not reachable by a stray tap) and a separate editor are acceptable; starting happens at the beep, so it is the largest and simplest action on the tile.
- The tile and editor are laid out with flex grow, so they fill the screen exactly in every rotation, and the rotation handler only switches the flow (column in portrait, row in landscape).

### Where each setting belongs

Today the settings menu follows the code modules (System, Digital Level, Sensor, WiFi, Power Management), so a user looking for "start the timer on my first shot" must know it is in Digital Level. Proposed rule: a setting lives with the feature the user thinks of when they want to change it; settings used during a match stay on the feature's tile, settings set once go in the menu; switches only an engineer should touch go to one Developer page at the bottom.

| Setting | Today | Proposed |
|---|---|---|
| Timer presets | Timer tile | Timer tile (edited there, this ADR) |
| Timer step (new) | - | **Timer** page (this ADR) |
| Start timer on first shot | Digital Level | **Timer** page (this ADR) |
| Advance dope card on shot | Digital Level | Dope page (dope ADR) |
| Recoil trigger level | Sensor | Sensor, renamed "Shot detection sensitivity" (settings ADR) |
| Level style, roll and pitch gain, level threshold, four colours | Digital Level | Digital Level |
| Screen rotation, brightness | System | **Display and power** page (settings ADR) |
| Idle timeout (screen off), sleep timeout (power off) | Power Management | **Display and power** (settings ADR): they are what the user sees, not the PMIC |
| VBUS current limit, charge current, charge voltage | Power Management | **Battery** page, with the battery readings (settings ADR) |
| Wi-Fi on/off, reset provisioning, QR code | WiFi | WiFi; the Wi-Fi window (600 s, not in the menu today) could join it (settings ADR) |
| Tare, sensor calibration | Sensor | Sensor |
| Global log level, remote control | System | **Developer** page (settings ADR) |
| Game rotation vector, linear acceleration, rotation vector reports | Sensor | **Developer** page (settings ADR): switching the game rotation vector off stops the level |

Menu order by how often a page is opened: Timer, Digital Level, Display and power, Sensor, WiFi, Battery, Developer, About. Two values have no menu entry today and need none: the level zero (set by tapping the angle) and the recoil edge.

This ADR does the timer rows: a Timer page with the step and "Start timer on first shot", moved out of Digital Level (both saved blocks reset once). The rest waits for the settings ADR, so the menu is reorganised once, by one rule, and the owner sees the whole of it together.

### Step 4: save only what changed

The timer tile saves when the editor's Done changes a preset, and only then; swiping away saves nothing. The dope tile keeps its swipe-away save but only when its settings differ from what it last loaded or saved. NVS would skip identical writes anyway; this removes the read and compare and the log line that wrongly reports a write.

## Not in this ADR

The level view: the timer widget (200 px round, `LEFT_MID` in landscape) and the dope strip (80 px, bottom) overlap by 40 px in landscape, because both are placed by hand on the same overlay. The owner wants the widget and the dope strip designed together as one system, in the dope ADR.

## How the device shows it works

- `snap` pictures of the tile and the editor in portrait and landscape, with the corner outline: nothing crosses it, every button measures at least 122 px on its short side.
- The owner uses the tile at a stage-like start: tile up, tap at a beep, level shows the time.
- Swiping away the timer tile logs no save; Done after a change saves once; the dope tile saves only after a change.

## For the owner to decide

1. ~~The panel's corner radius~~: 30 px, from the panel datasheet (2026-11-07; CO5300 is SH8601-compatible).
2. **Starting a preset returns to the level view** (recommended), or stays on the tile.
3. ~~Editing step~~: a setting, 5, 10 or 15 s, default 5 s (owner, 2026-11-07).
4. **Three presets** (recommended: 1:30, 2:00, 1:00), or two.
5. **The order**: capture first (recommended), then corners, tile, saving.
6. **Settings placement**: the map above as the plan, with only the timer rows in this ADR (recommended), or the whole menu reorganised here.

# ADR-008: the settings pages, in the design language of the other views

Status: **Open** 2026-11-07. The owner: "Let's build first and I will see if my finger will like larger or smaller items." Step 1 built ([progress](progress.md)); the owner chose 3 rows of 96 px in portrait by hand. Left: steps 2 and 3.

## What we want

The settings work, but they are hard to use: light theme on an AMOLED, controls too small for a thumb, and a look unlike the views rebuilt in ADR-006 and ADR-007. The settings get the same design language: dark, large targets, the panel's corner rule.

## Owner's direction (2026-11-07)

- "In most cases it works, and the only thing that doesn't work well is UI."
- Perhaps the dark mode works better on AMOLED; today it is the bright mode.
- "UI elements are just too small to handle." The design language of the recent views should carry over.
- Research small-screen devices (smart watches) first, and record the knowledge as the embedded UI design skill: done, `.agents/skills/embedded-ui-design/SKILL.md`.
- The same screen corner radius design.
- On the first draft (a hub of 122 px tiles): keep the status bar; the tiles show too little of what each page holds, perhaps the Apple Watch's list of full-width rows works better; show the busiest page as the example. The skill lives in `.agents/skills/` with the project's other skills; outdated skills may be removed (the old settings guidance in `c-style`, `lvgl-layout` and `lvgl-widget` was updated, `python-venv`'s root fixed).
- Lock each list to a grid: no half element at the end of a scroll; the dope tile has the same problem.

## What we know

### Research (2026-11-07, sources in the skill)

- Watch numbers convert at about 2 px a pt or dp on this panel. Wear OS requires touch targets of at least 48 x 48 dp (about 98 px, WO-V2) and a black background for all apps (WO-V13); Garmin: light text on black for AMOLED. Thumb studies put one-handed targets at 9.2 to 9.6 mm; gloves need 15 to 20 mm. Our 122 px (9.5 mm) and 20 px gaps are above every watch minimum.
- Settings on watches: full-width rows, one action per row, the current value shown on the row; choices open a full-screen list or a stepper, not a drop-down; toggles apply at once, with no Save; only destructive actions ask for confirmation; a hub and one level down, about 7 rows a page.
- Text: about 24 px for essential text (12 pt / 12 sp minimum), two sizes, no moving labels.

### The settings today (`config_view.c` and the page files, captures 2026-11-07, portrait)

- An LVGL `lv_menu`: a light grey hub of 9 rows (Timer, Dope, Kestrel, System Config, Digital Level, Sensor Config, WiFi Config, Power Management, About) and an "Update Available" button when one exists. Rows about 42 px tall (3.3 mm), all with the same gear icon. LVGL 9.6 marks `lv_menu` deprecated ("build menu navigation from base widgets instead").
- Pages: a label row then a control: drop-downs 36 px tall, switches, spin boxes (minus, a text field showing a cursor, plus), colour pickers (arrows around an LED), single icon buttons (a warning sign for Reset Provision and the calibrations).
- Labels longer than the row scroll in a loop and cannot be read at a glance ("on First Shot S", "emote control", "Provision Res").
- Every page ends in Save, Reload and Reset, as icon-only buttons (a disk, an upload arrow, a warning sign); a change is in memory until Save. The result shows in a light message box with OK.
- Back is a blue button at the bottom (portrait) or the top (landscape); a status bar shows the Wi-Fi and battery icons.
- The settings tile is swiped left and right to the other tiles, so a swipe cannot also mean "back" inside the settings.
- Found on the way: About shows garbage for "OTA1 Ver" when the other app slot holds no app.

The settings are only views: every setting's storage (its NVS namespace and block) stays as it is, so nothing saved is lost or reset by this ADR.

## What we will do, and why

### Look (rules 1, 2, 8, 9 of the skill)

Black background, grey panels (`lv_palette_darken(GREY, 3)`), white text, orange for values and the selected choice, red for destructive actions; buttons and rows from `value_editor_create_button()`, corner radius 22 px (30 minus the 8 px margin); labels montserrat 24, values 24 in orange, titles 28 to 32; long labels wrap to two lines, never scroll.

### Navigation: our own pages instead of `lv_menu`, as a watch's settings list

- **Every settings screen has a pinned header** (the Apple Watch's): portrait, 96 px across the top: Back (96 x 96, top left; none on the hub) and, right-aligned, the status line (Wi-Fi, battery) over the page's title in orange. Landscape: a 122 px column on the left: Back (122 x 96) over the status line and the title. The status bar stays on every screen.
- **The hub is a list of full-width rows**, like every page: an icon, the page's name, and one line saying what it holds ("Timer / 5 s step · first shot on", "Kestrel / HUD · HUD-8626470"). "Update available" is an orange row at the top when an update exists.
- **Rows lock to a grid** (owner): a list shows whole rows only. Every row of a list is one height: as many as fit the screen, stretched to fill it exactly (`display_locked_row_height`), and the list snaps to row starts, so no scroll stops on half a row. Row height follows the orientation: portrait, under a 92 px header, 3 rows of 96 px; landscape, 2 rows of 122 px. 96 px is 7.5 mm, Wear OS's 48 dp minimum and below our 122 px rule on the short side; a full-width row has 2.3 times the area of a 122 px square (width traded for height, Android for Cars). The alternative, 2 rows of 154 px in portrait, shows a third less. With no half row hinting at more, each list shows a scroll indicator. **The owner tried both by hand and chose 3 rows of 96 px (2026-11-07).**
- **The dope tile gets the same lock** (it had half rows too): landscape stays 2 rows of 122 px, now snapping; portrait becomes 2 rows of 210 px (cells 164 px). Prototyped first, before the settings, for the owner to feel: on the unit every scroll stops on whole rows in both rotations (the row heights are worked out when the list itself changes size; worked out in the rotation handler they used the old size). Every row fits within one screen. The owner keeps 2 rows in portrait (a 3-row tile would need the letter and range moved into the cells).
- The busiest page, Level (9 rows: a choice, 3 numbers, 4 colours, Reset), is 3 screens in portrait and 5 in landscape.
- The first draft's grid hub showed 4 to 6 pages at once but only a word of state each; the rows show the state in full.

### Rows (rule 3 and 4)

One action per row, the whole row 122 px tall and full width, its label and current value on it:

- **Choice** (step, card size, rotation, brightness, log level, timeouts, currents): opens a full-screen list of 122 px rows, the current one marked; a tap stores it and goes back.
- **Toggle** (Start on First Shot, Wi-Fi, remote control, sensor reports): the row toggles; a large switch drawing shows the state.
- **Number** (display gains, level threshold, recoil trigger level): opens the shared `value_editor` (big value, minus and plus, hold to repeat, Done), as the dope tile does.
- **Colour** (the level's four colours): opens a full-screen grid of colour swatches.
- **Action** (Find Kestrels, Show provisioning QR code, Install update, tare and sensor calibration): the row runs it; a long or destructive one asks first.
- **Status** (Wi-Fi status, battery, Kestrel status, versions): text, not a target, read only.

### Saving: a change is stored when it is made (rule 5)

No Save and no Reload. A choice, toggle or number is stored when it changes (once, only if it changed; opening, scrolling and leaving write nothing). Each page ends with **Reset** (red), which asks once on a full screen ("Reset Timer settings?", Reset / Cancel) and stores the defaults. The message box goes: the changed row shows the new value at once. Recommended over keeping Save: a choice is already a deliberate screen, and a forgotten Save is today's silent loss of a change.

### Pages (rule 7: about 7 rows, match settings first)

| Hub tile | Rows |
|---|---|
| Timer | Editor step, Start on first shot, Reset |
| Dope | Target names, Card size, Next target on recoil, Reset |
| Kestrel | Mode; HUD name or Your Kestrel and Find Kestrels; status; Reset |
| Level | Level style, Roll display gain, Pitch display gain, Level threshold, Left / Right / Level / Foreground colour, Reset |
| Display | Screen rotation, Screen brightness, Reset |
| Wi-Fi | Status, Wi-Fi on, Provisioning QR code, Forget network (asks), Reset |
| Power | Battery and supply status, Idle timeout, Sleep timeout, Reset |
| Advanced | Log level, Remote control, Recoil trigger level, the three sensor reports, Tare calibration, Sensor calibration, VBUS current limit, Battery charge current and voltage |
| About | Versions and build; Install update when one exists |

Display takes rotation and brightness from System Config; Advanced takes the engineering settings that a shooter does not change at a match (log level, remote control, sensor reports and calibration, charging). Each setting keeps its storage; only where it is shown changes.

## Steps

1. **The frame and two pages.** The page stack (hub, pages, full-screen list, confirmation), the row kinds, store-on-change, Reset with its confirmation; the hub; Timer and Dope pages moved onto it. Check on the unit: captures in both rotations, targets measured, writes counted (one a change, none on open or leave).
2. **The other pages**: Kestrel, Level, Display, Wi-Fi, Power, Advanced, About, the colour grid; the About slot-version fix.
3. **Remove the old**: `lv_menu`, the drop-down, spin box, colour picker and Save/Reload/Reset helpers, and the message box. The owner's check by hand.

## Not in this ADR

The level view, the timer and dope tiles (already in this design language), the provisioning flow itself, and the update flow beyond its row.

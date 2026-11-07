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

## Step 2: every page on the new frame (2026-11-07)

Built:

- **Row kinds added** to `settings_ui`: a number (the shared value editor, Done stores a change only), a colour (a choice whose options carry a swatch), an action (asks first when it has a question; red when destructive), a status line (text, not a target, refreshed twice a second while shown). Pages can rebuild their rows (`settings_page_clear`, `settings_page_relayout`).
- **Pages**, each change stored when made, each ending in its Reset:
  - **Kestrel**: the mode; HUD mode shows this unit's HUD name and "How to pair" (a page of text); phone mode shows the picked Kestrel and "Find Kestrels nearby", which opens a list filled in as the search hears them (a tap picks); the pull's status. A mode change cancels a running pull and rebuilds the page.
  - **Level**: style, roll and pitch display gain and the level threshold (the value editor, in tenths), the four colours (all 19 palette colours and black, with swatches), Reset (the zero stays).
  - **Display**: rotation, brightness (taken from System Config).
  - **Wi-Fi**: status, Wi-Fi on, the set-up QR code (its own page), Forget network (asks, red; it is the Wi-Fi page's reset).
  - **Power**: battery and supply readings, idle and sleep timeouts.
  - **Advanced**: log level, remote control, recoil trigger level, the three sensor reports, tare calibration (asks), USB current limit, charge current and voltage (applied to the chip at once), Reset (all of these).
  - **About**: version; build date and ESP-IDF; the running slot and the other slot's version, or "empty" (it showed garbage: the other slot's description was read without checking the read).
  - **Hub**: "Update available" (orange, at the top, hidden until a newer firmware is found), Timer, Dope, Kestrel, Level, Display, Wi-Fi, Power, Advanced, About, each with a line saying what it holds.
- The old menu is gone from the tile. Not carried over: "Sensor Calibration" (its button did nothing). The power button's menu (Reboot, Power off) keeps working; it still uses the old message-box helper until step 3.

Fixed on the way (on the unit):

- **Scrolling past the end.** A snap to the start of one of the last rows scrolled past the list's end, leaving blank rows. The last screen's rows are no snap points now (`display_locked_list_snap`), on the dope tile too (and again when the card size hides rows).
- **Back left the list a row off.** After a page opened and closed, the hub came back a row lower. Each page now remembers its scroll when a page opens over it and Back restores it; rows no longer scroll themselves into view on focus; the shown list is realigned from its own grid (rounded to a row edge, kept within the last full screen), not with LVGL's snap update. Checked: the hub returns exactly where it was (`h_*` captures).

On the unit (`rc_s2_*`, captures `a_*` to `m_*`, portrait and landscape): every page opens and returns; the hub and Advanced stop on whole rows at both ends; the number editor shows the page's own format and Done without a change stores nothing; the colour list opens on the current colour, ticked. No settings were written during the checks: changing settings on the owner's unit was left to the owner. Not checked yet: changing each setting, the Resets and Forget network, the phone-mode search list, the QR page, the update row (no update published), the power button's menu.

### The owner's first look, and a check of every view

The owner's notes after using it, and what changed:

1. **Recoil trigger level too coarse**: it now steps by 1 m/s² (1 to 100; held for over a second, 5 at a time).
2. **"Advanced" broke onto two lines in landscape**: titles are now fitted to the header: 28 px in portrait when the title fits one line, else 24 px when every word fits the width, else 20 px; landscape 24 px or 20 px. No title breaks inside a word.
3. **Forget network and Reset Wi-Fi**: not the same. Forget network erases the stored network so the unit waits to be set up again; Reset Wi-Fi only turned Wi-Fi back on. Reset Wi-Fi is removed; Forget network is the page's reset.
4. **The question above Forget was too large**: confirmation questions are 24 px now.

Also changed while checking: the toggle's switch is 60 px wide and the sensor toggles' labels are shorter, so no toggle label needs three lines; status lines have the rows' 20 px inset.

**Every view checked, portrait (rotation 0) and landscape (rotation 1)**, through a remote-control test command that opens pages, rows, lists, editors and confirmations without touching the screen and never changes a setting (`settings home|open|row|back|scroll`; toggles and actions that do not ask are refused; back cancels an editor or confirmation). Each page was captured screen by screen, and each of its rows opened; the Kestrel page in all three modes (the mode set in memory only, put back to HUD). 62 captures in portrait, 73 in landscape; no setting written (no NVS write in the log). All clean after the fixes above: whole rows only, no title or label broken inside a word, nothing under the screen's corners. The update row stays hidden (no update published); the owner checks the update last.

### Rows shown only while they do something

The owner: set-up should hide once Wi-Fi is set up, and Forget network while it is not. The same check across every page found two more rows that did nothing in some states. A row can now say when it shows (`settings_row_shown_when`); it is checked when the page shows, after a toggle and twice a second, and the page relays its grid when a row comes or goes.

- **Wi-Fi**: Set-up QR code only while the unit waits to be set up with Wi-Fi on (the set-up network runs); Forget network only once it has a network.
- **Level**: Roll and Pitch display gain only for the Level style (the Block style does not use them).
- **Advanced**: Recoil trigger level only while Linear accel is on (recoil is detected from that report).
- **Hub**: "Update available" moved onto the same rule (it was shown by clearing its hidden flag without laying the hub out again).

Kept as they are: the Kestrel page already rebuilds by mode; Start on first shot (Timer) and Next target on recoil (Dope) also need Linear accel, but hiding a row because of a setting on another page would leave the owner not knowing where it went. The embedded UI skill has the rule (13).

On the unit (`rc_shown_*`, captures `logs/rc/snaps/shown`), portrait and landscape, no setting written: the Wi-Fi page shows Status, Wi-Fi and Forget network, no set-up row (the unit is set up), and the test hook refuses the hidden row; the hub starts at Timer (no update); Level shows the gains (Level style) and Advanced the recoil level (Linear accel on). The other states (not set up, Block style, Linear accel off) are the owner's to switch by hand.

### The level's dope band goes round the card

Owner: after the last target a tap should go to the first, and a hold on the first to the last. The band's tap and hold now wrap over the set targets (`level_band.c`, `step_target`). Recoil's next target (Dope: Next target on recoil) still stops at the last target: a further shot or a false trigger there would otherwise bring back the first target's dope during the stage. At the last target the band still shows no "next", as the sign that the card ends there.

On the unit (rotation 1, a card of 8 set targets: 1 to 6, 11, 12; captures `logs/rc/snaps/wrap`): taps 1 → 2 … 12 → 1, hold 6 → 5, hold on 1 → 12, tap → 1. Stepping is memory only; no setting written. Left on target 1.

**The owner's check by hand (2026-11-07)**: "I have tested most of the feature you mentioned" (changing settings, the Resets, Forget network, the Kestrel search list, the QR page, the rows shown by state). Step 2 committed (`3e07b4d`).

## Step 3: the power button's menu, and the old helpers removed (2026-11-07)

- **The power button's menu** (hold the power button) was the old light message box: two 200 x 60 px buttons (a red Reboot, a blue Power Off) and an OK. It is now a full black screen over whichever view shows, as the settings' confirmations: "Power", then Reboot (orange), Power off (red) and Cancel. Portrait: stacked, 95 px each; landscape: side by side under the title, Cancel on the left, 133 x 122 px each, their text 24 px (at 32 px "Power off" was cut). It follows a rotation while open.
- **Removed**: the old settings helpers in `config_view.c` (menu rows, spin box, colour picker, drop-down, Save/Reload/Reset buttons, switch, single button, message box, page rebuild) and the shared message box. `config_view.h` now declares only the settings tile; the level's colour count moved to `digital_level_view.h`. Forget network's result showed in that message box; the Wi-Fi page's status line and rows now show it, and a failure to erase the credentials is logged.
- **Remote control**: `settings power show|hide` opens and closes the menu for a capture; its buttons are never pressed by it.

On the unit (`rc_power_*`, captures `logs/rc/snaps/power`): the menu over the level and over the settings, portrait and landscape, and rotated while open; closed, the view underneath is as it was. No setting written. Its buttons (Reboot, Power off, Cancel) are the owner's to press.

The owner pressed the menu's buttons by hand: "Power menu works."

## The update (OTA), last (2026-11-07)

Owner: set up the OTA server and run the whole update; the screens first with mock data, then the full test.

**Screens.** The update's two screens were the old light design: the offer (small Cancel and Start Upgrade buttons, recoloured inline text) and the OTA tile (circular-scrolling labels, a default bar, a small Reboot). Both rebuilt in the settings' design:

- **The offer**: black, "Update available" (or "Important update") in orange, the version and the release note, scrolling with a scroll bar when long; Install (orange) and Cancel. Portrait: the text over the two buttons; landscape: Install over Cancel in a 150 px column on the right, the text beside them (over the buttons it had 90 px).
- **The OTA tile**: the step (Connecting, Downloading, Checking) in orange, a 24 px orange bar, "1113 K of 2375 K"; at the end "Update installed / Restart to run it" or "Update failed", the reason, the error's name on its own line (so it is not broken inside) and "The unit keeps its firmware"; Restart. Landscape puts Restart in a column on the right (under the text it did not fit).
- **The hub's row**: orange with its version in white (`settings_row_highlight`; orange on orange could not be read).
- The update task now waits for the screen (1 s) to show each step and the outcome; it used to skip them when the screen was busy, which could leave the tile without its Restart.

Checked with mock data (remote control `ota mock [critical] | offer show|hide | view connecting|downloading|done|failed`, memory only, no download), portrait and landscape (`logs/rc/snaps/ota_ui`).

**The server.** `scripts/ota_host.py` (the owner's) announces `owmc_update.local` over mDNS and serves the manifest and the build: now with the declared version, the note and the importance as options, a manifest file served as written (format tests), every request printed, only those two paths served (it served the whole folder), and the network's address (the host name's address can be another adapter's).

**Full update, three times, landscape, Install and Restart tapped on the real buttons** (`rc_ota_*`, `logs/rc/snaps/ota_full`): the check at boot finds the host, the hub shows the row and the offer the real note; Install downloads into `ota_1` with the progress shown, "Update installed", Restart, and the unit boots from `ota_1` (`Loaded app from partition at offset 0x720000`). No setting written.

- **The download was slow: 87 s for 2.4 MB (28 KB/s).** It read 512 B at a time and paused 20 ms after each read. Now it reads 4 KB at a time with a 1 ms pause (the task runs above the screen's; the pause keeps the screen and the idle tasks' watchdog served): 41.5 s, no watchdog warning. The rest is the flash and the Wi-Fi; not pursued.
- Found on the way: with progress shown four times a second, the finished screen read "2361 K of 2375 K"; the whole amount is now shown when the check starts. The final build went over the air and shows "2375 K of 2375 K".

**Manifest formats**, each with a restart (`rc_ota_fmt_*`, `logs/rc/snaps/ota_formats`):

| Manifest | Result |
|---|---|
| Critical (importance 1), newer | The offer at once, over the level; the hub's row |
| The same version (v0.0.1), by version | Not offered |
| Without `importance` | Rejected ("Failed to extract importance"); not offered |
| Cut-off JSON | Rejected ("Failed to decode json string"); not offered |
| Version "latest", by version | Rejected ("Cannot read the manifest version"); not offered |

A rejected manifest goes on to the second source, the central server's placeholder, which does not answer, as before. The log line for a version not newer said "newer than"; it now says "the same as or newer than".

Left on the unit: running from `ota_1` (the next USB flash goes back to `ota_0`); the hub shows "Update available" until the next restart (the host offered the same build whatever its version); rotation 1, the level.

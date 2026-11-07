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

## Step 3: the Kestrel pull in HUD mode, profiled (2026-11-07)

Built:
- **`components/kestrel_link`**: the LiNK protocol without BLE or tasks, written from the library's ICD 1.18: HDLC framing and CRC-16/X.25, packets, AES-128-CBC with a zero IV (PSA crypto: mbedTLS 4.1 in IDF 6.1 makes the old `mbedtls_aes` API private), the host side of the authentication, the SetFullSoln decoding. A self-test runs at each pull against the ICD's `:A+` frame and a recorded authentication (nonce to proof); it passes. (Its first run failed on the test's own expected length, 11 instead of 10; fixed.)
- **`kestrel_service.c`**: its own task; Bluetooth (NimBLE, peripheral only, one connection, host memory in PSRAM) is started by a pull and stopped after it. The unit advertises as `HUD-` and 7 digits from its Bluetooth address (this unit: HUD-8626470) with the HUD service and NK manufacturer data, serves Device Information, the HUD service and the LiNK service as the Kestrel's own, ACKs FreeToCommunicate, authenticates as the host, reads TgtInfo (the designator) and the 10 targets with `0x0093 [t][t][ff][00]`, disconnects. Only those commands are sent (ICD §6.1, SAFE, and the authentication). A pull is refused while Kestrel is off, during Wi-Fi provisioning, and in the OTA tile. Every phase is timed and logged, with memory checkpoints.
- **Kestrel page** (settings, after Dope): the mode (Off, HUD (Device mode); default Off), the HUD name with the pairing steps, a Pull button, and the status: the last result (holds as the unit will show them) and the last pull's timing per phase. **`kestrel` / `kestrel pull`** on the remote control.
- Config: `CONFIG_BT_ENABLED`, NimBLE peripheral only, `MAX_CONNECTIONS=1`, `MEM_ALLOC_MODE_EXTERNAL`, software coexistence (`sdkconfig.defaults.esp32s3`). With Bluetooth in, Kconfig turns off Wi-Fi's two IRAM speed options (`WIFI_RMT_IRAM_OPT`, `WIFI_RMT_RX_IRAM_OPT`). App 2.28 MB (was 2.0), 46 % of the partition free.

Found on the way: the first pairing connected (after about 25 s, when the owner picked the HUD) but the pull did not see it. As a peripheral, NimBLE reports a connection after asking the central for its features; the Kestrel refuses that (status `0x1a`, unsupported remote feature, a Bluetooth 4.0 radio does not accept it from a peripheral) though the link is up. A connection now counts when the link exists, whatever that status.

On the unit with the owner's Kestrel (`rc_kestrel_pull_20261107_175544.log` and the next two), six pulls after the one-time pairing, all complete:

| | start | connect | subscribe | open | auth | info | 10 targets | stop | total |
|---|---|---|---|---|---|---|---|---|---|
| ms, range | 18 to 19 | 201 to 609 | 759 to 1060 | 120 to 180 | 1000 to 1100 | 740 to 840 | 8581 to 8941 | 22 to 142 | 11.8 to 12.9 s |

- The targets are 70 % of a pull: 0.80 to 0.96 s each. The connection interval is 20 ms (the Kestrel's choice, unchanged), so that is 40 or more connection events per target for about a dozen exchanges: the time is the Kestrel computing each solution (the library measured about 0.2 s to ACK and 1 s per long reply), not the link.
- The Kestrel reconnected to the paired HUD within 0.2 to 0.6 s of advertising. A Kestrel switched off and on by the owner just before the pull: connect 266 ms, auth 1380 ms, total 12.9 s, so no slow first connection in HUD mode (the slowness the owner has seen may be the phone link; to compare in step 5).
- **Memory**: internal RAM with Bluetooth running 108 KB free (38 KB for the stack); the lowest since boot 75.9 KB (pass mark 60 KB); after every pull free RAM is back to the same byte (146,479 after each of five pulls in a row). Two costs not expected: with Bluetooth built in but off, 34 KB less internal RAM at boot (free 145 KB, was 181 KB; lowest since boot 111 KB, was 145 KB), and after the first pull the largest free internal block is 34 to 40 KB (68 KB at boot). Nothing failed for it; to look into.
- The 10 targets read: ranges 33.8 to 118 m, every one with an elevation and two windage holds. The owner (2026-11-07): the Kestrel fills unused targets with default values, so nothing in the data tells a used target from an unused one. Pull (step 4) has to be told how many targets to take.

Not checked: a pull refused during Wi-Fi provisioning (it would mean resetting the Wi-Fi credentials); the code checks the provisioning state before starting.

## Step 4: Pull into the card (2026-11-07)

Owner (2026-11-07): the Kestrel fills unused Target Card targets with default values, so a pull cannot tell which targets are set up; Pull asks how many. The Target Card can be named A to J or 1 to 10, and that can differ between cards, so every pull takes the Kestrel's naming.

Built:
- **The Kestrel row** at the top of the dope tile, while Kestrel is on (Kestrel page): the pull's status, and a **Pull** button (122 px tall; full width in portrait, 150 px wide beside the status in landscape).
- **Pull asks how many targets**: the shared editor, titled "Targets to pull", showing "A to E" (or "1 to 5"), 1 to 10, starting at the last count (saved with the Dope settings, default 5). Done pulls; leaving the tile drops the question and pulls nothing (`value_editor_cancel`).
- The service reads only those targets (a pull of 5 is about 4 s shorter than 10).
- **A complete pull replaces the card**: the first targets from the Kestrel (elevation, wind, gust and range; the Kestrel's negative-up, negative-right signs turned into the card's U and R; rounded to 0.1 mil and 1 m; marked from the Kestrel), the others cleared; the Target Names follow the Kestrel's designator; one `DOPE` save (and one `DOPECFG` save when the names change). A pull that fails leaves the card as it was. The result is put on the card once, whichever tile is showing (a result id), so `kestrel pull [n]` on the remote control fills the card too.
- The Kestrel page lost its Pull button (Pull is where its targets go) and shows the last pull.
- Found on the card and fixed: the elevation editor stopped at 40.0 mil, and the owner's card has a .22LR target at 700 m needing 62.4; the editor now goes to 99.9. The gust was shown with the wrong side on the level ("L1.3 / R1.4": the gust's number was made positive, which the wind format reads as right); a shared gust format now gives "L1.3 / 1.4", and the tile's caption is "Wind / 1.4" (the old "Wind, gust L1.4" was cut off in portrait).

On the unit with the owner's Kestrel (`rc_pull_card_20261107_181039.log`): the first try found no Kestrel in 60 s (it had gone to sleep): "Kestrel not found", the card unchanged. With the Kestrel on: Pull, "A to E", Done: 5 targets in 9.4 s (connect 864 ms, subscribe 1158, open 140, auth 1160, info 700, targets 5321); one `DOPE` save; the card A 457 m 30.8 U L1.3 / 1.4, B 700 m 62.4 U L2.3 / 2.4, C 549 m, D and E 97 m; the level's band and the tile in both rotations showed them.

The owner's two Kestrel checks (2026-11-07):
- **Naming follows the Kestrel** (`rc_pull_card_20261107_181421.log`): the owner switched the Kestrel to a Target Card named 1 to 10; Pull of 5 in 8.0 s; one `DOPE` save and one `DOPECFG` save ("target names now 1 to 10, as the Kestrel's"); the level ("1  77 m  next 2") and the tile showed numbers in both rotations.
- **A pull cut off** (`rc_pull_cutoff_20261107_182920.log`): `kestrel pull 10`, the owner switched the Kestrel off while the unit showed "Reading target 3/10": targets 1 to 3 read, the link dropped during target 4 (reason `0x208`, supervision timeout), the pull ended "Target not read" after 9.0 s, no save, the card unchanged (1 77 m 1.1 U R0.4 / 0.6, and the rest).

## Step 5: phone mode (2026-11-07)

Built:
- **The unit as the central**, as the phone app and the library's PC client: NimBLE's central and observer roles added (app +24 KB; internal RAM at boot 3 KB less: 144 KB free, lowest 110 KB). A pull connects to the picked Kestrel by its address, finds the LiNK service and its two channels and the CCCD, subscribes to indications, writes in 20-byte chunks with write-with-response, opens with GetPrivacyStatus as the reference client does (the status is logged; the owner keeps privacy off, so the PIN is never sent), then authenticates and reads as in HUD mode. The phases keep their names: open is the privacy status here.
- **Finding the Kestrel**: the Kestrel page in Phone mode shows the picked Kestrel, a "Find Kestrels nearby" button (8 s search) and the Kestrels heard, as buttons with their signal; a tap picks one, saved by address (a firing line has many). A Kestrel is a device with NK's company id (0x00ea) and a name not starting "HUD-". The company id and the name come in separate results (advertisement and scan response), so they are gathered per address and judged when the search ends; the first version required both in one result and found nothing.
- **Remote control**: `kestrel find`, `kestrel pick <n>`; `kestrel` reports what the last search found.

On the unit with the owner's Kestrel in its normal Bluetooth mode (`rc_phone_find_*`, `rc_phone_pull_20261107_*`, `rc_kestrel_pull_*`, 18:49 to 18:53): the search heard 9 devices and listed 2 NK devices with Kestrel names, the owner's Kestrel and a second one with the next serial (the owner said there is one Kestrel; the second is taken to be one of the owner's own test devices); the owner's was picked. Three pulls, all complete:

| | start | connect | subscribe | open | auth | info | targets | stop | total |
|---|---|---|---|---|---|---|---|---|---|
| 5 targets | 19 | 1204 | 314 | 105 | 645 | 465 | 2566 | 17 | 5.3 s |
| 10 targets | 19 | 538 | 790 | 180 | 930 | 810 | 7651 | 32 | 11.0 s |
| 10 targets | 19 | 1313 | 285 | 105 | 600 | 495 | 5176 | 17 | 8.0 s |

- Phone mode is faster than HUD mode: 0.5 to 0.77 s a target against 0.8 to 0.96, and the authentication 0.6 to 0.9 s against 1.0 to 1.4. The connection interval settled at 15 ms (a parameter update after connecting) against the 20 ms the Kestrel keeps as the central in HUD mode; the link is part of the per-target time after all, not only the Kestrel's computing.
- No slow first connection here either: 0.5 to 1.3 s to connect.
- Memory: 38 KB for the stack while it runs, the lowest since boot 74.6 KB; free RAM back to the same byte after each pull.
- The pulls replaced the card (the last with 10 targets of the owner's 1-to-10 card).

Not done by hand yet: Find and pick on the Kestrel page with a finger (done over the remote control here).

### The search, live (owner's request, 2026-11-07)

The owner: the second device is a dummy Kestrel on another ESP32; the search should respond as it runs and clear what the last one found. Now a search empties the list at once (the page shows "Searching..." while it is empty), each Kestrel appears as soon as its company id and name are both heard, the status counts them ("Searching: 2 found"), and the end says "Search done, 2 found: pick yours" or "No Kestrel found". The picked Kestrel is ticked and orange in the list.

On the unit (`rc_find_ui_20261107_185626.log`, captures `find_20261107_185626`): Find tapped; the dummy listed 35 ms after the start, the owner's Kestrel at 2.4 s (ticked: it was picked earlier); the search ended at 8 s, 9 devices heard. The "Searching..." line was not caught on a capture: the first Kestrel came in before the first one (0.6 s). The pick made over the remote control earlier saved the Kestrel settings, so the unit is now saved in Phone mode with the owner's Kestrel picked.

### Reset forgets the picked Kestrel (owner's question, 2026-11-07)

Reset on the Kestrel page used to set only the mode to Off and kept the picked Kestrel; the owner expected it to forget it. Now Reset restores the defaults, Kestrel off and no Kestrel picked (`kestrel_settings_reset`); as on the other pages it is in memory until Save, and Reload undoes it. The HUD pairing is kept by the Kestrel (it remembers the unit's HUD name), so the unit's Reset cannot undo that one.

On the unit (`rc_nav_*`, 19:0x): saved state Phone mode with the owner's Kestrel; Reset: the mode Off; Phone mode chosen again: "None picked yet", and `kestrel pull 5` refused with "Pick your Kestrel on the Kestrel page"; Reload: Phone mode and the owner's Kestrel back. No settings write during the whole check.

### Card size: 10, 12 or 15 targets (owner's request, 2026-11-07)

The owner: PRS stages now run to 12 targets in most cases; make the size a setting, 12 by default, 10 or 15 selectable, and a Kestrel pull fills only the first 10 (its Target Card holds 10 per sector, and a pull reads only the current sector, ICD §7.10).

- **The card** always stores 15 targets; **Card Size** on the Dope page (10, 12, 15; default 12) says how many the tile and the level use. It shows at once; Save keeps it, as Target Names. Targets past a smaller size are kept, hidden; Clear all clears all 15.
- **Older saves load**: the card and the Dope settings only grew by appending, so `load_config_grown` (`common.c`) loads a shorter saved block into the first bytes and gives the rest the defaults: the 10-target card loads as targets 1 to 10, the 3-byte settings get the default size 12. Nothing is written until the next save. `load_config` is unchanged in behaviour (it is `load_config_grown` with no shorter size allowed).
- **Pull**: asks for 1 to 10 (fewer on a 10-target card, no change), fills the first ones it was asked for, empties the rest of the first 10, and leaves targets 11 to 15 alone.
- **The level** steps only through the card's size; a target past a smaller size gives way to the first set target.

On the unit (`rc_nav_20261107_190948.log`): the boot log shows `NS: DOPE, Configuration read from an older 100-byte version` and `NS: DOPECFG, ... older 3-byte version`, no write at boot; the tile listed targets 1 to 12, then Clear all. The owner was using the unit at the same time (a `DOPE` save 26 s after boot: the owner's own edit on the tile, confirmed), so the checks stopped there. The owner then checked the rest by hand (2026-11-07): all good.
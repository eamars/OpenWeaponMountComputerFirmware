# ADR-017: the level tile in two layers, a background and a foreground

Status: **Built** 2026-11-13. The owner agreed the approach in the discussion of that day and asked for it to be built first ("draft all known ADRs first, then build the first one"). Built and proven on COM13 ([progress](progress.md)): the settings tile stays in place, the eight pairs show in both rotations, free internal RAM is the same as on main and comes back to the byte after a tour of every pair. The owner's own look at the unit is still to come.

## What we want

The level tile shows two things at once: what the sensor says (the level, in one of its styles) and what the shooter reads (the timer and the dope). Today they are one fixed view. They become two layers, each chosen on its own, so that later ADRs can add a background (a target, the recoil scope) or a foreground (the dope card as rows, a live Kestrel target) without touching the other layer.

This ADR builds the two slots and moves what exists into them. It adds no new view except "Off" for each layer.

## Owner's direction (2026-11-13)

- Background: Level, Block, Chevrons, later Target and Acceleration. Foreground: Match (timer and dope), later DOPE and Stream.
- Each layer can be switched off.
- RAM must not go up, and a background or foreground that is not chosen is freed. Internal RAM is kept for the services that need it for performance; PSRAM is cheap, and the UI may use more of it.
- A tile that has no use with the chosen foreground is not offered ("disable if not supported").
- The choice is a setting: a user does not switch often (Target for training, Match for PRS, DOPE for field shooting or hunting).
- Bug: the settings tile must not slide toward the right, where there is no tile.
- One ADR for the layers; DOPE, Target, Stream and Acceleration each get their own (ADR-018 to ADR-021).

## What we know (checked in the code, measured on COM13, v1.4.1, 2026-11-13)

- **The bug:** the settings tile is added with `LV_DIR_HOR` (`main_tileview.c`), and it is the last column. A swipe toward the missing tile drags the hub about 24 px before it springs back (`logs/rc/snaps/adr017_look/settings_after_swipe_left.png`).
- **The background is already a slot.** The three styles share one interface (`digital_level_view_t`: constructor, destructor, update) and are built and deleted when the style changes. Chevrons holds about 0.5 MB of PSRAM while shown and frees it on leaving.
- **The foreground is not.** The tilt box, the hint and the band (`level_band.c`) are built once at start-up and never deleted. The band's widgets are file statics.
- **The timer is a service** (its own task, a listener the band registers). It stays; only its display belongs to the foreground. The level's reader starts an armed timer on recoil, and the hardware button works the timer through the band's time label.
- **Where UI memory comes from:** LVGL allocates from PSRAM (`lv_malloc_core`, `app.c`). Widgets cost no internal RAM; tasks, event groups and the Bluetooth stack do.
- **Today's memory,** Block style with the band, Wi-Fi connected: free internal 162.6 KB, largest DMA block 86.0 KB, lowest internal since boot 119.9 KB, free PSRAM 5.12 MB.
- **The swipe directions of a tile** are a field of the tile (`lv_tileview_tile_t.dir`), applied to the tile view whenever that tile becomes the active one.
- **The level's settings** (NVS `DLV`) hold the rifle's zero. The loader for a struct that grew by appending (`load_config_grown`) keeps what was saved and gives the new field its default.

## What we will do

1. **The settings tile** allows only the swipe back to the level (`LV_DIR_LEFT`).
2. **The background slot gains "Off":** a black screen. The Level page's "Level style" row becomes **Background**: Level, Block, Chevrons, Off. Stored in the same field with one more value, so the saved settings keep their layout.
3. **The foreground becomes a slot** with the same life cycle as the background: built when chosen, deleted when another is chosen. Two choices now: **Match** (today's band: the timer bar and the dope, unchanged) and **Off**. A new **Foreground** row on the Level page. Stored in a field appended to the level's settings: an older saved block loads as before, zero included, and the new field starts as Match.
4. **The tilt box stays in every pair.** It is the level's number and the zero's control, and it shows that the unit is on when both layers are off.
5. **Leaving Match disarms the timer.** A timer that nothing shows must not run or start on recoil. The rule lives in the band: the timer can only be shown while the band exists.
6. **Swipes that lead nowhere are off.** With the foreground Off, the level tile no longer offers the swipes to the timer tile and the dope tile; with Match it offers both, as today. One function on the tile view sets a tile's directions, for this ADR's foregrounds and the later ones.
7. **For the tests:** a remote control command, `layer [bg|fg <n>]`, reads or sets the pair (RAM only, as `rot`), and `scenario layer_tour` on the host walks every pair and checks the memory.

The chosen pair stays built while another tile is shown, so the swipe back is instant. The timer tile, the dope tile and the settings pages stay built as they are.

**The stored format changes:** the level's settings grow by one field, from 44 to 48 bytes. This firmware loads the older block as it is. An older firmware does not load the grown one: going back to v1.4.1 or earlier, after a Level setting was saved by this firmware, resets the Level settings and the zero.

## How the unit shows it works

On COM13, over USB:

- **The bug:** the same swipe and snap as above show the hub unmoved.
- **Nothing else moved:** `ui_snaps.py compare` of every tile and settings page before and after, in two rotations, differs only in live values and in the Level page's rows.
- **The saved settings survived the update:** the unit comes back in its Block style with its zero, and the foreground is Match.
- **Every pair:** a snap of each of the eight pairs in portrait and landscape.
- **Memory:**
  - after a reboot, free internal RAM and the largest DMA block are no lower than before the change, read the same way;
  - `scenario layer_tour`: after three rounds of every pair, free internal RAM is back within 1 KB of where it started and the largest DMA block is at least 70 KB;
  - `scenario state_change --min-largest-dma 70000` passes.
- **Foreground Off:** the swipes up and down from the level do nothing; an armed timer is disarmed by the switch.

## Not in this ADR

- The DOPE foreground (ADR-018), the Target background (ADR-019), the Stream foreground (ADR-020), the Acceleration background (ADR-021).
- Freeing the timer tile, the dope tile or the settings pages while they are not shown.
- A switch of the pair on the tile itself: the owner does not expect users to switch often.

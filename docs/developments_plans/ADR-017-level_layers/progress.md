# ADR-017 progress

COM13 (`owmc-3861330`), 2026-11-13, over USB. The unit ran v1.4.1 (the release, from its second update slot) before; the builds below were flashed with `idf.py flash`, which also resets the slot choice to the first. Its saved pair: Block, with a dope card of Kestrel targets. Snaps and logs: `logs/rc/snaps/adr017_*` and `logs/rc/rc_adr017_*` (not tracked).

## Built

- **The settings tile:** `LV_DIR_LEFT` in `main_tileview.c`.
- **`main_tileview_set_tile_dir(tile, dir)`:** sets a tile's swipe directions after it was added (the tile's own field, and the tile view's at once when that tile is the active one).
- **The background "Off":** a black object in `digital_level_view.c`, with the same three calls as the styles. `DIGITAL_LEVEL_VIEW_OFF` is stored value 3. A stored value the firmware does not know now shows the default (it was a null pointer).
- **The foreground slot** in `digital_level_view.c`: a table of constructor, destructor, rotation and the tiles it uses, one entry each for Match and Off. `show_level_foreground()` deletes the old one, builds the new one and sets the level tile's swipes.
- **Match** is `level_band.c` as it was, with `level_band_delete()`. The current target stays in RAM across a delete, so the band built again shows the same target.
- **The timer:** `countdown_timer_view_disarm()` (timer tile) runs when Match is deleted; `level_band_show_timer()` shows the timer only while the band exists.
- **The Level page:** "Level style" is now **Background** (Level, Block, Chevrons, Off) and a **Foreground** row follows it (Match, Off). With the background Off, the threshold and the four colour rows are not shown (they do nothing then).
- **Stored settings:** `foreground` (one byte) appended to the level's block, loaded with `load_config_grown`. The block grows from 44 to 48 bytes at the next save.
- **Remote control:** `layer`, `layer bg <n>`, `layer fg <n>` (not saved). Host: `scenario layer_tour`. Both in `AGENTS.md`.
- Build: no warning from our code; `dependencies.lock` unchanged. The image is 1,032 bytes larger; static internal RAM 16 bytes larger (the Off background's record).

## Measured

**The saved settings survived.** The first boot logged "NS: DLV, Configuration read from an older 44-byte version, the rest default", and `layer` answered `bg=1 fg=0`: Block as saved, Match by default.

**Memory after a reboot** (the level tile shown, three readings over the first minute, all three the same):

| Build | Free internal | Largest DMA block | Lowest internal since boot |
|---|---|---|---|
| v1.4.1, the release | 162,775 | 86,016 | 126,440 |
| main (7df2a11), unchanged | 162,071 | 81,920 | 125,432 |
| this ADR | 162,111 | 81,920 | 125,444 |

The 664 bytes and the one 4 KB step between the release and main came with what was merged after v1.4.1, not with this ADR: against main on the same unit the change costs nothing.

**`scenario layer_tour --min-largest-dma 70000`: PASS.** Three rounds of the eight pairs, 2 s each: free internal RAM 161,543 at the start and 161,543 at the end; the largest DMA block 81,920 in all 24 readings. Free PSRAM 5.12 MB, 4.62 MB while Chevrons is shown, back each time.

**`scenario state_change --min-largest-dma 70000`: PASS.** Largest DMA block 81,920 awake and 73,728 while idle. The 8 KB dip while idle is not new: the USB runs of 2026-11-08 and 11-09 show the same step (90,112 to 77,824; 81,920 to 73,728).

**On the screen** (`adr017_proof`):

- The settings tile after a swipe toward the missing tile, snapped at once: the hub in place (before: about 24 px to the left).
- The eight pairs in portrait and landscape: each background alone and under the band; Off and Off is a black screen with the tilt box.
- Foreground Off: a swipe up and a swipe down leave the level tile; a swipe to the settings still goes there. Match: the timer tile and the dope tile as before.
- A preset armed (the bar and 1:30 on the level), then the foreground switched Off: no band; the timer tile shows no preset armed. Back to Match: the band with the dope and no timer.

**Every other screen** (`ui_snaps.py compare`, 26 screens, before on v1.4.1 and after): the timer and dope tiles identical in portrait; the settings pages differ only in the header's status icons, the version on About, the Power and Wi-Fi readings, and the Level page's rows. The level tile differs by its live values (the angle, and the current target, which is RAM only and starts at the first target after a reboot).

## What building found

- **Going back to an older firmware resets the Level settings, the zero included,** once this firmware has saved them: v1.4.1 and earlier accept only the 44-byte block. Nothing is saved until a Level setting changes or the zero is set, so a unit that only ran this build still loads in an older one (seen: main read the block after this build had run).
- **"Foreground colour"** on the Level page is the colour of the Level style's line and the Block style's dark blocks. Beside the new "Foreground" row the name misleads. Not changed here; for the owner.
- **One baseline snap was a torn picture:** on v1.4.1, the first snap after `rot 1` (the timer tile in landscape) showed parts of three tiles. The same capture on this build was whole. Seen once, not looked into.

## Not done

- The owner's own look at the unit.

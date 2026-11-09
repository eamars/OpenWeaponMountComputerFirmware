# Working on this repository

Firmware for the Open Weapon Mount Computer: an ESP32-S3 with an AMOLED touch screen, a BNO085 motion sensor and an AXP2101 power manager, mounted on a rifle (see [README](README.md) and the [technical brief](docs/technical_brief.md)). This page is for anyone, person or agent, about to change it.

## Build, flash, update

- **Build:** ESP-IDF, at the version the [build workflow](.github/workflows/firmware.yml) uses (its container image). Clone with `--recursive`; `components/XPowersLib` and `components/bno08x/sh2` are submodules.
  - `idf.py build`, then `idf.py -p <port> flash`. Flashing and opening the unit's serial port can reset it.
  - `sdkconfig` is generated and not tracked. Change `sdkconfig.defaults` (the target) or `sdkconfig.defaults.esp32s3` (everything else), then delete `sdkconfig` and run `idf.py reconfigure`. Compare the old and new `sdkconfig`: only the change you meant should appear.
  - `main/` is built by a recursive glob: after adding or removing a source file, run `idf.py reconfigure`.
  - `dependencies.lock` is tracked; a build must not change it.
- **Over the air:** `scripts/ota_host.py` serves the build (or a release's OTA package) to units on the local network; see its docstring. Releases: [docs/releases/README.md](docs/releases/README.md).
- **Host tools:** Python from the venv at the repo root (`venv/`, built from `requirements.txt`). Never a second venv.

## Where code goes

`main/` is in folders by role (ADR-012):

| Folder | What belongs there |
|---|---|
| `app/` | `app_main`: start-up order and wiring. `app_cfg.h`: memory placement, the parts fitted (`USE_*`), task sizes and periods, profiling switches |
| `board/` | Bring-up of the board: panel and touch (`bsp`), the LVGL port and input devices (`lvgl_display`), the panel flush (`display_flush`). `board_cfg.h`: buses, pins, the panel |
| `common/` | Helpers with no UI: settings storage in NVS (`common`), memory diagnostics (`mem_diag`), small data structures |
| `services/` | Each owns its state and one task, and is started at boot without the UI: Wi-Fi, PMIC, Kestrel, buzzer, sensor frame, countdown timer, low power, OTA |
| `ui/core/` | The tile view (`main_tileview_jump_to()` for jumps between tiles), the settings page framework (`settings_ui`), the value editor, corner geometry, the UI bridge |
| `ui/views/` | The tiles: level (its styles, the band), timer, dope, sensor calibration; three disabled tiles kept for a later decision |
| `ui/settings/` | The settings pages, one file each (`*_page.c`), the hub, and two settings records with their rows (`system_config`, `sensor_config`) |
| `tools/` | Device tests and diagnostics: the remote control, its serial and Wi-Fi transports (`rc_serial`, `rc_net`), the crash report |
| `fonts/` | Generated fonts (lv_font_conv); regenerate, do not edit |

Our own drivers live in `components/`: `bno08x`, `esp_lcd_touch_ft3168`, `kestrel_link`, and `esp_driver_usb_serial_jtag` (a patched copy of the ESP-IDF component; its README says why and how to re-apply it on an ESP-IDF upgrade).

**The boundary between services and the UI** is guidance for new code, so services and UI stay clearly apart: a new service does not call LVGL or include anything from `ui/`, `tools/` or `board/`.
- A service publishes changes as ESP-IDF events, which `ui_bridge` turns into LVGL subjects; or it calls a listener the UI registers (the countdown timer).
- The UI reads services through their getters.
- Two older modules cross it, and stay as they are unless they need changing for another reason:
  - `low_power_mode` switches the display (it is its power switch; ADR-005);
  - `ota_mode` holds the update's screens with its tasks. It works, and it is the path that delivers fixes to units in the field.

To see what crosses it:

```bash
cd main && for f in services/*; do for inc in $(grep -oE '#include "[a-z0-9_]+\.h"' $f | sed 's/#include "//; s/"//'); do loc=$(find . -name "$inc" | head -1); case "$loc" in ./ui/*|./tools/*|./board/*) echo "$f -> $loc";; esac; done; done
```

Only those two older modules should print.

**Settings in NVS:**
- Each module stores one struct in its own namespace with `load_config` / `save_config` (`common/common.h`), with a CRC.
- The struct's layout is the stored format: changing it resets that module's saved settings on units in the field. If it only grows by appending, use `load_config_grown`.
- Never change a stored struct's layout without saying so in the ADR.

**Warnings:** CI fails on any compiler warning from `main/` or `components/` (ESP-IDF's and managed components' own warnings do not count). Keep the build clean locally too: a clean build prints none from our code.

**Style:** the skills in `.agents/skills`:
- `c-style` for all C and C++;
- `embedded-ui-design`, `lvgl-layout` and `lvgl-widget` for anything on screen;
- `esp32-debug` for work on a unit;
- `python-venv` for host tools.

## Proving a change on a unit

Changes are proven on a real unit, not only built. `tools/remote_control/rc_run.py` drives a unit over its USB console (remote control on: Settings, Advanced):
- `send <command>`: one command and its reply. Examples:
  - `state`;
  - `grav` (the sensor's live gravity and cant);
  - `snap` (the screen as a PNG under `logs/rc/snaps/`);
  - `tile <col> <row>`;
  - `settings open <page title>`;
  - `tap <x> <y> <ms>`;
  - `rot <n>` (RAM only).
- `scenario state_change`: a tour of tiles, idle and wake, rotation and Wi-Fi, with a PASS or FAIL verdict. `--min-largest-dma 70000` also checks internal DMA memory.
- `scenario rotation_sweep`: 2 minutes on the level in each rotation, about 8 minutes in all. Keep any one test under 10 minutes; no long soaks.
- `idle <s>`: idle, then wake by a timer after s seconds.
- `trace <s> [now]` (ADR-014): every 50 ms for s seconds from the next wake (or now), the cant shown, the fusion's own, the accelerometer's, the correction, pitch, report counts and |g|, in the log. `bno levelfix off` (RAM only) turns off the level's correction after a wake, to compare.

**Over Wi-Fi** (ADR-013): the same commands, snaps and log, with `--host` in place of `--port`.
- Turn it on with Settings, Advanced, "Wi-Fi remote control" (off by default; "USB remote control" is its own switch). Anyone on the network can then send commands, so leave it off when you're done.
- **Finding a unit:** every name a unit goes by carries its 7-digit number (`unit_number()` in `common/`): `HUD-<n>` for the Kestrel, `OWMC_<n>` while it is set up, and `owmc-<n>` on the network, as both its DHCP and mDNS host name.
  - The unit's Wi-Fi page shows its network name and address while connected.
  - `rc_run.py find` lists the units with the switch on: name, address and firmware version.
  - Use the name, not the address: the router can change the address. Example: `rc_run.py send state --host owmc-8626470.local`. `ui_snaps.py capture` takes `--host` too.
- **The turret rig** (ADR-014): the unit on the OpenAutoTurret's pitch cradle (`http://rpi-turret.local:8080`, `manual_step` pitch or yaw by 0.5, 1 or 5 degrees; never the actuator). Turret pitch is the unit's cant. The turret's profile can't shake the unit hard enough to wake it, and an idle unit can't be reached over Wi-Fi: before any stretch you aren't driving it, park it with `idle <s>` so it wakes itself, or it stays asleep until someone touches it.
- **Updating a unit with no cable** (the one on the turret): `scripts/ota_host.py --force --importance 1` serves the build, `send reboot --host ...` makes the unit check for it, then `tap` Install on the offer. Stop the host once the download is done, before you tap Restart: the restarted unit checks again, and with `--force` it would be offered the same build again. There is no app rollback, so a build that can't get back onto the network needs a USB cable.
- Use it to test a unit on battery: on USB power the unit never light-sleeps, because the firmware holds a power lock. Connecting doesn't reset the unit, and snaps come back whole.
- Idling stops the radio, which ends the session. Over Wi-Fi, scenarios idle with `idle <s>` and connect again after it. An idle unit can't be reached until something wakes it: a touch, the button, movement, or `wake` over USB.
- The ROM's reset and panic lines go to the console only.

`tools/remote_control/ui_snaps.py` checks that a change left the screens as they were. `capture` snaps every tile and settings page in two rotations; `compare` diffs two captures. Live values (angle, readings, version) differ by nature.

Session logs go to `logs/rc/`, which is not tracked.

## Decisions and ADRs

Work is planned and recorded as architecture decision records in [docs/developments_plans](docs/developments_plans/README.md). The process is in that README. The parts that trip people up:
- **Approval first:** an ADR is approved by the owner before work starts.
- **One at a time:** one ADR is open at a time; a functional change and a dependency update are never mixed in one.
- **Plain form:** plain words. What we want, the owner's direction, what we know (measured or checked in the code), what we do, how the unit shows it works. `progress.md` records what was built and measured.
- **Close before the PR:** an ADR is closed (Built, in its README, its progress and the index) in its branch before its PR is opened, so a merge never leaves one Open.
- **Legacy formats:** ADR-001 and ADR-004 are in a retired format. Do not copy them.

## Do not

- **Do not enable `CONFIG_HEAP_TASK_TRACKING`.** It deadlocks heap frees in critical sections, and the unit boot-loops.
- **Do not enable flash encryption.** This is an open-source project.
- **Do not change `LCD_QSPI_MAX_TRANSFER_SIZE` (4091) without measuring.** At 4091 a transfer is one DMA descriptor. At 4096 it becomes two, which doubles the internal DMA memory in flight (about 80 KB against 40 KB) with no gain in frame rate.
- **Do not change the display's anti-tearing (TE-synced flush) without measuring.** Turning it off was tried and the tearing was visible (ADR-010).
- **Do not reuse a log tag for something else.** The device tests match on log lines.

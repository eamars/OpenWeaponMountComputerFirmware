# ADR-012 progress

## Before (2026-11-08)

- **Baseline:** v1.2.0 on the COM11 unit.
- **Snaps:** 26 taken by a capture script driving the remote control, used to compare each step: the 4 tiles (timer, level, dope, settings) and the 9 settings pages, in rotation 0 and 1.
- **Warnings:** a clean build gives 125 unique warnings, the same locally and in CI (see the README).

## Step 1: warnings and dead code (2026-11-08)

**Removed:**
- The dev kit:
  - `main/dev_kit_app_cfg.h`;
  - `components/esp_lcd_jd9853` and `components/esp_lcd_touch_axs5106`, with their `REQUIRES`;
  - the JD9853 and AXS5106 branches of `bsp.c`;
  - the `USE_LCD_SH8601` and `USE_TOUCH_FT3168` switches, which no longer choose anything.
- The BNO085 SPI path: `bno085_spi.c`, its context type and its init declaration, and `esp_driver_spi` from the driver's `REQUIRES`.
- `enter_sleep_mode()`, which nothing called; ADR-009's automatic light sleep replaced it. With it went:
  - the "Low Power Mode" tile, only ever entered from there;
  - the `IN_SLEEP_MODE` flag, only ever set there, and `is_sleep_mode_activated()`;
  - `wakeup_cause`.

  This also removes the deprecated `esp_sleep_get_wakeup_cause` call.
- `mem_monitor_task()`, whose start was commented out.
- Unused code:
  - variables and a function in `digital_level_view_type_1.c` and `countdown_timer.c`;
  - 5 macros in `app_cfg.h`.
- Commented-out code in live files: low power, type 1, PMIC, BNO085 I2C, and a few single lines.
- The commented-out esp32c6 block in `main/CMakeLists.txt`.
- `CONFIG_LV_ASSERT_HANDLER_INCLUDE` from `sdkconfig.defaults.esp32s3`. `sdkconfig` was regenerated from the defaults, and the only change was that setting.

**Kept, by the owner's decision:**
- The point-of-aim, acceleration-analysis and OpenTrickler tiles, with the circular buffer.
- Their commented-out entries in `main_tileview.c`.

**Checks:**
- **Clean build:** 116 unique warnings left:
  - the 104 LVGL 9.6 deprecations (step 2);
  - 5 ESP-IDF CMake notes and 11 FreeRTOS `atomic.h` helpers, which are not ours.

  The LVGL assert warning (587 lines) and the unused-code warnings are gone.
- **`state_change --min-largest-dma 70000` on COM11:** PASS (`rc_state_change_20261108_225213.log`). The smallest `largest_dma` was 81,920 B, against 77,824 B in ADR-011's runs.

## Step 2: the LVGL 9.6 calls (2026-11-08)

**Built:**
- **Flag calls (98, in 12 files plus the kept OpenTrickler tile):** `lv_obj_add_flag`, `lv_obj_remove_flag` and `lv_obj_has_flag` became the per-flag setters, `lv_obj_set_<flag>(obj, true/false)` and `lv_obj_is_<flag>(obj)`. Two flags in one call became two setter calls.
  - This is exactly equivalent: in LVGL 9.6 the old calls are wrappers that call these same setters (`lv_obj.c`).
  - A script did the rewrite and listed anything it could not map; one call had a `/* */` comment and was done by hand.
- **The UI bridge's six subjects:** they are created with `lv_subject_create` and set to the services' state straight after, instead of `lv_subject_init_*`. The globals became pointers.
  - They are created in `ui_bridge_init()`, inside `lvgl_display_init()`. That is before the remote control starts and before any widget binds to them, so nothing reads one before it exists.

**Checks:**
- **Clean build:** no warning from our code. The 16 left are ESP-IDF's and FreeRTOS's.
- **On COM11:**
  - `state_change`: PASS (`rc_state_change_20261108_225858.log`, `largest_dma` at least 81,920 B).
  - `rotation_sweep`: PASS (`rc_rotation_sweep_20261108_230125.log`, `largest_dma` at least 90,112 B).
- **Snaps against the baseline, rotation 0:** 10 of the 13 screens are pixel-identical. The other three differ only in:
  - the version string on About;
  - the live readings on Power;
  - the settings hub's scroll position and the Wi-Fi icon, which is live state.

  `rotation_sweep` leaves the unit in rotation 3 (RAM only), so this capture took rotations 3 and 0. Rotation 1 is compared in step 3.

## Step 3: dependencies in one direction (2026-11-08)

**Built:**
- **Countdown timer.**
  - The service (`countdown_timer.c`) no longer includes LVGL or the level band.
  - It keeps its state machine, its task and its beeps. The beeps were inside the UI callback; they are now the service's own `beep_at()`, run at the same points and before the listener.
  - It reports the time left through its callback, `countdown_timer_setup(timer, cb, arg)`: on each tick, on re-arm, and on pause or continue (with the time last reported). This replaces its direct `level_band_update_timer()` calls.
  - The level band (UI) supplies the callback, `level_band_timer_update_cb`, which takes the LVGL lock as before. The band also owns the timer's controls: the tap and long-press handler, and showing, hiding and asking whether the timer is shown.
- **PMIC settings.** Their storage, defaults and timeout conversions moved from the Power page (`pmic_config.c`) to the service side (`pmic_settings.c`). The service no longer depends on a UI file; the page only shows and edits.
- **Includes.**
  - `lvgl.h` is no longer included by `sensor_config.h` and `low_power_mode.h`, which did not use it.
  - Unused `nvs.h` and `esp_crc.h` includes were removed from five files.

**Kept:**
- **`system_config.h` still includes LVGL.** Its stored struct holds LVGL's display rotation type. It is display configuration, not a service, and changing the stored type would put the settings layout at risk for no gain.
- **What ADR-005 accepted:** low power's display calls, and the views' sensor reader tasks.

**Checks:**
- Clean build: no warning from our code.
- **`state_change` on COM11:** PASS (`rc_state_change_20261108_231424.log`, `largest_dma` at least 81,920 B).
- **The timer, end to end on the unit** (a script driving the remote control in one session; arming is RAM only, so no setting changed):
  - armed: the 1:00 preset, the bar orange;
  - started (tap on the time): 0:57 after 3 s;
  - paused: the bar turned grey at once, through the new callback path, and stayed at 0:57 two seconds later;
  - disarmed: the timer hidden.
- **Snaps against the baseline, rotation 0 and 1:** every page is pixel-identical except About (version) and Power (live readings). The level tile differs in the live tilt and the settings hub in its scroll position. The Power page shows the stored timeouts after the reboot, now loaded from `pmic_settings.c`.

## Step 4: layout and names (2026-11-08)

**Built.** `main/` is now in folders by role, moved with `git mv` so `git log --follow` keeps each file's history:

| Folder | What | Files |
|---|---|---|
| `app/` | Start-up and wiring, the app's build settings | `app`, `app_cfg.h` |
| `board/` | Bring-up of the board: panel, touch, LVGL port, flush; pins and panel in `board_cfg.h` | `bsp`, `lvgl_display`, `display_flush`, `board_cfg.h` |
| `common/` | Helpers with no UI: settings storage, CRC, memory diagnostics | `common`, `circular_buffer`, `mem_diag` |
| `services/` | Own their state and task; no UI in new code (two older modules cross it, below) | `wifi`, `wifi_provision`, `pmic_axp2101`, `pmic_settings`, `kestrel_service`, `buzzer`, `sensor_frame`, `countdown_timer`, `low_power_mode`, `ota_mode` |
| `ui/core/` | The tile view, settings pages, value editor, corners, UI bridge | `main_tileview`, `settings_ui`, `value_editor`, `display_shape`, `ui_bridge` |
| `ui/views/` | The tiles | the level view and its styles, `level_band`, `timer_view`, `dope_view`, `sensor_calibration_view`; kept, disabled: `point_of_aim_view`, `acceleration_analysis_view`, `opentrickler_remote_controller_view` |
| `ui/settings/` | The settings pages | `settings_hub`, `about_page`, `wifi_page`, `power_page`, `kestrel_page`, `system_config`, `sensor_config` |
| `tools/` | Device tests and diagnostics | `remote_control`, `rc_serial`, `crash_report` |
| `fonts/` | Generated fonts | `font_dope_digits*` |

**Renamed, where the name hid the role:**

| Old | New |
|---|---|
| `config_view` | `settings_hub` |
| `about_config` | `about_page` |
| `wifi_config` | `wifi_page` |
| `pmic_config` | `power_page` |
| `kestrel_config_view` | `kestrel_page` |
| `countdown_timer_config_view` | `timer_view` |
| `dope_config_view` | `dope_view` |

Their includes, guards and comments followed. Function names did not change.

**Other changes:**
- **`app_cfg.h` split:**
  - `board/board_cfg.h` has the buses, pins, the panel and the chips;
  - `app_cfg.h` keeps memory placement, the parts fitted (`USE_*`), task sizes and periods, and profiling, and includes `board_cfg.h`, so no includer changed.
- **`main/CMakeLists.txt`:** lists the folders as include directories. The source glob is recursive; a new file needs `idf.py reconfigure`, because ESP-IDF reads component files in script mode, where `CONFIGURE_DEPENDS` is refused.
- **`main_tileview_jump_to(tile)`** replaces the "set the tile, send the change event, remember where we were" code written three times (OTA install, Sensor calibration in and out).
- **Leftover includes:** `system_config.h` and `sensor_config.h` in the timer and low power services, and `main_tileview.h` in low power. Nothing in those files used them.

**Two older modules still cross the boundary:**
- **`low_power_mode` calls the display:** it is the display's power switch, accepted in ADR-005.
- **`ota_mode` is the update service and its own screens** (tile, offer, prompt) in one file of about 1,050 lines.

The owner's direction (2026-11-08): the boundary is guidance for new code, not a rule to retrofit, so both stay as they are unless they need changing for another reason. OTA works, and it is the path that delivers fixes to units in the field.

A script shows what crosses it: no other file in `services/` includes a header from `ui/`, `tools/` or `board/`.

**Checks:**
- Clean build: no warning from our code.
- **`state_change` on COM11:** PASS (`rc_state_change_20261108_232808.log`, `largest_dma` at least 81,920 B).
- **A real update, end to end**, from `scripts/ota_host.py --force` serving this build:
  - "Update available", then the offer with the build's note;
  - Install, which jumps to the update tile through the new helper;
  - "Update installed", 2,390 K of 2,390 K, then Restart;
  - About: "Running ota_1".
- **Sensor calibration through the helper:** Advanced, Sensor calibration (tile 0,2, its first screen, "Calibrated, offset -2.1°"); Cancel goes back to the settings tile (3,1).
- **Snaps against the baseline, rotation 0 and 1:** every page is pixel-identical except About (version) and Power (live readings). The settings hub differs in its scroll position and in an "Update available" row: the unit had restarted while the forced test host still ran.

## Step 5: keep it that way (2026-11-08)

**Built:**
- **CI warning check** (`.github/workflows/firmware.yml`): the build's output is kept, and the job fails on any compiler warning whose file is in the repo's `main/` or `components/`. Colour codes are stripped first, because ESP-IDF compiles with `-fdiagnostics-color=always`. ESP-IDF's, LVGL's and the other managed components' own warnings do not count.
  - Tried on real logs: it finds 112 warnings in the v1.2.0 CI log and 104 in step 1's local log, and none in step 4's.
- **`AGENTS.md`** (new): build, flash and update; where code goes; the boundary between services and the UI, the two older modules that cross it, and a one-line check; settings in NVS; warnings; style (the skills); proving a change on a unit; the ADR rules, including closing an ADR before its PR; what not to do.
- **`tools/remote_control/ui_snaps.py`** (new): the capture and compare used in steps 2 to 4, for the next change to the screens.
- **`requirements.txt`** (new): what the repo's host tools import (pyserial, zeroconf, pillow), for the venv at the repo root.
- **Skills and docs:**
  - the skills' file paths follow the new layout;
  - `c-style` states where a new module goes and the boundary for new services;
  - the LVGL skills name `main_tileview_jump_to()` and the per-flag setters;
  - the technical brief points at `board_cfg.h`, `ota_host.py`'s docstring at `services/ota_mode.c`, and the README at `AGENTS.md`.

## Closed (2026-11-08)

All five steps are built and checked on COM11. The firmware behaves as v1.2.0 did, and the build prints no warning from our code.

**Left for later ADRs, if wanted:**
- the 136 `ESP_ERROR_CHECK` in runtime paths;
- the decision on the three disabled tiles.

`ota_mode` and `low_power_mode` keep their screens (owner, 2026-11-08).

COM11 runs the step 4 build from `ota_1`.

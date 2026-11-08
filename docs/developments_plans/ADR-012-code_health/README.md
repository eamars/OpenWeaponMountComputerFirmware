# ADR-012: code health: no warnings, no dead code, one direction of dependency, a layout that says what each file is

Status: **Built** 2026-11-08. Approved by the owner with the two decisions below; all five steps built and checked on the COM11 unit (state_change, rotation_sweep, the timer end to end, a real OTA update, snaps of every screen against v1.2.0). No warning from our code; CI fails on any new one. Details in [progress](progress.md).

## What we want

A firmware tree the next person can find their way in and change safely:
- the build prints no warning from our own code;
- nothing compiles that never runs;
- services never reach into the UI;
- each file sits where its role says it should, under a name that says what it is.

The behaviour stays the same: every step is proven on the unit (COM11) to change nothing a person can see. The rules end up in `AGENTS.md` and the skills, so they hold after this ADR.

## Owner's direction (2026-11-08)

- Start from the warnings recommendation made after v1.2.0: the LVGL assert setting, dead code, the deprecated sleep call, the LVGL calls deprecated in 9.6, then a CI check so new warnings fail the build.
- With a system engineering view, organise the code cleanly and sustainably, including the code from before ADR-001.
- Verify against regressions on the unit on COM11.
- At the end, `AGENTS.md`, the skills and the docs are up to date, so the next agent can pick up easily.
- Close the ADR (Built) before opening its PR, so no ADR stays Open after a merge.

## What we know (checked 2026-11-08)

**Warnings.** A clean build gives 125 unique warnings, the same locally and in CI.
- From our side:
  - one sdkconfig setting, `CONFIG_LV_ASSERT_HANDLER_INCLUDE="assert.h"`, deprecated in LVGL 9.6, which repeats in every LVGL file (587 lines). It only includes the C library's `assert.h` and defines no handler, so LVGL already uses its default (halt; the LVGL watchdog then restarts the unit). Removing it changes nothing.
  - 98 calls of `lv_obj_add_flag`, `lv_obj_remove_flag` and `lv_obj_has_flag` in 12 files, deprecated in 9.6. Each flag we use has its own setter (`lv_obj_set_hidden`, `lv_obj_set_clickable`, `lv_obj_set_scrollable` and the rest).
  - 6 calls of `lv_subject_init_int` and `lv_subject_init_pointer` in `ui_bridge.c`, replaced by `lv_subject_create`.
  - `esp_sleep_get_wakeup_cause` (ESP-IDF 6.1), only in `enter_sleep_mode()` in `low_power_mode.c`, which nothing calls. ADR-009's automatic light sleep replaced it.
  - 7 unused variables and functions: in `digital_level_view_type_1.c`, in `countdown_timer.c`, and in the dev-kit touch driver, which is also missing a return.
- Not ours: 5 CMake notes inside ESP-IDF's Wi-Fi components, and 11 unused FreeRTOS `atomic.h` helpers pulled in by LVGL's `lv_freertos.c`.

**Dead code that still compiles** (checked against the link map: none of it is in the firmware):

| What | Size | Why it is dead |
|---|---|---|
| `point_of_aim_view`, `acceleration_analysis_view`, `opentrickler_remote_controller_view` | 933 lines | Their tiles are commented out in `main_tileview.c` |
| `circular_buffer` | 71 lines | Used only by the acceleration analysis view |
| `dev_kit_app_cfg.h` | 136 lines | Included nowhere. It reuses `APP_CFG_H` as its guard |
| `components/esp_lcd_jd9853`, `components/esp_lcd_touch_axs5106` | 881 lines | Built, but nothing linked. Used only under `USE_LCD_JD9853` and `USE_TOUCH_AXS5106`, which are never defined; those branches in `bsp.c` need pins that exist only in the dev-kit header |
| `bno085_spi.c` | about 200 lines | `bno085_init_spi` is never called |
| `enter_sleep_mode()`, `mem_monitor_task()` and other leftovers | | Called only from commented-out code |
| Commented-out code | about 95 lines in `main/` | ADR-005 kept it, because the owner keeps some on purpose |

Also unused: 5 macros in `app_cfg.h`, the two dev-kit components in `REQUIRES`, and a commented-out esp32c6 block in `main/CMakeLists.txt`.

**Dependencies that point the wrong way** (ADR-005's rule: services own their state and task, never call LVGL, and publish changes the UI applies):
- The PMIC service loads its settings from the Power page's file. `load_pmic_config()` and `power_management_config` live in `pmic_config.c`, and `pmic_axp2101.cpp` calls into it.
- The countdown timer service takes the LVGL lock and calls the level band (`level_band_*`) directly. The level band also calls back into the timer.
- Service headers include `lvgl.h` for a type: `system_config.h` keeps an `lv_display_rotation_t` in its stored struct, plus `countdown_timer.h`, `low_power_mode.h` and `sensor_config.h`.
- Accepted in ADR-005 and kept: low power's display calls (it is the display's power switch), and the views' sensor reader tasks. The UI reading a service through its getters (Kestrel status, Wi-Fi state) points the right way.

**Layout.** `main/` is one flat folder of about 60 files, built by a glob:
- services, views, settings pages, the UI framework, board glue, test tools and generated fonts sit side by side;
- settings pages are named three ways (`*_config.c`, `*_config_view.c`, `*_view.c`);
- several names do not say what the file holds. `digital_level_view_controller.c` is the sensor poller. `dope_config_view.c` also owns the dope card's data. `pmic_config.c` owns the PMIC's settings;
- `app_cfg.h` mixes pins, feature switches, display geometry, task tuning and profiling flags;
- the "remember the tile, jump, come back" code is written three times.

## What we will do

One step at a time, each built, flashed to COM11 and checked before the next.

1. **Warnings and dead code.**
   - Remove the LVGL assert setting.
   - Remove the dead code (decision 1), the unused macros and `REQUIRES`, and the unused variables.
   - Proof: no warning from our code; `state_change` passes on COM11.
2. **LVGL 9.6 calls.**
   - The 98 flag calls become their setters, and the subjects use `lv_subject_create`. Mechanical, file by file.
   - Proof: no deprecation warning left; `state_change` and `rotation_sweep` pass; snaps of every view and settings page match before and after.
3. **Dependencies in one direction.**
   - The PMIC service owns its settings; the Power page only shows and edits them.
   - The countdown timer publishes its state, and the UI applies it, as the Wi-Fi and PMIC services already do.
   - Service headers stop including `lvgl.h`.
   - Proof: the timer start, run and stop on the level band; Power page changes stored and applied after a reboot; `state_change`.
4. **Layout and names** (decision 2).
   - Files move by role, with `git mv` so history follows.
   - Misleading names are fixed.
   - `app_cfg.h` splits into the board's pins and the app's tuning.
   - The tile jump becomes one helper.
   - Proof: same image behaviour; `state_change`, `rotation_sweep`.
5. **Keep it that way.**
   - CI fails on any compiler warning from `main/` or our components.
   - `AGENTS.md` (new) holds the layout, the dependency rule, how to build, flash and test, and the ADR rules, including closing the ADR before its PR. The skills and README follow it.
   - Then this ADR is closed and its PR opened.

Not in this ADR:
- dependency updates (`idf_component.yml` stays as it is);
- behaviour changes;
- the 136 `ESP_ERROR_CHECK` in runtime paths that ADR-005 recorded. Changing what happens on an error is behaviour, so it is a later ADR if wanted.

## Owner's decisions (2026-11-08)

1. **Dead code.**
   - The three disabled tiles stay for a later decision: point of aim, acceleration analysis and OpenTrickler, with the circular buffer they need. Their commented-out entries in `main_tileview.c` stay too, because that is how a tile comes back. They are migrated with everything else.
   - The rest goes: the dev-kit config, the JD9853 and AXS5106 components, the BNO085 SPI path, unused functions and macros, and the commented-out code.
2. **Layout: folders inside `main/`**, by role (`app`, `board`, `services`, `ui/core`, `ui/views`, `ui/settings`, `tools`, `fonts`). Files move with `git mv`, misleading names are fixed, and the dependency rule is written in `AGENTS.md`.

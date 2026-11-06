# ADR-005: review every module, then fix the known bugs

Status: **Open.** Approved 2026-11-06 ("Approve, start"). Step 1, the review, in progress.

## What we want

Every module mapped to its known bugs and performance issues, from a review of each subsystem on its own and of how the subsystems work together. Then the bugs on that map fixed at their cause, each proven on the device. Performance issues go on the map as a record; none is addressed in this ADR.

## Owner's direction (2026-11-06)

1. A new ADR for bug fixes, started after ADR-003.
2. No performance optimisation in this round. Only bugs are fixed.
3. The ADR starts with each module mapped to its known bugs and performance issues; only the bugs are addressed in ADR-005.
4. At system level, LVGL, the BNO085, Wi-Fi, the PMIC and the other peripherals are reviewed one by one, so the system-level interactions are covered too.
5. Bug fixes are functional changes on today's dependencies (ESP-IDF v6.1 and the managed components as locked by ADR-003). No dependency update inside this ADR.
6. Review not just the code but the life cycle of each submodule: the BNO085 from init to sleep, Wi-Fi from provisioning to auto shut-off, the LVGL locks and multi-thread rendering.
7. Most of the code is hand-written: check styling and consistency too.

## Step 1: the review and the module map

One review per subsystem, done as a walk through each life cycle rather than file by file:

- **LVGL and the display:** start-up (port task, lock, display, touch and button input, the two software draw units, `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`), every task that takes the LVGL lock and for how long, what the draw units touch while the LVGL task renders, timers, stop/resume across idle, the LVGL watchdog user, and the panel's on/off, brightness and TE input.
- **BNO085:** power-on and reset, detection (where the first-boot abort lives), report configuration, the sensor poller, calibration, sleep on idle, wake, and recovery from "BNO085 Reset Unexpectly".
- **Wi-Fi and network:** init, provisioned or not, SoftAP provisioning (PoP, QR, timeout), credentials, connect and retry, IP, mDNS, OTA polling and download, the OpenTrickler client, the expiry watchdog and auto shut-off, stop/start across idle, the user's on/off switch and provisioning reset.
- **PMIC:** init, interrupts, the monitor task, charge and battery reporting, power-off and shut-down paths.
- **Other peripherals:** FT3168 touch, the external button (GPIO0), buzzer (LEDC), both I2C buses and who shares them, the USB Serial/JTAG console and remote control, NVS settings (load, CRC, defaults, save, reset).
- **System:** `app_main` start-up order, active/idle/wake and power management, crash reporting and memory diagnostics, every task with its priority, core and stack, and the task watchdog.

Each walk checks:
- every piece of state through its whole life: created, used, released, and what happens across idle/wake and a restart;
- every place it meets another subsystem: the LVGL lock, shared variables, event groups, task priorities, the watchdog, power management and the idle/wake order;
- on the device, only where reading the code cannot settle a question (short scenarios, cycle tests of at most 10 minutes, nothing that blanks or resets the screen while the owner is at the unit).

Styling and consistency are checked against the project's own rules: `.agents/skills/c-style` for all C and C++, and `.agents/skills/lvgl-layout` and `lvgl-widget` for the views. Also checked: naming, error handling, logging and comments that differ from module to module. Third-party code (`components/XPowersLib`, the `sh2` library) is out of scope for style.

| Review | Modules |
|---|---|
| LVGL and the display | `lvgl_display.c`, `display_flush.c`, `main_tileview.c`, every view and settings page (`*_view.c`, `*_config.c`, `config_view.c`, `countdown_timer*.c`), the SH8601 panel setup in `bsp.c` |
| BNO085 | `components/bno08x` (with the `sh2` library), `digital_level_view_controller.c`, `sensor_config.c`, `sensor_calibration_view.c`, `acceleration_analysis_view.c` |
| Wi-Fi and network | `wifi.c`, `wifi_config.c`, `wifi_provision.c`, `ota_mode.c`, `opentrickler_remote_controller_view.c`, mDNS |
| PMIC | `pmic_axp2101.cpp`, `pmic_config.c`, `components/XPowersLib` |
| Other peripherals | FT3168 touch (`components/esp_lcd_touch_ft3168`), external button (GPIO0), buzzer (`buzzer.c`, LEDC), the two I2C buses, the display's TE input, the USB Serial/JTAG console (`components/esp_driver_usb_serial_jtag`, `rc_serial.c`, `remote_control.c`), NVS configuration storage (`common.c`) |
| System | `app.c` start-up order, `low_power_mode.c` (idle/wake, power management), `crash_report.c`, `mem_diag.c`, `circular_buffer.c`, the task list, stacks and the task watchdog |

The 1.47" display and its touch controller (`esp_lcd_jd9853`, `esp_lcd_touch_axs5106`) belong to the dev kit, not to this unit. They are checked only so far as they still build.

The map is a table per module: known bugs (fixed in step 2), performance issues (recorded only) and style and consistency findings. Each entry says how it is known: seen on the device, read in a log, or found in the code with its effect worked out. The map goes to the owner, who approves the bug list for step 2.

### Already known, to be placed on the map

Firmware:
1. **Display strip stall** (LVGL and the display). Now and then one display strip transfer never completes. The LVGL task then waits forever in `spi_device_acquire_bus` (from `display_flush_cb`) holding the LVGL lock, and the LVGL watchdog reboots the unit after 5 s (about 9 s until the UI is back). Seen about once in 600 or more idle/wake cycles on v5.5.2, and once 15 s after a boot; not seen on v6.1 so far (67 cycles). Cause unknown. The core dumps keep only task stacks, so the SPI driver's state is missing from them.
2. **Lock-up for good while idle** (System). The LVGL watchdog is paused in idle by design. If the LVGL lock is stuck while idle, nothing reboots the unit, and a wake request then blocks the low-power task as well. Shown with the remote-control `hang` command in idle; the unit stayed locked until a console `reboot`. Trigger in real use unknown, but there is no recovery path.
3. **BNO085 abort on the first boot after a reset** (BNO085). After a flash or any esptool reset, the first boot aborts on "Failed to detect BNO085" (`ESP_ERROR_CHECK(bno085_init_i2c(...))`, `main/app.c:194`) and reboots, which also overwrites the stored core dump. The owner said earlier that they would fix this one (see the question below).
4. **The second OTA source can never be reached** (Wi-Fi and network). `main/ota_mode.c` passes `"http://owmc_update.vfnz.pro"` as the HTTP client's `host`, so the lookup is for a host of that name and always fails. Same on v5.5.2 and v6.1.
5. **Console replies lost now and then** (Other peripherals). `stats` replies went missing 2 of 40 times in a 20-minute run and twice in one `state_change` run, all after the USB Serial/JTAG fix; none in the last 10-minute run (21 of 21). Cause unknown; `stats` takes no lock.

Boot-log errors that are not failures but hide real ones:
6. `E gpio: ... GPIO isr service already installed` (Other peripherals): `app_main` installs the GPIO ISR service, then `esp_lcd_touch` tries again.
7. `E task_wdt: delete_entry(...): task not found`, several times (System): tasks call `esp_task_wdt_delete(NULL)` without having been added (`acceleration_analysis_view.c`, `digital_level_view_controller.c`, `low_power_mode.c`, `opentrickler_remote_controller_view.c`, `ota_mode.c`, `point_of_aim_view.c`, `sensor_calibration_view.c`).
8. `W gpio: conflict found for GPIO[0]` (Other peripherals), new on v6.1, when the external button is set up. No project code sets up GPIO0 elsewhere, and the button works (owner). Cause unknown.

Not yet understood:
9. **About 32 KB of internal RAM borrowed during v6.1 start-up** (System). Before any project code runs, the lowest free internal heap is about 32 KB below the free heap (equal on v5.5.2). It is given back, and it is the same whatever the size of the stored core dump. It may be expected v6.1 behaviour; the review finds out. Until then `min_int` cannot show pressure at runtime.

Test tooling:
10. **Device-test logs live in the build folder.** `tools/remote_control/rc_run.py` writes to `build/rc_logs/`, which `idf.py fullclean` and `set-target` delete; ADR-002's raw logs were lost this way on 2026-11-06.
11. **`scenario watchdog` does not wake the unit first.** Run while idle, it locks the unit instead of testing the watchdog (see 2).

Checked and not a bug:
- `axp2101_deinit` does not free its context (FIXME in `pmic_axp2101.cpp`), but nothing calls it.
- The level-style switch's "TODO: Add lock" (`digital_level_view.c`) runs in the LVGL task with the lock held.
- `PMU.disableTSPinMeasure()` (battery temperature protection off) is set on purpose.

Known risk, not a bug yet: the core dump routine had 436 B of its 1,792 B stack left when it saved the watchdog dump. A deeper crash could overflow it and lose the dump. Raising `CONFIG_ESP_COREDUMP_STACK_SIZE` costs internal RAM.

## Step 2: fix the bugs on the map

- Only the bugs the owner approves from the map; performance issues stay on the map.
- One bug per change, each built, flashed and proven on the device before the next. The proof shows the failure before the fix where it can be reproduced, and shows it gone after.
- Tooling first (10, 11), so the evidence for the rest is kept.
- The cause before the fix. Where the cause is unknown, the first step is to find it, and the fix is proposed once it is known.
- The boot-log noise as one change.

## For the owner to decide

1. Is the BNO085 first-boot abort (3) in this round, or yours to fix?
2. The core dump stack margin: leave it, or raise it at a small internal RAM cost?
3. Style findings: recommended as a separate pass after the bug fixes, module by module, so each bug fix stays a small change that is easy to check. Kept out of the bug-fix changes either way. In this ADR as step 3, or a later ADR?

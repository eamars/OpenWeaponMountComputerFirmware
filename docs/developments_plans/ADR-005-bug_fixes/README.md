# ADR-005: fix the known bugs

Status: **Draft.** Written 2026-11-06 for the owner's approval.

## What we want

The bugs we already know about fixed at their cause, each proven on the device. Nothing else in this round.

## Owner's direction (2026-11-06)

1. A new ADR for bug fixes, started after ADR-003.
2. No performance optimisation in this round. Only known bugs.
3. Bug fixes are functional changes on today's dependencies (ESP-IDF v6.1 and the managed components as locked by ADR-003). No dependency update inside this ADR.
4. More bugs are expected, and a detailed scan of every module and workflow is needed. That scan is not part of this round, which covers known bugs only. It can be the next ADR.

## The known bugs (checked 2026-11-06)

Each one has been seen on the device, read in a log, or found in the code with its effect worked out. Listed by how much they hurt the unit in use.

### Firmware

1. **Display strip stall.** Now and then one display strip transfer never completes. The LVGL task then waits forever in `spi_device_acquire_bus` (from `display_flush_cb`) holding the LVGL lock, and the LVGL watchdog reboots the unit after 5 s (about 9 s until the UI is back). Seen about once in 600 or more idle/wake cycles on v5.5.2, and once 15 s after a boot. Not seen on v6.1 so far (67 cycles). The cause is not known. The core dumps keep only task stacks, so the SPI driver's state is missing from them. First step: make the next stall leave that state behind, then find the cause.
2. **The unit can lock up for good while idle.** The LVGL watchdog is paused in idle by design. If the LVGL lock is stuck while the unit is idle, nothing reboots it, and a wake request then blocks the low-power task as well. Shown with the remote-control `hang` command in idle: the unit stayed locked until a `reboot` from the console. The trigger in real use is unknown, but there is no recovery path.
3. **BNO085 abort on the first boot after a reset.** After a flash or any esptool reset, the first boot aborts on "Failed to detect BNO085" (`ESP_ERROR_CHECK(bno085_init_i2c(...))`, `main/app.c:194`) and reboots. That abort also overwrites the stored core dump. The owner said earlier that they would fix this one; it is listed so the round has every known bug in one place (see the question below).
4. **The second OTA source can never be reached.** `main/ota_mode.c` lists `"http://owmc_update.vfnz.pro"` and passes it as the HTTP client's `host`, so the lookup is for a host named `http://owmc_update.vfnz.pro` and always fails. Same on v5.5.2 and v6.1.
5. **Console replies get lost now and then.** `stats` replies went missing 2 of 40 times in a 20-minute run and twice in one `state_change` run, all after the USB Serial/JTAG fix. None were lost in the last 10-minute run (21 of 21). The cause is not known; `stats` takes no lock.

### Boot log errors that are not failures

These print as errors or warnings on every boot and hide real ones.

6. `E gpio: gpio_install_isr_service(...): GPIO isr service already installed`: `app_main` installs the GPIO ISR service, then `esp_lcd_touch` (managed component) tries again.
7. `E task_wdt: delete_entry(...): task not found`, several times: tasks call `esp_task_wdt_delete(NULL)` without ever having been added (`acceleration_analysis_view.c`, `digital_level_view_controller.c`, `low_power_mode.c`, `opentrickler_remote_controller_view.c`, `ota_mode.c`, `point_of_aim_view.c`, `sensor_calibration_view.c`).
8. `W gpio: conflict found for GPIO[0]`, new on v6.1, when the external button is set up. No project code sets up GPIO0 elsewhere, and the button works (owner). The cause is not known yet.

### Not yet understood

9. **About 32 KB of internal RAM borrowed during v6.1 start-up.** Before any project code runs, the lowest free internal heap is about 32 KB below the free heap (on v5.5.2 the two were equal). It is given back, and it is the same whatever the size of the stored core dump. It may turn out to be expected v6.1 behaviour rather than a bug; finding out is the job. Until then `min_int` cannot show pressure at runtime.

### Test tooling

10. **Device-test logs live in the build folder.** `tools/remote_control/rc_run.py` writes to `build/rc_logs/`, which `idf.py fullclean` and `set-target` delete. That is how ADR-002's raw logs were lost on 2026-11-06.
11. **`scenario watchdog` does not wake the unit first.** Run while the unit is idle, it locks the unit instead of testing the watchdog (see 2).

### Checked and not a bug

- `axp2101_deinit` does not free its context (FIXME in `main/pmic_axp2101.cpp`), but nothing calls `axp2101_deinit`.
- The level-style switch's "TODO: Add lock" (`main/digital_level_view.c`): the switch runs in the LVGL task with the lock held.
- `PMU.disableTSPinMeasure()` (battery temperature protection off): set on purpose.

### Known risk, not a bug yet

- The core dump routine had 436 B of its 1,792 B stack left when it saved the watchdog dump. A deeper crash could overflow it and lose the dump. Raising `CONFIG_ESP_COREDUMP_STACK_SIZE` costs internal RAM, so it is the owner's call.

## Approach

- One bug per change, each built, flashed and proven on the device before the next. The proof shows the failure before the fix where it can be reproduced, and shows it gone after.
- Tooling first (10, 11), so the evidence for the rest is kept.
- Then the firmware bugs in the order above, the boot-log noise together as one change.
- The cause before the fix. Where the cause is unknown (1, 5, 8, 9), the first step is to find it, and the fix is proposed once it is known.
- Device checks as the owner set them: short scenarios, cycle tests of at most 10 minutes, nothing that blanks or resets the screen while the owner is at the unit.

## For the owner to decide

1. Is the BNO085 first-boot abort (3) in this round, or yours to fix?
2. The core dump stack margin: leave it, or raise it at a small internal RAM cost?

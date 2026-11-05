# ADR-004 progress

Execution record for [ADR-004](README.md). Times are NZDT.

## Stage 1 - starting point (2026-11-05)

- Plan approved by the owner 2026-11-05. Branch `remote_control` from `display_pipeline` at `46cabb6` (ADR-002 step 1 code `320bb15` included).
- ADR-002 step 1: Stages 1-3 passed; overnight run in progress on COM11 (started about 23:45). Device stages of this plan wait until that capture ends. The step 1 verification ELF is kept outside the repo for decoding any overnight core dump.
- Tree builds (last build: ADR-002 step 1 verification build).

Result: pass.

## Stage 2 - production change complete (2026-11-06)

- Production subagent wrote `main/remote_control.*`, `main/rc_serial.*`, the System settings switch and `"RC"` NVS config, the three refactors (`system_config_set_rotation`, `wifi_config_set_enable`, `low_power_mode_request_idle`), the `app.c` hooks and `tools/remote_control/rc_run.py`, under the `c-style` and `python-venv` skills (the owner removed `development-plan` and `py-style` during this stage).
- Build: pass after `idf.py reconfigure`. Zero warnings from `remote_control.c` / `rc_serial.c`. Remaining warnings in `system_config.c` and `wifi_config.c` are LVGL 9.6 deprecations (`lv_menu_*`, `lv_obj_add_flag`/`remove_flag`) and `config_item` set-but-unused, all of the same kind as before this change.
- Greps: no `lv_` in `main/rc_serial.c`. Every `lv_` call in `main/remote_control.c` checked by the subagent against its lock region; the pointer read callback runs in the LVGL task (lock held).
- Discrepancies reported by the subagent (recorded, to verify on the device where marked):
  1. RC flag loaded right after `load_system_config()` (the switch is built inside `lvgl_display_init()`); the reader still starts after `wifi_init()`.
  2. Swipe of 60% from the centre is clamped at the edge: real travel about 50%. Verify a swipe changes tile.
  3. `tap` while idle: LVGL is stopped, the gesture is dropped after its duration + 1 s, reply `@err busy`.
  4. `wake` acts only when idle; always `@ok wake`.
  5. `low_power_mode_request_idle()` is a no-op when already idle, ignores the idle-prevent flag (works on the OTA and calibration tiles), and starts the idle period at the current tick.
  6. `wifi_config_set_enable` keeps the switch's `esp_wifi_start/stop`; after Wi-Fi expiry a remote `wifi on` returns an error (no crash) where the on-screen switch would be disabled.
  7. `save system|wifi` runs the same save calls without the "Configuration Saved" message box.
  8. `stats`: task names with spaces use `_`; a reply over 1,024 B drops whole entries and ends with ` ...`; `@err too_long` at the 129th character.
  9. Console reads must not set `O_NONBLOCK` (with no driver that never delivers data); the default reads return one byte per call without blocking.
  10. Runner timings: 3 s per tile, 10 s after `wake`, 5 s after `rot`, up to 90 s for the IP address; tile tour (2,0) (2,1) (2,2) (3,1) (0,2) then (2,1).
  11. Runner markers: `rst:` and one BNO085 abort allowed until the first `@ok` to `state`.
  12. `stats` does not count as activity: with this unit's idle timeout (about 60 s), the 120 s dwells of `rotation_sweep` would go idle. Fixed in the runner by the parent: `dwell_awake` re-selects the home tile every 20 s during those dwells (a no-op command that counts as activity, the job the owner's countdown timer did in the manual runs).

## Stage 3/4 - first device runs (2026-11-06 00:16-00:23)

Verification build flashed (`874d966` + profiling and trace facility on). Owner asked to run the overnight on this firmware instead of the step 1-only build.

- `rc_run.py send state`: `@ok tile=2,1 rot=1 wifi=on ip=0 idle=0 rc=1` (first command after flashing). `send stats`: one-line reply with heap and per-task stack headroom (`rc_serial` 2,168 B free). `send help`: `@ok help help state stats tap swipe tile rot wifi save idle wake reboot`.
- `rc_run.py scenario state_change --min-lvgl-stack 2048 --min-largest-dma 64000` (session log `build/rc_logs/rc_state_change_20261106_001710.log`): every command answered `@ok` except one `stats` (no reply within 5 s); tile tour, three idle/wake cycles, rotation 1 and 2 with saves, Wi-Fi off/on all executed; zero failure markers. Readings: `largest_dma` min 118,784; `taskLVGL` min free 9,124; `sys_evt` 1,500; `rc_serial` 1,064 (of 4,096). Verdict: `VERDICT FAIL timeout: stats`.
- Cause of the missing reply: console output dropped. At the same moment a `display_prof` line is cut mid-word and runs into the next log line (`...full_I (90695) LowPowerMode...`). ESP-IDF's no-driver console write (`esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c`, `usb_serial_jtag_tx_char_no_driver`) waits for space in the 64-byte USB FIFO only until 50 ms (`TX_FLUSH_TIMEOUT_US`) after the last byte that went out, then drops every byte until the FIFO frees. Any host pause above 50 ms deletes output. After a long quiet period the 50 ms window has already passed, so a momentary full FIFO drops bytes at once, matching the "first lines of a stack-report round lost after idle" seen in ADR-001. Why the host paused for over 50 ms is not established (the runner reads every 50 ms, the capture script every 100 ms, both into large buffers).
- Not yet run: `tap`/`swipe` (needs the owner's eyes), `rotation_sweep`, the switch-off A/B check.
- Overnight run started about 00:25 on this build (passive capture), covering ADR-002 step 1 and ADR-004 together.

## Console driver (owner decision 2026-11-06)

- Owner approved installing the USB Serial/JTAG driver to stop console output drops (amends Deferred "No USB Serial/JTAG driver install"). `app_main` installs it first thing after the failed-allocation hook (`usb_serial_jtag_driver_install`, TX 4,096 B and RX 256 B ring buffers, `CONSOLE_TX_BUFFER_SIZE` / `CONSOLE_RX_BUFFER_SIZE` in `main/app_cfg.h`; ring buffers are internal RAM) and switches the console to it (`usb_serial_jtag_vfs_use_driver`). `rc_serial` reads with `usb_serial_jtag_read_bytes(..., 0)` in its 20 ms poll. `esp_driver_usb_serial_jtag` added to `main/CMakeLists.txt`.
- Host-absent behaviour checked in `usbjtag_tx_char_via_driver`: a write first tries without waiting; only the first write that finds the buffer full waits up to 50 ms, after which bytes drop at once until one succeeds again. A unit with no PC attached stalls once per disconnect, not per log line.
- Build: pass, no warnings from changed files.
- `scenario state_change` on this build (`build/rc_logs/rc_state_change_20261106_002610.log`): `VERDICT PASS`. Readings: `largest_dma` min 114,688 (118,784 before the driver); `taskLVGL` 9,136; `sys_evt` 1,580; `rc_serial` 1,000.
- Lines with dropped bytes (a log line cut and joined to the next): 20 of 801 received lines without the driver, 3 of 924 with it. One of the 3 is the bootloader before `app_main` (expected); two remain (at uptime 39 s during the Wi-Fi connect burst, and 113 s). The host pause that fills even a 4 KB buffer is not explained yet; the verification build prints about 1 KB/s of profiling output.
- Overnight run restarted on this build about 00:30.

## LVGL lock freeze and the lock-up watchdog (2026-11-06)

- Freeze seen once, on the console-driver build, at uptime about 161 s after a passing `state_change` (capture `hang.txt`): `display_prof` lines stop, `digital_level_prof` reports `ui_ok=0 lock_fail=51` every second, remote commands answer `@err busy`; no panic, no reset. The LVGL task stopped while the LVGL lock was held. Not reproduced since (15 min `watch` run on the `bt` build, `build/rc_logs/rc_watch_20261106_003529.log`: no `@err busy`). Root cause open.
- Diagnostics added: remote `bt` prints every task's backtrace (ROM printing, no LVGL lock needed); runner `scenario watch` sends `bt` after three `@err busy` in a row.
- Lock-up watchdog (owner request): the LVGL task is a task watchdog user `lvgl`, fed by a 1 s LVGL timer, so it is fed only while the LVGL task runs its timers. While LVGL is stopped on purpose (idle mode, from `lvgl_port_stop()` to `lvgl_port_resume()`) the watchdog is paused and the low-power task feeds it each loop (first version fed on the idle flag; corrected after review, see below). `CONFIG_ESP_TASK_WDT_PANIC=y` (timeout 5 s): a stall panics, saves a core dump and reboots. A stall that starts while LVGL is stopped is caught 5 s after any wake call resumes it. OTA runs in its own task and takes the LVGL lock only briefly; not exercised on this build.
- Proof (`hang` debug command takes the LVGL lock and never gives it back; `rc_run.py scenario watchdog`, `build/rc_logs/rc_watchdog_20261106_010315.log`): trigger 4.7 s after `hang` naming ` - lvgl (CPU 0/1)`, core dump saved (43,972 B), reboot, next boot `Reset reason: TASK_WDT (6)` and `Stored core dump: task=IDLE0` (the panicking context; the dump holds every task's stack, read the LVGL task with `idf.py coredump-info`). `VERDICT PASS`.
- Boot log noise, not from this change: `task_wdt: delete_entry(240): task not found` several times after boot (also in earlier logs). Seven tasks call `esp_task_wdt_delete(NULL)` without ever having been added.

## Console output loss: ESP-IDF connection monitor (2026-11-06)

- The `watch` run lost 5 of 39 `stats` replies with no fragment of them on the host, plus 6 whole `LowPowerMode` lines and 2 cut lines, mostly in the first 5 minutes (Wi-Fi start/stop, idle/wake, rotations). 80 back-to-back `stats`/`help` on a quiet idle device lost none.
- Mechanism (read in ESP-IDF v5.5.2, unchanged on master): `usb_serial_jtag_write()` (`esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c`) returns -1 for the whole write when `usb_serial_jtag_is_connected()` is false. That flag comes from a tick hook (`usb_serial_jtag_connection_monitor.c`) that spends one of 3 "no SOF" credits on every tick that saw no USB SOF, and refills the credits only on a disconnected-to-connected transition, never while connected. After 3 delayed or doubled ticks since connection, every further one reports a disconnect for one tick. newlib aborts the rest of a `printf` after a failed write, so a 128-byte chunk failing drops the rest of the line or reply (a `stats` reply is about 6 chunks, a log line usually 1-2).
- Fits the evidence (whole replies lost, cut lines, losses following load) but not yet measured directly.
- Runner fix: `wait_for_ip_address` with `accept_connected` now asks `state` first. Before, a device already on Wi-Fi printed no new IP line, the 90 s wait let it go idle (Wi-Fi off), and the run failed with `no 'Connected with IP Address:'`.

## Watchdog false-alarm run (2026-11-06 01:03-01:26)

- `scenario watch --watch-s 900` on the first watchdog build (`build/rc_logs/rc_watch_20261106_010343.log`): state-change routine, 4 idle entries, 3 wakes, 15 min of `state` checks. Zero task-watchdog triggers, zero resets, zero failure markers. Runner IP fix confirmed (`note: state reports the device already connected`). Verdict FAIL only from 5 lost `stats` replies (the console bug above). Readings: `largest_dma` min 114,688; `taskLVGL` 9,120; `sys_evt` 1,452; `rc_serial` 944; `LPMON` 1,848.

## Stage 6 - independent code review (2026-11-06)

One review subagent, scope as in the plan plus the watchdog, the debug commands and the connection-monitor patch. Clean: thread safety (every `lv_` call under the lock or in `read_cb`; no LVGL call in `rc_serial.c`; point queue and drop path), semantic commands and refactored callbacks, line handling, the `"RC"` namespace and switch lifecycle, and the connection-monitor patch ("correct and minimal"; real disconnect detection and the PM/PLL calls unchanged). Verdict: CHANGES REQUESTED, four should-fix findings, all fixed:

1. Watchdog fed on the idle flag, which is set before LVGL stops and cleared by an LVGL timer: blind to a stall during idle entry/wake, and a wake landing between the idle request and its timers could leave LVGL stopped and unfed (false reboot). Fixed: `lvgl_watchdog_pause(true)` next to `lvgl_port_stop()` in `delayed_stop_lvgl`, `lvgl_watchdog_pause(false)` next to `lvgl_port_resume()` in `wake_from_idle_mode`; the low-power task calls `lvgl_watchdog_feed_while_paused()` every loop.
2. `rc_serial_stop()` (called from the LVGL task by the switch) waited up to 5,000 ms, as long as the watchdog timeout. Fixed: 2,000 ms (longest command about 1.2 s).
3. `wifi on` after the Wi-Fi window expired replied `@ok` though the screen disables the switch and `esp_wifi_start` fails. Fixed: `@err expired` when expired; `wifi_config_set_enable` returns the start/stop result and a failure replies `@err wifi <name>` (the switch ignores it, as before).
4. Runner matched replies by arrival order only; the boot burst of `state` answers could shift every later reply onto the next command. Fixed: each `@ok` must carry its command's prefix (`state` and `tile` told apart by ` rot=`), and `wait_ready` drains 1 s after the first answer.

Nits fixed: scenarios read 5 s more after the last command before the verdict; the `watchdog` scenario forgives only the reset, panic and abort markers and requires this crash's `Core dump has been saved to flash.` (the stored dump is never erased); the unreachable `bt` branch of `watch` removed (a stuck lock now reboots in 5 s and fails on `rst:`); `rc_serial` discards input buffered while it was off.

Recorded, not changed:
- `CONFIG_ESP_TASK_WDT_PANIC=y` also turns an idle-task starvation trip on either core from a log line into a reboot. Taken as part of the owner's "reboot when lockup" request.
- `bt` and `hang` stay as debug commands: the interface is a debugging feature and defaults on only during development (the default flips before a release build). `bt` prints through the ROM routine, so its output can interleave with buffered log lines.
- Lead for the open LVGL freeze (reviewer): `wifi off` (switch or remote) calls `esp_wifi_stop()` while holding the LVGL lock, and the Wi-Fi event handlers call `wifi_config_update_status()` → `lvgl_port_lock(0)`. If the event task blocks on the lock while `esp_wifi_stop()` waits on the event task, that is a lock-order cycle that looks like the freeze. Unconfirmed; the next freeze leaves a core dump.
- Pre-existing, outside this ADR (reviewer's reading of the timer order, not seen on the device): a wake landing after `enter_idle_mode(true)` but before its `delayed_stop_lvgl` timer can leave LVGL stopped while the idle flag is clear, so the screen stays frozen until the next idle request and wake. The watchdog stays fed in that state (paused at the stop) rather than rebooting.

## Stage 4 on the fixed build (2026-11-06 01:23-01:33)

Build: review fixes plus the patched connection monitor (`components/esp_driver_usb_serial_jtag`), profiling and trace facility on.

- `scenario state_change` (`build/rc_logs/rc_state_change_20261106_012303.log`): `VERDICT PASS`. 3 idle entries, 3 wakes, 4 IP connects, zero failure markers, zero watchdog triggers.
- `scenario rotation_sweep` (`build/rc_logs/rc_rotation_sweep_20261106_012523.log`): `VERDICT PASS`, rotations 0-3 with 120 s each.
- Readings (both): `largest_dma` min 114,688; `taskLVGL` 8,736; `sys_evt` 1,356; `rc_serial` 860; `LPMON` 1,848.
- Console loss: `stats` replies 6 of 6 and 17 of 17; `LowPowerMode` once-a-second lines 140 and 487 with none missing; zero cut lines; zero replies dropped by the runner's matching. Before the patch the same `state_change` load lost about 10 lines and replies in its first 5 minutes (`rc_watch_20261106_003529.log`). The connection-monitor fix holds.
- Remaining (owner at the device, 2026-11-07): `tap`/`swipe` (Stage 3), switch-off A/B (Stage 5), final build with profiling off and `sdkconfig` regenerated, registry Built.

## Passive capture after Stage 4 (2026-11-06 01:34-02:34)

- Capture of the fixed build with nothing sent (`scratchpad/adr4_fix_overnight.log`), device idle from the start. Covers uptime 638-4,275 s, then `AXP2101: System is powering off...` at 3,601 s of idle: the unit's sleep timeout (1 h) powers it off by design, so this was 1 h of idle, not an overnight run.
- 3,595 once-a-second `LowPowerMode` lines with none missing; zero watchdog triggers (the paused-watchdog path fed for the whole hour), zero failure markers. One cut line, at the very start: the device's TX buffer content from before the port was opened (the driver drops output while no host reads, as designed).
- An overnight soak with the display active needs the unit kept awake (scripted activity, as `rotation_sweep` does) or a longer sleep timeout set by the owner.

## Final build and the LVGL freeze root cause (2026-11-06 09:41-10:00)

- Owner turned the sleep timeout off. Display-active soak dropped at the owner's call (not needed: `largest_dma` floor and console loss already settled). Its opening `state_change` on the profiling build (`build/rc_logs/rc_soak_20261106_094139.log`): `VERDICT PASS`, `largest_dma` min 114,688, `taskLVGL` 8,224, `rc_serial` 924.
- Final build: `DISPLAY_PROFILING_ENABLED 0`, `sdkconfig` regenerated from the defaults (`CONFIG_FREERTOS_USE_TRACE_FACILITY` off, `CONFIG_ESP_TASK_WDT_PANIC=y`), image 0x1d4970 (54% of the app partition free). Flashed; boots.
- `scenario state_change` on it (`build/rc_logs/rc_state_change_20261106_094546.log`): `VERDICT FAIL`. On the second `idle`, the LVGL watchdog fired 4.2 s into idle (`- lvgl`), saved a core dump and rebooted; the rest of the scenario ran on the rebooted unit. This is the LVGL freeze, now caught by the watchdog.
- Core dump (`idf.py coredump-info`): `taskLVGL` (priority 10/4, inherited from `axp2101_monitor` waiting in `lvgl_port_lock`) is in `lv_display_refr_timer` → `display_flush_cb` (`main/display_flush.c:248`, second strip) → `panel_sh8601_draw_bitmap` → `panel_io_spi_tx_param` → `spi_device_acquire_bus(portMAX_DELAY)`, blocked in `spi_bus_lock` `dev_wait`. The LCD is the only device on SPI2, so the wait is on the SPI driver's background side: the first strip's queued DMA transaction never completed. The LVGL lock is held for good, which matches the earlier freeze (lock held, no panic, at uptime ~161 s: about the 60 s idle timeout after a `state_change`).
- Order in the idle entry: `delayed_enter_idle_mode` (LVGL timer) runs `enter_idle_mode(true)`: `wifi_request_stop()` (`esp_wifi_stop`, Wi-Fi releases its APB power lock), panel off, BNO085 sleep, `esp_pm_configure` to 80 MHz, then creates `delayed_stop_lvgl`. LVGL keeps rendering and flushing in that same pass, so a display DMA transfer runs while Wi-Fi stops and the clock config changes. The stall began right after `esp_pm_configure` and the Wi-Fi stop in the same pass. Which of the two stalls the transfer is not proven.
- Rate: 40 more idle/wake cycles on the digital level tile with Wi-Fi connected at 39 of 40 entries (`build/rc_logs/rc_idle_cycles_20261106_095101.log`): 0 stalls. On this build, 1 stall in 42 idle entries.
- Outside ADR-004 (pre-existing low-power code). The reviewer's Wi-Fi lock-order lead is not what this dump shows.

## Idle-entry fix and the residual display stall (2026-11-06 10:00-12:30)

Owner: "Fix it now"; outside ADR-004, its own commit.

- Broken invariant: no display DMA may be in flight while idle entry stops Wi-Fi and changes the clock config. LVGL's `lv_timer_enable(false)` does not end the pass it is called from, so with idle entry run from an LVGL timer the refresh timer still flushed in the same pass, after `esp_pm_configure` and the Wi-Fi stop.
- Fix (`main/low_power_mode.c`): the low power monitor task (LPMON) alone runs idle entry and wake. `low_power_mode_request_idle()` and `wake_from_idle_mode()` post a request bit and notify LPMON, which runs it at once (the 1 s poll keeps its period). Entry: LVGL lock (no pass running), `lvgl_port_stop()`, watchdog pause, panel off (waits for the last frame's strips), BNO085 sleep, unlock, then Wi-Fi stop and `esp_pm_configure` outside the lock. Wake is the reverse: clock up, lock, BNO085 wake, panel on, idle flag cleared, watchdog resume, `lvgl_port_resume()`, unlock, Wi-Fi start. A wake posted together with an idle request wins. The external-button wake (`main/lvgl_display.c`) uses `wake_from_idle_mode()`. This also removes the reviewer's wake-before-stop race (stage 6). The old LVGL-timer chain (`delayed_enter_idle_mode`, `delayed_exit_idle_mode`, `delayed_stop_lvgl`) is gone.
- LPMON stack (4096 B) with `esp_wifi_stop()` now on it: lowest free 1,832 B over 30 cycles on a profiling build (1,848 B before). Unchanged.
- Results on the final build with the fix: 200 idle/wake cycles (`build/rc_logs/rc_idle_cycles_20261106_101515.log`): 1 stall; 400 cycles (`rc_idle_cycles_20261106_110855.log`): 0 stalls, `stats` 149 of 149; `scenario state_change` (`rc_state_change_20261106_122339.log`): `VERDICT PASS`, `largest_dma` min 114,688. Plus 30 cycles on the profiling build: 0 stalls. Before the fix: 1 stall in 42 idle entries. After: 1 in 630. One event on each side, so "lower", not a measured factor.
- Residual stall (open, outside ADR-004): cycle 80 of the 200-cycle run, about 0.4 s after a wake with the display active, during the Wi-Fi scan after `esp_wifi_start()`. Same place in the core dump: `display_flush_cb` → `spi_device_acquire_bus` waiting on a strip transfer that never completed (strip y 308-336, mid-frame); `taskLVGL` priority 20/4, inherited from `sys_evt` waiting on the LVGL lock. Here `esp_pm_configure` ran with LVGL stopped and the panel drained, so a clock change mid-DMA does not explain it. 100 Wi-Fi off/on toggles with the display active (`rc_wifi_toggle_20261106_105432.log`): 0 stalls, which at this rate does not clear Wi-Fi. Cost when it happens: up to 5 s frozen, watchdog reboot, UI back about 3 s later (~9 s; no BNO085 abort after a watchdog reset).
- Next: a dump read straight from flash (`read_flash` before anything else; a `coredump-info` reset lets the BNO085 first-boot abort overwrite it) to tell a transfer the hardware never finished (`cur_cs` set, `in_isr`) from a wake-up the driver lost (`cur_cs` none with requests queued).
- Console: 2 `stats` replies lost in about 236 sent across the fixed-code runs, both while idle; `stats` takes no lock. Separate from the stall; not chased.

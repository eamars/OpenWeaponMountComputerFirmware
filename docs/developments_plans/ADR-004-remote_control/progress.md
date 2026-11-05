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

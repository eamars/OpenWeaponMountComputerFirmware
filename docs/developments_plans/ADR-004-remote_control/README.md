# adr-004: remote control driver and serial interface for automated device tests

## Summary

- Goal: let a host script drive and observe the device over COM11, so the device checks in the ADRs (state-change runs, rotation sweeps, soaks) run without the owner at the device and end in a machine-checked verdict.
- Plan class: large
- Status: in_progress (approved by the owner 2026-11-05)
- Mandatory skills: `esp32-debug`, `development-plan`, `lvgl-widget` (settings switch)
- Overall cutover strategy: bigbang for new code; the existing UI keeps working unchanged, and the feature can be switched off in Settings.
- Highest-risk areas: commands touching LVGL from another task; commands that bypass the UI's own callbacks; the serial reader competing with log output on the same port.
- Acceptance criteria: every command in the command table works on the device with the expected reply; the host runner executes the state-change scenario unattended and prints a verdict; with the switch off, no reader task exists and the firmware behaves as before.

## Owner's direction (2026-11-05)

1. Purpose: fully automated device tests in future, so the owner need not sit at the device. For now it is a debugging feature; authentication waits for a later web transport.
2. Two layers: a universal `remote_control` driver tied to LVGL, and a serial interface on top. A web-based remote control may be added later as another transport.
3. Everything the owner was asked to do on the device in ADR-001 and ADR-002 must be possible over the serial interface.
4. An on/off setting for the serial interface goes in System settings, next to the global log level.
5. The existing USB Serial/JTAG console already supports input; no USB driver is added.

## Context

- Console: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`. ESP-IDF's default console functions read without a driver: "Read is non-blocking, write is busy waiting until TX FIFO has enough space. These functions are used by default." (`esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag_vfs.h`). Nothing in the firmware reads input today.
- UI structure (`main/main_tileview.c`): one `lv_tileview` (`main_tileview`). Tiles (column, row): (2,0) countdown timer config, (2,1) digital level, (2,2) DOPE config, (3,1) settings menu (`config_view.c`, `lv_menu`), (0,2) sensor calibration, (0,0) low-power view and (0,1) OTA view (entered automatically only). `tile_change_callback` runs each tile's enable callback on `LV_EVENT_VALUE_CHANGED`.
- Owner actions used in ADR-001/002 checks and where they live: Wi-Fi switch and Save (`main/wifi_config.c`: `toggle_enable_wifi`, `on_save_button_pressed`); rotation dropdown and Save (`main/system_config.c`: `update_rotation_event_cb`, `on_save_button_pressed`); tile swipes (touch → `lv_tileview`); idle and wake (`main/low_power_mode.c`: inactivity in `low_power_monitor_task`, `enter_idle_mode`, `wake_from_idle_mode`, wake on touch in `touchpad_read_cb_wrapper`).
- NVS configs are blobs with a CRC (`main/common.c`, `load_config`): a struct that grows fails the CRC once and resets to defaults. Adding a field to `system_config_t` would reset the owner's saved rotation, brightness and log level.
- Host side: the repo `venv` (Python 3.15.2) has `pyserial` 3.5.

Adjacent improvement areas intentionally left for later plans:

- Web transport and its authentication.
- Screenshots over serial.
- Replacing the scratchpad capture scripts used in ADR-001/002 (they keep working).

## Mandatory Skills

- `esp32-debug` (`.agents/skills/esp32-debug/SKILL.md`): load before building, flashing or capturing serial output.
- `development-plan` (`.agents/skills/development-plan/SKILL.md`): load before executing, verifying or signing off this plan.
- `lvgl-widget` (`.agents/skills/lvgl-widget/SKILL.md`): load before adding the settings switch.

## Mandatory Rules

- After any automatic context compaction, the parent or active execution agent must reread this entire plan before continuing implementation, verification, handoff, or final reporting.
- After signing off any major progress checklist stage, the parent or active execution agent must reread this entire plan before starting the next stage.
- Before final completion, lifecycle status changes, merge, or sign-off, the parent agent must run the plan's `Independent Code Review` gate and record the result in `Execution Evidence`.
- The plan's `Execution Model` must use parent-led native subagent execution unless the user explicitly approves fallback execution.
- All ADR-001 build and device rules apply unchanged (ADR-001 README, Mandatory Rules).
- Every LVGL call from the remote control runs under `lvgl_port_lock` with a finite timeout (1,000 ms); on timeout the command replies `@err busy`.
- Semantic commands call the same functions the UI callbacks call. Where a UI callback takes an `lv_event_t`, the logic is moved into a plain function that both the callback and the remote control call; the callback keeps its behaviour.
- The serial reader never blocks the console: reads are the default non-blocking reads, polled every 20 ms.
- Remote-control replies are single lines starting `@ok` or `@err`, so they can be told apart from log lines.
- Warn the owner before any command that resets the device, and never use COM11 while a capture is open.

## Must Do

### Layer 1: `remote_control` (transport-agnostic)

- Create `main/remote_control.h` / `main/remote_control.c` with the interface in Contracts And Data Shapes.
- Create a virtual pointer input device (`lv_indev_create`, `LV_INDEV_TYPE_POINTER`) in `remote_control_init`, fed by a small internal queue of points, for `tap` and `swipe`. Coordinates are logical (as LVGL sees them at the current rotation).
- Every command except `idle`, `state`, `stats` and `help` counts as user activity (`update_low_power_mode_last_activity_event`), so a scripted session keeps the device awake the way a person does.
- Implement every command in the command table, with exactly the replies shown.

### Layer 2: `rc_serial` (serial transport)

- Create `main/rc_serial.h` / `main/rc_serial.c`: `rc_serial_start()` creates one task `rc_serial` with `xTaskCreateWithCaps(..., HEAPS_CAPS_ALLOC_DEFAULT_FLAGS)` (stack 4,096 B) that polls `read(STDIN_FILENO, ...)` every 20 ms, assembles lines of up to 128 characters (longer lines reply `@err too_long`), passes each line to `remote_control_execute`, and prints the reply with one `printf` and `fflush(stdout)`. `rc_serial_stop()` deletes the task with `vTaskDeleteWithCaps`.

### Settings switch and persistence

- Add a "Remote control" switch to the System settings page (`create_system_config_view_config` in `main/system_config.c`), below the global log level.
- Store the flag in its own NVS namespace `"RC"` through `load_config` / `save_config` (struct `remote_control_config_t { bool serial_enable; }`, default `true` while the firmware is in development). Do not change `system_config_t`.
- Toggling the switch starts or stops `rc_serial` immediately. The System page's Save, Reload and Reset buttons also save, reload and reset this flag.
- At boot, start `rc_serial` after `wifi_init()` if the flag is on.

### UI refactors needed for semantic commands

- `main/system_config.c`: `system_config_set_rotation(lv_display_rotation_t)` holds the body of `update_rotation_event_cb` and also sets the dropdown's selection; the callback calls it.
- `main/wifi_config.c`: `wifi_config_set_enable(bool)` holds the body of `toggle_enable_wifi` and also sets the switch state; the callback calls it.
- `main/low_power_mode.c`: `low_power_mode_request_idle()` runs the same path the inactivity timeout runs to enter idle mode; `wake_from_idle_mode()` is used as is.

### Host runner

- Create `tools/remote_control/rc_run.py` (repo `venv`, `pyserial`), with commands `send "<line>"` (one command, prints the reply) and `scenario <name>` (runs a named scenario). It opens COM11 without toggling DTR/RTS, writes command lines, waits for the matching `@ok`/`@err` (timeout 5 s per command), keeps reading log lines, and writes the whole session to a log file under `build/rc_logs/`.
- Scenario `state_change`: exactly the ADR-001 Stage 5 steps: `wifi on`, `save wifi`, wait for `Connected with IP Address:`; `tile` through every tile that can be entered; three `idle` → 20 s → `wake` cycles; `rot 1`, `save system`, `rot 2`, `save system`; `wifi off`, 10 s, `wifi on`, wait for the IP address; `stats` every 30 s throughout.
- Scenario `rotation_sweep`: `rot 0..3`, each followed by 120 s on the digital level tile with `stats` every 30 s.
- Each scenario ends with a verdict line `VERDICT PASS` or `VERDICT FAIL <reasons>`, judged on: every command `@ok`, zero failure markers (as in ADR-001, plus no `@err`), and the stack and heap floors given on the command line (`--min-lvgl-stack`, `--min-largest-dma`), with the readings printed.

## Deferred

- No web transport, authentication, JSON or screenshot support.
- No changes to display, memory layout settings, Wi-Fi behaviour, or any existing UI behaviour beyond the three refactors.
- No USB Serial/JTAG driver install. (Amended 2026-11-06 by the owner: the driver is installed, and a patched copy of the IDF component fixes false disconnects; see `progress.md`.)
- No remote control of OTA updates or provisioning reset.

## Cutover Policy

Overall strategy: bigbang

| Area | Policy | Instruction |
|---|---|---|
| Remote control and serial transport | bigbang | New code; on by default; switchable in Settings. |
| UI callbacks | bigbang | Logic moved into plain functions; callbacks call them. No duplicate logic. |
| NVS | bigbang | New namespace `"RC"`; existing namespaces untouched. |

- The responsible execution agent must follow the selected policy for each area and must not choose a more conservative strategy by default.
- Any change to a cutover policy requires owner approval before implementation.

## Target State

- With the switch on, a line typed on COM11 is executed and answered with one `@ok`/`@err` line within the command's time; logs keep flowing as before.
- `tools/remote_control/rc_run.py scenario state_change` runs the ADR-001 Stage 5 routine without the owner and prints a verdict.
- With the switch off, no `rc_serial` task exists (stack report), input on COM11 is ignored, and the UI works as before.

## Design Decisions

| Topic | Decision | Rationale |
|---|---|---|
| Layer boundary | `remote_control_execute(line, reply)`: text in, one reply line out | A future web transport passes the same lines; no transport knowledge in layer 1. |
| Input fidelity | Virtual LVGL pointer for `tap`/`swipe`, plus semantic commands for settings | Taps and swipes go through LVGL's real input path; semantic commands make tests independent of screen coordinates for the common actions. |
| Wake | `wake` calls `wake_from_idle_mode()`; `tap` while idle does not wake | The real wake hook sits in the touch-panel read wrapper, which the virtual pointer does not pass through; an explicit command keeps both paths honest. |
| Flag storage | Own NVS namespace `"RC"` | Growing `system_config_t` would reset the owner's saved settings (CRC mismatch). |
| Default | On while in development | Automated tests must not need a tap on the device first. Flip before a release build. |
| Reader | Non-blocking console reads polled every 20 ms, PSRAM stack | No driver, no internal RAM; latency of 20 ms is irrelevant for tests. |

## Contracts And Data Shapes

### `main/remote_control.h`

```c
esp_err_t remote_control_init(void);   // after lvgl_display_init(); creates the virtual pointer
// Execute one command line (no newline). Writes one reply line (no newline) into reply.
// Thread-safe; may block up to the command's time. Returns true for @ok, false for @err.
bool remote_control_execute(const char *line, char *reply, size_t reply_size);
```

### `main/rc_serial.h`

```c
esp_err_t rc_serial_start(void);   // idempotent
void rc_serial_stop(void);         // idempotent
bool rc_serial_is_running(void);
```

### Command table

| Command | Action | Reply on success |
|---|---|---|
| `help` | List commands | `@ok help <command names separated by spaces>` |
| `state` | Read state | `@ok tile=<col>,<row> rot=<0-3> wifi=<on\|off> ip=<0\|1> idle=<0\|1> rc=1` |
| `stats` | Heap snapshot; stack headroom when the trace facility is on | `@ok free_int=<B> largest_dma=<B> min_int=<B> free_spiram=<B> [stack:<task>=<B> ...]` |
| `tap <x> <y>` | Press and release at logical `(x, y)` over 100 ms | `@ok tap` |
| `swipe <left\|right\|up\|down>` | Press at the screen centre, move 60% of the screen in 6 steps over 150 ms, release | `@ok swipe <dir>` |
| `tile <col> <row>` | Show that tile and run its enable callback exactly as a swipe would | `@ok tile=<col>,<row>`; `@err no_tile` if it does not exist |
| `rot <0-3>` | `system_config_set_rotation` | `@ok rot=<n>` |
| `wifi <on\|off>` | `wifi_config_set_enable` | `@ok wifi=<on\|off>`; `@err expired` after the Wi-Fi window expired (the screen disables the switch); `@err wifi <esp_err name>` if start/stop fails |
| `save <system\|wifi>` | The same save function the page's Save button calls | `@ok save <page>`; `@err nvs <esp_err name>` on failure |
| `idle` | `low_power_mode_request_idle` | `@ok idle` |
| `wake` | `wake_from_idle_mode` | `@ok wake` |
| `reboot` | Reply, flush, then `esp_restart()` after 100 ms | `@ok reboot` |
| `bt` | Debug (added 2026-11-06): every task's backtrace on the console, no LVGL lock needed | `@ok bt <complete\|corrupt>` |
| `hang` | Debug (added 2026-11-06): take the LVGL lock and never release it; proves the LVGL watchdog | `@ok hang`, then a watchdog panic and reboot |

Errors: `@err unknown <cmd>`, `@err args`, `@err busy` (LVGL lock timeout), `@err too_long`.

## Change Surface

### Create

- `main/remote_control.h`, `main/remote_control.c`: layer 1. Public entrypoints `remote_control_init`, `remote_control_execute`.
- `main/rc_serial.h`, `main/rc_serial.c`: layer 2. Public entrypoints `rc_serial_start`, `rc_serial_stop`, `rc_serial_is_running`.
- `tools/remote_control/rc_run.py`: host runner.
- `docs/developments_plans/ADR-004-remote_control/progress.md`: evidence.

### Modify

- `main/system_config.c` / `.h`: `system_config_set_rotation`; Remote control switch; Save/Reload/Reset include the `"RC"` flag.
- `main/wifi_config.c` / `.h`: `wifi_config_set_enable`.
- `main/low_power_mode.c` / `.h`: `low_power_mode_request_idle`.
- `main/app.c`: `remote_control_init()` after `lvgl_display_init()`; `rc_serial_start()` after `wifi_init()` when the flag is on.
- `docs/developments_plans/README.md`: registry row.

### Keep

- Display path (ADR-002 step 1), memory layout (ADR-001), all other UI.

## Overdesign Guardrail

- Actual problem: device checks need a person at the device and end in "roughly ok".
- Minimal change: one command layer, one serial transport, one switch, one host script with two scenarios.
- Ownership boundaries: `remote_control` owns command grammar and LVGL interaction; `rc_serial` owns serial line I/O; UI modules own their behaviour and expose one plain function each for the remote control; the host runner owns verdicts.
- Rejected complexity: a plugin or command-registration framework, JSON, a web server, authentication, screenshots, a USB driver, scripting inside the firmware.
- Evidence threshold: a second transport actually being built, or a test that cannot be expressed with these commands.

## Agent Autonomy Boundaries

- The responsible agent may choose local implementation mechanics only when they preserve the contracts in this plan.
- The responsible agent must not introduce new architecture, alternate migration strategies, compatibility layers, fallback paths, or extra features.
- Changes are limited to the Change Surface.
- The responsible agent must not perform unrelated cleanup, formatting churn, dependency upgrades, or broad refactors.
- If the plan and code disagree, the responsible agent must preserve the plan's stated intent and report the discrepancy.
- If a required instruction is impossible, the responsible agent must stop and report the blocker instead of inventing a substitute.

## Implementation Order

1. Parent records the starting point: ADR-002 step 1 Built (or its overnight result), current tree builds.
2. Production subagent implements layer 1, layer 2, the switch, the refactors and the host runner.
3. Parent builds a verification build, flashes, runs each command by hand with `rc_run.py send`, then `scenario state_change` and `scenario rotation_sweep`.
4. Parent runs the A/B check: switch off, reboot, confirm no `rc_serial` task and input ignored; switch on again.
5. Final build, independent code review, status updates.

## Execution Model

- Parent agent owns orchestration, builds, flashing, device runs, execution evidence, review feedback remediation, commits, lifecycle updates, and final sign-off.
- Parent agent establishes the focused contract first (the command table as the expected replies) before production implementation starts.
- Production-code subagent: exactly one native subagent; owns the production code and the host runner in Change Surface only; closes after they are complete, excluding review fixes.
- Independent code-review subagent: exactly one native subagent, started after planned verification passes; reviews the plan, diff, and evidence; reports findings; does not implement fixes.
- If native subagent capability is unavailable, stop before execution unless the user explicitly requests fallback execution.

## Progress Checklist

- [ ] Stage 1 - starting point recorded
  - Verify: tree builds; ADR-002 step 1 status recorded.
  - Sign-off: `<agent/date>`
- [ ] Stage 2 - production change complete
  - Verify: build passes with no warnings from changed files; greps: no LVGL call in `rc_serial.c`; no `lv_` call in `remote_control.c` outside `lvgl_port_lock`.
  - Evidence: grep output, build tail.
  - Sign-off: `<agent/date>`
- [ ] Stage 3 - commands on the device
  - Verify: every command in the table sent with `rc_run.py send` returns the documented reply and has the documented effect (checked through `state`, logs, and the owner once for `tap`/`swipe`).
  - Evidence: session log path and the reply of each command.
  - Sign-off: `<agent/date>`
- [ ] Stage 4 - scenarios
  - Verify: `scenario state_change` and `scenario rotation_sweep` end in `VERDICT PASS` unattended.
  - Evidence: session logs, verdict lines, readings.
  - Sign-off: `<agent/date>`
- [ ] Stage 5 - switch off A/B
  - Verify: `tap` the Remote control switch off and `save system`, then `reboot`: no `rc_serial` in the stack report and `state` gets no reply. Switching it back on needs the touch screen (the serial interface is off), so the owner switches it on in Settings once and presses Save.
  - Evidence: logs.
  - Sign-off: `<agent/date>`
- [ ] Stage 6 - final build, review, status
  - Verify: final build boots; independent review approved; registry updated.
  - Sign-off: `<agent/date>`

## Verification

### Build

- `idf.py build` passes in `build/` (run `idf.py reconfigure` first for new sources).

### Device

- Stage 3, Stage 4 and Stage 5 as listed above, with the ADR-001 failure markers counted in every session.

## Independent Code Review

Run this gate after all `Verification` checks pass and before final sign-off. The parent agent creates one independent code-review subagent through the harness's native subagent capability.

Review scope:

- Thread safety: every LVGL call under the lock; no LVGL call from `rc_serial`; the virtual pointer queue between the command task and the LVGL task.
- Semantic commands call the same functions as the UI; the refactored callbacks behave as before.
- Line handling: length limit, CR/LF handling, no buffer overrun, reply always one line.
- The `"RC"` NVS namespace and the switch lifecycle (start, stop, save, reload, reset, boot).
- Host runner verdict logic against the plan's criteria.

## Acceptance Criteria

This plan is complete when:

- Every command in the table works on the device with the documented reply.
- Both scenarios end in `VERDICT PASS` without the owner at the device.
- The switch-off A/B check passes.
- The independent code review has approved, and `progress.md` holds the evidence.
- The registry row for ADR-004 says Built.

## Risks

| Risk | Mitigation | Verification |
|---|---|---|
| A command touches LVGL without the lock | Lock rule; review gate | Grep in Stage 2; review |
| Semantic command diverges from the UI path | Refactor to one shared function | Review; Stage 3 effects checked through logs |
| Serial reader slows or drops logs | Non-blocking reads every 20 ms | Failure-marker counts and log continuity in Stage 4 |
| Scripts keep the device awake unrealistically | `idle`/`state`/`stats` do not count as activity | Idle cycles in `state_change` |

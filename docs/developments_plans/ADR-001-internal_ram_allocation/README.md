# adr-001: internal ram allocation, move non-critical memory to psram

## Summary

- Goal: free internal SRAM for the display driver by moving memory that does not need internal RAM into PSRAM, with no change to display performance behaviour, behind a safety net that leaves evidence of every runtime failure.
- Plan class: medium
- Status: in_progress (approved by the owner 2026-11-05)
- Mandatory skills: `esp32-debug`, `development-plan`
- Overall cutover strategy: bigbang per change, in separately flashed and verified commits: safety net, task stacks, QSPI transfer size, then (after the soak) Change 4 and its revert and Change 5 stack sizes.
- Highest-risk areas: task self-deletion lifecycle for stacks created in PSRAM; the QSPI transfer size change on the display bus; failures that appear only during state changes or after hours.
- Acceptance criteria: with Wi-Fi off, steady-state internal free heap ≥ 155,000 B and largest DMA-capable block ≥ 80,000 B; a 10-minute Wi-Fi-on run through the listed state changes, with zero failure markers, zero failed-allocation reports, no new core dump, and every application task's minimum free stack ≥ 512 B; frame rate on the digital level view not below 15 frames/s at QSPI 4091 (16 frames/s with the stack move alone).

## Owner's direction (2026-11-05)

1. The point of moving memory to PSRAM is to free internal SRAM for the display driver.
2. Order of work: first the non-performance steps (move memory to PSRAM). Every performance change is treated as risky (unpredictable runtime instability) and is added one at a time, in ADR-002.
3. The QSPI transfer size 4092 (configured as 4091) is intentional.
4. Anything left in the code base around tearing is deliberate: the full-screen redraw of the digital level view and the current `full_refresh = false` stay as they are. The tearing seen before was diagonal ("everything morphed 45°").
5. Newer managed dependencies are accepted (LVGL 9.6.0, tinyusb 0.21.0 are now in `managed_components/`).
6. Add five risk controls: core dump to flash with a boot-time crash report; a log line with backtrace for every failed allocation; a task-stack headroom report in verification builds; the stack move and the QSPI change flashed and verified separately; a soak through state changes plus an overnight run. Unit tests are not used for this plan: its failure modes (memory placement, Wi-Fi load timing, task lifecycle) appear only on the running device.
7. Stage 3 correction (2026-11-05): the internal free heap target is 155,000 B, not 168,000 B. The first estimate counted three tasks whose views are commented out (never created) and the pre-dependency-update starting point.
8. Stage 4 decision (2026-11-05): keep QSPI 4091. It costs about 6 ms of flush per frame against 4096 (15.8 vs 19.4 frames/s once stacks are in PSRAM) and gives a 114.7 KB largest DMA block instead of 64 KB. The frame-rate floor for this plan becomes 15 frames/s; ADR-002 step 1 owns recovering the flush cost.
9. Change 4 added (2026-11-05; premise wrong, see item 10): the Stage 5 reboot was `assert failed: spinlock_acquire (lock->count == 0)` in `esp_lcd_touch_ft3168_read_data`; the core dump puts the touch handle, which holds a `portMUX_TYPE`, at `0x3c1bb584` (PSRAM). Spinlocks are not safe in PSRAM on the ESP32-S3. Both touch drivers allocate the handle with `MALLOC_CAP_DEFAULT`; they move to internal RAM. A class audit found no other spinlock in PSRAM.
10. Change 4 reverted, Change 5 added (2026-11-05): the item 9 premise is wrong. ESP-IDF v5.5.2 handles spinlocks in PSRAM by disabling interrupts and taking an internal CAS lock (`esp_cpu_compare_and_set`). The corrupted spinlock sat 236 B below the base of the `taskLVGL` stack, and the second Stage 5 run measured `taskLVGL` at 44 B free and `sys_evt` at 300 B free. The leading explanation, not proven, is a `taskLVGL` stack overflow that wrote below its base. Change 5 raises both stacks; Change 4 is reverted because, under that explanation, it would only move the corrupted neighbour.
11. Overnight run deferred (2026-11-05): the owner does not run the device overnight at this stage. The overnight run is removed from this plan's acceptance and left for a later soak; Stage 5 is the state-change run only.

## Context

Measured on the device on COM11 (ESP32-S3, 8 MB PSRAM), branch `spiram_optimization` at `c6f2c63`, LVGL 9.5.0, Wi-Fi off by the device's saved setting, 75-second capture on 2026-11-05:

| Reading | Value |
|---|---|
| Internal free heap at NVS init (boot) | 301,315 B |
| Internal free heap, steady state | 128,215 B |
| Largest internal / DMA-capable free block | 40,960 B |
| Minimum internal free (watermark) | 121,687 B |
| PSRAM free | 5,743,672 B of a 6.5 MB heap |
| Frames per second (`flush=` count, digital level view) | 17 |
| QSPI actual max transaction | 8,184 B (configured 4096) |

PSRAM is not short. Internal SRAM is: about 173 KB of internal heap is taken at runtime, and the largest free DMA-capable block is only 40 KB.

Static internal RAM is already small (`idf.py size`: DIRAM 132,075 B used of 341,760 B, about 146 KB internal across all libraries). Nearly all of it is code that must stay internal (FreeRTOS, HAL, `spi_flash`, heap, Wi-Fi fast paths). This plan therefore targets runtime allocations.

Runtime internal consumers that can move:

- Twelve application task stacks are created with plain `xTaskCreate`, so their stacks and TCBs come from internal RAM (46,080 B of stack in total). `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` is already set.
- `LCD_QSPI_MAX_TRANSFER_SIZE` was raised from 4091 to 4096 in `c6f2c63`. ESP-IDF rounds this up to two DMA descriptors (8,184 B per transaction). The SPI master allocates a temporary internal DMA buffer for every queued transaction whose source is in PSRAM (`esp_driver_spi/src/gpspi/spi_master.c`, `setup_priv_desc`), and the LCD IO queue depth is 10. That is up to about 80 KB of internal DMA memory in flight, against about 40 KB at 4091. The earlier trial (`references/display-refresh-rate-rca.md`) measured no frame-rate gain from 4096.

Why PSRAM stacks are safe here: this build sets `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and `CONFIG_SPIRAM_RODATA=y`, so ESP-IDF does not disable the cache for flash operations (`SPI_FLASH_CACHE_NO_DISABLE`, `spi_flash/spi_flash_os_func_app.c`). A task with a PSRAM stack can write NVS. The LVGL task already runs with a PSRAM stack (`task_stack_caps` in `main/lvgl_display.c`, since `0ab2c9e`, 2026-02-06), and every settings Save button writes NVS from it; Stage 5 presses Save on the device to confirm this path.

Failure evidence today: a panic prints to serial and reboots (`CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y`). There is no core dump (the `espcoredump` component is not in the build), the firmware does not log the reset reason, and a failed allocation leaves no trace beyond whatever the caller prints. A failure that happens while no serial monitor is attached leaves nothing. `partitions.csv` leaves 0x12000–0x320000 unused (about 3 MB). The FreeRTOS stack-overflow canary and both watchdogs are already on.

Adjacent improvement areas intentionally left for later plans:

- The abort on first boot after a reset ("Failed to detect BNO085", `ESP_ERROR_CHECK` in BNO085 init) is a bug to be fixed later (owner, 2026-11-05). This plan only tolerates it as a known marker.
- OTA app rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is off): needs a bootloader reflash over serial on every unit.
- The two LVGL software draw threads (2 × 8 KB internal) are created inside LVGL's FreeRTOS OSAL with plain `xTaskCreate`; moving them needs a patch to the LVGL component.
- `trans_size` / `LVGL_DISPLAY_TRANS_SIZE` is not read by `esp_lvgl_port` 2.7.1 on LVGL 9; the profiler line that reports it is misleading.
- `components/XPowersLib` and `components/bno08x/sh2` are gitlinks with no `.gitmodules`; a fresh clone cannot fetch them.
- Bug B-001 (`references/bug_list.md`): the Wi-Fi disable condition in `wifi_init` is always true.
- Host-run unit tests for pure logic with past bugs (angle wrapping, B-008 colour gaps, config CRC load/save).
- Wi-Fi IRAM optimisations, `CONFIG_LOG_IN_IRAM`, LVGL draw unit count.

## Mandatory Skills

- `esp32-debug` (`.agents/skills/esp32-debug/SKILL.md`): load before building, flashing or capturing serial output.
- `development-plan` (`.agents/skills/development-plan/SKILL.md`): load before executing, verifying or signing off this plan.

## Mandatory Rules

- After any automatic context compaction, the parent or active execution agent must reread this entire plan before continuing implementation, verification, handoff, or final reporting.
- After signing off any major progress checklist stage, the parent or active execution agent must reread this entire plan before starting the next stage.
- Before final completion, lifecycle status changes, merge, or sign-off, the parent agent must run the plan's `Independent Code Review` gate and record the result in `Execution Evidence`.
- The plan's `Execution Model` must use parent-led native subagent execution unless the user explicitly approves fallback execution.
- Build environment: ESP-IDF v5.5.2 at `C:\workspace\esp\frameworks\esp-idf-v5.5.2`. Before dot-sourcing `export.ps1`, set `IDF_TOOLS_PATH=c:\workspace\esp\tools`, `IDF_PYTHON_ENV_PATH=c:\workspace\esp\tools\python_env\idf5.5_py3.11_env`, and prepend `c:\workspace\esp\tools\tools\idf-python\3.11.2` to `PATH`. Without these, `export.ps1` picks a missing Python 3.12 environment.
- The device is on COM11. `.vscode/settings.json` still says COM5; do not use it.
- Build only in the project's `build/` directory with the project `sdkconfig`. Do not create a second build directory or pass a different `SDKCONFIG`: on 2026-11-05 that made the component manager re-resolve and replace the shared `managed_components/`.
- `sdkconfig` is untracked and generated; `sdkconfig.defaults.esp32s3` is the tracked source. After editing the defaults, delete `sdkconfig`, run `idf.py reconfigure`, and grep `sdkconfig` to confirm every intended value before building.
- Never enable `CONFIG_HEAP_TASK_TRACKING` or heap poisoning. On 2026-11-05 task tracking deadlocked `heap_caps_free` inside critical sections and boot-looped the device; poisoning adds bytes to every allocation and skews the readings this plan relies on.
- Opening COM11 resets the board. The first boot after a reset may abort once on "Failed to detect BNO085" and then recover; this one abort is known and allowed. Any other abort, panic or reset is a failure.
- Do not change the device's saved settings yourself. The owner performs every on-device action in Stage 5.
- Do not leave temporary firmware changes behind (`esp32-debug`): any temporary edit is reverted before its stage is signed off, and `git status` shows only the change surface below.
- Keep the project convention: allocate with `HEAPS_CAPS_ALLOC_DEFAULT_FLAGS` (defined as `MALLOC_CAP_SPIRAM` in `main/app_cfg.h`); do not write `MALLOC_CAP_SPIRAM` literally at call sites.
- The failed-allocation hook may run in any context, including ISRs and critical sections: it must be `IRAM_ATTR`, must not allocate, and may print only with `esp_rom_printf` and `esp_backtrace_print`.
- Commit each of the three changes (safety net, task stacks, QSPI) separately, only after its stage passes. Do not push without the owner's approval.

## Must Do

### Change 1: safety net

- Add a `coredump` partition to `partitions.csv`: type `data`, subtype `coredump`, offset `0x12000`, size `0x20000`. All other rows unchanged.
- Add `espcoredump` to `ALL_DEPS` in `main/CMakeLists.txt`.
- Set in `sdkconfig.defaults.esp32s3`: `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y`, `CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y`, `CONFIG_ESP_COREDUMP_CHECK_BOOT=y`. Leave `CONFIG_ESP_COREDUMP_STACK_SIZE` at its forced default (1792 B internal, because task stacks may be in PSRAM).
- Create `main/crash_report.c` and `main/crash_report.h` with `void crash_report_log_last_reset(void)`. It logs the reset reason (`esp_reset_reason()`) on every boot. When the reason is `ESP_RST_PANIC`, `ESP_RST_INT_WDT`, `ESP_RST_TASK_WDT` or `ESP_RST_WDT` and `esp_core_dump_image_check()` returns `ESP_OK`, it logs the summary from `esp_core_dump_get_summary()`: task name, exception PC, and the backtrace PCs. It does not erase the dump; the next crash overwrites it.
- Add `void mem_diag_install_alloc_failure_hook(void)` to `main/mem_diag.c` / `.h`. It registers a callback with `heap_caps_register_failed_alloc_callback` that prints the requested size, the caps bits, the heap function name, and a backtrace of depth 8.
- Add `void mem_diag_start_stack_report(void)` to `main/mem_diag.c` / `.h`. When `CONFIG_FREERTOS_USE_TRACE_FACILITY` is enabled, it creates one task with `xTaskCreateWithCaps(..., HEAPS_CAPS_ALLOC_DEFAULT_FLAGS)` (stack 4096) that every 300 s prints, for every task from `uxTaskGetSystemState`, the task name, whether its stack is internal or PSRAM, and its minimum free stack in bytes (`usStackHighWaterMark`). When the option is disabled, the function body is empty.
- In `app_main` (`main/app.c`), call `mem_diag_install_alloc_failure_hook()` and `crash_report_log_last_reset()` first, before `esp_event_loop_create_default()`, and call `mem_diag_start_stack_report()` after `wifi_init()`.

### Change 2: task stacks to PSRAM

- Create each of these twelve tasks with `xTaskCreateWithCaps(..., HEAPS_CAPS_ALLOC_DEFAULT_FLAGS)` instead of `xTaskCreate`, keeping name, stack size, parameter, priority and handle unchanged:

| Task name | Created in | Stack (B) |
|---|---|---:|
| `accel_poller` | `main/acceleration_analysis_view.c` | 3072 |
| `buzzer_task` | `main/buzzer.c` | 4096 |
| `countdown_timer` | `main/countdown_timer.c` | 4096 |
| `sensor_poller` (unified, digital level) | `main/digital_level_view_controller.c` | 3072 |
| `STABILITY` | `main/low_power_mode.c` | 3072 |
| `OT_poller` | `main/opentrickler_remote_controller_view.c` | 4096 |
| `ota_poller` | `main/ota_mode.c` | 4096 |
| `axp2101_monitor_task` | `main/pmic_axp2101.cpp` | 4096 |
| `poi_sensor_poller` | `main/point_of_aim_view.c` | 4096 |
| `RVReportPoller` | `main/sensor_calibration_view.c` | 4096 |
| `wifi_deinit` | `main/wifi.c` | 4096 |
| `sensor_poller` (BNO085) | `components/bno08x/src/bno085.c` | 4096 |

- Delete every task created with caps through `vTaskDeleteWithCaps`, never `vTaskDelete`: the `ota_poller_task` self-delete in `main/ota_mode.c`, the `wifi_deinit_task` self-delete in `main/wifi.c`, and `axp2101_deinit` in `main/pmic_axp2101.cpp`.

### Change 3: QSPI transfer size

- Set `LCD_QSPI_MAX_TRANSFER_SIZE` in `main/app_cfg.h` back to `4091`.

### Change 4: reverted

- Revert commit `6f5f32a` (touch handles in internal RAM) with `git revert`. Its premise was wrong (owner item 10).

### Change 5: stack headroom for LVGL and the event loop

- In `main/lvgl_display.c`, set `lvgl_cfg.task_stack` to `16384` (was `8192`). The stack stays in PSRAM (`task_stack_caps`).
- In `sdkconfig.defaults.esp32s3`, set `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=3584` (was `2304`). The ESP-IDF event loop task runs this firmware's Wi-Fi event handler; its stack is internal.

### All changes

- Set `DISPLAY_PROFILING_ENABLED` in `main/app_cfg.h` to `0` in the final build. Verification builds set it to `1` and enable `CONFIG_FREERTOS_USE_TRACE_FACILITY` in the local `sdkconfig` only.
- Run every check in `Verification` and record the results in `progress.md`.

## Deferred

- Do not move `LPMON` (`low_power_monitor_task`): it owns the light-sleep path (`enter_sleep_mode`, `esp_light_sleep_start`), which must run from an internal stack. The call is commented out today; the stack stays internal so re-enabling sleep stays safe.
- Do not move `ota_updater` (`ota_update_task`): it writes the OTA partition and there is no OTA test on the device yet.
- Do not change the display flush path, buffer modes, `full_refresh`, rotation, invalidation, QSPI clock, LVGL draw units or LVGL draw-thread stacks (ADR-002 owns display performance).
- Do not patch LVGL or `esp_lvgl_port`.
- Do not change Wi-Fi, LWIP, mbedTLS, NVS, mDNS, panic or watchdog settings in `sdkconfig.defaults.esp32s3`; only the four core dump options and `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` (Change 5) change.
- Do not enable OTA app rollback, add unit tests, fix B-001, add `.gitmodules`, or remove `trans_size`.
- Do not add lazy task creation, stack-size reductions, wrapper helpers, or config flags beyond those named in Must Do.

## Cutover Policy

Overall strategy: bigbang

| Area | Policy | Instruction |
|---|---|---|
| Crash evidence | bigbang | Core dump and crash report on permanently; no runtime switch. |
| Failed-allocation hook | bigbang | Installed permanently in every build. |
| Stack report | bigbang | Compiled only when `CONFIG_FREERTOS_USE_TRACE_FACILITY` is on; no other switch. |
| Task stack placement | bigbang | Replace `xTaskCreate` with `xTaskCreateWithCaps` at the twelve listed sites. No flag to switch back. |
| Task deletion | bigbang | Replace the three listed `vTaskDelete` calls with `vTaskDeleteWithCaps`. |
| QSPI transfer size | bigbang | Set `4091`. No runtime selection. |
| Touch handle placement | bigbang | Change 4 reverted; touch handles stay as allocated before ADR-001. |
| LVGL and event-loop stacks | bigbang | Set 16,384 B and 3,584 B. No runtime selection. |
| Profiling | bigbang | Default `0`; the code stays. |

- The responsible execution agent must follow the selected policy for each area and must not choose a more conservative strategy by default.
- Any change to a cutover policy requires owner approval before implementation.

## Target State

- Every boot logs its reset reason. On units flashed over serial with this partition table (an OTA update does not rewrite the partition table, so OTA-updated units have no `coredump` partition and log `ESP_ERR_NOT_FOUND`): after a panic or watchdog reset, the boot log shows the crashed task, exception PC and backtrace, and `idf.py coredump-info` reads the full dump from the `coredump` partition.
- Every failed heap allocation prints its size, caps and a backtrace.
- Twelve application task stacks live in PSRAM (their TCBs stay internal: `xTaskCreateWithCaps` allocates the TCB with `pvPortMalloc`, internal only); `LPMON`, `ota_updater`, LVGL draw threads and ESP-IDF system tasks keep internal stacks.
- Each LCD color transaction is one 4,092-byte DMA descriptor (`display_prof_cfg` reports `qspi_actual_max_transfer=4092`).
- The final firmware does not print `display_prof`, `digital_level_prof` or stack-report lines.
- Behaviour visible to the user is unchanged: same screens, same frame rate, settings save, Wi-Fi connect, OTA manifest polling.

## Design Decisions

| Topic | Decision | Rationale |
|---|---|---|
| Crash evidence | ELF core dump to a flash partition, CRC32 | Survives a reboot with no monitor attached; ELF is required for the on-device summary; CRC32 needs less of the 1.8 KB dump stack than SHA256. |
| Partition placement | `0x12000`, 128 KB, in the unused gap | No app partition moves; 128 KB covers about 30 tasks' used stack. |
| Dump lifecycle | Kept until the next crash overwrites it; summary printed only on a crash reset | A summary on every boot would look like a new crash. |
| Allocation hook output | `esp_rom_printf` + `esp_backtrace_print` | The hook can run in ISRs and critical sections; `ESP_LOG` and allocation are not allowed there. The heap passes only its own function name, so the backtrace identifies the caller. |
| Stack report switch | `CONFIG_FREERTOS_USE_TRACE_FACILITY` | `uxTaskGetSystemState` needs it anyway; no new project flag. |
| Stack API | `xTaskCreateWithCaps` / `vTaskDeleteWithCaps` | ESP-IDF's supported way to place a task's stack in PSRAM (the TCB stays internal); self-deletion is supported in v5.5.2 (`freertos/esp_additions/idf_additions.c`). Core dumps accept PSRAM stacks (`espcoredump/src/port/xtensa/core_dump_port.c`). |
| Caps value | `HEAPS_CAPS_ALLOC_DEFAULT_FLAGS` | Existing project convention for "this belongs in PSRAM". |
| `bno08x` component edit | Allowed | The BNO085 poller is created inside the component; moving its stack cannot be done from `main/`. One call site changes. |
| `LPMON`, `ota_updater` stay internal | Fixed | Light sleep and OTA flash writes are the two paths this plan cannot verify on the device. |
| QSPI 4091 | Fixed | Owner's intended value; halves in-flight internal DMA buffers versus 4096 with no measured frame-rate cost. |
| Three commits | Fixed | A failure points at one change, and each change reverts on its own. |

## Change Surface

### Create

- `main/crash_report.c`, `main/crash_report.h`: boot-time reset reason and core dump summary. Public entrypoint `crash_report_log_last_reset()`.
- `docs/developments_plans/ADR-001-internal_ram_allocation/progress.md`: execution evidence.

### Modify

- `partitions.csv`: `coredump` row.
- `sdkconfig.defaults.esp32s3`: the four core dump options.
- `main/CMakeLists.txt`: `espcoredump` dependency.
- `main/mem_diag.c`, `main/mem_diag.h`: `mem_diag_install_alloc_failure_hook()`, `mem_diag_start_stack_report()`.
- `main/app.c`: the three calls in `app_main`.
- `main/acceleration_analysis_view.c`, `main/buzzer.c`, `main/countdown_timer.c`, `main/digital_level_view_controller.c`, `main/low_power_mode.c` (`STABILITY` only), `main/opentrickler_remote_controller_view.c`, `main/point_of_aim_view.c`, `main/sensor_calibration_view.c`: task creation call.
- `main/ota_mode.c`: `ota_poller` creation and its self-delete. `ota_updater` creation and self-delete stay unchanged.
- `main/wifi.c`: `wifi_deinit` creation and its self-delete.
- `main/pmic_axp2101.cpp`: monitor task creation and the delete in `axp2101_deinit`.
- `components/bno08x/src/bno085.c`: sensor poller creation (outside `main/`; see Design Decisions).
- `main/lvgl_display.c`: `lvgl_cfg.task_stack` (Change 5).
- `sdkconfig.defaults.esp32s3`: `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE` (Change 5).
- `components/esp_lcd_touch_ft3168/esp_lcd_touch_ft3168.c`, `components/esp_lcd_touch_axs5106/esp_lcd_touch_axs5106.c`: Change 4 and its revert; net unchanged.
- `main/app_cfg.h`: `LCD_QSPI_MAX_TRANSFER_SIZE` and `DISPLAY_PROFILING_ENABLED`.
- `docs/developments_plans/README.md`: registry row at sign-off.

### Keep

- `sdkconfig.defaults.esp32s3` as on the branch apart from the core dump options: `LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n`, the reduced Wi-Fi/LWIP buffers, mbedTLS in external memory with dynamic buffers, NVS cache and mDNS task in PSRAM.
- `mem_diag_log_checkpoint()` and the display profiler code in `main/lvgl_display.c` (only `lvgl_cfg.task_stack` changes there).

## Overdesign Guardrail

- Actual problem: internal SRAM, not PSRAM, is exhausted at runtime, which blocks giving the display driver more internal DMA memory; and a runtime failure today can leave no evidence.
- Minimal change: move twelve task stacks to PSRAM, restore the 4091 transfer size, and add three small diagnostics that ESP-IDF already provides (core dump, failed-allocation callback, task system state).
- Ownership boundaries: each module keeps owning its task's lifecycle; ESP-IDF owns placement through caps; `crash_report` owns boot-time crash reporting; `mem_diag` owns memory diagnostics.
- Rejected complexity: a task-creation wrapper, a config flag per task, lazy start, stack-size tuning, heap tracing or poisoning builds, on-device unit tests, dump upload or erase logic, failure counters persisted to NVS.
- Evidence threshold: a stack-overflow report, a cache-related panic, a failed-allocation report, or a frame-rate drop below 15 frames/s on the device would be needed before reconsidering any listed task or adding further diagnostics.

## Agent Autonomy Boundaries

- The responsible agent may choose local implementation mechanics only when they preserve the contracts in this plan.
- The responsible agent must not introduce new architecture, alternate migration strategies, compatibility layers, fallback paths, or extra features.
- Changes outside `main/`, `partitions.csv` and `sdkconfig.defaults.esp32s3` are limited to the one call site in `components/bno08x/src/bno085.c` (and the net-zero Change 4 revert in the touch drivers).
- The responsible agent must not perform unrelated cleanup, formatting churn, dependency upgrades, or broad refactors.
- If the plan and code disagree, the responsible agent must preserve the plan's stated intent and report the discrepancy.
- If a required instruction is impossible, the responsible agent must stop and report the blocker instead of inventing a substitute.

## Implementation Order

1. Parent records the Static Greps on the unchanged tree as the focused contract.
2. Production subagent implements Change 1. Parent regenerates `sdkconfig`, builds, erases the `coredump` region once (`esptool erase_region 0x12000 0x20000`), flashes, and runs the Stage 2 device check. Commit.
3. Production subagent implements Change 2. Parent builds, flashes, runs the Stage 3 device check. Commit.
4. Production subagent implements Change 3. Parent builds, flashes, runs the Stage 4 device check. Commit.
4a. Production subagent implemented Change 4; it passed Stage 4a and was committed (`6f5f32a`), then the second Stage 5 run disproved its premise.
4b. Parent reverts `6f5f32a`. Production subagent implements Change 5. Parent regenerates `sdkconfig`, builds, flashes, runs the Stage 4b device check. Commit.
5. Parent runs the Stage 5 state-change run with the owner. (The overnight run is deferred, owner item 11.)
6. Parent restores the final configuration (profiling `0`, `sdkconfig` regenerated from defaults), builds, flashes, checks one clean boot.
7. Independent code review, then registry update.

## Execution Model

- Parent agent owns orchestration, static checks, builds, flashing, device captures, execution evidence, review feedback remediation, commits, lifecycle updates, and final sign-off.
- Parent agent establishes the focused contract first (static grep expectations) and records today's counts before production implementation starts.
- Production-code subagent: exactly one native subagent, started after the focused contract is established; owns the production code changes in Change Surface only, one change at a time when the parent asks for it; closes after Change 3 is complete, excluding review fixes.
- Independent code-review subagent: exactly one native subagent, started after planned verification passes; reviews the plan, diff, and evidence; reports findings to the parent; does not implement fixes.
- If native subagent capability is unavailable, stop before execution unless the user explicitly requests fallback execution.

## Progress Checklist

- [ ] Stage 1 - focused contract recorded
  - Covers: step 1.
  - Verify: Static Greps on the unchanged tree; expected today: 15 `xTaskCreate(` lines (14 calls plus the commented one in `main/app.c`), 0 `xTaskCreateWithCaps(`, 4 `vTaskDelete(`, 0 `vTaskDeleteWithCaps(`, `LCD_QSPI_MAX_TRANSFER_SIZE 4096`.
  - Evidence: counts in `progress.md`.
  - Handoff: next is Stage 2.
  - Sign-off: `<agent/date>`
- [ ] Stage 2 - safety net on the device
  - Covers: step 2.
  - Verify: Device check "Safety net".
  - Evidence: `sdkconfig` grep, partition table line from the boot log, reset-reason lines, crash summary lines, `idf.py coredump-info` output head, commit hash.
  - Handoff: next is Stage 3.
  - Sign-off: `<agent/date>`
- [ ] Stage 3 - task stacks in PSRAM
  - Covers: step 3.
  - Verify: Static Greps for Change 2; build; Device check "Task stacks".
  - Evidence: grep output, averaged heap readings, stack report, failure-marker counts, commit hash.
  - Handoff: next is Stage 4.
  - Sign-off: `<agent/date>`
- [ ] Stage 4 - QSPI transfer size 4091
  - Covers: step 4.
  - Verify: Static Greps for Change 3; build; Device check "QSPI".
  - Evidence: `display_prof_cfg` line, averaged heap readings, frame counts, failure-marker counts, commit hash.
  - Handoff: next is Stage 5.
  - Sign-off: `<agent/date>`
- [ ] Stage 4a - touch handles in internal RAM
  - Covers: step 4a.
  - Verify: Static Greps for Change 4; build; Device check "Touch handle".
  - Evidence: grep output, boot log line with the handle address, failure-marker counts, commit hash.
  - Handoff: next is Stage 5 (rerun from the start).
  - Sign-off: `<agent/date>`
- [ ] Stage 4b - Change 4 reverted, Change 5 in
  - Covers: step 4b.
  - Verify: Static Greps for Change 5; `sdkconfig` shows `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=3584`; build; Device check "Stacks".
  - Evidence: grep output, revert and change commit hashes, stack report, failure-marker counts.
  - Handoff: next is Stage 5 (rerun from the start).
  - Sign-off: `<agent/date>`
- [ ] Stage 5 - soak
  - Covers: step 5.
  - Verify: Device check "State-change run" (the overnight run is deferred, owner item 11).
  - Evidence: capture file paths, the owner's confirmation of each on-device action, failure-marker and failed-allocation counts, stack reports and heap readings of the state-change run.
  - Handoff: next is Stage 6.
  - Sign-off: `<agent/date>`
- [ ] Stage 6 - final build flashed
  - Covers: step 6.
  - Verify: `DISPLAY_PROFILING_ENABLED 0`; `sdkconfig` regenerated and `CONFIG_FREERTOS_USE_TRACE_FACILITY` off; build succeeds; one boot reaches the main screen with only the allowed BNO085 abort; `git status` shows only the change surface.
  - Evidence: boot log excerpt, `git status --short`.
  - Handoff: next is Stage 7.
  - Sign-off: `<agent/date>`
- [ ] Stage 7 - independent code review
  - Covers: step 7, review part.
  - Verify: review subagent reports; parent fixes in-surface findings; Static Greps and build rerun after any fix.
  - Evidence: findings, fixes, reruns, approval status in `progress.md`.
  - Handoff: next is Stage 8.
  - Sign-off: `<agent/date>`
- [ ] Stage 8 - lifecycle
  - Covers: step 7, registry part.
  - Verify: registry row for ADR-001 says Built.
  - Evidence: registry diff.
  - Sign-off: `<agent/date>`

## Verification

### Static Greps

After Change 2:

- `rg -n "xTaskCreate\(" main components/bno08x/src` returns exactly three lines: `low_power_mode.c` (`LPMON`), `ota_mode.c` (`ota_updater`), and the commented-out `mem_monitor_task` line in `main/app.c`. Any other match fails.
- `rg -n "xTaskCreateWithCaps\(" main components/bno08x/src` returns exactly thirteen lines: one per task in Change 2, plus the stack-report task in `main/mem_diag.c`.
- `rg -n "vTaskDelete\(" main components/bno08x/src` returns exactly one line: the `ota_update_task` self-delete in `main/ota_mode.c`.
- `rg -n "vTaskDeleteWithCaps\(" main components/bno08x/src` returns exactly three lines (`ota_mode.c`, `wifi.c`, `pmic_axp2101.cpp`).

After Change 5:

- `rg -n "MALLOC_CAP_DEFAULT" components/esp_lcd_touch_ft3168 components/esp_lcd_touch_axs5106` returns two lines again, one per driver (Change 4 reverted).
- `rg -n "task_stack = 16384" main/lvgl_display.c` returns one line.
- `rg -n "^CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=3584" sdkconfig.defaults.esp32s3 sdkconfig` returns one line in each file.

After Change 3:

- `rg -n "define LCD_QSPI_MAX_TRANSFER_SIZE" main/app_cfg.h` shows `4091`.

### Build

- `idf.py build` succeeds in `build/` after each change.

### Device

Verification builds: `DISPLAY_PROFILING_ENABLED 1` and `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` in the local `sdkconfig`. Failure markers, counted in every capture: `Guru Meditation`, `abort()` (except one allowed BNO085 detect abort right after a reset), `ESP_ERR_NO_MEM`, `queue) color failed`, `stack overflow`, Wi-Fi buffer allocation failure, failed-allocation hook output, and `rst:` lines not caused by the capture opening the port. Each check passes only with zero failure markers. Heap readings are averaged from `display_prof` lines: `free_int`, `largest_int`, `min_int`, `free_dma`, `largest_dma`, `min_dma`, `free_spiram`.

- Safety net (Wi-Fi off, 360 s): the boot log shows the `coredump` partition at `0x12000` and a reset-reason line on every boot. If the allowed BNO085 abort occurs, the following boot prints its summary (task name, PC, backtrace) and `idf.py -p COM11 coredump-info` decodes it. If no crash occurs in three port-open resets, flash a verification build with a temporary `abort()` in `app_main` after `wifi_init()`, confirm the summary and `coredump-info`, then remove the `abort()`, rebuild and reflash before sign-off. The stack report prints at 300 s.
- Task stacks (Wi-Fi off, 360 s, digital level view on screen): average `free_int` ≥ 155,000 B; `flush=` ≥ 16 per 1-s window on average; in the stack report, every Change 2 task alive at report time shows PSRAM, `LPMON` and `ota_updater` show internal, and every application task (created in `main/` or `components/bno08x/`) has minimum free stack ≥ 512 B. ESP-IDF system tasks are recorded but not judged: their stack sizes are ESP-IDF settings outside this plan (`ipc0` 500 B and `ipc1` 428 B free in Stage 2, by design).
- QSPI (Wi-Fi off, 120 s): `display_prof_cfg` reports `qspi_actual_max_transfer=4092`; average `largest_dma` ≥ 80,000 B; average `free_int` ≥ 155,000 B; `flush=` ≥ 15 per 1-s window on average.
- Stacks (Wi-Fi off, 330 s): stack report shows `taskLVGL` in PSRAM with minimum free ≥ 8,192 B and `sys_evt` with minimum free ≥ 1,024 B; zero failure markers. In Stage 5, `taskLVGL` must keep ≥ 2,048 B free and `sys_evt` ≥ 512 B.
- State-change run (600 s, owner on the device, in this order): turn Wi-Fi on in settings and press Save; wait for `Connected with IP Address:`, the `mdns_init end` checkpoint and `Network connected, will poll OTA source`; open every main tile once, including sensor calibration (the acceleration analysis, point of aim and OpenTrickler views are commented out in `main/main_tileview.c` and cannot be opened); let the device go idle and wake it three times; change rotation once, press Save, change it back, press Save; turn Wi-Fi off and on with the settings switch and wait for the IP address again.
- Overnight run: deferred by the owner (item 11); not part of this plan's acceptance.

## Independent Code Review

Run this gate after all `Verification` checks pass and before final sign-off. The parent agent creates one independent code-review subagent through the harness's native subagent capability. If native subagents are unavailable, stop unless the user explicitly approves fallback execution.

Review scope:

- Every changed creation and delete site against Must Do and Deferred, including that no caps-created task is deleted with `vTaskDelete` and no internal task is deleted with `vTaskDeleteWithCaps`.
- Task handles: every handle stored before still stored, and cleared after deletion where it was before.
- The failed-allocation hook obeys its context rule (IRAM, no allocation, ROM printing only).
- `crash_report_log_last_reset()` prints a summary only after a crash reset, and the partition row matches Must Do.
- Alignment with Change Surface, Cutover Policy and Agent Autonomy Boundaries; no edits outside the listed files.
- Evidence quality in `progress.md`: commands, capture paths, readings, and failure-marker counts are present.

The parent fixes concrete findings only inside the change surface, reruns Static Greps and the build, and records findings, fixes and approval status in `progress.md`.

## Acceptance Criteria

This plan is complete when:

- The Static Greps, the builds and every device check pass as written above.
- A crash on this firmware leaves a decodable core dump and a boot-log summary, shown once on the device.
- The final flashed build has profiling and the stack report off and boots cleanly.
- Each change is its own commit.
- The independent code review has approved, and `progress.md` holds the evidence.
- The registry row for ADR-001 says Built.

## Risks

| Risk | Mitigation | Verification |
|---|---|---|
| A moved task runs code that needs an internal stack | Light-sleep and OTA-write tasks excluded; flash writes from PSRAM stacks are allowed in this build | State-change run with Save presses; core dump captures any panic |
| Wrong delete API leaks or corrupts a stack | Every caps task deleted with `vTaskDeleteWithCaps` | Static Greps; review gate. `wifi_deinit` self-deletes only after the 600 s Wi-Fi expiry timeout and `axp2101_deinit` has no callers, so these two delete paths are not exercised on the device |
| A moved task's stack is too small once in PSRAM | Sizes unchanged; canary on | Stack report ≥ 512 B free for every application task in the state-change run |
| Failure appears only after hours or during transitions | State-change run; overnight run deferred (owner item 11), so failures that need hours are not covered by this plan | Zero failure markers; core dump partition empty of new dumps |
| Failed-allocation hook itself disturbs timing | ROM printing only, no allocation | Any hook output fails the run anyway and is investigated |
| Slower PSRAM stack access delays sensor polling | Pollers run at 20 ms or slower | Frame count ≥ 15/s; `ui_ok` in `digital_level_prof` not lower in Stage 3 than in Stage 2 (`lock_fail` follows the sensor report rate and is not a slowdown measure) |

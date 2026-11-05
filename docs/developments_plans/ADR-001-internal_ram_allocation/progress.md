# ADR-001 progress

Execution record for [ADR-001](README.md). Times are NZDT.

## Stage 1 - focused contract (2026-11-05)

Tree at `c6f2c63` (branch `spiram_optimization`), before any ADR-001 code change.

| Check | Expected | Found |
|---|---|---|
| `rg -n "xTaskCreate\(" main components/bno08x/src` | 15 | 15 (14 calls + commented `mem_monitor_task` in `main/app.c:123`) |
| `rg -n "xTaskCreateWithCaps\(" ...` | 0 | 0 |
| `rg -n "vTaskDelete\(" ...` | 4 | 4 (`ota_mode.c:378`, `ota_mode.c:536`, `pmic_axp2101.cpp:451`, `wifi.c:114`) |
| `rg -n "vTaskDeleteWithCaps\(" ...` | 0 | 0 |
| `LCD_QSPI_MAX_TRANSFER_SIZE` | 4096 | 4096 |
| `DISPLAY_PROFILING_ENABLED` | - | 1 |

Result: pass.

## Stage 2 - safety net on the device (2026-11-05)

Build: Change 1 in the working tree; `sdkconfig` regenerated from the defaults (old copy kept outside the repo). The regeneration also re-resolved managed components: LVGL 9.6.0~1, tinyusb 0.21.0~3, esp_tinyusb 2.4.0 (owner accepted newer dependencies). Kconfig now warns `unknown kconfig symbol 'TINYUSB'` from the defaults; no effect while `usb_init()` is commented out. Verification-only: `CONFIG_FREERTOS_USE_TRACE_FACILITY=y` in the local `sdkconfig`, `DISPLAY_PROFILING_ENABLED 1`.

`sdkconfig` after reconfigure: `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y`, `CONFIG_ESP_COREDUMP_CHECKSUM_CRC32=y`, `CONFIG_ESP_COREDUMP_CHECK_BOOT=y`, `CONFIG_ESP_COREDUMP_STACK_SIZE=1792`; branch memory options unchanged (`LV_ATTRIBUTE_FAST_MEM_USE_IRAM` off, `ESP_WIFI_STATIC_RX_BUFFER_NUM=4`, `MBEDTLS_DYNAMIC_BUFFER=y`, `NVS_ALLOCATE_CACHE_IN_SPIRAM=y`).

Coredump region erased once (`esptool erase_region 0x12000 0x20000`), then flashed.

Device, Wi-Fi off, two captures (360 s, then 330 s after the stack-report pacing fix):

- Boot log: `coredump Unknown data 01 03 00012000 00020000`; `Found partition 'coredump' @ 12000 131072 bytes`.
- Reset reason logged on every boot: `USB (11)` on the port-open reset, `PANIC (4)` after the allowed BNO085 abort.
- The BNO085 abort wrote a dump: `Core dump used 1200 bytes on stack. 684 bytes left free`, `Found core dump 17700 bytes in flash`.
- Next boot: `Stored core dump: task=main pc=0x40380b2d depth=7` plus backtrace.
- `idf.py -p COM11 coredump-info` decodes it: `abort()` from `_esp_error_check_failed` on `bno085_init_i2c(...)` at `main/app.c:179` (`app_main`). This is the first-boot BNO085 bug the owner will fix later.
- Failure markers: 1 `rst:` (port open), 1 `abort()` = the allowed BNO085 abort; 0 Guru, 0 `ESP_ERR_NO_MEM`, 0 LCD queue failures, 0 stack overflow, 0 `alloc_failed:`.
- Heap (avg after 10 s): `free_int` 122,583; `largest_int` 32,768; `min_int` 111,419; `free_dma` 114,843; `largest_dma` 32,768; `min_dma` 103,679; `free_spiram` 5,661,372. Frames 16.8/s. `qspi_actual_max_transfer=8184`. Reference before ADR-001 (LVGL 9.5, no core dump): `free_int` 128,215, `largest_dma` 40,960. The drop includes the 1,792 B dump stack and the dependency update; not separated further.
- Stack report (one round at 300 s, all `INTERNAL` except `taskLVGL` `PSRAM`): application tasks min free: `sensor_poller` 864 and 1,936, `STABILITY` 1,096, `LPMON` 1,916, `taskLVGL` 1,828, `buzzer_task` 1,948, `axp2101_monitor` 2,116, `countdown_timer` 2,336, `ota_updater` 2,844, `ota_poller` 3,252, `RVReportPoller` 3,252, `wifi_deinit` 3,280. ESP-IDF tasks (recorded, not judged): `ipc1` 428, `ipc0` 500, `sys_evt` 556, `IDLE0` 576, `IDLE1` 584, `Tmr Svc` 1,216, `tiT` 2,384, `esp_timer` 3,316, `wifi` 4,260, `swdraw` 6,504 / 6,544.
- Known limitation: the console is the S3 USB-Serial-JTAG port. Without pacing it dropped 11 of about 30 report lines; with 20 ms pacing the first two lines of a round (`stack_rpt: begin`, `mem_stack_rpt`) were still lost after a 4-minute idle period with no other output. Root cause not known. Completeness is judged across rounds in the overnight run.
- Plan correction (factual): the stack criterion applies to application tasks; ESP-IDF system tasks `ipc0`/`ipc1` run at 500/428 B free by ESP-IDF's own sizing.

Result: pass.

## Stage 3 - task stacks in PSRAM (2026-11-05)

Static Greps after Change 2: `xTaskCreate(` 3 (`low_power_mode.c` LPMON, `ota_mode.c` ota_updater, commented line in `app.c`); `xTaskCreateWithCaps(` 13 (twelve Change 2 sites + `mem_stack_rpt`); `vTaskDelete(` 1 (`ota_update_task` self-delete); `vTaskDeleteWithCaps(` 3 (`ota_mode.c`, `wifi.c`, `pmic_axp2101.cpp`). Pass.

Build: pass; no warnings from changed files (pre-existing `unknown kconfig symbol` warnings from the full-dump defaults only). Firmware `v0.0.1-99-gdf140bd-dirty`.

Discrepancies recorded:

- `components/bno08x/src/bno085.c` passes `MALLOC_CAP_SPIRAM` directly with a comment: the component cannot include `main/app_cfg.h`, so the `HEAPS_CAPS_ALLOC_DEFAULT_FLAGS` convention cannot apply there.
- `main/countdown_timer.c` gained `#include "app_cfg.h"`: it had reached the macro only indirectly through `buzzer.h`.
- `accel_poller`, `poi_sensor_poller` and `OT_poller` are never created: their views are commented out in `main/main_tileview.c`. Their sites change but cannot be exercised on the device.
- `wifi_deinit` self-deletes only after the 600 s Wi-Fi expiry timeout; `axp2101_deinit` has no callers. Both `vTaskDeleteWithCaps` paths are covered by review, not by the device.

Device, Wi-Fi off, 360 s:

- Heap (avg after 10 s): `free_int` 159,118 (min 153,715); `largest_int` 63,969; `min_int` 144,591; `free_dma` 151,378; `largest_dma` 63,969; `min_dma` 136,851; `free_spiram` 5,626,469. Versus Stage 2: `free_int` +36,535 B, `largest_dma` 32,768 → 63,969 B.
- Frames 19.0/s average. `digital_level_prof` average per 1-s window: `ui_ok` 16.1 (Stage 2: 16.3), `lock_fail` 20.8 (Stage 2: 18.4).
- Failure markers: 1 `rst:` (port open), 1 allowed BNO085 abort; all others 0, including `alloc_failed:`.
- Stack report, complete round (`begin count=24`, 24 lines): PSRAM: `taskLVGL` 1,816, `sensor_poller` 908 and 1,780, `STABILITY` 1,064, `buzzer_task` 1,944, `axp2101_monitor` 2,112, `countdown_timer` 2,348, `RVReportPoller` 3,308, `ota_poller` 3,340, `wifi_deinit` 3,364, `mem_stack_rpt` 3,376. Internal: `LPMON` 1,916, `ota_updater` 2,844; ESP-IDF tasks unchanged from Stage 2 (`ipc1` 428, `ipc0` 500, `IDLE0` 576, `IDLE1` 584, `sys_evt` 732, ...).
- First result against the plan as written: `free_int` 159,118 < 168,000, fail. Cause: the estimate counted 11,264 B of stack for three tasks that never exist and used the pre-update starting point (128,215 B instead of 122,583 B). The nine running tasks hold 34,816 B of stack, matching the measured gain with their control blocks.
- Owner decision 2026-11-05: correct the target to 155,000 B and continue. The risk check for polling delay is corrected to `ui_ok` (not lower than Stage 2 by more than noise): 16.1 vs 16.3.

Result: pass (corrected target).

## Stage 4 - QSPI transfer size 4091 (2026-11-05)

Static Grep: `rg -n "define LCD_QSPI_MAX_TRANSFER_SIZE" main` → `main/app_cfg.h:38: ... 4091`. `main/dev_kit_app_cfg.h` does not define it and is not included in this build. Build: pass.

Device, Wi-Fi off, 120 s:

- `display_prof_cfg`: `qspi_actual_max_transfer=4092`.
- Heap (avg after 10 s): `free_int` 194,323; `largest_int` 114,688; `min_int` 189,667; `free_dma` 186,583; `largest_dma` 114,688; `min_dma` 181,927; `free_spiram` 5,626,507.
- Failure markers: 1 `rst:` (port open), 1 allowed BNO085 abort; all others 0.
- Display timing, windows with ≥ 10 flushes:

| Capture | QSPI actual | Frames/s | Render (µs, includes flush) | Flush callback (µs) |
|---|---:|---:|---:|---:|
| Stage 2 (stacks internal) | 8,184 | 16.8 | 49,219 | 25,092 |
| Stage 3 (stacks in PSRAM) | 8,184 | 19.4 | 47,517 | 24,055 |
| Stage 4 | 4,092 | 15.8 | 54,499 | 30,434 |

- `digital_level_prof`: `ui_ok` 15.7, `lock_fail` 22.7 per window.
- First result against the plan as written: frames 15.8 < 16, fail. The earlier RCA trial had found no frame-rate difference between 4,092 and 8,184 B transfers; with stacks in PSRAM the difference is 3.6 frames/s. Cause of the difference from the RCA trial not established.
- Owner decision 2026-11-05: keep 4091; frame-rate floor for this plan is 15 frames/s; ADR-002 step 1 owns recovering the flush cost.

Result: pass (corrected floor).

## Stage 5, first run - failed (2026-11-05)

Owner on the device, Wi-Fi on (`Connected with IP Address:192.168.2.146`), settings saved (`NS: WIFI`, `NS: CT`, `NS: DOPE` written from the LVGL task without fault). About 65 s after the Wi-Fi connected, while switching tiles: `assert failed: spinlock_acquire spinlock.h:142 (lock->count == 0)` in `taskLVGL`. The core dump (`idf.py coredump-info`) shows `esp_lcd_touch_ft3168_read_data (ctx=0x3c1bb584)` at `components/esp_lcd_touch_ft3168/esp_lcd_touch_ft3168.c:34`, called from `lvgl_port_touchpad_read`. `0x3c1bb584` is PSRAM; the handle holds `portMUX_TYPE lock`, and spinlocks are not safe in PSRAM on the ESP32-S3. The handle was allocated with `heap_caps_calloc(..., MALLOC_CAP_DEFAULT)`.

Whether ADR-001 made this more likely is not established: Changes 1-3 do not touch this allocation or the heap caps policy, and the firmware before ADR-001 had no core dump to show where the handle was.

Class audit: no `portMUX_TYPE` / spinlock in project code; LVGL's only one (`critSectionMux` in `lv_freertos.c`) is a static in internal RAM; `HEAPS_CAPS_ATTR` statics hold no locks; no queue, semaphore, event group, stream buffer or ring buffer is created `WithCaps`. The only spinlocks that can reach PSRAM are the `esp_lcd_touch_t` handles in the FT3168 and AXS5106 drivers.

Owner observation: horizontal rotation is laggier than vertical. Measured: rotation 1 (90°) 12.9 frames/s, flush callback 43.0 ms; rotation 2 (180°) about 16 frames/s, flush 30 ms. Software 90° rotation reads PSRAM column by column. Recorded for ADR-002 step 1.

Owner decision 2026-11-05: add Change 4; rerun Stage 5 from the start. A second reboot during the investigation was caused by `idf.py coredump-info` (esptool resets the chip), not by the firmware.

## Stage 4a - touch handles in internal RAM (2026-11-05)

Static Greps: `MALLOC_CAP_DEFAULT` in both touch drivers: no matches. `MALLOC_CAP_INTERNAL`: 2 (`esp_lcd_touch_axs5106.c:55`, `esp_lcd_touch_ft3168.c:122`). Build: pass.

Device, Wi-Fi off, 120 s: `FT3168 initialized successfully, handle=0x3fcebb8c` (internal DRAM). Heap avg: `free_int` 193,359; `largest_dma` 114,539; frames 16.8/s; `qspi_actual_max_transfer=4092`. Failure markers: 1 `rst:` (port open), 1 allowed BNO085 abort; all others 0.

Result: pass. Touch response is checked by the owner at the start of the Stage 5 rerun.

## Stage 5, second run - failed on stack headroom (2026-11-05)

Capture 900 s starting at device uptime 208 s (the port open did not reset the board this time). Owner actions seen in the log: Wi-Fi connect (`Connected with IP Address:192.168.2.146`, `mdns_init end`), Wi-Fi off/on with reconnect, saves (`NS: WIFI`, `NS: CT` ×2, `NS: DOPE` ×3), rotation 2 → 1 → 3 → 2, idle entry at 418 s and wake at 1,066 s. The log stops at 1,066 s: the owner disconnected the pogo connector by accident; no reset or panic precedes it. `Network connected, will poll OTA source` did not appear in this capture.

- Failure markers: all 0 (no `rst:`, panic, abort, `ESP_ERR_NO_MEM`, LCD queue failure, `alloc_failed:`).
- Heap (avg after 10 s): `free_int` 198,692 (min 193,163); `largest_dma` 114,688; `min_int` 188,067; `free_spiram` 5,623,701. Frames 19.5/s average (includes partial-refresh windows).
- Stack report: three complete rounds (`begin count=24` … `end`). Minimum free per task over the run: `taskLVGL` (PSRAM, 8,192 B stack set in `main/lvgl_display.c`) **44 B**; `sys_evt` (ESP-IDF event loop, 2,304 B, runs this firmware's Wi-Fi event handler) **300 B**; `ipc1` 428; `ipc0` 500; `IDLE0` 576; `IDLE1` 584; `sensor_poller` 808; `STABILITY` 932; `Tmr Svc` 1,216; `tiT` 1,824; `LPMON` 1,852; `buzzer_task` 1,912; `countdown_timer` 2,072; `axp2101_monitor` 2,112; `mdns` 2,496; `wifi_deinit` 3,280; `RVReportPoller` 3,304; `wifi` 3,376; `mem_stack_rpt` 3,388; `ota_updater` 2,840; `esp_timer` 3,108; `swdraw` 6,184.
- Result against the plan: fail. `taskLVGL` is an application-configured task below the 512 B floor. In the Wi-Fi-off Stages 2-4 it never went below 1,816 B.

### Correction to the first-run diagnosis

The first-run analysis said spinlocks are not safe in PSRAM on the ESP32-S3. That is wrong for ESP-IDF v5.5.2: `esp_cpu_compare_and_set` (`components/esp_hw_support/cpu.c`, the `XCHAL_HAVE_S32C1I && CONFIG_SPIRAM` branch) detects a target in external RAM and achieves atomicity by disabling interrupts and taking an internal CAS lock. A spinlock's placement in PSRAM does not by itself corrupt it. Change 4 was therefore made on a false premise.

Re-reading the first-run core dump: `taskLVGL` stack used/free at the panic was 1,456 / 6,728 B (8,184 B). With the panic frame's stack pointer `0x3c1bd140`, the stack top is about `0x3c1bd6f0` and the stack base about `0x3c1bb6f8`. The corrupted spinlock was at `0x3c1bb60c`, 236 B below that base, in the heap block directly under the LVGL task's stack. Together with the 44 B minimum seen under the same workload in the second run, the leading explanation is a `taskLVGL` stack overflow that wrote below its base without touching the canary bytes. This is not proven; no run has caught the overflow itself.

## Stage 4b - Change 4 reverted, Change 5 in (2026-11-05)

- Change 4 code reverted in `ab0be64` (both touch drivers back to `MALLOC_CAP_DEFAULT`; `rg` returns `esp_lcd_touch_axs5106.c:54`, `esp_lcd_touch_ft3168.c:121`). The docs from `6f5f32a` are kept.
- Change 5: `main/lvgl_display.c:384 lvgl_cfg.task_stack = 16384;` and `sdkconfig.defaults.esp32s3:1130 CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=3584`. After deleting `sdkconfig` and `idf.py reconfigure`: `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=3584` and the deprecated alias `CONFIG_SYSTEM_EVENT_TASK_STACK_SIZE=3584` (the old-name line in the defaults' deprecated block does not override it). Build: pass.
- Device, Wi-Fi off, 330 s, firmware `v0.0.1-103-gab0be64-dirty`: stack report `taskLVGL` PSRAM min free 9,944 B (of 16,384), `sys_evt` internal min free 1,868 B (of 3,584). Heap avg `free_int` 193,133, `largest_dma` 114,483; frames 16.5/s. Failure markers: 1 `rst:` (port open), 1 allowed BNO085 abort; all others 0.

Result: pass. Next: Stage 5 from the start.

## Stage 5, third run - state-change part passed (2026-11-05)

Capture from device uptime 595 s to 950 s (stopped after the owner reported "roughly ok"). Firmware `v0.0.1-103-gab0be64` + Change 5 (`793cbbc`), verification build. Owner actions in the log: Wi-Fi connect (`Connected with IP Address:192.168.2.146`), rotation 2 → 1, Wi-Fi off/on with reconnect, saves (`NS: DOPE`, `NS: CT`), three idle → wake cycles (742 s, 816 s, and the wake at 595 s), each followed by a Wi-Fi reconnect. Rotation was not changed back (device left at rotation 1). Owner verdict: "roughly ok".

- Failure markers: all 0 (no `rst:`, panic, abort, `ESP_ERR_NO_MEM`, LCD queue failure, stack overflow, `alloc_failed:`).
- Heap (avg after 10 s): `free_int` 194,075 (min 190,803); `largest_dma` 114,688; `min_int` 186,931; `free_spiram` 5,614,991.
- Stack report, two rounds (604 s, 904 s): `taskLVGL` min free 8,328 B of 16,384 (8,056 B used: the old 8,192 B stack would have had about 136 B left, consistent with the 44 B reading and the overflow explanation); `sys_evt` min free 1,548 B of 3,584 (2,036 B used: about 268 B would have been left in the old 2,304 B stack). Application tasks: lowest `sensor_poller` 876, `STABILITY` 936. ESP-IDF tasks as before (`ipc1` 428, `ipc0` 500).
- `Network connected, will poll OTA source` did not appear in this capture: the poller logs it once per boot, after the first connection, which happened before the capture started. `ota_poller` is absent from every stack-report round in the second and third runs (it was present in Stages 2-4 with Wi-Fi off), so it finished and deleted itself through `vTaskDeleteWithCaps(NULL)` without fault.

Result: pass (`taskLVGL` ≥ 2,048 B, `sys_evt` ≥ 512 B, application tasks ≥ 512 B, zero markers). Overnight run started immediately afterwards, device untouched, Wi-Fi on, rotation 1.

Overnight run: started after the third run, stopped after a few minutes at the owner's direction (owner item 11, deferred). Not evaluated.

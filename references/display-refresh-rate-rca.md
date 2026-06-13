# Display Refresh Rate RCA Notes

## Scope

This reference records the root-cause candidates investigated for slow display response on the ESP32-S3 firmware using an SH8601 QSPI display, LVGL, and `esp_lcd`.

The current RCA list has three actionable items. Software rotation is no longer listed as an RCA because online datasheet review and driver review confirm that SH8601 does not provide the hardware transforms required by the project. Since screen rotation is a hard requirement, software rotation is a fixed design constraint and must be budgeted for, not treated as a removable root cause.

The investigation used the real device attached through the active ESP-IDF serial port configuration. Debug workflows should discover serial ports and ESP-IDF paths from VS Code ESP-IDF settings or the active ESP-IDF environment, not hard-code local values.

## Relevant Hardware And Firmware Path

- Display panel: SH8601.
- Display bus: QSPI through ESP-IDF `esp_lcd`.
- LVGL port: `managed_components/espressif__esp_lvgl_port`.
- Active display configuration:
  - Resolution: 280 x 456.
  - RGB565 frame size: 127,680 pixels, about 255,360 bytes per full frame.
  - Default screen update period: 20 ms in `DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS`.
  - Default game/sensor report period: 20 ms.
  - Default rotation: 180 degrees.
- Important source files:
  - `main/app_cfg.h`
  - `main/bsp.c`
  - `main/lvgl_display.c`
  - `main/system_config.c`
  - `main/digital_level_view_controller.c`
  - `main/digital_level_view_type_1.c`
  - `managed_components/espressif__esp_lcd_sh8601/esp_lcd_sh8601.c`
  - `managed_components/espressif__esp_lcd_sh8601/include/esp_lcd_sh8601.h`
  - `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c`

## External Research: SH8601 Hardware Rotation

### Conclusion

SH8601 should be treated as not supporting hardware rotation for this project.

The SH8601A datasheet documents `MADCTL (36h): Memory Data Access Control`, but the parameter bits are limited to:

- D6: `MX`, memory write direction horizontal flip.
- D3: RGB/BGR order.
- Other transform-like bits are reserved.

The datasheet does not expose the normal `MY` vertical flip or `MV` row/column exchange bits that many other display controllers use for 180-degree or 90/270-degree hardware rotation. A single horizontal flip is not sufficient for the project's rotation requirement.

The Espressif SH8601 driver matches this:

- `panel_sh8601_mirror()` maps `mirror_x` to `MADCTL` bit 6.
- `panel_sh8601_mirror()` returns `ESP_ERR_NOT_SUPPORTED` for `mirror_y`.
- `panel_sh8601_swap_xy()` always returns `ESP_ERR_NOT_SUPPORTED`.

Therefore:

- Hardware 180-degree rotation is not available because vertical mirror is missing.
- Hardware 90/270-degree rotation is not available because X/Y swap is missing.
- LVGL software rotation is the correct functional approach when the project requires rotated display output.

### Sources

- SH8601A datasheet: `MADCTL (36h)` lists only `MX` and `BGR` as active parameter bits, with the other transform bits reserved. Source: `https://admin.osptek.com/uploads/SH_8601_A0_Data_Sheet_Preliminary_UCS_V0_0_191226_1_143481d321.pdf`.
- Espressif component registry for `esp_lcd_sh8601` links the SH8601 datasheet and shows SH8601 QSPI usage. It also notes `rgb_ele_order` is implemented by LCD command `36h`. Source: `https://components.espressif.com/components/espressif/esp_lcd_sh8601/versions/2.0.1~1/readme`.
- Espressif SH8601 driver implementation supports only `mirror_x`, rejects `mirror_y`, and rejects `swap_xy`. Source: `https://github.com/espressif/esp-iot-solution/blob/master/components/display/lcd/esp_lcd_sh8601/esp_lcd_sh8601.c`.
- Local managed component has the same behavior in `managed_components/espressif__esp_lcd_sh8601/esp_lcd_sh8601.c`.

### Impact On This RCA File

Software rotation remains a measured cost, but it is not an RCA. It is a fixed constraint.

The measured rotation-0 experiment is still useful as a cost characterization:

- Baseline default rotation 180:
  - Render avg about 51,017 us.
  - Flush callback avg about 30,219 us.
- Temporary rotation 0:
  - Render avg about 42,803 us.
  - Flush callback avg about 22,010 us.

This shows the approximate cost of the hard requirement, not a candidate fix. Future performance work should reduce the number of pixels that need rotation rather than trying to remove rotation.

## Instrumentation Added During Investigation

Two profiling paths were added for device-side verification.

### Display Flush Profiler

The display profiler records LVGL display flush behavior and timing around the LVGL flush callback.

Captured data includes:

- Render callback count per reporting window.
- Flush callback count per reporting window.
- Flush-ready event count.
- Full-screen flush count.
- Total flushed pixels.
- Average rendered pixels per flush.
- Average render callback duration in microseconds.
- Maximum render callback duration.
- Average flush callback duration in microseconds.
- Maximum flush callback duration.
- LVGL trans-size setting.
- QSPI bus max transfer size.
- Internal free heap.
- Largest and minimum internal heap watermarks.
- DMA-capable free heap, largest block, and minimum watermark.
- 8-bit heap free size, largest block, and minimum watermark.
- SPIRAM free heap.
- Largest SPIRAM block.

Important limitation:

- In this project's LVGL 9 path, `disp_cfg.trans_size` does not appear to split SPI flushes. The LVGL 9 port calls `esp_lcd_panel_draw_bitmap()` directly for the invalidated area. The practical transfer-size knob for this display path is the SPI bus `max_transfer_sz` passed through the SH8601 bus configuration in `main/bsp.c`.

### Digital Level View Profiler

The digital level profiler records the view-controller update loop and LVGL lock behavior.

Captured data includes:

- Controller loop count.
- UI update attempts.
- Successful LVGL lock/update count.
- LVGL lock failures.
- Whether updates were skipped because the screen was not the active digital level screen.
- Current configured update period.

This was necessary because the screen may request updates faster than the display pipeline can consume them, and lock failures are a visible symptom of saturation.

## Test Matrix Summary

The following values are approximate from serial captures and are intended to capture the direction and magnitude of each effect.

| Test | Main Change | Result |
| --- | --- | --- |
| Baseline | Existing 20 ms UI update, rotation 180, QSPI 40 MHz, transfer 4091 | About 17 full-screen flushes/sec. Render avg about 51.0 ms. Flush callback avg about 30.2 ms. Digital-level loop about 51/sec, UI attempts about 40/sec, successes about 17/sec, lock failures about 23/sec. |
| UI update period 100 ms | `DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS` changed from 20 ms to 100 ms | Full-screen flushes dropped to 10/sec. LVGL lock failures dropped to 0. Per-frame cost remained high: render avg about 50.8 ms, flush callback avg about 30.1 ms. |
| Rotation 0 characterization | Forced `LV_DISPLAY_ROTATION_0` only to measure cost of required software rotation | Render avg dropped to about 42.8 ms. Flush callback avg dropped to about 22.0 ms. This is not an RCA fix because rotation is a hard requirement and SH8601 lacks the needed hardware transforms. |
| QSPI pclk 80 MHz | Temporary QSPI pixel clock change from 40 MHz to 80 MHz | Render avg dropped to about 47.3 ms. Flush callback avg dropped to about 26.5 ms. Frame rate did not materially improve while the screen still redrew full-screen at high update pressure. |
| QSPI max transfer 8192 | `LCD_QSPI_MAX_TRANSFER_SIZE=8192` | Repeated transmit failures and memory failures. No stable display-profile sample. |
| QSPI max transfer 6144 | `LCD_QSPI_MAX_TRANSFER_SIZE=6144` | Unstable. Sometimes produced one profile sample with low internal heap, then Wi-Fi allocation failures and reset loop. |
| QSPI max transfer 4096 | `LCD_QSPI_MAX_TRANSFER_SIZE=4096` | Unstable. Sometimes produced one profile sample with low internal heap, then Wi-Fi allocation failures and reset loop. |
| QSPI max transfer 4091 | Restored safe transfer size | Stable. Internal free heap returned to about 42 KB in the selected capture. No panic markers in final selected capture. |

## RCA 1: Full-Screen Invalidation From Digital Level View Saturates The Pipeline

### Observation

The digital level screen attempts to refresh much faster than the display pipeline can complete full-screen redraws.

The baseline captured behavior was:

- Full-screen flushes: about 17 per second.
- Render callback average: about 51,017 us.
- Flush callback average: about 30,219 us.
- Flushed pixels per frame: 127,680, matching the full 280 x 456 screen.
- Digital-level controller loop: about 51 loops/sec.
- UI update attempts: about 40 attempts/sec.
- Successful LVGL updates: about 17/sec.
- LVGL lock failures: about 23/sec.

The lock failures are not the primary root cause by themselves. They are a symptom that the view controller is trying to update while LVGL/display work is already consuming substantial time.

### Code Path

The likely path is:

1. The digital-level controller wakes at the configured update cadence.
2. The controller takes the LVGL lock and updates digital-level state.
3. The type 1 digital-level view invalidates the full digital-level object.
4. The invalidated object covers the display-sized drawing area.
5. LVGL renders, software-rotates because rotation is required, and flushes the full 280 x 456 screen.

Files to inspect:

- `main/app_cfg.h`
  - `DIGITAL_LEVEL_VIEW_DISPLAY_UPDATE_PERIOD_MS`
  - `SENSOR_GAME_REPORT_PERIOD_MS`
- `main/digital_level_view_controller.c`
  - Controller loop cadence.
  - LVGL lock/update section.
- `main/digital_level_view_type_1.c`
  - Full object invalidation through `lv_obj_invalidate(digital_level)`.
- `main/lvgl_display.c`
  - Display flush instrumentation and LVGL display configuration.

### Device Verification

The configured UI update period was temporarily changed from 20 ms to 100 ms.

Measured result:

- Full-screen flush count dropped from about 17/sec to about 10/sec.
- Digital-level lock failures dropped from about 23/sec to 0/sec.
- Per-frame render and flush costs remained almost unchanged:
  - Render avg stayed around 50.8 ms.
  - Flush callback avg stayed around 30.1 ms.

This verifies that the 20 ms update cadence is overdriving the display pipeline. It also proves that reducing update rate relieves scheduler and LVGL lock pressure but does not solve the per-frame rendering cost.

### Interpretation

This is the primary user-visible cause of slow response. The firmware is asking for roughly 40 UI updates/sec on a view that effectively causes full-screen redraws, while the measured full-frame pipeline cost supports only about 17 frames/sec in the baseline configuration.

The display is therefore not slow because LVGL randomly misses updates. It is slow because the requested work per second exceeds the measured display path capacity.

Because software rotation is required, this RCA should be attacked by reducing the amount and frequency of redraw work rather than by trying to disable rotation.

### Future Investigation

Recommended next checks:

- Replace full-screen invalidation with smaller invalidated regions where possible.
- Split the digital-level view into LVGL widgets or draw parts that invalidate only changed geometry.
- Limit display updates based on actual value delta rather than a fixed 20 ms cadence.
- Decouple sensor sample/report rate from display redraw rate.
- Compare type 1 and type 2 digital-level rendering paths if both are available.
- Add a counter for invalidated area size before each flush so the firmware can report partial versus full redraw behavior directly.
- Consider a display-rate governor that skips visual updates when a previous full-frame flush is still outstanding.

## RCA 2: QSPI Bus Throughput Matters, But It Is Not The Dominant Bottleneck Alone

### Observation

The SH8601 QSPI configuration uses a 40 MHz pixel clock in the panel bus IO macro.

A full frame is:

- 280 x 456 = 127,680 pixels.
- RGB565 = 2 bytes per pixel.
- Full-frame payload = about 255,360 bytes before command, address, color-swap, queueing, driver, software-rotation, and synchronization overhead.

Because the display path is QSPI, transfer throughput directly affects flush time. However, display-profile results showed that increasing QSPI clock alone did not fix the frame rate while the firmware still performed full-screen redraws at high update pressure.

### Code Path

Files to inspect:

- `managed_components/espressif__esp_lcd_sh8601/include/esp_lcd_sh8601.h`
  - `SH8601_PANEL_IO_QSPI_CONFIG(...)`
  - Pixel clock definition used by the panel IO configuration.
- `main/bsp.c`
  - Panel IO and bus configuration.
- `main/lvgl_display.c`
  - Flush callback and display profiler.

### Device Verification

The QSPI pixel clock was temporarily increased from 40 MHz to 80 MHz.

Measured result:

- Render avg dropped from about 51,017 us to about 47,296 us.
- Flush callback avg dropped from about 30,219 us to about 26,487 us.
- Full-screen flush count did not materially improve while RCA 1 remained active.
- The temporary test did not show immediate serial-captured instability in the selected run, but it was not a complete hardware validation.

### Interpretation

QSPI clock affects throughput and therefore contributes to frame time, but it is not the dominant limiter by itself in the observed baseline. The larger issue is the amount and frequency of full-screen pixel work.

Increasing bus clock improves the transfer portion of the pipeline. It does not remove LVGL rendering cost, required software rotation cost, full-screen invalidation, or update overdrive.

### Future Investigation

Recommended next checks:

- Use a logic analyzer or oscilloscope to confirm actual QSPI clock, command overhead, and data duty cycle.
- Validate 80 MHz over temperature, voltage range, cable/display flex tolerance, and repeated resets.
- Check for display artifacts, tearing, or missed updates at higher clocks.
- Make QSPI pixel clock configurable behind a named project setting if higher values are accepted.
- Re-test QSPI clock only after reducing full-screen invalidation, because reducing pixel work may shift the bottleneck toward the bus.

## RCA 3: QSPI Max Transfer Size Is A Heap-Stability Cliff

This is the item referred to as RCA 4 in the earlier four-item investigation list. The current document has three actionable RCAs because the SH8601 hardware-rotation item was removed after datasheet and driver review.

### Observation

The QSPI transfer size can influence frame rate, but larger transfer sizes caused heap and stability failures on the real device.

Important distinction:

- `LVGL_DISPLAY_TRANS_SIZE` is not the main active transfer splitter for the current LVGL 9 SPI flush path.
- The active transfer-size knob is the SPI bus `max_transfer_sz` supplied through `SH8601_PANEL_BUS_QSPI_CONFIG(...)`.
- This was made explicit through `LCD_QSPI_MAX_TRANSFER_SIZE` in `main/app_cfg.h` and by logging the value in `display_prof_cfg`.

The historically stable value was 4091. Tests at 4096 and above showed memory instability.

### Code Path

Files to inspect:

- `main/app_cfg.h`
  - `LCD_QSPI_MAX_TRANSFER_SIZE`
  - `LVGL_DISPLAY_TRANS_SIZE`
- `main/bsp.c`
  - `SH8601_PANEL_BUS_QSPI_CONFIG(..., LCD_QSPI_MAX_TRANSFER_SIZE)`
- `main/lvgl_display.c`
  - `disp_cfg.trans_size`
  - Display profiler configuration logging.
- `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c`
  - LVGL 9 flush path calls the panel draw API directly.

The deeper allocation path is:

1. `main/bsp.c` passes `LCD_QSPI_MAX_TRANSFER_SIZE` into `SH8601_PANEL_BUS_QSPI_CONFIG(...)`.
2. The SH8601 macro sets the SPI bus `.max_transfer_sz` and the LCD SPI IO `.trans_queue_depth = 10`.
3. ESP-IDF SPI bus setup calls `spicommon_dma_desc_alloc(...)`.
4. `spicommon_dma_desc_alloc(...)` computes descriptor count as `ceil(max_transfer_sz / 4092)` because `DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED` is 4092 bytes.
5. The SPI bus actual max transaction length becomes `descriptor_count * 4092`, not exactly the configured value.
6. `esp_lcd_new_panel_io_spi(...)` stores that actual max transaction length in the LCD SPI IO object.
7. `panel_io_spi_tx_color(...)` chunks a color transfer using this actual max length and can queue up to the IO queue depth.
8. `spi_device_queue_trans(...)` calls `setup_priv_desc(...)`.
9. `setup_priv_desc(...)` allocates a temporary DMA-capable buffer with `heap_caps_aligned_alloc(..., MALLOC_CAP_DMA)` when the transmit pointer is not DMA-capable or not aligned for the SPI DMA engine.

This project currently configures LVGL draw buffers with:

- `buff_spiram = true`
- `buff_dma = false`

That means the large LVGL pixel buffers live in PSRAM and are not intentionally allocated as DMA-capable draw buffers. In the ESP-IDF SPI master path inspected for this build, the DMA-capability check is `esp_ptr_dma_capable(...)`. For this path, non-DMA or unaligned source buffers force internal DMA bounce-buffer allocation during queued transfers.

The DMA descriptor arrays are not the large memory consumer. Descriptor memory scales by a few dozen bytes when crossing 4092-byte boundaries. The large consumer is the temporary internal DMA copy buffer used per queued color transaction.

### Current Memory Layout

Static build-size evidence from `idf.py size`:

- DIRAM used: 236,959 bytes out of 341,760 bytes, about 69.33%.
- DIRAM remaining before runtime allocation: 104,801 bytes.
- IRAM used: 16,384 bytes out of 16,384 bytes, 100%.

Largest static DIRAM contributors from `idf.py size-components`:

- LVGL: about 105,199 bytes DIRAM.
- Wi-Fi/PHY stack examples:
  - `libpp.a`: about 17,103 bytes DIRAM.
  - `libphy.a`: about 8,375 bytes DIRAM.
  - `libnet80211.a`: about 4,748 bytes DIRAM.

Boot-time heap map from serial logs:

- Internal RAM heap region at `3FCC1E38`, length `0x278D8`, about 158 KiB.
- Internal RAM heap region at `3FCE9710`, length `0x5724`, about 21 KiB.
- Internal DRAM heap region at `3FCF0000`, length `0x8000`, about 32 KiB.
- RTC RAM heap region at `600FE030`, length `0x1FB8`, about 7 KiB.
- PSRAM heap pools: about 6513 KiB plus a 48 KiB aligned gap pool.
- ESP-IDF reserves 32 KiB of internal memory for DMA/internal allocations because PSRAM is enabled.

Runtime safe-state heap evidence from the real device at `LCD_QSPI_MAX_TRANSFER_SIZE=4091`:

- Logged actual SPI transaction max: 4092 bytes.
- Internal free heap: about 42,423 bytes.
- Largest internal free block: about 28,672 bytes.
- Minimum internal free watermark in the selected capture: about 37,575 bytes.
- DMA-capable free heap: about 34,683 bytes.
- Largest DMA-capable block: about 28,672 bytes.
- Minimum DMA-capable watermark in the selected capture: about 29,835 bytes.
- SPIRAM free heap: about 5.79 MiB, with largest block about 5.77 MiB.

This shows the shortage is not total memory. The pressure is specifically internal DMA-capable heap and largest contiguous internal DMA-capable block size.

### Transfer-Size Boundary Analysis

ESP-IDF rounds the configured bus max transfer size up to a descriptor multiple of 4092 bytes:

| Configured `LCD_QSPI_MAX_TRANSFER_SIZE` | Descriptor count | Actual SPI max transaction length | Approx queued bounce-buffer exposure at queue depth 10 |
| --- | ---: | ---: | ---: |
| 4091 | 1 | 4092 bytes | about 40 KiB |
| 4096 | 2 | 8184 bytes | about 80 KiB |
| 6144 | 2 | 8184 bytes | about 80 KiB |
| 8192 | 3 | 12276 bytes | about 120 KiB |

The exposure column is an upper-bound model for internal DMA bounce buffers when queued transfers all need temporary DMA-capable copies. The exact value depends on DMA alignment and how many queued transactions are simultaneously resident, but the step changes are real: moving from 4091 to 4096 doubles the actual per-transaction payload window from 4092 to 8184 bytes.

This explains the observed cliff:

- 4091 stays just below the boundary often enough to run.
- 4096 is not a small 5-byte increase at runtime. It crosses the 4092-byte descriptor boundary and doubles the actual LCD SPI chunk size.
- 6144 lands in the same actual chunk class as 4096.
- 8192 crosses the next boundary and is worse again.

### Device Verification

Several transfer sizes were tested on the real device.

#### 8192

Observed failure pattern:

- `lcd_panel.io.spi: panel_io_spi_tx_color(...): spi transmit (queue) color failed`
- `sh8601: panel_sh8601_draw_bitmap(...): send color data failed`
- Wi-Fi static buffer allocation failures.
- `ESP_ERR_NO_MEM`.
- Abort/reset loop.
- No steady display-profile samples.

Interpretation:

- 8192 maps to an actual SPI max transaction length of 12276 bytes. At queue depth 10, that can expose the system to roughly 120 KiB of temporary internal DMA buffer demand in the worst case, before accounting for Wi-Fi and other internal-only allocations. This is larger than the available internal DMA heap observed in the safe configuration.

#### 6144

Observed failure pattern:

- Sometimes produced one display-profile sample.
- Internal free heap was very low, around 11.8 KB in the captured sample.
- Then Wi-Fi allocation failure, for example an allocation around 752 bytes failed.
- Guru Meditation / reset loop followed.

Interpretation:

- 6144 maps to an actual SPI max transaction length of 8184 bytes. It is in the same runtime allocation class as 4096, not halfway between 4096 and 8192 in allocation behavior. It can temporarily improve transfer efficiency, but it consumes enough internal DMA-capable heap to destabilize the system.

#### 4096

Observed failure pattern:

- Sometimes produced one display-profile sample.
- Internal free heap was very low, around 11.7 KB in the captured sample.
- Wi-Fi allocation failures followed.
- Guru Meditation / reset loop followed.

Interpretation:

- 4096 crosses the 4092-byte descriptor boundary and maps to an actual SPI max transaction length of 8184 bytes. That makes it a major memory-behavior change, not a small increment from 4091. It is over the stability boundary for the current firmware and runtime load.

#### 4091

Observed behavior after restoring:

- Stable profile output.
- Logged actual SPI max transaction length was 4092 bytes.
- Internal free heap returned to around 42 KB in the selected capture.
- Largest DMA-capable free block was around 28 KB in the selected capture.
- No selected panic, abort, or `ESP_ERR_NO_MEM` markers in the final capture.

Interpretation:

- 4091 is not arbitrary in practice. It sits just below a memory-allocation cliff for this firmware and device configuration.

#### Reduced Wi-Fi/LWIP/TLS Memory Profile At 4091

A reduced network-memory profile was tested without disabling SoftAP provisioning and without changing task stack sizes.

Changed configuration:

- Wi-Fi static RX buffers: 6 to 4.
- Wi-Fi dynamic RX buffers: 16 to 8.
- Wi-Fi static TX buffers: 6 to 4.
- Wi-Fi cache TX buffers: 16 to 8.
- Wi-Fi RX management buffers: 5 to 3.
- Wi-Fi management short buffers: 32 to 16.
- Wi-Fi AMPDU TX/RX BA windows: 6/8 to 4/4.
- LWIP sockets: 10 to 6.
- LWIP TCP/IP receive mailbox: 32 to 16.
- LWIP max active/listening TCP: 16/16 to 6/4.
- LWIP TCP send/receive windows: 5760/5760 to 2880/2880.
- LWIP TCP accept mailbox: 6 to 2.
- LWIP max UDP PCBs: 16 to 6.
- mbedTLS allocator: internal memory to external SPIRAM.
- mbedTLS dynamic TX/RX buffers: enabled.

Build result:

- Build succeeded with ESP-IDF Kconfig validation.
- Static DIRAM usage stayed at 236,959 bytes out of 341,760 bytes, about 69.33%.
- This is expected because the changed items primarily affect runtime heap allocation, not static image layout.

Real-device result at `LCD_QSPI_MAX_TRANSFER_SIZE=4091`:

- Stable display-profile output.
- No selected allocation failure, panic, abort, or reset markers in the reduced-profile capture.
- Logged actual SPI max transaction length remained 4092 bytes.
- Internal free heap improved from about 42,423 bytes to about 49,063 bytes.
- DMA-capable free heap improved from about 34,683 bytes to about 41,323 bytes.
- Minimum internal heap watermark improved from about 37,575 bytes to about 44,215 bytes.
- Minimum DMA-capable heap watermark improved from about 29,835 bytes to about 36,475 bytes.
- Largest DMA-capable block remained about 28,672 bytes.

Interpretation:

- Reducing Wi-Fi/LWIP/TLS memory pressure helps. The improvement was about 6.6 KiB of internal/DMA-capable heap in the selected steady-state profile.
- This does not yet justify an unmitigated increase to `LCD_QSPI_MAX_TRANSFER_SIZE=4096` because the largest DMA-capable block did not increase, and queue depth remains 10.
- The next transfer-size experiment should still bound LCD SPI queue depth first. Network reductions are useful margin, not a replacement for bounding LCD DMA bounce-buffer demand.

#### Non-Lazy NVS/mDNS SPIRAM Trial At 4091

A second memory trial targeted non-critical runtime services without lazy-starting workflows and without changing the LVGL renderer.

Changed configuration:

- `CONFIG_NVS_ALLOCATE_CACHE_IN_SPIRAM=y`.
- `CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM=y`.
- `CONFIG_MDNS_TASK_CREATE_FROM_INTERNAL` disabled.
- `CONFIG_MDNS_MEMORY_ALLOC_SPIRAM=y` retained.
- `CONFIG_MDNS_ENABLE_CONSOLE_CLI` disabled.

The trial intentionally did not move OTA updater task stack memory to SPIRAM. The OTA updater calls `esp_https_ota_perform()` and eventually writes flash through the OTA path, and ESP-IDF warns that external RAM is inaccessible while flash cache is disabled. Moving that stack to PSRAM would create a plausible cache-disabled failure mode during OTA. OTA task lifecycle also was not lazy-started in this trial because lazy start was rejected as a debugging strategy for this stage.

Additional checkpoints were added around:

- NVS initialization.
- OTA poller/updater task creation.
- mDNS restart/free/init after Wi-Fi got an IP address.

Build and flash result:

- Build succeeded with ESP-IDF v5.5.2 after refreshing the stale CMake build directory that referenced the old `esp-14.2.0_20241119` Xtensa toolchain.
- The image flashed to the configured ESP-IDF serial port on the real ESP32-S3 device.
- The boot log reported ESP32-S3 with 8 MB PSRAM and app version `v0.0.1-95-g097bd26-dirty`.

Real-device checkpoint evidence:

- NVS start: `free_int=197131`, `largest_int=106496`, `free_dma=189391`, `largest_dma=106496`.
- NVS end: `free_int=196347`, `largest_int=106496`, `free_dma=188607`, `largest_dma=106496`.
- OTA poller creation consumed about 4.5 KiB internal/DMA heap.
- OTA updater creation consumed about 4.5 KiB internal/DMA heap.
- Before mDNS init after STA got IP: `free_int=60163`, `largest_int=31744`, `free_dma=52423`, `largest_dma=31744`.
- mDNS logged `mDNS task will be created from SPIRAM`.
- After mDNS init: `free_int=59551`, `largest_int=31744`, `free_dma=51811`, `largest_dma=31744`, `free_spiram=5802056`.

Steady-state display-profile evidence at `qspi_actual_max=4092`:

- Earlier baseline capture: average `free_int` about 42.5 KiB. That baseline profiler did not yet include `free_dma` or `largest_dma`.
- Non-lazy NVS/mDNS trial boot capture: average `free_int` about 63.2 KiB across display-profile samples, with `free_dma` averaging about 55.4 KiB.
- Non-lazy NVS/mDNS trial steady capture: average `free_int` about 64.1 KiB and average `free_dma` about 56.3 KiB.
- Largest internal/DMA-capable block stayed at about 31,744 bytes in the trial captures.
- Frame rate remained about 16-18 fps with full-screen redraws, consistent with the earlier baseline.

Interpretation:

- The trial is useful because it improves total internal/DMA-capable free heap by roughly 20 KiB compared with the earlier baseline capture.
- The trial verifies that mDNS task-stack placement moved to SPIRAM and still allows STA connect, IP acquisition, mDNS init, and OTA manifest polling attempts.
- The trial does not yet justify raising `LCD_QSPI_MAX_TRANSFER_SIZE`. The limiting variable for the QSPI transfer-size cliff is still the largest contiguous DMA-capable block plus simultaneous queued bounce-buffer exposure. That block remained about 31 KiB, while moving from 4091 to 4096 doubles the actual SPI transaction length from 4092 to 8184 bytes.
- The next transfer-size experiment should still first reduce or bound LCD SPI IO queue depth, then retest with Wi-Fi enabled.

### Deeper DMA-Capable Memory Candidate Audit

This pass looked for other non-critical services or code-placement choices that consume internal RAM which could otherwise remain available to the QSPI display path. On ESP32-S3, this matters even when the source of pressure is "code" rather than a conventional heap allocation: static internal DRAM and IRAM/DIRAM placement reduce the internal memory left for `MALLOC_CAP_DMA` allocations.

The current build already uses some important PSRAM-friendly settings:

- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`.
- `CONFIG_SPIRAM_RODATA=y`.
- Project default heap allocation macro is `MALLOC_CAP_SPIRAM`.
- Project static `HEAPS_CAPS_ATTR` objects are placed in external RAM.
- LVGL display buffers are configured with `buff_spiram = true` and `buff_dma = false`.
- The main ESP LVGL port task stack is explicitly created with `task_stack_caps = HEAPS_CAPS_ALLOC_DEFAULT_FLAGS`, so that 8192-byte task stack is already in SPIRAM.
- mDNS heap allocations already use `CONFIG_MDNS_MEMORY_ALLOC_SPIRAM=y`.
- mbedTLS was changed to use external memory and dynamic buffers during the Wi-Fi memory reduction pass.

Current map evidence from the existing built ELF:

- `.iram0.text`: 220,007 bytes.
- `.dram0.data`: 21,692 bytes.
- `.dram0.bss`: 10,616 bytes.
- `.ext_ram.bss`: 14,712 bytes.
- `liblvgl__lvgl.a` contributes about 105,199 bytes of DIRAM, mostly `.text`.
- `libfreertos.a` contributes about 20,899 bytes of DIRAM.
- Wi-Fi/PHY contributors include:
  - `libpp.a`: about 17,103 bytes DIRAM.
  - `libphy.a`: about 8,375 bytes DIRAM.
  - `libnet80211.a`: about 4,748 bytes DIRAM.
  - `libesp_wifi.a`: about 892 bytes DIRAM.
- `libesp_hw_support.a`: about 14,484 bytes DIRAM.
- `libhal.a`: about 16,305 bytes DIRAM.
- `libspi_flash.a`: about 13,040 bytes DIRAM.
- `libesp_driver_spi.a`: about 3,883 bytes DIRAM.
- `libespressif__mdns.a`: about 2,302 bytes DIRAM.

Additional real-device passive capture after the network-memory reduction showed a healthier idle/display-profile state than the earlier baseline:

- `qspi_actual_max=4092`.
- Internal free heap around 88.6 KiB.
- DMA-capable free heap around 80.9 KiB.
- Largest internal/DMA-capable free block around 31,744 bytes.
- Minimum DMA-capable watermark around 41.7 KiB in that capture.

This is not directly comparable to the active full-screen redraw baseline because the capture window had little display work. It is still useful because the largest DMA block remained only about 31 KiB even while total DMA-capable free heap was much higher. Fragmentation and simultaneous DMA allocations remain the constraint for increasing QSPI transaction size.

Candidate findings:

| Candidate | Evidence | Expected DMA/Internal Benefit | Risk / Tradeoff | Current Recommendation |
| --- | --- | --- | --- | --- |
| Disable `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` | LVGL contributes about 105 KiB DIRAM, mostly fast-memory `.text`; Kconfig maps `LV_ATTRIBUTE_FAST_MEM` into IRAM when enabled. | Potentially large static internal-memory recovery if LVGL fast render/blend code moves out of IRAM/DIRAM. | Directly slows LVGL blend/render hot paths. Could reduce frame rate even while freeing QSPI DMA headroom. | Strong candidate for a controlled A/B test, not an assumed fix. Measure heap, render time, flush time, and visual responsiveness. |
| Trim unused LVGL software color-format support | Display path is `LV_COLOR_FORMAT_RGB565`; current config enables RGB565, RGB565 swapped, RGB565A8, RGB888, XRGB8888, ARGB8888, ARGB8888 premultiplied, L8, AL88, A8, and I1. Large `lv_draw_sw_blend_*` functions appear in fast memory. | Could reduce LVGL code footprint, including fast-memory footprint if fast memory remains enabled. | Need verify every UI object, image, QR code, opacity/layer path, and software rotation still renders correctly. | Good targeted follow-up. Keep RGB565 paths. Disable one family at a time and verify all screens on device. |
| Reduce `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT` from 2 to 1 | LVGL creates one `swdraw` thread per draw unit through plain `xTaskCreate`; each uses `CONFIG_LV_DRAW_THREAD_STACK_SIZE=8192`. | Likely saves one internal FreeRTOS task stack, about 8 KiB plus task overhead. | May reduce render parallelism and worsen frame time. | Good memory experiment if QSPI heap is more important than render parallelism. Verify frame rate because it can trade one bottleneck for another. |
| Lazy-start OTA tasks | `create_ota_mode_view()` creates `ota_poller` and `ota_updater` at UI construction. Each stack is 4096 bytes and created through plain `xTaskCreate`. OTA is not normal display operation. | Saves about 8 KiB internal heap at boot if tasks are created only when OTA is requested. | OTA prompt/update flow needs lifecycle cleanup and idempotency. It also changes update-check timing if manifest polling is delayed. | High-value application-level candidate. Prefer lazy creation over stack-size reduction. |
| Lazy-start or SPIRAM-place sensor calibration poller | Sensor calibration tile creates `RVReportPoller` at boot; it blocks until calibration mode is entered. Stack is 4096 bytes and created through plain `xTaskCreate`. | Saves or moves about 4 KiB internal heap. | Calibration is part of normal workflow, so behavior must remain immediate and reliable on tile entry. | Candidate, but lower priority than OTA. Lazy-start on entering calibration is safer than reducing stack size. |
| Move mDNS task stack to SPIRAM | `CONFIG_MDNS_MEMORY_ALLOC_SPIRAM=y` already moves mDNS heap data, but `CONFIG_MDNS_TASK_CREATE_FROM_INTERNAL=y` keeps the 4096-byte mDNS task stack internal. Kconfig supports `CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM` when external task memory is allowed, and this project has `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y`. | About 4 KiB internal heap after Wi-Fi connects. | mDNS task stack in PSRAM can be slower. Need verify mDNS works with Wi-Fi connected and after reconnect. | Good low-risk config experiment if mDNS remains enabled. |
| Reduce mDNS static capacity | `CONFIG_MDNS_MAX_INTERFACES=3`, `CONFIG_MDNS_MAX_SERVICES=10`, `CONFIG_MDNS_ACTION_QUEUE_LEN=16`, and `CONFIG_MDNS_ENABLE_CONSOLE_CLI=y`. Kconfig says lowering interfaces/services reduces memory. | Small static/internal benefit. | SoftAP provisioning and future mDNS service assumptions need review. | Only reduce after deciding exact mDNS product requirement. Disabling console CLI looks safer than changing AP/STA coverage. |
| Enable `CONFIG_NVS_ALLOCATE_CACHE_IN_SPIRAM` | Kconfig says NVS page cache and key hash list can be allocated in PSRAM instead of internal RAM. NVS is used for config load/save, not per-frame rendering. | Runtime internal heap benefit depends on NVS partition/key count. | NVS operations slow down. | Good low-risk config experiment. Verify config save/load and provisioning credentials. |
| Reduce or disable GDB stub task-list support | `CONFIG_ESP_GDBSTUB_SUPPORT_TASKS=y`, `CONFIG_ESP_GDBSTUB_MAX_TASKS=32`. | Small static/internal benefit. | Loses `info threads` visibility in GDB stub. | Candidate for production memory profile, not for active debug builds. |
| Disable `CONFIG_LOG_IN_IRAM` | Current config places log support in IRAM. | Frees some internal code memory. | Logging during flash-cache-disabled windows can become unsafe/unavailable depending call path. | Candidate only after checking logging use in ISR/cache-disabled contexts. Lower priority. |
| Disable Wi-Fi IRAM optimizations | `CONFIG_ESP_WIFI_IRAM_OPT=y` and `CONFIG_ESP_WIFI_RX_IRAM_OPT=y`. User accepts reduced Wi-Fi performance. | Could free internal instruction memory used by Wi-Fi fast paths. | Can reduce Wi-Fi throughput/latency and may affect stability under provisioning/OTA load. | Last-resort or production-profile experiment. Test SoftAP provisioning, STA connect, mDNS, and OTA. |
| TinyUSB runtime allocations | TinyUSB is configured, but `usb_init()` is commented out in `app_main()`. | No current runtime heap to free if USB is not initialized. | None unless USB is re-enabled. | Not a current DMA heap target. |
| SoftAP provisioning scan capacity | `CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES=16` stores provisioning scan results. | Saves memory during provisioning scans if reduced. | SoftAP provisioning is part of normal workflow; fewer visible networks can hurt setup. | Avoid for now per project workflow constraint. |

#### LVGL Memory Candidate Device Trials

The following LVGL-focused trials were run one at a time on the real device with the QSPI transfer size left at the stable setting. The display profiler reported `qspi_actual_max=4092`, full-screen software-rotated refreshes, render timing, flush-callback timing, and heap state. These results test LVGL memory candidates only; they do not yet prove a larger QSPI transfer size is safe.

| Trial | Changed Setting | Avg FPS | Avg Render | Avg Flush Callback | Avg Free DMA | Largest DMA Block | Min DMA Watermark | LVGL Lock Failures | Result |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Baseline | Draw units 2, draw stack 8192, LVGL fast memory in IRAM | 16.98 | 50,919 us | 30,302 us | 54,806 B | 31,744 B | 43,791 B | 23.8/sec | Reference behavior. |
| Renderer units | `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` | 16.98 | 50,930 us | 30,281 us | 63,299 B | 31,744 B | 52,339 B | 33.8/sec | Frees about 8.5 KiB DMA-capable heap, but does not improve refresh rate. Lock failures increased because the producer still overdrives the display path. |
| LVGL IRAM usage | `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n` | 17.20 | 50,996 us | 30,267 us | 159,299 B | 88,064 B | 149,955 B | 24.1/sec | Strongest memory result. It freed about 104 KiB average DMA-capable heap and raised the largest DMA block from 31 KiB to 88 KiB with no measured refresh-rate penalty in this 45-second run. |
| Draw-thread stack | `CONFIG_LV_DRAW_THREAD_STACK_SIZE=4096` | 17.18 | 50,850 us | 30,247 us | 63,288 B | 31,744 B | 51,979 B | 24.8/sec | Frees about 8.5 KiB DMA-capable heap, but does not improve refresh rate. No stack overflow was observed in the short run, but this needs stack-watermark evidence before adoption. |

Conclusions from the LVGL trials:

- The measured frame-rate ceiling stayed around 17 FPS in all LVGL memory trials. This means these LVGL memory knobs are not the primary refresh-rate bottleneck at the current 4092-byte QSPI transaction size.
- The render and flush-callback timings were effectively unchanged across trials. The per-frame cost is still dominated by full-screen software-rotated rendering plus the current QSPI flush path.
- Disabling LVGL fast-memory IRAM use is the only tested LVGL setting that materially changes the DMA allocation landscape. It increases both total DMA-capable free heap and the largest DMA-capable block enough to justify a guarded future QSPI transfer-size trial.
- Reducing renderer units or draw-thread stack size is memory-positive but not refresh-positive. These are secondary memory-profile options, not a display-performance fix.
- The draw-thread stack-size trial is not production-ready evidence by itself. It did not crash during the capture, but no LVGL draw-thread high-watermark was recorded.
- LVGL display buffers are already in SPIRAM with `buff_dma=false`, so they are not the current large internal DMA allocation to free. The likely runtime cost is the SPI driver's internal DMA bounce-buffer behavior when flushing non-DMA-capable PSRAM draw buffers.

#### Combined LVGL Fast-Memory And QSPI Transfer Trial

A follow-up real-device trial combined the strongest LVGL memory candidate with the first meaningful QSPI transfer-size increase:

- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n`.
- `LCD_QSPI_MAX_TRANSFER_SIZE=4096`.
- The SPI driver reported `qspi_actual_max=8184`, confirming that the configured increase crossed the 4092-byte DMA descriptor boundary and doubled the effective maximum transaction length.
- QSPI pixel clock remained 40 MHz.
- LVGL draw units remained 2.
- LVGL draw-thread stack remained 8192 bytes.

Measured result:

| Trial | QSPI Actual Max | Avg FPS | Avg Render | Avg Flush Callback | Max Flush Callback | Avg Free DMA | Largest DMA Block | Min DMA Watermark | LVGL Lock Failures | Stability |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Baseline | 4092 B | 16.98 | 50,919 us | 30,302 us | 38,192 us | 54,806 B | 31,744 B | 43,791 B | 23.8/sec | Stable after normal boot. |
| LVGL fast memory out of IRAM only | 4092 B | 17.20 | 50,996 us | 30,267 us | 35,356 us | 159,299 B | 88,064 B | 149,955 B | 24.1/sec | Stable during 45-second capture. |
| LVGL fast memory out of IRAM + QSPI 4096 | 8184 B | 17.25 | 46,043 us | 25,323 us | 28,853 us | 123,039 B | 40,960 B | 110,383 B | 23.6/sec | Stable during 75-second capture after the usual first BNO085 transient reset. No QSPI, LCD, Wi-Fi allocation, Guru, or NO_MEM failure appeared after the stable boot. |

Interpretation:

- The combined change improved per-frame transfer/render timing but did not materially improve the observed refresh rate. FPS stayed near 17 FPS because the display path is still dominated by full-screen software-rotated redraws and update pressure.
- The QSPI transfer increase reduced average flush-callback time by about 4.9 ms versus the LVGL-fast-memory-only trial, roughly a 16% flush-time reduction.
- Average render timing also dropped by about 5.0 ms, likely because the LVGL refresh cycle spends less time blocked in the flush path.
- DMA-capable free heap remained much healthier than the original baseline, but the largest DMA-capable block dropped from 88 KiB in the LVGL-fast-memory-only trial to 40 KiB with the larger QSPI transaction enabled. This confirms the earlier heap-cliff model: increasing transfer size consumes or fragments internal DMA-capable memory even when the system stays stable.
- This is evidence that `LCD_QSPI_MAX_TRANSFER_SIZE=4096` becomes viable when LVGL fast-memory code is moved out of IRAM, but it is not evidence that larger transfer sizes are safe. The remaining largest DMA block is too small to justify jumping to a larger transfer size without reducing SPI transaction queue depth or adding allocation-failure guards.

#### Investigation Status Matrix

Attempts completed so far:

| Area | Attempt | Device Result | Current Status |
| --- | --- | --- | --- |
| Baseline profiling | Existing 20 ms digital-level update, software rotation, QSPI 40 MHz, transfer 4091 | About 17 FPS, render about 51 ms, flush callback about 30 ms, many LVGL lock failures | Baseline bottleneck reproduced. |
| Update pressure | Digital-level update period changed from 20 ms to 100 ms | Full-screen refresh dropped to 10/sec and lock failures disappeared, but per-frame render/flush cost stayed high | Confirms overdrive/update pressure is real, but not the only cost. |
| Rotation characterization | Forced rotation 0 for measurement only | Render and flush became faster, but project requires rotation and SH8601 does not provide the needed hardware rotation | Removed as a fix candidate. Useful only as cost characterization. |
| QSPI clock | QSPI pixel clock temporarily changed from 40 MHz to 80 MHz | Flush/render improved, but FPS did not materially improve while full-screen redraw pressure remained | Bus speed matters, but is not sufficient alone. |
| QSPI transfer size | 8192, 6144, and unmitigated 4096 | 8192 failed; 6144 unstable; 4096 unstable before freeing LVGL IRAM/DIRAM | Larger transfers hit DMA-capable heap and bounce-buffer limits. |
| Safe QSPI transfer size | 4091 | Stable, actual max transaction 4092 bytes | Baseline safe transfer setting. |
| Wi-Fi/LWIP/TLS memory reduction | Reduced Wi-Fi buffers, moved TLS/external memory use where acceptable | Improved heap but largest DMA block stayed near 31 KiB in the key capture | Committed as non-debug memory relief, but not enough for transfer-size increase by itself. |
| NVS/mDNS SPIRAM | NVS cache in SPIRAM, mDNS task in SPIRAM, mDNS CLI disabled | Improved runtime memory but largest DMA block still stayed near 31 KiB in the key passive capture | Committed as non-critical service memory relief. |
| LVGL draw units | `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` | Freed about 8.5 KiB DMA-capable heap, no FPS gain, lock failures increased | Not a performance fix. Possible memory-profile option only. |
| LVGL fast memory | `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n` | Freed about 104 KiB average DMA-capable heap, largest DMA block rose to 88 KiB, no measured FPS loss at QSPI 4091 | Strongest memory candidate. |
| LVGL draw-thread stack | `CONFIG_LV_DRAW_THREAD_STACK_SIZE=4096` | Freed about 8.5 KiB DMA-capable heap, no FPS gain, no short-run crash | Needs stack high-watermark evidence before adoption. |
| Combined LVGL/QSPI | `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n` plus `LCD_QSPI_MAX_TRANSFER_SIZE=4096` | Stable 75-second capture, actual max transaction 8184 bytes, flush callback improved from about 30.3 ms to 25.3 ms, FPS still about 17.25 | Viable trial setting, but not a frame-rate fix by itself. |

Baseline path handoff: `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y` and `LCD_QSPI_MAX_TRANSFER_SIZE=4091`.

This is the original stable path and should be treated as the reference configuration for future agents. It has LVGL fast-memory code in IRAM/DIRAM and QSPI configured below the 4092-byte DMA descriptor boundary. The device reports `qspi_actual_max=4092` in this path.

What has already been tried on this path:

| Approach | Result | Do Not Repeat Unless |
| --- | --- | --- |
| Baseline active display profiling | Reproduced about 17 FPS, render about 51 ms, flush callback about 30 ms, successful UI updates about 17/sec, and LVGL lock failures about 24/sec. | Needed only as a regression comparison after major code changes. |
| Slowing digital-level update period to 100 ms | Lock failures dropped to zero and refreshes dropped to 10/sec, but full-screen per-frame render/flush cost stayed high. | Rechecking whether controller overdrive returned after view changes. |
| Forcing rotation 0 | Render and flush became faster, proving software rotation has a measurable cost. Not acceptable as a fix because project rotation is required and SH8601 does not support the needed hardware rotation. | Only for cost characterization on a new display driver. |
| Raising QSPI pclk to 80 MHz | Improved render/flush timing, but did not materially improve FPS while full-screen redraw pressure remained. | Retest after partial invalidation reduces per-frame pixel work. |
| Raising QSPI max transfer to 4096, 6144, or 8192 while keeping LVGL fast memory in IRAM | 4096 and 6144 were unstable; 8192 failed with transmit/memory failures. | Only after reducing queue depth or proving a no-bounce-buffer DMA path. Do not repeat unmitigated. |
| Reducing Wi-Fi/LWIP/TLS memory pressure | Improved total heap but did not raise the largest DMA-capable block enough to justify larger QSPI transfer size. | Useful as supporting margin, not a standalone display fix. |
| Moving NVS/mDNS non-critical memory to SPIRAM | Verified STA/IP/mDNS/OTA-poll path still worked and improved runtime memory, but largest DMA block stayed near 31 KiB in the key capture. | Already committed. Repeat only for regression verification. |
| Reducing LVGL draw units to 1 | Freed about 8.5 KiB DMA-capable heap, no FPS gain, lock failures increased. | Only if a future memory profile needs about 8 KiB and accepts the scheduler tradeoff. |
| Reducing LVGL draw-thread stack to 4096 | Freed about 8.5 KiB DMA-capable heap, no FPS gain, no short-run crash. | Only with stack high-watermark instrumentation. |

What has not yet been tried while preserving this baseline path:

| Untested Approach | Why It Is Still Open | Caution |
| --- | --- | --- |
| Reduce LCD SPI transaction queue depth while keeping `IRAM=y` and `LCD_QSPI_MAX_TRANSFER_SIZE=4091` | May reduce simultaneous DMA bounce-buffer exposure without changing the stable transaction-size class. | Could reduce throughput if queue depth becomes too shallow. Measure flush time and wait time. |
| Reduce LCD SPI queue depth, then retry `LCD_QSPI_MAX_TRANSFER_SIZE=4096` with `IRAM=y` | This is the smallest mitigated test of whether queue depth, not only LVGL IRAM placement, controls the 4096 cliff. | Must be guarded by DMA free/largest/min logging and failure-marker capture. |
| DMA-capable or direct-PSRAM-DMA flush-buffer path while keeping `IRAM=y` | If the SPI LCD path can avoid internal bounce buffers, the baseline IRAM setting may survive larger transactions. | Must prove source buffers are accepted by `esp_ptr_dma_capable(...)` or equivalent for this IDF/S3 path. |
| Partial invalidation of the digital-level view | The strongest likely FPS fix because it reduces full-screen software-rotated pixel work, independent of memory placement. | Requires view/renderer redesign and visual correctness checks under required rotation. |
| LVGL color-format pruning while keeping fast memory in IRAM | Could reduce LVGL IRAM/DIRAM footprint without fully disabling fast memory. | Must verify every screen, image, opacity/layer path, and rotation mode. |
| Disable log-in-IRAM or Wi-Fi IRAM optimizations | Could free internal memory while preserving LVGL fast memory in IRAM. | Needs cache-disabled logging audit and Wi-Fi/provisioning/OTA validation. |
| Longer product workflow soak on the stable baseline | The current captures are targeted display/memory tests, not full end-to-end product validation. | Include provisioning, reconnect, OTA polling, low-power transitions, and screen navigation. |

Not tried yet:

| Area | Untested Work | Why It Matters | Required Evidence |
| --- | --- | --- | --- |
| LCD SPI queue depth | Reduce SH8601/LCD SPI transaction queue depth before trying larger transfer sizes | Current queue depth 10 makes bounce-buffer exposure large. Lower queue depth may allow 8192-class transfers with less simultaneous DMA pressure. | Device capture with qspi actual max, FPS, flush time, free/largest/min DMA, and no transmit/allocation failures. |
| QSPI 8192 with mitigations | Test 8192 only after queue-depth reduction or DMA-buffer changes | 8192 maps to actual max 12276 bytes and about 120 KiB worst-case queued bounce-buffer exposure at depth 10. | Must be tested incrementally. Do not run unmitigated with IRAM enabled based on current data. |
| DMA-capable LVGL flush buffers | Prove whether this IDF/panel path can avoid internal bounce buffers by using DMA-capable draw/flush buffers | Current `buff_spiram=true`, `buff_dma=false` likely forces internal DMA bounce buffers during SPI color transfers. | Allocation success, heap impact, PSRAM/internal tradeoff, visual correctness, and SPI path confirmation. |
| PSRAM DMA path | Verify whether ESP32-S3 + this IDF SPI LCD path can DMA directly from PSRAM for the display buffers | If direct PSRAM DMA works, it may remove the main internal bounce-buffer pressure. If not, the current heap model remains. | Source-level proof plus device capture showing reduced internal DMA churn. |
| Partial invalidation | Redesign digital-level view to invalidate only changed geometry instead of full screen | Current FPS is capped because every accepted update still forces full-screen software-rotated redraw. This is the strongest likely frame-rate fix. | Lower invalidated pixels, lower render/flush time, higher successful UI updates, fewer lock failures. |
| Renderer/data model redesign | Move digital-level rendering away from heavyweight LVGL object invalidation where appropriate | Could reduce per-frame LVGL render cost and lock contention. | Measured render time and visual correctness under rotation. |
| LVGL color-format pruning | Disable unused LVGL software blend/color-format families one family at a time | May reduce LVGL fast-memory/code footprint while keeping IRAM enabled or reducing flash/cache pressure. | All screens, images, QR/opacity/layer paths, and rotation must still render correctly. |
| Stack watermarking | Measure LVGL draw-thread stack high-watermark before accepting smaller stack | The 4096 stack trial did not crash, but absence of crash is not safety evidence. | High-watermark logs under worst UI screens and rotation. |
| OTA lazy-start or SPIRAM placement | Defer or move OTA poller/updater task stacks | OTA is non-critical during normal display use and consumes internal stack memory. User previously rejected lazy-start as a general strategy, so this remains a design tradeoff rather than an active plan. | OTA workflow validation plus boot/display heap improvement. |
| Sensor calibration poller placement | Move calibration poller stack to SPIRAM or start only when entering calibration | Saves about one 4 KiB task stack, but calibration is normal workflow. | Calibration responsiveness and heap improvement. |
| Disable log-in-IRAM | `CONFIG_LOG_IN_IRAM=n` | Could free internal instruction memory. | Audit no logging is needed in cache-disabled/ISR paths. |
| Wi-Fi IRAM optimization reduction | Disable Wi-Fi IRAM optimization settings | User accepts reduced Wi-Fi performance; may free internal code memory. | STA connect, SoftAP provisioning, mDNS, OTA, and display heap capture. |
| GDB stub task-list reduction | Reduce/disable task-list support in production profile | Small internal-memory saving. | Confirm debugging impact is acceptable. |
| QSPI clock plus transfer-size combined | Try 80 MHz QSPI together with the stable memory/transfer profile | Clock and transfer size both reduce flush overhead, but previous clock-only test did not lift FPS under full-screen pressure. | Must be tested after reducing invalidation or it may again show little FPS improvement. |
| Longer soak and workflow coverage | Run combined 4096 profile across provisioning, Wi-Fi reconnect, OTA poll, screen changes, and low-power transitions | Current combined test is a 75-second capture on the active display path, not full product validation. | No allocation failures, resets, visual corruption, or workflow regressions. |

After the controlled LVGL trials, the strongest follow-up candidates are:

1. `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n`, followed by a guarded QSPI transfer-size test. This is now backed by real-device heap evidence, not just map-file evidence.
2. Lazy-start OTA tasks because it should free boot-time internal task stacks without affecting normal display operation.
3. Move mDNS task creation to SPIRAM and enable NVS cache in SPIRAM because these are direct "move non-frame-critical service memory out of internal RAM" changes.
4. Reduce LVGL draw unit count from 2 to 1 only if about 8 KiB additional DMA-capable heap is worth the possible scheduler/render tradeoff.

Any accepted change must be verified on the device with Wi-Fi enabled. A candidate is not considered proven unless the capture reports all of:

- `free_dma`
- `largest_dma`
- `min_dma`
- `free_int`
- `largest_int`
- QSPI actual max transfer size
- render/flush timing
- no Wi-Fi allocation failure, LCD queue failure, abort, or reset markers

The largest DMA block must be treated as first-class evidence. A change that improves total free DMA heap but leaves the largest block unchanged may still fail when QSPI transfer size crosses the 4092-byte descriptor boundary.

### Interpretation

Transfer size is a real performance knob, but it is bounded by internal heap availability and driver allocation behavior.

The most important finding is that larger transfers sometimes reduce the measured flush callback duration when they survive briefly, but they destabilize the rest of the system. Therefore, transfer-size changes must be treated as memory-architecture changes, not just display-performance tuning.

This RCA also explains why increasing transfer size can look attractive in short tests but fail in integrated firmware: Wi-Fi, LVGL, SPI, display buffers, task stacks, and driver queues all compete for internal heap.

The current evidence supports this root cause:

- The real device reports actual transaction length 4092 at the stable 4091 setting.
- ESP-IDF source shows 4096 and 6144 both round to actual length 8184.
- ESP-IDF source shows SPI master allocates temporary DMA buffers when queued transaction buffers are not DMA-capable or aligned.
- The LVGL buffers are configured for SPIRAM and not DMA.
- The SH8601 LCD SPI IO queue depth is 10.
- Failure logs show LCD SPI queue failure and Wi-Fi allocation failure in the same boot window.
- The safe state has only about 34 KB free DMA-capable heap and about 28 KB largest DMA-capable block, which is not enough for unbounded larger queued bounce buffers plus Wi-Fi pressure.

Based on this evidence, the transfer size should not be increased by itself.

A controlled transfer-size increase must include one or more memory controls:

- Reduce LCD SPI IO transaction queue depth before increasing `LCD_QSPI_MAX_TRANSFER_SIZE`.
- Prove whether the active SPI path can directly DMA from PSRAM for this panel and IDF version before relying on PSRAM DMA-capable LVGL buffers.
- Add boot/runtime guardrails that log actual SPI max transaction size, queue depth, DMA free heap, largest DMA block, and minimum DMA watermark.
- Keep Wi-Fi enabled during verification because Wi-Fi allocation pressure is part of the real failure mode.

### Future Investigation

Recommended next checks:

- Run heap tracing around display initialization and first flush if more exact allocation attribution is needed.
- Add minimum internal heap watermark reporting before and after:
  - Wi-Fi startup.
  - LVGL buffer allocation.
  - Display bus initialization.
  - First full-screen flush.
- Test larger transfer sizes with Wi-Fi disabled only as an isolation experiment, not as acceptance evidence.
- Reduce Wi-Fi static buffer counts if the product permits it.
- Audit task stack sizes and internal-only allocations.
- Confirm which allocations require internal/DMA-capable memory and which can move to SPIRAM.
- Evaluate whether `buff_dma`, PSRAM DMA, or bounce-buffer strategies are supported by the target and ESP-IDF version before enabling them.
- Add boot-time guardrails that reject unsafe transfer-size configurations when internal heap is below a measured threshold.
- Keep `LCD_QSPI_MAX_TRANSFER_SIZE` as an explicit project macro so future experiments are visible in logs and diffs.
- If testing 4096 again, do not test it unmitigated. The first safer experiment is to reduce the LCD SPI IO queue depth enough that `actual_max_transfer * queue_depth` fits within the measured DMA heap budget with margin.

## Combined Root-Cause View

The three actionable RCAs are connected:

1. Full-screen invalidation at a 20 ms display-update cadence creates too much work per second.
2. QSPI throughput contributes to transfer time, but bus speed alone cannot overcome the full-screen redraw workload.
3. Increasing QSPI max transfer size can reduce transfer overhead, but the current system is near an internal-heap stability boundary, so larger values cause allocation failures and resets.

Required software rotation is a fixed cost layered on top of all three RCAs. It should be included in performance budgets, but it is not a fix target unless the display controller or hardware architecture changes.

The strongest immediate RCA is the full-screen invalidation/update-pressure issue. The safest performance path is to reduce invalidated pixels and display update rate first, then revisit QSPI clock and transfer size.

## Recommended Investigation Order

1. Keep the safe transfer size at 4091 while debugging other causes.
2. Keep software rotation enabled because it is required for correct display orientation.
3. Reduce or gate digital-level invalidation so the display no longer redraws the full frame for every small value change.
4. Re-test with the 20 ms controller period after partial invalidation is implemented.
5. Re-test QSPI pclk after rendering and invalidation are under control.
6. Only then revisit larger QSPI transfer sizes with heap tracing and Wi-Fi memory pressure measured.

## Current Safe State From Investigation

The final safe device state restored:

- `LCD_QSPI_MAX_TRANSFER_SIZE=4091`.
- QSPI pclk back to the original project value.
- Default display rotation restored.
- Digital-level update period restored to the original value.
- Profiling left available through project macros.

The final selected capture showed stable display profiling and no selected panic or abort markers.

## Capture Files From Investigation

Generated serial captures were placed under `build/` during the investigation:

- `build/display_profile_capture_baseline.log`
- `build/display_profile_capture_update_100ms.log`
- `build/display_profile_capture_rotation0.log`
- `build/display_profile_capture_qspi_80mhz.log`
- `build/display_profile_capture_qspi_xfer_8192.log`
- `build/display_profile_capture_qspi_xfer_6144.log`
- `build/display_profile_capture_qspi_xfer_4096.log`
- `build/display_profile_capture_final_xfer_restored.log`

These captures are generated artifacts, not source-of-truth configuration. Prefer repeating the profiling run on the active hardware when investigating a new build.

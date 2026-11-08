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

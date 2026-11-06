# ADR-003 progress

What was built for [ADR-003](README.md) and what it showed. Times are NZDT.

## Step 1: build-only trial on v6.1 (2026-11-06)

### Install, the same way as v5.5.2

v5.5.2 was set up by the VS Code ESP-IDF extension: framework clone in `C:\workspace\esp\frameworks\esp-idf-v5.5.2`, tools shared in `C:\workspace\esp\tools` (`tools\<name>\<version>`, Python environments in `python_env\`), installs listed in `C:\workspace\esp\tools\esp_idf.json`. v6.1 went in the same way so other projects can share it (owner, 2026-11-06):

- `git clone --branch v6.1 --depth 1 --recursive --shallow-submodules` into `C:\workspace\esp\frameworks\esp-idf-v6.1` (v6.1 is still the latest stable release; the newer v6.0.3 and v5.5.6 are older lines).
- `install.ps1 esp32s3` with `IDF_TOOLS_PATH=C:\workspace\esp\tools` and the shared Python 3.11.2: new toolchains `xtensa-esp-elf` / `riscv32-esp-elf` 15.2.0_20251204 and gdb 17.1 beside the v5.5 ones, Python environment `idf6.1_py3.11_env` beside `idf5.5_py3.11_env`. Nothing of v5.5.2 changed.
- Entry `esp-idf-c5a7d4903b4a98082b365df4692d3c1e` (md5 of `c:/workspace/esp/frameworks/esp-idf-v6.1`, the same scheme as the 5.5.2 entry) added to `esp_idf.json`; `idfSelectedId` and the global VS Code settings stay on 5.5.2, so no other project moves.
- Shell use, as for v5.5.2: set `IDF_TOOLS_PATH=c:\workspace\esp\tools`, `IDF_PYTHON_ENV_PATH=c:\workspace\esp\tools\python_env\idf6.1_py3.11_env` and put `tools\idf-python\3.11.2` first on `PATH`, then dot-source `export.ps1`.
- The PC blue-screened during the first tools install (20:29). The two tool folders unpacked in that run and the partial download were deleted and the install run again; the installer checks each cached download's size and SHA-256 before using it. The v6.1 clone and the firmware repo were checked afterwards (no changed files, `git fsck` clean).

### Trial build

Trial branch `idf61_trial` in its own worktree `C:\workspace\OWMC-idf61-trial`, so this checkout and its `managed_components/` stay untouched. Record commit `9978ecf` (not for merge).

Found on the way, outside the upgrade: `components/XPowersLib` (`830eb6b`) and `components/bno08x/sh2` (`b514b1e`) are tracked as bare commit links with no `.gitmodules`, so a fresh clone or worktree gets two empty folders and cannot build. Only this checkout has them, as nested clones. For the trial they were cloned from this checkout at those commits.

What v6.1 needed, in the order the build hit it:

1. `wifi_provisioning` is gone; `espressif/network_provisioning` 1.4.0 replaces it. One-to-one renames in `main/wifi.c` and `main/wifi_provision.c` (`wifi_prov_mgr_*` → `network_prov_mgr_*`, `is_provisioned` → `is_wifi_provisioned`, `reset_provisioning` → `reset_wifi_provisioning`, `WIFI_PROV_*` events → `NETWORK_PROV_*` / `NETWORK_PROV_WIFI_CRED_*`, `.wifi_prov_conn_cfg` → `.network_prov_wifi_conn_cfg`).
2. v6.1 turns protocomm security 1 off by default (v5.5.2 had 0, 1 and 2 on). The firmware and its QR code use security 1 with a PoP, so `CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1=y` keeps the behaviour. Moving to security 2 would change the provisioning protocol and belongs in a functional ADR.
3. `esp_tinyusb` removed (manifest, `main/CMakeLists.txt`, `main/usb.c`, `main/usb.h`, the include and the commented-out `usb_init()` call in `main/app.c`); `espressif/tinyusb` goes with it.
4. The `driver` component no longer brings the drivers: `main` and `components/bno08x`, `esp_lcd_jd9853`, `esp_lcd_touch_axs5106`, `esp_lcd_touch_ft3168` now list `esp_driver_gpio` / `_i2c` / `_ledc` / `_spi` as they use them.
5. `components/esp_lcd_jd9853`: `rgb_endian` / `LCD_RGB_ENDIAN_*` renamed to `rgb_ele_order` / `LCD_RGB_ELEMENT_ORDER_*`.
6. `esp_http_client` has two new events (`HTTP_EVENT_ON_STATUS_CODE`, `HTTP_EVENT_ON_HEADERS_COMPLETE`); the switches in `main/ota_mode.c` and `main/opentrickler_remote_controller_view.c` fail `-Werror=switch` without them. Added as cases that do nothing.
7. `components/esp_driver_usb_serial_jtag`: the v5.5.2 copy no longer compiles on v6.1 (`esp_vfs_usb_serial_jtag.h` gone). v6.1 still refills the no-SOF tolerance only on reconnect, the bug the copy exists for, so the v6.1 component was copied in again (without `test_apps`) and the same 5-line change re-applied.

Then it builds: `idf.py build` PASS.

Other managed components resolve unchanged against v6.1: `esp_lvgl_port` 2.7.2, `json_parser` 1.0.3 (`jsmn` 1.1.0), `mdns` 1.15.1, `esp_lcd_sh8601` 2.0.1, `esp_lcd_touch` 1.2.1, `lvgl` 9.6.0, `cmake_utilities` 0.5.3. Newer major versions (`esp_lvgl_port` 2.9, `json_parser` 2.0) are not needed for v6.1 and are left out: updating them is not required by the move.

### Configuration

`sdkconfig.defaults.esp32s3` is a full v5.5 dump (1,594 settings). Compared setting by setting with what v5.5.2 and v6.1 each produce from it:

- 80 settings that v5.5.2 applied are unknown to v6.1: the removed TinyUSB options (11), driver placement and debug options for peripherals the firmware does not use (GPTimer, MCPWM, RMT, PCNT, SDM, TWAI, temperature sensor; about 35), the legacy-driver deprecation switches (16), mbedTLS options for ciphers and key exchanges (`KEY_EXCHANGE_ECDH_*`, `SECP192/224`, Blowfish, XTEA, Poly1305, HKDF; 11), core dump `DATA_FORMAT_ELF` and `CHECKSUM_CRC32` (ELF is the only format in v6), `FREERTOS_SMP=n`, a few FreeRTOS placement options, `SPI_FLASH_ROM_DRIVER_PATCH`, `RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=n`.
- Two values spelled differently: `ESPTOOLPY_BEFORE` / `AFTER` (`default-reset`, `hard-reset`, esptool v5 names).
- Unchanged where it matters: PSRAM, power management, task watchdog panic, core dump to flash with boot check, 1 kHz tick, mbedTLS external memory and dynamic buffers, HTTP OTA, custom partition table, 16 MB DIO 80 MHz flash, USB Serial/JTAG console, LVGL settings, `-O2`. Flash layout identical (app at `0x320000`).

### Size

| | v5.5.2 | v6.1 trial |
|---|---:|---:|
| DIRAM used (static) | 141,719 B | 148,346 B (+6,627) |
| Flash code | 1,223,764 B | 1,287,164 B |
| Image | 1,921,823 B | 2,006,870 B (fits the 4 MB OTA slots) |

Per library, most moves are the v6 reorganisation (`hal` split into `esp_hal_*`, `newlib` → `esp_libc`), which nets out. The real growth is the SPI flash driver in IRAM (+6.6 KB) and `esp_hw_support` (+1.8 KB), against `esp_ringbuf` (−1.5 KB). The flash driver options are the same on both versions (`SPI_FLASH_PLACE_FUNCTIONS_IN_IRAM=y` as before), so this is v6.1's own code. Expected at runtime: about 6.5 KB less free internal heap than today (about 192 KB free now).

### Warnings

94 warnings in project code, all of kinds v5.5.2 already shows: LVGL 9.6 deprecations (77) and unused variables or functions in existing code. LVGL's `LV_ASSERT_HANDLER_INCLUDE is deprecated` still prints once per file. GCC 15 adds no new kinds.

## Step 2: the change (2026-11-06, `80de986`)

Owner approved the whole list, items 8 and 9 included ("Approve all").

- Items 2-7 are the trial edits (`9978ecf`) applied to `esp_idf_upgrade`; item 7's README now describes the v6.1 copy.
- Item 1: this project's `.vscode/settings.json` (untracked, local) points at v6.1 and the 15.2.0 toolchain; global settings unchanged.
- Item 8: `.gitmodules` registers `components/XPowersLib` (`lewisxhe/XPowersLib`) and `components/bno08x/sh2` (`ceva-dsp/sh2`); both tracked commits exist upstream. A fresh `git clone --recurse-submodules` of the branch gets both libraries at the tracked commits.
- Item 9: `sdkconfig.defaults.esp32s3` is now the v6.1 `save-defconfig` set (86 settings, from 2,326 lines); `sdkconfig.defaults` stays empty. Acceptance check: the `sdkconfig` generated from the old file and from the new one are identical in all 2,076 settings, and v6.1 reports no unknown options.
- Build in this checkout: PASS, same size as the trial (DIRAM 148,346 B, image 2,006,870 B).

## Step 3: on the device (2026-11-06)

Owner, 2026-11-06: no unit has ever been deployed in the field, so an OTA from an old bootloader does not need testing. The ADR's open question on OTA and partition compatibility for field units is therefore closed. (One run of that path was made before the owner said so: the v6.1 app flashed alone over the v5.5.2 bootloader booted and passed `state_change`.)

- Full flash (v6.1 bootloader, partition table, app). Boot, from a `reboot`: `ESP-IDF v6.1 2nd stage bootloader`, app `ESP-IDF: v6.1`, core dump to flash initialised and the stored dump found, `CrashReport` reset reason `SW`. Wi-Fi connects with the saved credentials; saved settings (rotation 180°, Wi-Fi on) kept.
- Boot errors and warnings against v5.5.2's boot log: one new line, `W gpio: conflict found for GPIO[0]`, when the external button (GPIO0) is configured. No project code configures GPIO0 elsewhere, and SPI ignores the unset `data4`-`7` fields (all 0 counts as unused). v6.1's GPIO driver only warns and still configures the pin. To check: the button works (owner).
- OTA polling logs `couldn't get hostname` for both sources, the same as on v5.5.2. Not caused by the upgrade: the second source in `main/ota_mode.c` is `"http://owmc_update.vfnz.pro"`, passed as a host name. A fix is functional and belongs in its own change.
- Task watchdog and crash evidence (`scenario watchdog`, after `wake`): PASS. The watchdog names `lvgl`, the core dump is saved (42,272 B; the dump routine had 436 B of its stack left), the reboot reports `TASK_WDT` and the stored dump's backtrace. The first try ran with the unit idle, where the LVGL watchdog is paused by design, so `hang` only locked the unit until a `reboot`. The scenario does not wake the unit first; that is a gap in the test, not in v6.1.
- Memory, Wi-Fi on, steady state: `free_int` about 186,900 B (v5.5.2 about 191,000 B), `largest_dma` 106,496-110,592 B (v5.5.2 114,688 B), as expected from the 6.6 KB more static DIRAM. `min_int` reads about 150,400 B against about 187,000 B before. Before any project code runs, v6.1's own start-up borrows about 32 KB of internal RAM and gives it back (`memopt: nvs_init begin` at 3 ms shows `free_int` 282,287 but `min_int` 250,256; on v5.5.2 the two were equal). The dip is the same with a 17.7 KB and with a 42 KB stored core dump, so the dump check is not the cause. Cause not found yet. It is a one-off at boot, but `min_int` no longer shows runtime pressure.
- `scenario state_change --min-largest-dma 80000` (v6.1 app on the old bootloader, same app as the full flash): PASS, `largest_dma` 106,496 B throughout.

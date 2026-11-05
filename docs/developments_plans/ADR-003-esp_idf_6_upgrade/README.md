# adr-003: move to the latest ESP-IDF release and drop esp_tinyusb

Status: **Draft 2026-11-05.** Starts after ADR-002 step 1 is Built. Not executable yet: the executable plan is written once ADR-002 step 1 is done, from a build-first trial on a throwaway branch.

## Owner's direction (2026-11-05)

1. Skip ESP-IDF v5.5.6 and go straight to the latest stable release (v6.1 on 2026-11-05) to avoid a second migration.
2. Remove `esp_tinyusb` (USB is not initialised: `usb_init()` is commented out in `app_main`).
3. Do this after ADR-002 step 1, not mixed with it.

## Context (checked 2026-11-05)

Current: ESP-IDF v5.5.2. Releases available: v5.5.6 (latest on the 5.5 line), v6.0.3, v6.1 (latest stable; v6.1 tags `v6.1-beta1`, `v6.1-rc1`, `v6.1`).

Managed components against the registry on 2026-11-05:

| Component | Locked | Latest | Note |
|---|---|---|---|
| `espressif/esp_lvgl_port` | 2.7.2 | 2.9.0 | Manifest pins `~2.7.1`; after ADR-002 step 1 only the task, lock, touch and stop/resume are used. |
| `espressif/esp_tinyusb` | 2.4.0 | 3.0.0 | To be removed (owner item 2), with `main/usb.c` and the `CONFIG_TINYUSB` line that Kconfig reports as unknown. |
| `espressif/json_parser` (+ `jsmn`) | 1.0.3 (1.1.0) | 2.0.0 (1.2.0) | Major; used by `main/ota_mode.c` and `main/opentrickler_remote_controller_view.c`. |
| `espressif/cmake_utilities` | 0.5.3 | 1.3.0 | Transitive. |
| `lvgl/lvgl`, `espressif/mdns`, `espressif/esp_lcd_sh8601`, `espressif/esp_lcd_touch`, `espressif/tinyusb` | current | same | Nothing to do. |

Known v6.x migration items for this codebase (from the v6.1 tree and the v6.0 migration guide):

- `wifi_provisioning` is removed from ESP-IDF; it is now the managed component `espressif/network_provisioning` with renamed APIs. Affects `main/wifi.c`, `main/wifi_config.c`, `main/CMakeLists.txt`, `main/idf_component.yml`. SoftAP provisioning is part of normal use and must be verified on the device.
- The legacy `driver` component is deprecated and no longer re-exports the `esp_driver_*` components; `main/CMakeLists.txt` lists `driver` and must list the drivers it uses instead.
- Public driver headers no longer include FreeRTOS headers; files relying on that must include them.
- No blocker found in I2C: `components/XPowersLib` already uses the new driver (`CONFIG_XPOWERS_ESP_IDF_NEW_API=y`) and `main/` includes no legacy driver header. The legacy `driver/i2c.h` still exists in v6.1.
- mbedTLS memory options used by ADR-001 (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`, `CONFIG_MBEDTLS_DYNAMIC_BUFFER`) still exist in v6.1.
- Not yet checked: every managed component's declared IDF range, `sdkconfig.defaults.esp32s3` option renames (the file is a full v5.5 dump), the OTA image format and partition compatibility for units in the field, and the core dump and heap behaviour ADR-001 relies on.

## Plan outline (to be made executable after ADR-002 step 1)

1. Throwaway trial branch: install v6.1 beside v5.5.2, build, list every error and Kconfig warning. No device time.
2. Write the executable plan from that list: dependency changes, provisioning migration, config renames, removal of `esp_tinyusb`.
3. Execute with the ADR-001 safety net in place; verify on the device including SoftAP provisioning, Wi-Fi, OTA polling, every tile, rotations, and an overnight run.

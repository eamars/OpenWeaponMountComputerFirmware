# ADR-003: move to the latest ESP-IDF release and drop esp_tinyusb

Status: **Open.** Started 2026-11-06 after ADR-002 was Built (owner: "Merge and start ADR-003"). Step 1, the build-only trial, in progress.

ADR-002 is functional change without any dependency update (ESP-IDF v5.5.2 and today's managed components stay as they are). ADR-003 is the dependency update without functional change. The two never cross over: one is finished before the other starts (owner, 2026-11-06). So the firmware behaves the same before and after this ADR; anything that would change behaviour is a separate ADR.

## What we want

One migration straight to the latest stable ESP-IDF (v6.1 as of 2026-11-05), so the firmware does not go through a second one, and the unused `esp_tinyusb` gone.

## Owner's direction

1. Skip v5.5.6; go straight to the latest stable release.
2. Remove `esp_tinyusb`: USB is not initialised (`usb_init()` is commented out in `app_main`).
3. Do it after ADR-002, not mixed with it.

## What we know (checked 2026-11-05 unless dated)

Current: ESP-IDF v5.5.2. Releases then available: v5.5.6, v6.0.3, v6.1.

Managed components against the registry:

| Component | Locked | Latest | Note |
|---|---|---|---|
| `espressif/esp_lvgl_port` | 2.7.2 | 2.9.0 | Manifest pins `~2.7.1`; only the task, lock, touch and stop/resume are used now (the project owns the display). |
| `espressif/esp_tinyusb` | 2.4.0 | 3.0.0 | To be removed, with `main/usb.c` and the `CONFIG_TINYUSB` line Kconfig reports as unknown. |
| `espressif/json_parser` (+ `jsmn`) | 1.0.3 (1.1.0) | 2.0.0 (1.2.0) | Major version; used by `main/ota_mode.c` and `main/opentrickler_remote_controller_view.c`. |
| `espressif/cmake_utilities` | 0.5.3 | 1.3.0 | Transitive. |
| `lvgl/lvgl`, `espressif/mdns`, `espressif/esp_lcd_sh8601`, `espressif/esp_lcd_touch`, `espressif/tinyusb` | current | same | Nothing to do. |

Known v6.x changes that hit this code:

- `wifi_provisioning` left ESP-IDF; it is now the managed component `espressif/network_provisioning`, with renamed APIs. Touches `main/wifi.c`, `main/wifi_config.c`, `main/CMakeLists.txt`, `main/idf_component.yml`. SoftAP provisioning is part of normal use.
- The legacy `driver` component no longer re-exports the `esp_driver_*` components; `main/CMakeLists.txt` must list the drivers it uses.
- Public driver headers no longer pull in FreeRTOS headers; files that relied on that must include them.
- I2C is not a blocker: `components/XPowersLib` already uses the new driver (`CONFIG_XPOWERS_ESP_IDF_NEW_API=y`), and `main/` includes no legacy driver header.
- The mbedTLS external-memory options the firmware uses (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`, `CONFIG_MBEDTLS_DYNAMIC_BUFFER`) still exist in v6.1.
- `components/esp_driver_usb_serial_jtag` is a patched copy of the v5.5.2 component (the connection monitor never refilled its missed-SOF tolerance and dropped console output; 2026-11-06). On v6.1 either re-apply the patch to the new copy or drop it if upstream fixed it. Its README says how.
- LVGL 9.6 warns `LV_ASSERT_HANDLER_INCLUDE is deprecated` in every file; clean up with the upgrade.

Not yet checked: every managed component's declared IDF range; option renames in `sdkconfig.defaults.esp32s3` (a full v5.5 dump); the OTA image format and partition compatibility for units in the field; the core dump, heap and task-watchdog behaviour the firmware relies on (core dump to flash at `0x12000`, failed-allocation log, LVGL lock-up watchdog).

## Approach

1. Install v6.1 beside v5.5.2 and build the firmware on a throwaway branch in its own worktree (its own `managed_components/`, so the working tree is untouched). List every error and Kconfig warning. No device time.
2. From that list, write down what changes: dependencies, provisioning migration, config renames, the `esp_tinyusb` removal, the USB Serial/JTAG patch. The owner approves it.
3. Make the change and check it on the device: boot and crash reporting, SoftAP provisioning, Wi-Fi, OTA polling, every tile, all four rotations, idle/wake and the `state_change` run. A long run only if a specific risk needs one.

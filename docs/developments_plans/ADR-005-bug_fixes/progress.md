# ADR-005 progress

What was found and fixed for [ADR-005](README.md). Times are NZDT.

## Step 1: the review (started 2026-11-06)

Working notes, per subsystem, as the review goes. B = bug (fixed in step 2 once approved), P = performance issue (recorded only), S = style or consistency, R = risk worth knowing, not shown to fail. "Code" means found by reading, with the effect worked out; "device" means seen on the unit.

### System: start-up, tasks, idle/wake (`app.c`, `low_power_mode.c`, the LVGL watchdog in `lvgl_display.c`)

- **B, code: touch never counts as activity and never wakes the unit from idle.** `touchpad_read_cb_wrapper` (`low_power_mode.c:70`) tests `data->state == LV_INDEV_STATE_PR` before it calls the real touch read, but LVGL zeroes `data` before every read callback (`indev_read_core`, `lv_indev.c:169`), so the test is always false. Touch therefore neither resets the idle timer nor wakes the unit; only the button and motion (BNO085 stability reports) do. This fits the earlier need to keep the screen awake with the countdown timer. To confirm on the device: idle the unit, touch the screen without moving it.
- **Known bug 2, narrowed (code):** while idle, the low-power task feeds the paused LVGL watchdog (`lvgl_watchdog_feed_while_paused`). A stuck LVGL lock in idle is therefore not caught until the next wake request: `exit_idle_mode` then blocks on `lvgl_port_lock(0)`, the feeding stops, and the watchdog reboots after 5 s. So no permanent lock-up, but a stuck lock goes unseen during idle and costs one reboot on wake. The 2026-11-06 `hang` test sent no wake, which is why it stayed locked.
- The wake path itself works while LVGL's timers are stopped: the touch and button interrupts post `LVGL_PORT_EVENT_TOUCH`, and the port task then calls `lv_indev_read` directly (`esp_lvgl_port.c:228`).
- **R, code:** the sleep timeout calls `pmic_power_off()` straight from the low-power task (`low_power_mode.c:383`) with Wi-Fi, NVS and the display still running; whether anything can be lost is for the PMIC review.
- **R, code:** the low-power event group and both of its tasks are created inside `create_low_power_mode_view()` (a view's create function). `is_idle_mode_activated()` called before the UI exists would pass a NULL event group. Today every caller runs after the UI is up.
- **B, code (minor):** `axp2101_dev` from `heap_caps_malloc` is not checked for NULL in `app_main` (`app.c:178`); the BNO085 one next to it is.
- **P/noise, device:** `LowPowerMode: Active duration ... Idle duration ...` logs at INFO every second, and the stability task logs every report. Both fill the console the remote control shares.
- **S:** `app.c` keeps the "FreeRTOS Real Time Stats Example" header, an unused `mem_monitor_task`, unused includes, two near-identical I2C init functions, a tab-indented `lv_free_core`, and a stale `// Initialize USB` comment left by ADR-003. `low_power_mode.c` keeps a dead `enter_sleep_mode()` (light sleep, never called) and its wake-source setup, `%d` for `uint32_t`, a typo ("sayed"), and the same active PM configuration written out a second time in `app.c`.

### LVGL lock use across tasks (first pass)

- 31 of the 32 `lvgl_port_lock` calls pass `0`, which `esp_lvgl_port` takes as "wait forever"; only the level controller uses a timeout. With the LVGL watchdog this is by design (a stuck lock ends in a reboot), but every one of those callers hangs with it.
- **B, code: the status bar icons are changed behind LVGL's back.** `status_bar_update_wireless_state` and `status_bar_update_battery_level` (`config_view.c:487`, `:531`) `memcpy` a new symbol into the label's static text buffer (`lv_label_set_text_static`, created empty), with no `lv_label_set_text*` call and no invalidate. LVGL neither redraws nor re-measures the label, so the icons can stay stale, or zero-width, until something else redraws that area. The Wi-Fi updates come from the system event task without the LVGL lock, while the draw units may read the same bytes; the PMIC task's battery updates do hold the lock. To confirm on the device: watch the status bar while Wi-Fi changes state.

### Wi-Fi and network: life cycle (`wifi.c`, `wifi_config.c`, `wifi_provision.c`; OTA and OpenTrickler still to read)

Life cycle as built: `wifi_init` at boot → expiry timer (default 600 s) started at once → provisioned: manager de-initialised, STA started; not provisioned: SoftAP provisioning with a random PoP → `GOT_IP` stops the expiry timer and restarts mDNS → idle stops Wi-Fi, wake starts it → a disconnect while active and enabled re-arms the expiry timer → expiry: `wifi_deinit_task` de-initialises Wi-Fi for good (until reboot) and disables the switch.

- **B, code: no reconnect after a disconnect.** On `WIFI_EVENT_STA_DISCONNECTED` the handler calls `esp_wifi_start()` (`wifi.c:150`), but Wi-Fi is already started, so that does nothing and no `STA_START` (the only place that calls `esp_wifi_connect`) follows. After an AP drop, or a failed first connect at boot, the unit stays disconnected until an idle/wake cycle stops and starts Wi-Fi, or until the expiry shuts Wi-Fi down. To confirm on the device: switch the hotspot off and on while the unit stays awake.
- **B, code: the provisioning name and hostname come from uninitialised memory.** `wifi_init` calls `esp_wifi_get_mac()` before `esp_wifi_init()` (`wifi.c:228`); that returns `ESP_ERR_WIFI_NOT_INIT` (ESP-IDF header) and leaves `eth_mac` unset, so `OWMC_xxxxxx` is built from stack contents. It may happen to be the same each boot; to confirm, compare the name across two boots. `esp_read_mac()` reads the MAC from eFuse without Wi-Fi.
- **B, code: "Reset provisioning" crashes the unit after the Wi-Fi expiry.** `wifi_provision_reset` wraps `network_prov_mgr_reset_wifi_provisioning()` in `ESP_ERROR_CHECK` (`wifi_provision.c:96`); that calls `esp_wifi_restore()`, which fails once `wifi_deinit_task` has de-initialised Wi-Fi, so the abort fires from the button's callback. Also, after a reset nothing restarts provisioning; the message asks the user to re-provision, which needs a reboot.
- **B, code: the Wi-Fi password is written to the console.** `NETWORK_PROV_WIFI_CRED_RECV` logs SSID and password in plain text (`wifi_provision.c:34`).
- **B, code (minor): wrong state after an expiry when already provisioned.** `wifi_deinit_task` always sets `WIRELESS_STATE_PROVISION_EXPIRE` (`wifi.c:90`), also when the unit was provisioned and only failed to connect (`NOT_CONNECTED_EXPIRE` exists for that).
- **B, code (minor): a condition that is always true.** `if (wireless_state != PROVISION_EXPIRE || wireless_state != NOT_CONNECTED_EXPIRE)` (`wifi.c:319`) should be `&&`; at that point in `wifi_init` the state cannot be expired yet, so it has no effect today.
- **R, code:** after the expiry `wifi_deinit_task` stops and de-initialises Wi-Fi but leaves mDNS running on the dead interface, and calls `esp_netif_deinit()`, which ESP-IDF does not support.
- **R, code:** `wifi_config_set_enable` calls `esp_wifi_start/stop` directly, ignoring the expiry and the idle state (the switch is disabled after the expiry, but the remote control's `wifi on` is not).
- **P, code:** every `GOT_IP` (so every wake) runs `mdns_free()` and `mdns_init()`, deleting and re-creating the mDNS task. Memory stayed steady over 54 idle/wake cycles on v6.1.
- **Wi-Fi and LVGL races (code), owner's question 2026-11-06:**
  - The status bar icon is written from the system event task with no lock while LVGL's draw units may read it (B2).
  - The Wi-Fi switch's callback (LVGL task) and the remote control's `wifi on/off` (which takes the LVGL lock first, `remote_control.c:507`) call `esp_wifi_start/stop` with the LVGL lock held. Idle entry deliberately does the opposite ("Outside the LVGL lock: Wi-Fi event handlers take it", `low_power_mode.c:153`), because the Wi-Fi event handlers take the LVGL lock (`wifi_config_update_status`). It has not deadlocked in the runs so far; the rule is kept in one place and broken in two.
  - `wireless_state` is written by the system event task, `wifi_deinit_task` and `wifi_init` with no ordering; the last writer wins, whatever the real state.
  - The expiry is check-then-act: `wifi_request_start` (low-power task) checks `wifi_is_expired()` and then calls `esp_wifi_start()`, while `wifi_deinit_task` may be de-initialising Wi-Fi between the two.
  - Wi-Fi and low power depend on each other: the disconnect handler reads `is_idle_mode_activated()`, and idle entry and exit call `wifi_request_stop/start`. It is correct only because idle entry sets the idle flag before stopping Wi-Fi and wake clears it before starting.
- **Design (code): races handled by ordering and guards, not by structure.** Wi-Fi calls the UI directly (`status_bar_update_wireless_state`, `wifi_config_update_status`, `wifi_config_disable_*`), so it must start after the UI exists. Event groups are created lazily in every accessor (`create_wireless_event_group()`, `create_lvgl_display_event_group()`) so callers survive any start-up order. The PMIC checks `lvgl_display_is_ready()` before touching labels. Services start inside views' create functions (the low-power tasks, the OTA tasks). The configuration view must be created last (FIXME in `main_tileview.c`). A rotation is followed by a 1 ms timer to re-select the tile. The level controller locks with a timeout "to prevent a deadlock if the LVGL event wants to continue".
- **S:** `wifi_expiry_watchdog_restart` logs "as disconnected from WiFi hotspot" also when called from provisioning events; the task-creation error names "wifi_state_poller_task" for `wifi_deinit`; `wireless_state` is a global written from several tasks.

### Wi-Fi and network: OTA and OpenTrickler (`ota_mode.c`, `opentrickler_remote_controller_view.c`)

- Known bug 4 confirmed in code (`ota_sources[]`, `ota_mode.c:58`).
- **B, code (minor): a failed OTA leaves its handle open.** Every failure after `esp_https_ota_begin` jumps to `finally`, which shows the Reboot button and deletes the task without `esp_https_ota_abort()`, so the OTA handle and its HTTP connection stay open until the reboot.
- **B, code (minor):** the progress percentage divides by `esp_https_ota_get_image_size()`, which is -1 when the server sends no length.
- **R, code:** OTA is plain HTTP (`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`) with no image signing, so anything on the local network answering as `owmc_update.local` could serve firmware. The download task correctly uses an internal stack (flash writes).
- **R, code:** OpenTrickler discovery runs once, at the first Wi-Fi connection after boot; a trickler switched on later is not found until a reboot. Possibly by design.
- **S:** `OTA_EVENT_MANIFEST_READY` is set when Wi-Fi connects, before any manifest; the manifest is logged in full at INFO; `json_obj_get_*` results are checked two different ways in the same function.

### BNO085: life cycle (`components/bno08x`, `digital_level_view_controller.c`, `low_power_mode.c`)

Life cycle as built: `bno085_init_i2c` at boot (pins, ISR, soft reset, `sh2_open`, product IDs, sensor poller task in PSRAM) → reports are enabled per view: the level controller turns game rotation and linear acceleration on and off on tile changes (LVGL task), the stability task turns its report on at start, calibration / point-of-aim / acceleration views their own → interrupt → poller task runs `sh2_service()` → each report lands in a one-deep queue that the consumer polls → idle: `sh2_devSleep()` from the low-power task; wake: `sh2_devOn()` → an unexpected sensor reset re-enables every saved report from inside the `sh2` callback.

- **B, code: the `sh2` library is used from several tasks with no lock.** The `sh2`/SHTP state is single-threaded, and `sh2_setSensorConfig` and friends service the transport themselves while they wait for the sensor's reply. Yet `sh2_service()` runs in the poller task while reports are switched from the LVGL task (tile changes), the stability task, the views' tasks and `sensor_config.c`, and sleep/wake from the low-power task (`bno085_i2c.c` has no mutex). Two tasks inside the transport at once can corrupt a packet or make an operation fail, and every one of those calls is wrapped in `ESP_ERROR_CHECK`, so a failed collision aborts the unit.
- **B, code: report re-enable after an unexpected sensor reset runs inside the `sh2` callback.** `sh2_event_callback` calls `sh2_setSensorConfig` for every saved report while `sh2_service()` is still dispatching (`bno085.c:145`). `sh2` operations are not meant to start from its own callback; if they fail (return value ignored), the reports are not restored and the level stops updating until a reboot.
- **B, code: every BNO085 call aborts on failure.** All report switches and the stability task's start use `ESP_ERROR_CHECK` (`digital_level_view_controller.c`, `low_power_mode.c:393`, `sensor_config.c`, the views), so any sensor hiccup at a tile change reboots the unit. Known bug 3 (the first-boot abort) is the same pattern at init.
- **Known bug 3 (first-boot abort after a reset): a likely cause, not proven.** `_bno085_ctx_init` drives the BNO085's reset line (GPIO8) and BOOT line (GPIO1) high but never pulses reset and does not wait; `bno085_init_i2c` then probes only 5 times 10 ms apart (about 50 ms) and returns `ESP_FAIL`, which `ESP_ERROR_CHECK` at `app.c:194` turns into an abort. An esptool/RTS reset is a chip reset that returns the ESP32-S3 pins to their defaults, so the sensor is probably held in reset until GPIO8 is driven high again and then needs its start-up time (believed to be about 100 ms; to check against the datasheet) before it answers. The abort's own reboot is a software reset that keeps the pins as they were, so the sensor was never reset and answers at once. This fits what was seen: the abort follows a flash or esptool reset, not a console `reboot` or a watchdog reset. Proof would be one timestamped boot after a chip reset. The likely fix: a defined reset pulse, then wait for the sensor's ready signal (interrupt line low) instead of a fixed 50 ms window, and no abort on failure (B7). In this round, as part of B7 (owner, 2026-11-06).
- **R, code:** the report table has 60 entries and valid sensor IDs end at 46, so normal reports stay in bounds; only a corrupted packet could index past it (`sh2_sensor_callback`).
- **S:** "Reset Unexpectly", "sh2_decodeSensorEventd", "unified_ensor_poller_task" typos; a decode failure logs at INFO; `sensor_roll_thread_unsafe` and friends are shared globals by design (named so).

### PMIC (`pmic_axp2101.cpp`, `pmic_config.c`)

Life cycle as built: `axp2101_init` at boot (load config, ISR on the IRQ pin, `PMU.begin` on I2C bus 0, which the touch controller shares; charge and VBUS limits written once) → monitor task wakes on the IRQ or every 2 s, handles key presses (short: activity and wake; long: power menu) and VBUS changes, reads six values over I2C and updates the battery icon and the power page under the LVGL lock → power-off from the power menu or the sleep timeout: `disableBATFET()` then `shutdown()`.

- **B, code: charge current, charge voltage and the USB current limit only change after a reboot.** The settings page stores them (`pmic_config.c:92-135`), but they are written to the AXP2101 only in `axp2101_init` (`pmic_axp2101.cpp:369-376`), and the page does not say a reboot is needed. Idle and sleep timeouts, by contrast, apply at once.
- Checked: the ISR only sets an event bit; the battery icon update holds the LVGL lock; power-off has nothing in flight worth saving (settings are written only on Save).
- **R, code:** power-off with USB connected: whether the AXP2101 then restarts at once is not known.
- **P, code:** the monitor task reads six PMIC values over I2C every 2 s also while idle, and updates LVGL labels while LVGL is stopped.
- **S:** the event-group error says "sensor_event_control" (copied from the BNO085 driver); `axp2101_deinit` is unused.

### Other peripherals (touch, button, buzzer, NVS settings, console)

- **B, code: the touch controller's reset drives GPIO0 instead of its reset pin.** `esp_lcd_touch_new_ft3168` reads `touch_handle->config.rst_gpio_num` before it copies the config in (`esp_lcd_touch_ft3168.c`: `calloc`, reset block, then `memcpy(&touch_handle->config, ...)`), so it sees pin 0. It configures GPIO0 (the external button) as an output and pulses it low for 10 ms, and never pulses the real reset pin (GPIO16). This is the cause of v6.1's `conflict found for GPIO[0]` warning (known bug 8): the button later re-configures GPIO0 as an input, which is why the button still works.
- **R, code:** the FT3168 init writes with an infinite I2C timeout (`-1`), and its GPIO-config error paths leak the handle.
- **B, code (minor):** `buzzer_run` ignores a full command queue (depth 1, no wait) and returns `ESP_OK`, so a beep requested while another is pending is dropped silently.
- NVS settings (`common.c`): blobs carry a CRC; a missing, short, corrupt or differently sized blob falls back to the defaults and is rewritten. So a change to a settings struct's layout resets that page to defaults, by design.
- **B, code (minor):** `parse_git_describe_version` ignores `sscanf`'s result, so a version string without the leading `v` leaves the fields as uninitialised stack values for the OTA version compare.
- **S:** `crc32_wrapper` passes `0xEDB88320` as `esp_crc32_le`'s initial value but names it the polynomial (consistent on both sides, so it works).
- Console and remote control: reviewed in ADR-004; the reply loss (known bug 5) stays open. `rc_serial` keeps about 944 B of stack free at its lowest, the same before and after ADR-003.

### LVGL views and the tile life cycle (`main_tileview.c`, views, `countdown_timer.c`, `crash_report.c`)

- Each tile carries an enable callback; a tile change disables the old one and enables the new one, all in the LVGL task. The level tile's callback switches BNO085 reports (see the BNO085 lock bug).
- Checked and fine: the countdown timer's state is behind a mutex, its nested `lvgl_port_lock` calls are safe (the port's lock is recursive), and a running countdown counts as activity. Panel commands from LVGL events (brightness) are safe because `tx_param` waits for strips still in flight.
- **B, code (minor): after a task-watchdog reset the crash report names the wrong task.** It prints the core dump's interrupted task (`IDLE0` in the 2026-11-06 watchdog test), not the watchdog user that hung (`lvgl`, which only the pre-reset log shows).
- Dead code in the build: the point-of-aim, acceleration-analysis and OpenTrickler tiles are commented out in `main_tileview.c`, so their modules compile but never run; `circular_buffer.c` is used only by the acceleration view.

### Cross-cutting style and consistency (against `.agents/skills/c-style`)

- **Fatal checks in runtime paths:** 136 `ESP_ERROR_CHECK` in `main/`, many in UI callbacks and task bodies (Rule 4 reserves it for initialisation where failure is fatal). This is what turns several of the bugs above into reboots.
- **Globals shared through `extern`:** 77 `extern` declarations in `.c` files instead of headers (Rule 1).
- **Formatting:** 7 function definitions indented by one space (`digital_level_view.c:80`, `ota_mode.c:157`, `:386`, `:544`, `:549`, ...); one tab-indented function in `app.c`; `%d` used for `uint32_t` in several modules.
- **Tags:** mostly `CamelCase` (`LowPowerMode`, `WiFiConfig`), with `BUZZER`, `BSP`, `AXP2101`, `POIView` and `OTAMode` breaking the pattern.
- **Commented-out code:** about 95 lines of commented-out calls across `main/` (the owner keeps some on purpose, e.g. in `low_power_mode.c`).
- **Typos and stale text:** listed per module above.

## Step 2: the bug fixes (approved 2026-11-06)

Owner: the list and order are approved; B13 is a placeholder, not a bug; B2 moves to step 3; the BNO085 first-boot abort stays in, as part of B7.

### B22: device-test logs out of the build folder (2026-11-06, `10fffa3`)

`tools/remote_control/rc_run.py` writes session logs to `logs/rc/` (git-ignored) instead of `build/rc_logs/`, so `idf.py fullclean` and `set-target` no longer delete them. Proof: a `send state` session's log landed in `logs/rc/`; the 14 logs still in `build/rc_logs/` were moved there.

### B23: `scenario watchdog` wakes the unit first (2026-11-06)

The scenario now sends `wake` and waits 2 s before `hang`, because the LVGL watchdog is paused while idle. Proof (`bd4d102`, `logs/rc/rc_watchdog_20261106_222240.log`): with the unit put into idle first (`idle=1`), the scenario woke it, `hang` locked LVGL, the watchdog named `lvgl` and rebooted the unit, which reported `TASK_WDT` and a stored core dump. PASS.
- Noted on the way: before the hang, `min_int` read 140,588 B (about 150,400 B near boot) with `free_int` 185,863 B, so some time in the session internal RAM dipped about 10 KB lower than near boot; the owner's SoftAP provisioning test is the likely moment, not checked.

### B16: the touch controller is reset on its own pin (2026-11-06)

`esp_lcd_touch_new_ft3168` now copies the config into the handle before the reset block, so the reset pulses GPIO16 (the FT3168's reset line) instead of GPIO0, the external button. Proof (`logs/rc/rc_reboot_20261106_222444.log`): no `conflict found for GPIO[0]` in the boot log (0 lines; it printed on every v6.1 boot before), and the FT3168 initialises after its reset ("Successfully wrote to FT3168" at 824 ms). Owner, 2026-11-06: touch and the button work.

### B3: owner's report on waking by touch (2026-11-06)

- Owner: touch and the button both wake the screen, and **the touch that wakes the screen is also passed on to LVGL**, which may trigger something the user cannot see. The owner does not want the wake touch to reach LVGL.
- This fits B3 rather than contradicting it: if the wrapper saw the touch, it would wake the unit and return before the touch reached LVGL. The touch reaching LVGL shows the wrapper never notices it; the wake most likely comes from the BNO085 stability detector reacting to the tap's jolt. To settle it: log the order of "Stability detector report" and "Exiting idle mode" while the owner taps the idle unit.
- The fix must also swallow the rest of that touch: the finger is usually still down after the wake, so the touch is ignored until it lifts; otherwise the release lands as a click.
- Seen on the way: the first boot after the app flash (an RTS reset) reported the previous reset as `PANIC` with a core dump in task `main`, the BNO085 first-boot abort (B7) reproducing as expected.

### B5: one lock around the `sh2` library (2026-11-06, `22aca2b`)

A recursive mutex in the driver context is held around every `sh2_*` call: `sh2_service()` in the poller, report changes, sleep/wake, init, and the calibration view's tare, which now goes through new driver wrappers (`bno085_tare_now`, `bno085_persist_tare`) instead of calling `sh2` directly. Recursive because the `sh2` callbacks run inside `sh2_service()` and the reset handler calls back into `sh2` (B6).
- Before the fix the race did not show: 400 tile flips at 0.25 s (each one switches two reports from the LVGL task while the poller services 50 Hz reports) passed without an error. The fix rests on the code: `sh2` is single-threaded and was entered from several tasks.
- After: the same 400 flips (`rc_tile_flip_*` in `logs/rc/`), `scenario state_change` and 54 idle/wake cycles in 10 minutes (`rc_idle_cycles_20261106_223945.log`, sleep/wake goes through the lock) all PASS, no BNO085 errors, `stats` 21 of 21, `largest_dma` 110,592 B at its lowest.

### B7: BNO085 errors no longer abort; the first-boot abort fixed (2026-11-06)

- **First-boot abort, cause found and fixed.** `_bno085_ctx_init` now drives the BOOT line high first (the BNO085 samples it when it leaves reset; it was configured after the reset line), then pulses reset low for 10 ms and waits up to 500 ms for the sensor's ready signal (H_INTN low) before the probe. Before, it only set the reset line high, after a chip reset that line had been undriven, and the 50 ms probe window started at once.
- Proof: the sensor's measured ready time after reset is 96-103 ms (5 boots), twice the old probe window. Five chip resets (the app flash's RTS reset and four `esptool --after hard-reset`), boot logs in `logs/rc/rc_boot_*`: every boot reports `Reset reason: USB (11)` and finds the sensor on the first probe; before the fix every chip reset that day ended in the abort (`PANIC`, core dump in `main`). A software reset (`reboot`): `SW (3)`, ready after 96 ms.
- **Runtime calls are not fatal any more.** The 14 `ESP_ERROR_CHECK` around BNO085 calls in live code (`digital_level_view_controller.c`, `low_power_mode.c`, `sensor_config.c`, `sensor_calibration_view.c`) are plain calls, and the driver logs a failed report change with the sensor ID, interval and `sh2` status. A sensor that is still missing after the reset and wait stays fatal at init: nearly every module uses it, and running without it is a larger change than this bug. The dead point-of-aim and acceleration views keep theirs for the style pass.
- `scenario state_change`: PASS, no report errors. A failing report change cannot be forced on demand, so the runtime half rests on the code.

### B6: report re-enable after an unexpected sensor reset — checked, not a bug (2026-11-06)

To test it, the remote control gained `bnoreset` (debug, like `hang`): it pulses the BNO085's reset line through the new `bno085_hardware_reset()` (under the `sh2` lock), so the sensor restarts mid-run as after an unexpected reset. On the level tile, three forced resets (`logs/rc/rc_bnoreset_*`): each time the driver logged the reset, re-enabled all three saved reports (IDs 4, 8, 28) from inside the `sh2` callback, and the sensor confirmed every configuration within about 130 ms. No "Failed to configure report" (which B7 now logs). Re-entering `sh2` from its own callback works with this library, so B6 is closed without a change; `bnoreset` stays as a tool for later checks.

### B9: provisioning name and hostname from the real MAC (2026-11-06)

`wifi_init` reads the STA MAC with `esp_read_mac(..., ESP_MAC_WIFI_STA)` (eFuse, no Wi-Fi needed) instead of `esp_wifi_get_mac()` before `esp_wifi_init()`, and logs the result. Proof: `Service name and hostname: OWMC_83A124 (STA MAC 44:1b:f6:83:a1:24)` on three boots in a row, and `esptool read-mac` reads `44:1b:f6:83:a1:24`. The old code never logged the name, so what it produced is not on record; the "before" rests on ESP-IDF's contract (`ESP_ERR_WIFI_NOT_INIT`, buffer not written). If the old value differed, the SoftAP name and the hostname change once with this fix; the saved Wi-Fi credentials are not affected.

### B11: no Wi-Fi password in the console (2026-11-06)

The `NETWORK_PROV_WIFI_CRED_RECV` log now names the SSID only. Proof without a fresh provisioning (which would replace the saved credentials): the built firmware no longer contains the `Password : %s` format string (1 occurrence before, 0 after), and the new line is present. Flashed; the unit answers.

### B4: a stuck LVGL lock during idle now reboots the unit (2026-11-06)

While the screen is off, the LVGL watchdog is paused and the low-power task fed it on every pass, so a task stuck holding the LVGL lock went unnoticed until the next wake. `lvgl_watchdog_feed_while_paused()` now feeds only if it can take the lock within 100 ms; idle holders keep it for milliseconds, and one that holds it for the whole 5 s timeout stops the feeds.
- Before: `idle`, then `hang` (takes the LVGL lock for good): no watchdog and no reset in the 15 s watched.
- After: the same steps, `hang` at 23:06:58.5, the task watchdog fired naming `lvgl` at 23:07:03.1 (4.6 s later, no wake), the unit rebooted and reported `Reset reason: TASK_WDT (6)`.
- No false trips: 54 idle/wake cycles in 10 minutes, 7 s idle each (longer than the timeout), `rc_idle_cycles_20261106_230729.log`: PASS, no watchdog, `largest_dma` 110,592 B at its lowest.

### B14: OTA failure paths, progress and version parse (2026-11-06)

- **Handle.** Every failure after `esp_https_ota_begin` now goes through `esp_https_ota_abort` (closes the connection, frees the handle, drops the partial image). The review found one more case of the same leak: when the server sends no content length, `esp_https_ota_perform` returns `ESP_OK` at the end, the old loop left on any code other than "in progress" and never called `esp_https_ota_finish`, so the handle leaked and the image was never validated. The loop now follows ESP-IDF's pattern: perform until it stops reporting progress, then finish if all data arrived, else abort. `esp_https_ota_finish` frees the handle whether it succeeds or not (checked in v6.1's source).
- **Progress.** With no size (-1) the screen shows only the bytes read; with a size it shows percent as before. The first label showed the size in bytes marked as K; now it is K.
- **Version.** `parse_git_describe_version` accepts `v1.2.3`, `1.2.3` and `v1.2.3-4-gabc`, and returns `ESP_ERR_INVALID_VERSION` for anything else; a version that cannot be read is never offered.
- Proof: built clean, flashed, rebooted (`rc_ota_boot_20261106_232148.log`): `Reset reason: SW (3)`, Wi-Fi up, the poller tries both sources and both fail to connect as before, no errors. The OTA paths themselves rest on the code: a test server on this PC needs a firewall permission that is the owner's, and the owner ruled OTA tests out (no unit in the field).

### B12: the Wi-Fi expiry ends in the right state (2026-11-06)

Tools first (own commit): the remote control's `wifi expire` (debug, like `hang`) fires the expiry now, as the timer would; Wi-Fi stays off until the next boot, nothing is saved. The Wi-Fi status text (the config page's "Status: ...") is now also logged as `WiFiConfig: Status: ...`.
- Before (provisioned unit, `rc_wifi_expire_*` in `logs/rc/`): `Status: provision expired`, then the expiry's own `esp_wifi_stop()` came back as a disconnect: `Status: disconnected`, `Start Wifi expiry timer as disconnected`, `Failed to start WiFi: ESP_ERR_WIFI_STOP_STATE`. So the screen ended on "disconnected", not on an expiry.
- Cause: the expiry ends the Wi-Fi life cycle but left the event handlers registered, so the stop was handled as a lost connection. Fix: `wifi_deinit_task` unregisters the four handlers first, then stops and de-initialises Wi-Fi, clears the connected bit, and sets the final state last: `connect expired` when provisioned, `provision expired` when not. The provisioning page disables the reset button for both; the "Provision Expired" note is shown only for the second. The always-true `||` in `wifi_init` is now `&&` (no effect today, as reviewed).
- After: `Wifi is not active in time`, `Wi-Fi stack deinitialized`, `Status: connect expired`, nothing else from Wi-Fi; `wifi on` replies `@err expired`. Then 3 idle/wake cycles (`rc_wifi_expire_idle_*`): every start/stop request is ignored, no errors, no reset. A reboot brings Wi-Fi back (`ip=1`). The not-provisioned branch rests on the code: testing it would wipe the saved credentials.

### B18: the newest beep request wins (2026-11-06)

The buzzer task treats every received command as an override of the one playing, but `buzzer_run` sent to its one-slot queue without waiting, so while a command was still pending the newer one was dropped and the older one played. Non-blocking requests now use `xQueueOverwrite`, so the pending command is replaced and the newest request wins, as the task intends; `block_wait` still waits for the slot. Proof: built, flashed, rebooted (`rc_buzzer_boot_*`): the boot beep is received once and plays its two beeps (duty on/off at 3822, 3922, 3972, 4072 ms). The drop needs two requests within the buzzer task's reaction time, which cannot be forced without a new test hook; for a minor, harmless loss that hook would not earn its place, so the drop path rests on the code.

### B20: the crash report names the watchdog user that hung (2026-11-06)

Cause: the task watchdog panics from its interrupt, so the core dump records the task that interrupt cut into (`IDLE0`), and the user that missed its feed was only on the console before the reset. `crash_report.c` now defines ESP-IDF's `esp_task_wdt_isr_user_handler()` hook, which collects the missing users' names through `esp_task_wdt_print_triggered_tasks()` into RTC no-init memory (kept through the panic reset, checked by a magic number); the next boot's crash report prints them and clears the record, so a later reset of another kind does not repeat them. The commented-out stub of the same hook in `app.c` is removed.
- Before (B4 test, `rc_idle_hang_20261106_230652.log`): `Reset reason: TASK_WDT (6)`, `Stored core dump: task=IDLE0`, nothing names `lvgl` after the reset.
- After (`hang` in idle, `rc_idle_hang_*` 23:37): `Reset reason: TASK_WDT (6)`, `Task watchdog: not fed in time by lvgl (CPU 0/1)`, then the core dump line as before. A normal `reboot` after it: `Reset reason: SW (3)` only (`rc_crash_clear_*`).

Observation, not a bug: since the build with the Wi-Fi status log, `largest_dma` reads 106,496 B after boot instead of 110,592 B, while free internal RAM at the same checkpoints is equal (186,363 vs 186,367 B). The total did not change, only where the largest free block ends up; nothing after boot needs a block near this size.

### B24: PMIC context allocation — checked, not a bug (2026-11-06)

`axp2101_init` returns `ESP_ERR_INVALID_ARG` for a NULL context before it touches it (and zeroes the context itself), and `app.c` wraps the call in `ESP_ERROR_CHECK`. So a failed allocation already stops the boot, as the BNO085 path does after its own check; only the error name differs (`INVALID_ARG` for `NO_MEM`). A second check would be a stacked guard, so no change.

### B19: boot log noise (2026-11-06)

- **`task_wdt: delete_entry: task not found`, fixed.** Seven tasks called `esp_task_wdt_delete(NULL)` "to disable the watchdog", but no task adds itself to it: only the idle tasks (by config) and the `lvgl` user are subscribed. The calls were no-ops that logged an error; all seven are removed (four run at boot: sensor poller, stability detector, rotation-vector poller, OTA updater; three in the dead views). Before: 4 per boot (`rc_crash_clear_*`); after: none (`rc_boot_noise_20261106_234203.log`).
- **`GPIO isr service already installed`, kept, upstream's.** `app.c` installs the service once, before the PMIC, BNO085 and button add handlers. The second install is `esp_lcd_touch`'s own when the LVGL port registers the touch interrupt; its source says it expects the application to have installed it and ignores the result, and v6.1's GPIO driver logs that as an error. Removing it means changing a managed component (no dependency change in this ADR) or muting the `gpio` tag, which would hide real GPIO errors such as the one that found B16.
- After the change every error line at boot is accounted for: the GPIO line above and the OTA placeholder sources failing to connect (B13).
- Seen in passing, for B8: every `reboot` logs `Failed to start WiFi: ESP_ERR_WIFI_STOP_STATE` (also in older logs, so not new). `esp_restart()` stops Wi-Fi and the disconnect handler takes that stop for a lost connection: the class B12 fixed for the expiry. B8's fix to that handler should tell an intentional stop from a lost connection by the event's disconnect reason (`WIFI_REASON_ASSOC_LEAVE` for the unit's own stop), not by one flag per state.

### Where step 2 stands (2026-11-06, end of the owner-free run)

- Done and proven: B22, B23, B16, B5, B7 (with the first-boot abort), B9, B11, B4, B14 (OTA paths rest on the code), B12, B18 (drop path rests on the code), B20, B19 (watchdog half; the GPIO line is upstream's). Checked, not bugs: B6, B24.
- Regression after the run: `scenario state_change` PASS (`rc_state_change_20261106_234308.log`), no error lines in the whole run, `largest_dma` 106,496 B at its lowest.
- Waiting for the owner: B3 (wake touch, with the note that the wake touch must not reach LVGL), B8 (reconnect, needs the hotspot off and on; see the B19 note on the disconnect reason), B15 (PMIC settings after reboot), B10 (its test wipes the Wi-Fi credentials; ask first).
- Open causes still to investigate: B1, B17, B21.

### B7 regression: no BNO085 reports after boot, so the level did not move (2026-11-06, found by the owner)

- **Symptom.** The level never moved. With the level controller's profiling switched on for a diagnostic build, the poller looped 51 times a second and received 0 game-rotation and 0 acceleration reports, after boot and after idle/wake. No error anywhere. The sensor's INT line was high (idle) throughout, so it held no unread data; it never confirmed a report configuration (`EventHandler Sensor Config` never appeared).
- **Which change.** Counting `EventHandler Sensor Config` per log in `logs/rc/`: every run up to 22:39 (the B5 build) has them (3 to 5 per reboot, 22 in `state_change`, 108 in the idle cycles); from the first B7 boot (22:51) on, every run has 0, except right after a `bnoreset`. A mid-run `bnoreset` brought the reports back at full rate (game 51/s, acceleration 20/s), so the hub, the configurations and the level code all worked.
- **Root cause.** B7 added a hardware reset to `_bno085_ctx_init`, but the I2C transport's `open` still sent its own soft reset, so the sensor was reset twice: the soft reset arrived about 120 ms into its start-up from the hardware reset, with its start-up advertisement still unread. After that the sensor answered requests (product IDs) but never acted on a report configuration until the next reset. Why the hub firmware behaves so is not known; the cause is shown by the experiment below. Before B7 the soft reset was the only reset. B7's proof checked that the sensor was found at boot, not that it delivered reports, so the regression got through.
- **Fix.** The sensor is reset once: the I2C `open` sends its soft reset only when no reset line is wired; with one, the hardware reset and ready wait in `_bno085_ctx_init` is the reset. Boot is also about 250 ms faster (the soft reset's 300 ms wait is gone).
- **Proof.** Diagnostic build with one reset: configurations confirmed at boot, then 50 to 51 game-rotation and 20 to 21 acceleration reports a second; idle stops them and wake brings them back. Release build: one chip reset (`Reset reason: USB (11)`, `rc_boot_20261106_235833.log`) and three more on the diagnostic build, all ready after 96 ms, no abort, all three configurations (IDs 8, 4, 28) confirmed within about a second of boot; a software `reboot` the same; idle/wake reconfigures the level's report as before (`rc_level_counts_*`).
- Not changed: the SPI transport's `open` does its own full hardware reset (with PS0/WAKE set for SPI). B7's common reset comes before PS0 is set, so on SPI the sensor is also reset twice, but the second reset is a proper one. This board uses I2C, so the SPI path cannot be tested here.
- Lesson for later BNO085 changes: the proof must show reports arriving (`EventHandler Sensor Config`, or the level profile's report counts), not only that the sensor was found.


### B3: touch counts as activity, and the wake touch never reaches LVGL (2026-11-07)

- **Before, on the device with the owner** (`rc_owner_watch_20261107_000320.log`): with the countdown widget enabled and the screen off, a touch woke the screen and also started the timer (`Exiting idle mode` at 689085 ms, `CountdownTimer: Current state 2` and its beep 69 ms later), so the touch had gone into LVGL with the screen dark.
- **Cause, three parts.** (1) The LVGL port reads every pointer and keypad input on a touch interrupt, also while LVGL is stopped for idle. (2) The touch wrapper tested `data->state` before the real read filled it in (LVGL clears `data` before each read), so it never saw a press: touch never counted as activity, and the press went to LVGL. (3) The button counts as a keypad, so the port read it on the touch interrupt too, and its read callback woke the unit on any read while idle, pressed or not. That accident was the only reason a touch woke the screen.
- **Fix.** One rule in `low_power_mode.c`, `low_power_mode_swallow_wake_press()`, used by both read callbacks: a press counts as activity; a press that starts while idle wakes the unit, and that whole press, until release, is reported to LVGL as released. The touch wrapper now calls the real read first. The button callback counts activity and wakes on a real press only. Same function, same class: the button's read timer paused 450 ms after the press interrupt even while the button was held (the interrupt sees the press edge only), so a long press lost its release; it now pauses only once the button is released, which the swallowed wake press also needs.
- **After, owner at the unit** (`rc_owner_watch_20261107_001652.log`): three wakes from idle (two by touch over the timer, one by the button), each `Exiting idle mode` with no timer event; the timer started only on the owner's next tap, 1.1 to 4.4 s later; normal presses worked. Owner: "all three steps are confirmed working".

### B25: the level reads 180 degrees off (owner, 2026-11-07), recorded

Owner, while checking the B7 regression fix by hand: "I'd expect when the level is sitting on its edge that the screen direction is 'looking right', then the reading should be 0." The reading is `sensor roll - rotation x 90 degrees + the saved zero offset` (`digital_level_view_controller.c`); the owner suspects the rotation term. To be worked out before a fix is proposed.

### B8: the unit reconnects after a lost connection (2026-11-07)

The owner could not switch the hotspot off without outages elsewhere, so the test uses a new debug command (own commit): `wifi drop` disconnects the radio from the AP for real and marks that one disconnect to be handled as a lost connection; it cannot show the radio noticing a vanished AP (a beacon timeout), which is ESP-IDF's part. The disconnect reason is now logged.
- Before (`rc_wifi_drop_*` 00:2x, first run): after `wifi drop`, no reconnect in 25 s: the handler armed the expiry timer and called `esp_wifi_start()` on the started driver, which does nothing, and no `STA_START` (the only `esp_wifi_connect` call) followed. The unit's own stops all arrive with reason 8 (`WIFI_REASON_ASSOC_LEAVE`): switch off, idle entry and reboot; the reboot also logged `Failed to start WiFi: ESP_ERR_WIFI_STOP_STATE` (the B19 note).
- Fix: the disconnect handler tells the two apart by the reason, not by one flag per state: reason 8 only updates the status; any other reason (a lost connection or a failed connect attempt) arms the expiry timer and calls `esp_wifi_connect()`. The expiry ends the retries if the AP stays away.
- After (second run): `wifi drop` reconnected by itself (disconnect at 9.9 s, IP at 13.5 s); switch off/on and idle/wake as before (IP in about 4 s), now without arming the expiry timer; reboot without the `Failed to start WiFi` line. `scenario state_change` PASS (`rc_state_change_20261107_002806.log`), four disconnects, all reason 8, no Wi-Fi errors.
- A failed first connect at boot (AP absent) now retries until the expiry, one attempt per scan, where it used to stay disconnected; not tested (needs the AP away).

### B15: PMIC charge settings take effect on save (2026-11-07, owner chose "apply on save")

- Before: the VBUS current limit, charge current and charge voltage were written to the chip only in `axp2101_init`, so a saved change waited for the next boot, and the page did not say so.
- Fix: one function writes the three settings, `axp2101_apply_charge_settings()`, used by init and by the PMIC monitor task. The page's Save asks the monitor task to apply them (`axp2101_request_apply_charge_settings()`, a new event bit the task waits on) instead of writing over I2C from the LVGL task, because the monitor task reads the chip every 2 s without a lock: the task that owns the chip after init stays the only one that touches it (the step 3 direction).
- Proof, owner at the unit (`rc_owner_watch_*` 00:4x): charge current one step down and Save: `Configuration write successfully` at 79993 ms, the chip read back charge current 15 (was 16) at 79996 ms; back up and Save: 16 again at 87949 ms, so the owner's saved setting (1000 mA) is as before. At boot the same function writes the saved values (VBUS 5, current 16, voltage 5, raw register codes).

B25, code inspection (2026-11-07; the owner could not reproduce it). The shown angle is `round(deg(wrap(roll - rotation x 90 deg + user_offset)))`, with `roll` the Z-Y-X Euler roll of the game rotation vector. Paths that give exactly 180 degrees:
1. **Tare plus the rotation term (most likely).** The calibration page's Tare (`sensor_calibration_view.c`) tares all three axes and persists it in the BNO085's own flash, so the pose at tare time becomes the sensor's zero: the raw roll there reads 0. The controller still subtracts `rotation x 90 deg`, which assumes the untared sensor, whose raw roll in the upright pose for setting r is r x 90 deg. So after a tare in the normal pose at rotation 2 (the owner's), that pose reads -180, shown as 180; at rotation 0 it reads 0, at 1 or 3 it reads 90 off. The tare survives reboots and reflashing; nothing in the code clears it (`sh2_clearTare` is unused). The label-tap zero (`tilt_angle_button_short_press_cb`) includes the rotation term and hides the offset again, which may be why it did not come back.
2. **Rotation setting against the way the unit is held.** For the same pose, settings 0 and 2 differ by exactly 180 degrees (and the screen turns with them); by design, but a setting saved half a turn from the mounting reads 180 off.
3. **Euler flip.** Pitch comes from `asin`, limited to +-90 degrees; tipping the unit end over end past vertical reports the same cant as roll + 180 with pitch mirrored. Not reachable in normal use at the level's pose (pitch near 0).
4. **Label-tap zero taken in a flipped pose.** A single click on the angle label zeroes the level at the current pose (RAM only until the level page's Save). Before the B3 fix a wake tap could land on it with the screen dark; a zero taken with the unit upside down gives a constant 180.
Also noted: the tare uses the magnetic rotation vector as its basis while the level reads the game rotation vector.

### B25: Tare also sets the rotation-aware zero (2026-11-07, not run on the unit)

Fix for path 1 above: after a successful tare, the calibration page calls the new `digital_level_view_zero_after_tare()` (level module), which sets the level's zero offset to `rotation x 90 deg` and saves the level settings, so the tare pose reads 0 at the current rotation and the offset is kept with the tare across reboots (the tare is saved in the sensor; the label-tap zero stays RAM-only until the page's Save). A later rotation change with the unit turned to match still reads 0, as before. Not run: the owner said not to (aligning this unit's tare to true north is laborious), so the tare path rests on the code. Built clean (only old warnings), flashed, rebooted: the level settings load and all three report configurations are confirmed (`rc_tare_fix_boot_*`). Path 1's other point stays open: the tare uses the magnetic rotation vector as its basis while the level reads the game rotation vector.

### B10: a failed provisioning reset no longer aborts the unit (2026-11-07)

Checked first: the expiry disables the Reset provisioning button in both expiry states (before B12 and after it), so the abort was reachable only in the milliseconds between Wi-Fi being torn down and the button being disabled. The broken invariant is still real: `wifi_provision_reset()` wrapped `network_prov_mgr_reset_wifi_provisioning()` in `ESP_ERROR_CHECK`, so any failure (it fails once Wi-Fi is de-initialised) aborted the whole unit from a button callback. It now returns the error and logs it, and the page shows "WiFi Provisioning reset failed. Power cycle the device and try again." instead of the success text. Not tested: a successful reset wipes the saved credentials, and the failure needs the expiry plus a click on a disabled button. Built, flashed, rebooted: Wi-Fi connects, the level reports (`rc_b10_boot_*`). The other half of the review note (after a reset nothing restarts provisioning until a power cycle) is how the page already tells the user to proceed, so it stays.

### B17: console reply loss — not reproduced on the current firmware (2026-11-07)

Counted over every log in `logs/rc/` (v6.1 with the patched USB Serial/JTAG driver, 2026-11-06 21:09 on): about 1,700 commands, and every command sent while the remote control was running got its reply (153 of 153 `stats`, 866 of 866 `tile`, ...). The only misses are 19 `state` probes from the runner's `wait_ready`, all sent during a boot before `RemoteControl: Remote control initialized` (about 3.7 s into boot); those are the expected retries. The losses on record (ADR-004: 5 of 39 in one `watch` run, 2 of about 236 later) fit the ESP-IDF connection monitor that dropped whole writes as "disconnected", which the driver patch `3579052` (2026-11-06 01:34) fixed; whether the last 2 came before or after it is not on record. No change; B17 is closed as not reproduced, to reopen if a reply goes missing while the remote control is up.

### B21: the ~32 KB "start-up borrow" is an accounting artefact of v6.1, nothing is borrowed (2026-11-07)

- Measured with temporary checkpoints (not committed): at the first line of `app_main` free internal RAM is 288,535 B and the low-water mark 256,504 B, a gap of 32,031 B, so the cause is in ESP-IDF's start-up before any project code. Our early steps (console driver, crash report, event loop) move both numbers together.
- Cause, read in v6.1: `reclaim_startup_stack_memory_for_heap()` (`freertos/app_startup.c`) first calls `heap_caps_enable_nonos_stack_heaps()`, which ends the start-up phase (`s_in_startup = false`), then reserves the internal DMA pool (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`): it allocates 32 KB from the internal heap and registers the block as a heap region of its own. v6.1's `heap_caps_add_region_with_caps()` sets the low-water mark of a region added after start-up to 0 "so the newly added heap does not artificially inflate minimum free size". Total free: -32 KB + 32 KB, unchanged; total low-water mark: -32 KB + 0. v5.5.2 has no such rule (the region kept its mark), so free and minimum matched there. 32,031 B is the 32,768 B pool less the region's heap overhead.
- Consequence: on v6.1 `min_int` reads about 32 KB below the real low-water mark of the general internal heap, and the DMA pool's own low-water mark is not visible. No memory is lost and nothing to fix in our code; the tests already judge memory by `largest_dma`. Read `min_int` as "real minimum minus about 32 KB" when comparing with v5.5.2 numbers.

### B1: display strip stall — not reproduced since the idle-entry fix and v6.1 (2026-11-07)

Every log in `logs/rc/` (v6.1, 2026-11-06 21:09 on) checked for task-watchdog trips, panics and `TASK_WDT`/`PANIC`/`INT_WDT` resets not caused by `hang`: none from the display. The two `PANIC` resets on record (22:24, 22:34) are the BNO085 first-boot abort fixed in B7 (`Stored core dump: task=main`). Covered: four 10-minute idle/wake runs (about 270 cycles), about 800 tile flips, 5 `scenario state_change`, about 40 boots, and the owner's hands-on tests. The stall known from ADR-002/004 had a trigger in the idle entry (Wi-Fi stop and clock change with a strip in flight), fixed in `d8d78bf`; the one later stall (ADR-002, 15 s after a flash boot) had the same SPI queue wait in its dump. Cause of any remaining stall unknown; the core dump keeps task stacks only, not the SPI/GDMA state. No change now. If it recurs: add a check in the flush that a strip in flight for over 100 ms logs the SPI and GDMA channel state before the watchdog fires, then chase it from that.

### Step 2 done (2026-11-07)

- Fixed and proven on the device: B3, B4, B5, B7 (and its regression), B8, B9, B10 (failure path on the code), B11, B12, B14 (OTA paths on the code), B15, B16, B18 (drop path on the code), B19 (watchdog half), B20, B22, B23, B25 (tare path on the code, not run at the owner's request).
- Checked, not bugs: B6, B24; B13 intentional (owner). Not reproduced on the current firmware: B1, B17. Explained, nothing to fix: B21 (v6.1 heap accounting). Upstream's, kept: B19's GPIO line. Moved to step 3: B2.
- Debug commands added for the proofs: `bnoreset`, `wifi expire`, `wifi drop` (all RAM only).

## Step 3: decouple the services from the UI

### Wi-Fi pilot (2026-11-07)

Design as built, within the approach accepted on 2026-11-06:
- **Wi-Fi service** (`wifi.c`): owns its state (`wifi_status_t`: state, enabled, expired, provisioned), its settings (it loads them itself; the settings page used to, which is what tied Wi-Fi's start to the UI) and one task, `wifi_service`. Every change is a request to that task: bring-up, the user's setting (switch, remote control), suspend/resume (idle, sleep), provisioning reset, expiry, `wifi drop`, and the ESP-IDF Wi-Fi/IP/provisioning events, which the system-event handler only logs and forwards. So one task writes the state, and `esp_wifi_*` is never called with the LVGL lock held. One rule decides the radio: on when the user wants Wi-Fi, the unit is awake and the window has not expired (the switch used to call `esp_wifi_start/stop` on its own terms). The separate expiry task is gone. The service never calls LVGL; after each change it posts `WIFI_SERVICE_EVENT_STATUS_CHANGED` on the default event loop (no payload: readers take `wifi_get_status()`, so they always see the newest).
- **UI bridge** (`ui_bridge.c`, new): the one place that turns service events into UI updates under the LVGL lock. It holds the Wi-Fi status as four LVGL subjects (`lv_subject_t`, the observer module, first use on this unit) and sets them from `wifi_get_status()`. The status bar icon, the settings page's status line, the Enable switch, the Reset provision button and the provisioning note bind to them as observers when they are created and are updated in LVGL context from then on. B2's Wi-Fi half is fixed by this: the icon is set with `lv_label_set_text` under the lock, not written into a static buffer from the system event task. The provisioning reset's result comes back as an event and the bridge shows it in the message box.
- **Low power** calls `wifi_request_suspend(true/false)`, which returns once the service task has stopped or started the radio. Not the event the approach named: B8 already removed Wi-Fi's read of the idle flag, so the cycle is gone and the dependency is one-way; and light sleep needs the radio off before `esp_light_sleep_start()`, which a posted event cannot promise. The call is made outside the LVGL lock, as before.
- **Start-up order:** `wifi_init()` now runs before the display; `ui_bridge_init()` runs under the LVGL lock before the widgets are created and reads the current status.
- Cost: the service task's 4 KB stack is in internal RAM (it writes NVS through the provisioning reset; a PSRAM stack cannot run flash operations), while the old expiry task's stack was in PSRAM: free internal RAM after boot about 181,900 B (was about 186,700), `largest_dma` 102,400 B.
- Behaviour changes, small: the remote control's `wifi on/off` answers `@ok` at once (a start/stop error is logged by the service) instead of reporting the `esp_wifi_*` result; Wi-Fi logs `WiFi: Status: ...` (was `WiFiConfig: Status: ...`).

Proof:
- Boot (`rc_s3_boot_*`): `Status: not connected` and `connecting` at 1.4 s, `UI bridge ready, Wi-Fi status: connecting` at 1.8 s (the bridge came up after Wi-Fi had started), connected at 5.0 s; the level reports.
- `scenario state_change` twice, PASS, no error or warning lines (`rc_state_change_20261107_010937.log`, `..._011449.log`): three idle/wake cycles and the switch off/on, one radio stop and one start each.
- `wifi drop` reconnects (3.6 s); switch off/on and idle/wake reconnect in about 4 s; reboot clean (`rc_wifi_drop_*` 01:1x).
- `wifi expire`: `Status: connect expired`, `wifi on` answers `@err expired`, three idle/wake cycles then only log suspend/resume, no radio start, no error; reboot brings Wi-Fi back (`rc_wifi_expire_*`, `rc_wifi_expire_idle_*` 01:13).
- Not tested on the device: provisioning (the unit is provisioned; testing it means wiping the credentials), the provisioning reset, and the look of the status bar icon and settings page (needs the owner's eye).

Found on the way, runner: `scenario state_change` ended on a fixed `rot 2` + `save system`, so it overwrote the owner's saved rotation (1 at the time; restored with `rot 1` + `save system`). It now turns to another rotation and back to the one the unit had.

Wi-Fi life cycle, added to the pilot (owner, 2026-11-07):
- **The expiry keeps the stack** (owner: "keep the Wi-Fi stack so new features never ignore its memory"). It stops the radio and shows the expired state; switching Wi-Fi on (switch or `wifi on`) starts a new window. `esp_netif_deinit()` was a no-op anyway (v6.1 returns `ESP_ERR_NOT_SUPPORTED`); the teardown that freed memory was `esp_wifi_deinit()`, about 26 KB internal and 23 KB PSRAM, now held for good so memory readings always include Wi-Fi. Measured: `wifi expire` leaves free internal RAM unchanged (179,071 against 179,023 B); `wifi on` then reconnects in 3.6 s without a reboot (`rc_wifi_revive_20261107_013338.log`).
- **Provisioning reset without a reboot** (the reboot was our code's limitation, not the hardware's: the manager was de-initialised once provisioned and nothing started it again). The service stops the radio, erases the credentials, starts the provisioning manager again with a new proof of possession and a new window; the page rebuilds the QR code. A reset while already waiting for credentials only gives a new window.
- **While waiting to be provisioned, idle leaves the radio on** (it cut the SoftAP the phone was using; the old code did the same). Light sleep still stops it: `wifi_request_suspend()` takes the power state (none, idle, sleep). The service drives the station only once provisioned; before that the provisioning manager does (no more `ESP_ERR_WIFI_SSID` on every wake, no double connect attempts during provisioning).
- Owner's provisioning tests (`rc_owner_watch_20261107_014154.log`): first try failed, the phone could not join the SoftAP. Cause: three Reset presses; the second and third stopped the radio and failed to start provisioning again ("already started"), leaving the radio off, and each made a new PoP for the QR code while the running provisioning kept the first. Fixed: the service tracks whether provisioning runs (a reset then only gives a new window), and the PoP shown changes only once provisioning has started with it. Then, without a reboot: five repeated presses, all harmless; provisioning from the unprovisioned unit; a full reset from the provisioned unit and provisioning again; both connected. Reboot afterwards: connected with the new credentials (`rc_s3_final_boot_*`).
- Two bugs seen in that log, both older than this ADR, fixed: provisioning's last events (credentials accepted, provisioning ended) came after the IP address and restarted the expiry timer, so a just-provisioned unit would expire 10 minutes later while connected (the window now runs only while not connected); and "provisioning ended" overwrote the link state with "provisioned" (now kept when connected; with the new status-bar binding it showed the loop icon). Not yet seen on a new provisioning: shown by the code and the log order only.
- Final pilot build, 10 minutes of idle/wake (`rc_idle_cycles_20261107_014616.log`): 54 cycles, PASS, no trips, no error or warning lines; Wi-Fi reconnected after each wake; free internal RAM 181,835 to 181,907 B, `largest_dma` 102,400 B throughout.

### PMIC service (2026-11-07)

Design, as for Wi-Fi:
- **PMIC service** (`pmic_axp2101.cpp`): after init, the monitor task is the only code that talks to the AXP2101. It keeps its readings (`pmic_get_status()`, a copy under a spinlock, with a `valid` flag) and posts `PMIC_SERVICE_EVENT_STATUS_CHANGED` when a reading changes, and `..._POWER_KEY_LONG_PRESS` for the power menu. It no longer calls LVGL or checks `lvgl_display_is_ready()`. Power off is a request, `pmic_request_power_off()` (an event bit the task waits on, like B15's charge settings): the Power Off button (LVGL task) and the sleep timeout (low-power task) used to write to the chip from their own tasks while the monitor task read it with no lock.
- **UI bridge:** two more subjects, `ui_battery_level` (int, 101 = USB icon) and `ui_pmic_status` (a pointer to the bridge's copy of the readings, notified when it changes). The status bar's battery icon binds to the first with `lv_label_set_text` (fixes B2's battery half: it was copied into a static buffer with no refresh); the settings page's two status labels bind to the second. The power menu is shown by the bridge.
- `pmic_axp2101.h` no longer includes LVGL; the page's functions moved to `pmic_config.h`. Settings storage stays in `pmic_config.c` (it touches no LVGL; moving it is churn).
- Small behaviour change: the battery icon shows the real level from the first frame (init takes a reading before the UI exists); it used to show the USB icon until the first reading 2 s later. On USB power, as on this unit, it is the USB icon either way.
- Tool: the remote control's `pmic` (observer, like `stats`) answers the service's reading next to the bridge's values: `@ok pmic valid soc vbat vbus vsys usb chg icon shown_vbat`.

Proof (`rc_pmic_20261107_015837.log`):
- Reboot: the PMIC starts before Wi-Fi and the UI; `UI bridge ready` at 1.8 s; only the two known boot lines (BNO085 reset report, GPIO ISR service); the level reports; Wi-Fi connects.
- 12 `pmic` reads over 40 s, awake, idle and after wake: the icon value matched the reading every time (USB, 101); the voltage the page shows matched the newest reading every time; five different battery voltages seen, so the updates flow.
- `scenario state_change` PASS, no error or warning lines (`rc_state_change_20261107_015947.log`); rotation back to the owner's 1.
- Not tested: Power Off and the sleep timeout's power off (they turn the unit off, and nobody was there to turn it back on); the power key's long press (needs a finger); the look of the icon and the page.
- Known, for the low-power change: a power key press before the UI exists would read the low-power event group, which the low-power view creates (`create_low_power_mode_view`), while it is still NULL.

### Low power: state and tasks out of the view (2026-11-07)

The low-power event group and both tasks (the idle/sleep monitor and the stability detector) were created inside `create_low_power_mode_view()`, a view's create function, so anything asking `is_idle_mode_activated()` before the UI existed read a NULL event group (the review's R finding; the PMIC's power key could do it in the first 1.8 s of boot). Now:
- `low_power_mode_init()` creates the event group right after the default event loop, before the PMIC and every other caller.
- `low_power_mode_start()` installs the touch wake wrapper (under the LVGL lock) and starts both tasks, called from `app_main` once the display and its touch input exist, before the remote control. Same effective order as before, now explicit.
- The view only builds the black tile.
- Not changed: low power still drives the display itself (LVGL stop and resume, panel off, watchdog pause) and tells Wi-Fi and the BNO085 directly. See "For the owner" below.

Proof:
- Reboot (`rc_pmic_20261107_020628.log`): only the two known boot lines; the level reports; idle entry and wake through the remote control work.
- `scenario state_change` PASS, no error or warning lines (`rc_state_change_20261107_020720.log`).
- 10 minutes of idle/wake (`rc_idle_cycles_20261107_020945.log`): 54 cycles, PASS, no trips, no error or warning lines; free internal RAM 181,271 to 181,347 B, `largest_dma` 102,400 B.
- Not tested: the touch wake wrapper (needs a finger; the same two calls, now made at start-up instead of in the view), and the sleep timeout (1 hour at the owner's setting).

For the owner (README, "For the owner to decide", items 4 and 5): BNO085 needs no service change, and low power keeps driving the display itself; both are recommendations, not built. If both are accepted, step 3 is complete with the three changes above.

### Step 3 done (2026-11-07)

The owner accepted both recommendations ("Accept both"): no BNO085 service change, and low power keeps its display calls. Step 3 is the Wi-Fi pilot, the PMIC service and the low-power start-up change, plus removing the display readiness check they left unused.

Still to check by hand (needs the owner): the power-key long press opens the power menu; Power Off and the sleep-timeout power-off; a tap wakes the unit without pressing a widget; the battery icon and the Power Management page look right; the Wi-Fi status after a new provisioning.

## Step 4: the remaining code issues (approved 2026-11-07, "Approve, start")

### C2: BNO085 report index — checked, not a bug (2026-11-07)

`sh2_sensor_callback` indexes its 60-entry table only after `sh2_decodeSensorEvent()` succeeded, and the decoder returns `SH2_ERR` for any report ID it has no case for (`sh2_SensorValue.c`, `default:`); its highest case is `0x2E` (46). A corrupted packet either carries a known ID, in bounds, or is dropped before the index. `bno085_configure_report` is only called with the five constant IDs. The review missed the decoder's check. Left for C4: the table is sized with `SH2_MAX_SENSOR_EVENT_LEN` (a byte length, 60) where the sensor-ID count (`SH2_MAX_SENSOR_ID + 1`, 47) is meant.

### C3: FT3168 I2C timeouts (2026-11-07)

The FT3168 shares I2C0 with the PMIC, and the ESP-IDF I2C driver waits for the bus lock with the caller's timeout (`i2c_master.c`, `s_i2c_synchronous_transaction`). With `-1`, a touch read, which runs in the LVGL task at every input poll, could wait forever behind the bus. Now:
- `FT3168_I2C_TIMEOUT_MS` (100 ms, as the BNO085 and AXS5106 drivers use) for init and both reads.
- A read publishes the point count with its coordinates, after both reads succeed; before, the count was set first, so a failed coordinate read left a count with stale coordinates.
- Both GPIO-config error paths free the handle.

Not changed: XPowersLib (third-party) also passes `-1` on the same bus; a stuck bus would then hang the PMIC task, while touch now times out.

Proof (`rc_c3_20261107_122011.log`): reboot, `FT3168: Successfully wrote to FT3168` at 1.7 s, then only the two known boot lines; 40 s awake with tile flips and one idle/wake, no FT3168 or other error lines (the reads run at every input poll, touched or not). Not tested: a real touch after this change (needs a finger) and a stuck bus.

### C1: runtime errors no longer reboot the unit (2026-11-07)

Rule: an `ESP_ERROR_CHECK` stays where it runs once at start-up (a failure there is fatal by design); after start-up a failure is logged and the action fails where it happened. Recounted on the code: every check left in `main/` is start-up code or sits in a disabled tile (point of aim, acceleration analysis, OpenTrickler) or in the unused `axp2101_deinit`.

- Settings pages (`0c9e5bb`): Save and Reload on the system, Wi-Fi, PMIC, sensor and level pages show "Save failed: <error>" or "Reload failed: <error>" in the page's message box (new `update_info_msg_box_result()` in `config_view.c`); the cause is already logged in `common.c`. The PMIC page applies the charge settings only after a successful save. The system page tries both of its saves and shows the first failure. The countdown presets, saved when the tile is swiped away, log a failed save and stay in RAM.
- Buzzer and backlight (`f9ae095`): LEDC failures are logged (`ESP_ERROR_CHECK_WITHOUT_ABORT`), the buzzer keeps its last duty. The backlight function returns its error; it is the dev kit's (JD9853), the unit's SH8601 path had no check.
- Low power (`262fd94`): `esp_pm_configure` on idle entry and exit, light sleep and the wake-pin setup are logged and the unit carries on in its current state. Light sleep is not reached today (`enter_sleep_mode` has no caller).
- OTA (`a91c6b6`): the update task registers its event handler after the user confirms; a failure now shows "OTA Failed - <error>" and the Reboot button, like the other OTA failures (B14).
- Wi-Fi: kept. `wifi_bring_up` is Wi-Fi's first request at start-up, not a wake path; a wake goes through `wifi_apply_radio`, which already logs and goes on. The step 4 plan listed it as "after a wake"; reading the code corrected that.

Proof: each change built with no new warnings and flashed; `scenario state_change` PASS after the pages (`rc_state_change_20261107_122410.log`) and after the rest (`rc_state_change_20261107_123006.log`), no error or warning lines, rotation back to 1. The failure paths are not forced (no remote-control way to fail an NVS write or an LEDC call without new test code); they rest on reading. Not seen by eye: the message box texts.

### C4: style (2026-11-07)

Two changes:
- `b22aea6`: every shared global is declared once, in the header of the module that defines it; the 68 `extern` lines in `.c` files are gone and each user includes that header, so the compiler checks every use against one declaration. New `app.h` declares the two device handles `app_main` creates (`bno085_dev`, `axp2101_dev`). Found on the way: `sensor_calibration_view.h` had `#ifndef` with no `#define`, so its include guard never worked; fixed.
- `552fe1e`: the one-space and tab-indented function definitions; `PRIu32` where `%d` printed a `uint32_t` (six places; ESP-IDF v6.1's log macros do not check formats, so the compiler never flagged them, and only values above 2^31 would have printed wrong); the BNO085 report table sized by the sensor-ID range (`SH2_MAX_SENSOR_ID + 1`, 47) instead of a byte length (60); `pmic_axp2101.h` declared a `default_power_management_config_t` that does not exist; typos and stale text (the example header and "Initialize USB" in `app.c`, "sayed", "unified_ensor", "Reset Unexpectly", "sh2_decodeSensorEventd", now logged as a warning, the PMIC event-group error naming the BNO085's); `common.c`'s CRC constant renamed for what it is, an initial value (the value stays: every saved setting carries a CRC made with it).

Left as they are, as planned: log tags, commented-out code, dead modules. The review's other notes are already gone: the Wi-Fi expiry log and task name were rewritten in step 3.

Proof:
- Both built with no new warnings (the 13 old unused-variable warnings remain).
- After the second, reboot (`rc_c3_20261107_124121.log`): settings load (so the CRC is unchanged), 21 `EventHandler Sensor Config` lines; `bnoreset` (`rc_bnoreset_20261107_124200.log`), which runs the changed table loop: all three reports re-enabled and confirmed by the sensor. PASS.
- Seen once: on the first software reboot after the flash, boot stalled 0.8 s between two buzzer log lines, before any of the changed code runs; two more reboots (`rc_boot_timing_20261107_124259.log`) and every earlier boot log show no such gap. Cause not known; recorded, not chased.

### Where step 4 stands (2026-11-07)

C1 to C4 done, each committed and checked on the unit's normal path. Still to run: one 10-minute idle/wake run (it blanks the screen, so not while the owner is at the unit), and the owner's hands-on checks, which now also cover a real touch after C3 and a Save on a settings page after C1.

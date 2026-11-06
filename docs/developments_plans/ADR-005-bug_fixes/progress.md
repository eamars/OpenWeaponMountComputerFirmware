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

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

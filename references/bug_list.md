# Bug List (Independently Reviewed)

Date: 2026-06-09
Reviewer: Senior system engineer independent review pass

## Scope and Rule
- This list keeps real code defects even when the original note had incorrect or overstated justification.
- Incorrect justification is corrected in place instead of deleting the item.
- Items are removed only when the surface issue is not a bug under the intended design or relevant firmware scope.
- BNO085 SHTP transport reads intentionally consume the SHTP header and do not return it in the caller buffer.

## Removed Non-Bugs

### [x] B-011: bno08x transport read drops SHTP header bytes (I2C)
- **Reason removed**: intentional SHTP transport behavior. The header is consumed by the transport layer to determine packet length and is not intended to be returned in the caller payload buffer.

### [x] B-012: bno08x transport read drops SHTP header bytes (SPI)
- **Reason removed**: same intended SHTP transport behavior as B-011. The SPI path is also not active in the current firmware configuration.

## Bug Log (Re-ordered)

### [ ] [P1] B-001: Wi-Fi state expiry condition is logically inverted
- **File / Lines**: `main/wifi.c:310`
- **Issue**: the condition uses `||` where `&&` is required:
  - `wireless_state != WIRELESS_STATE_PROVISION_EXPIRE || wireless_state != WIRELESS_STATE_NOT_CONNECTED_EXPIRE`
- **Why bug**: one state cannot equal both values at once, so the condition is always true.
- **Impact**: Wi-Fi disable logic can run in states that were intended to be excluded.
- **Fix**: replace `||` with `&&`.

### [ ] [P2] B-013: FT3168 reset GPIO setup uses config before copying it
- **File / Lines**: `components/esp_lcd_touch_ft3168/esp_lcd_touch_ft3168.c:127-180`
- **Issue**: reset GPIO setup reads `touch_handle->config.*` before `memcpy(&touch_handle->config, config, ...)`.
- **Why bug**: `touch_handle` is zero-initialized, so this is not random uninitialized memory, but it still uses the wrong config source. The configured reset pin and reset level are ignored during reset setup.
- **Impact**: FT3168 reset behavior can be skipped or configured incorrectly.
- **Fix**: copy `config` into `touch_handle->config` before any use of `touch_handle->config.*`, or use `config->...` directly during early setup.

### [ ] [P3] B-008: Digital level color mapping leaves gaps in angle coverage
- **File / Lines**: `main/digital_level_view_type_2.c:84-110`
- **Issue**: roll angle thresholds do not fully partition the range.
- **Why bug**: uncovered bands and exact threshold boundaries perform no LED state assignment.
- **Impact**: stale LED colors can persist when roll moves into an uncovered band.
- **Fix**: make the threshold chain exhaustive with explicit handling for every angle band and a final fallback.

### [ ] [P4] B-007: Sensor-report type mismatch for POA flow
- **File / Lines**: `main/point_of_aim_view.c:88`, `main/point_of_aim_view.c:118-129`, `main/point_of_aim_view.c:255`
- **Issue**: POA reads and enables the game rotation vector report, but guards the enable/disable path with `sensor_config.enable_rotation_vector_report` and initializes the regular rotation vector report.
- **Why bug**: the POA feature mixes two report families. If POA is enabled, the config gate can prevent the report actually being used by the poller, or initialize/disable the wrong report type.
- **Impact**: POA can use an unintended or unavailable orientation data source.
- **Fix**: choose the intended POA report family and make the config gate, initialization, enable, disable, and read paths use the same report type.
- **Scope note**: POA view creation is currently commented out in `main_tileview.c`, so this is a real code bug in an inactive UI path.

### [ ] [P5] B-005: Low-power input callback swap lacks null safety
- **File / Lines**: `main/low_power_mode.c:426-428`
- **Issue**: `lv_indev_get_read_cb(lvgl_touch_handle)` and `lv_indev_set_read_cb(lvgl_touch_handle, ...)` are called without validating `lvgl_touch_handle`.
- **Corrected justification**: normal startup order creates the LVGL touch handle before creating the low-power view, so the original startup-race explanation is not supported. The real defect is lack of defensive handling if `lvgl_port_add_touch()` fails or returns null.
- **Impact**: null dereference if touch input registration fails.
- **Fix**: check the return value of `lvgl_port_add_touch()` during display init and guard the low-power callback swap.

### [ ] [P6] B-003: OTA task/event-group lifecycle handles are not reset or reclaimed
- **File / Lines**: `main/ota_mode.c:33`, `main/ota_mode.c:42-43`, `main/ota_mode.c:377`, `main/ota_mode.c:535`, `main/ota_mode.c:611-631`
- **Issue**: OTA tasks self-delete, but their task handles are not reset. The OTA event group is created lazily and never deleted/reset.
- **Corrected justification**: normal tile entry does not recreate the OTA view, so the original repeated-navigation leak explanation is overstated. The real defect is incomplete ownership cleanup if OTA/UI lifecycle is ever recreated or restarted.
- **Impact**: stale handles and persistent event bits can make future OTA lifecycle handling unsafe or misleading.
- **Fix**: reset task handles before task exit and define explicit ownership for deleting or preserving `ota_event_group`.

### [ ] [P7] B-002: OTA view creation is not idempotent
- **File / Lines**: `main/ota_mode.c:566`, `main/ota_mode.c:611-631`
- **Issue**: `create_ota_mode_view()` always creates OTA poller/update tasks and prompt UI without guarding prior creation.
- **Corrected justification**: normal OTA view entry does not call this function, so the original “re-entering the view” explanation is wrong. The real issue is that the create function is non-idempotent despite owning persistent static task/UI state.
- **Impact**: duplicate tasks/UI objects if the main UI is recreated or `create_ota_mode_view()` is called more than once.
- **Fix**: either document/enforce one-shot creation or guard creation using existing object/task handles.

### [ ] [P8] B-010: Wi-Fi deinit path does not fully reset lifecycle-owned objects
- **File / Lines**: `main/wifi.c:69-107`, `main/wifi.c:205-208`, `main/wifi.c:211-320`
- **Issue**: the deinit task stops/deinitializes Wi-Fi and network stack, but event handlers, timer ownership, event-group state, and task handle state are not symmetrically reset.
- **Corrected justification**: this is not proven as a normal repeated-init bug from current call sites. The real issue is incomplete lifecycle ownership for recovery/reinitialization flows.
- **Impact**: stale callbacks, stale event bits, timer reuse hazards, or invalid state if Wi-Fi initialization is attempted again after expiry deinit.
- **Fix**: define Wi-Fi lifecycle states explicitly and make init/deinit idempotent, including event handler unregister, timer cleanup/reset, event-bit cleanup, and task handle reset where appropriate.

### [ ] [P9] B-018: XPowers legacy I2C init ignores setup return codes
- **File / Lines**: `components/XPowersLib/src/XPowersCommon.hpp:263-267`
- **Issue**: the legacy ESP-IDF I2C path calls `i2c_param_config()` and `i2c_driver_install()` without checking return values.
- **Why bug**: failed I2C setup can be silently ignored before `initImpl()` runs.
- **Impact**: PMIC initialization failures can become opaque on legacy ESP-IDF builds.
- **Fix**: check setup return values and return `false` on failure.
- **Scope note**: current firmware config sets `CONFIG_XPOWERS_ESP_IDF_NEW_API=y`, so this is not active in the current build, but it remains a real compatibility-path bug.

### [ ] [P10] B-004: PMIC status update is not null-safe
- **File / Lines**: `main/pmic_config.c:187-189`, `main/pmic_config.c:248`
- **Issue**: `power_management_view_update_status(axp2101_dev)` eventually dereferences `ctx` without validating it.
- **Corrected justification**: current config has `USE_PMIC=1`, and PMIC view creation is guarded by `#if USE_PMIC`, so the original “PMIC disabled build” explanation is not the active failure path. The real defect is that the status update API itself has no null contract enforcement.
- **Impact**: null dereference if PMIC allocation/init fails before config view creation, or if the function is reused with a null context.
- **Fix**: either enforce non-null with earlier fatal init checks and document the contract, or add a runtime null guard before dereferencing `ctx`.

### [ ] [P11] B-006: DoPE config view allocation is not idempotent
- **File / Lines**: `main/dope_config_view.c:307-314`
- **Issue**: `create_dope_config_view()` allocates `all_dope_data` without freeing or reusing an existing allocation.
- **Corrected justification**: normal tile re-entry does not call this function, so the original repeated-navigation leak explanation is wrong. The real issue is non-idempotent create-time allocation on persistent static state.
- **Impact**: heap leak if the main UI is recreated or `create_dope_config_view()` is called more than once.
- **Fix**: either document/enforce one-shot creation or free/reuse the existing `all_dope_data` allocation before allocating again.

### [ ] [P12] B-009: Wi-Fi status update depends on UI creation order
- **File / Lines**: `main/app.c:171-175`, `main/config_view.c:407-429`, `main/wifi.c:211-217`
- **Issue**: Wi-Fi init emits status updates through UI-facing functions.
- **Corrected judgement**: current startup order creates the LVGL UI before `wifi_init()`, so the original claim that startup updates happen before UI readiness is incorrect.
- **Why still tracked**: the Wi-Fi module directly calls status/UI update functions and depends on app initialization order rather than a clear readiness boundary.
- **Impact**: future startup-order changes or headless/non-UI Wi-Fi use can break status updates.
- **Fix**: either keep the current ordering as an explicit contract or make status emission tolerant of UI not being ready.

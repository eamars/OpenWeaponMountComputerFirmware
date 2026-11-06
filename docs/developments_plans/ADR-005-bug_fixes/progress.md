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

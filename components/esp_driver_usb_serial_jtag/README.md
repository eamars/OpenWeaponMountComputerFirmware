# esp_driver_usb_serial_jtag (patched copy)

Copy of the ESP-IDF v5.5.2 component of the same name (`components/esp_driver_usb_serial_jtag`, without
`test_apps`). A project component with an IDF component's name replaces the IDF one in the build.

One change, in `src/usb_serial_jtag_connection_monitor.c` (`usb_serial_jtag_sof_tick_hook`): the
"no SOF" tolerance (3 ticks) is refilled on every tick that sees a SOF while connected. Upstream (v5.5.2
and master as of 2026-11-06) refills it only on a disconnected-to-connected transition, so after three
delayed FreeRTOS ticks every later delayed tick marks the port disconnected for one tick.
`usb_serial_jtag_write()` returns -1 for a write made in that tick and newlib drops the rest of the
`printf`: console lines and remote-control replies vanish with the host attached.
Evidence: `docs/developments_plans/ADR-004-remote_control/progress.md`.

When upgrading ESP-IDF (ADR-003): if the new release refills the tolerance while connected, delete this
directory; otherwise copy the new release's component here again and reapply the change.

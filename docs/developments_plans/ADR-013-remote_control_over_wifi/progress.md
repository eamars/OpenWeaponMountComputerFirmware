# ADR-013 progress

Steps 1 to 6 on the COM11 unit, powered by USB, 2026-11-09. It was `owmc-83a124` then, and is `owmc-8626470` since step 7; 192.168.2.150 both times. Step 7 on the same unit mounted on the turret, on battery, with no cable. Approved by the owner with no password on the network control.

## Step 1: commands send their extra lines through the transport

- `remote_control_execute()` takes a writer (`remote_control_write_fn`) for the lines a command sends before its reply. Only `snap` sends any. The serial transport's writer prints them, as before.
- **Proof:** a snap of the Dope tile over USB is pixel-identical to one taken before the change.

## Steps 2 and 3: the log stream and the network transport (`main/tools/rc_net.c`)

The log stream can only be proven through the network transport, so they were built and checked together.

**The log:**
- A hook on the ESP-IDF log (`esp_log_set_vprintf`) still prints every line to the console. While the switch is on, it also copies each line into a 16 KB ring in PSRAM.
- When the ring is full, the oldest lines go, so it always holds the newest. Dropped lines are counted and reported to the client as `@drop <n> log lines`.
- The logger never waits on the network: formatting takes a mutex for at most 10 ms, and is skipped (and counted) from interrupts or with the scheduler stopped.
- What the unit logs while no client is connected stays in the ring: after reconnecting, the client reads what happened during the idle.

**The transport:**
- A task (`rc_net`, 5120 B stack in PSRAM) listens on TCP port 3333 while the switch is on and the unit has an address. It serves one client at a time; a new client replaces the old one.
- Command lines are read as on the console. Replies, snap lines and the log come out as console lines, and the log written so far goes out before each reply, so lines arrive in the order they happened.
- With no client it blocks in `select()` with no timeout, so it never wakes the chip out of light sleep. A loopback datagram wakes it when the switch or the network changes. With a client connected it polls every 50 ms.
- It tracks the address itself, from the IP and Wi-Fi events. The Wi-Fi service's "connected" bit follows them only a moment later from its own task: the first build waited on that bit and listened again for a moment while the radio was stopping at each idle. The bit is now used only to know mDNS has started.
- mDNS: the Wi-Fi service names the unit `owmc-` plus the last three bytes of its station MAC address, as both host and instance name. The transport announces `_owmc-rc._tcp` on port 3333 while it listens.

## Step 4: `idle <seconds>`

- Idle, then a one-shot timer wakes the unit after that time, with the wake cause "timer". `idle` with no argument works as before.
- Over Wi-Fi this is how a scenario idles: the radio stops with the idle, so no `wake` command can reach the unit.

## Step 5: the switch

- "Over Wi-Fi" on the Advanced page, shown while Remote control is on. It is off by default.
- `remote_control_config_t` gains `wifi_enable`, appended; older saved settings load through `load_config_grown` with it off. The serial switch's saved value is unchanged.

## Step 6: the host

- `rc_run.py` and `ui_snaps.py` take `--host <address or name.local>`. `rc_run.py find` lists the units announcing the service.
- `NetPort` has the serial port's read and write calls, and connects again after a drop.
- `state_change` over Wi-Fi idles with `idle 20` and connects again after each, and skips Wi-Fi off and on, which would cut its own link.

## Checks

| What | Result |
|---|---|
| `state_change` over USB, switch on, no client (`rc_state_change_20261109_191918.log`) | PASS, largest DMA block at least 77,824 B |
| `state_change` over Wi-Fi, three `idle 20` (`rc_state_change_20261109_192341.log`) | PASS, largest DMA block at least 90,112 B. Each idle's log lines arrived after the reconnect, and no lines were dropped |
| `state_change` over USB on the final build, switch off (`rc_state_change_20261109_193517.log`) | PASS, largest DMA block at least 77,824 B |
| Idle over USB with the switch on (after the address fix) | Stops listening with the idle; listens again 3.7 s after the wake, once mDNS has its name |
| 20 snaps over Wi-Fi | 20 of 20 whole, median 0.35 s (0.33 to 0.39) |
| 10 snaps over USB, same screen | 10 of 10 whole, median 0.44 s; picture identical to the Wi-Fi one |
| `ui_snaps.py` capture over Wi-Fi against over USB, 26 screens | Identical except live values: the Power page's readings, and the level tile's full-screen colour, which follows the desk's tilt |
| CPU with a client connected (run-time stats in the local `sdkconfig` only, then off again) | `rc_net` 0.0%; the idle tasks 40.2% and 58.7%, against 39.6% and 58.6% over USB with no client |
| `rc_net` stack | 840 B left of 4096 after snaps and `stats`: raised to 5120 |
| Internal RAM, switch on and off | 166,671 and 166,559 B free: no difference beyond noise |
| Switch off | Port 3333 refuses connections; `find` lists no unit |
| Build | No warning from our code |

The switch was turned on for these checks and left off afterwards.

## Step 7: finding a unit, telling units apart, two switches (the owner's review of PR #13)

**Built:**
- `unit_number()` in `common/`: 7 digits from the Bluetooth MAC address, as the Kestrel HUD name has used since ADR-007. Every name now carries it:
  - `HUD-<n>` (unchanged);
  - `OWMC_<n>`, the set-up access point;
  - `owmc-<n>`, the DHCP host name and the mDNS host and instance name.

  Before, the set-up name and the DHCP name were `OWMC_` plus the Wi-Fi MAC address, and the mDNS name was `owmc-` plus the same MAC: four names, none tied to the HUD name.
- **The Wi-Fi page:** "Network name" and "Address" while connected, each as a label line over its value. The first build put each on one line ("Name: owmc-8626470.local"), which broke mid-word in both rotations.
- **The mDNS announcement** carries the firmware version, and `rc_run.py find` prints it.
- **Two switches:** "USB remote control" and "Wi-Fi remote control", each on its own. The stored layout is unchanged.
  - The first labels, "Remote control over USB" and "… over Wi-Fi", took three lines. The new ones take two, except "Wi-Fi remote control" in portrait, which takes three and still sits inside its row.
- `AGENTS.md`: the switches, the names, and how to update a unit that has no cable.

**Put on the unit by OTA over the remote control,** twice (`v1.2.0-14-ge283583`, then the layout fix `v1.2.0-15-ga1f5c6d`):
1. `ota_host.py --force --importance 1` serves the build.
2. `reboot` over Wi-Fi; about 13 s later the unit asks for the manifest.
3. The offer shows; `tap` Install.
4. 2398 K is downloaded and written in about 25 s, then "Update installed".
5. `tap` Restart.

The first time, the OTA host was still running when the unit restarted, so the unit was offered the same build again, and its offer covered every page in the next snaps (Cancel by `tap`). The second time, the host was stopped before Restart. `AGENTS.md` says so.

**Checks** (on battery, `usb_power=0`):

| What | Result |
|---|---|
| `find` after each restart | `owmc-8626470.local  192.168.2.150  v1.2.0-15-ga1f5c6d` |
| Kestrel page | "HUD-8626470", unchanged |
| Wi-Fi page, rotations 3 and 0 | Network name `owmc-8626470.local` and Address `192.168.2.150`, each under its label |
| Advanced page, rotations 3 and 0 | "USB remote control" and "Wi-Fi remote control", both on (as stored before), each its own switch |
| `state_change` over Wi-Fi (`rc_state_change_20261109_212039.log`) | PASS, largest DMA block at least 90,112 B; three timed idles, each reconnected |
| `sleep` afterwards | idle 58.1 s on battery, asleep 57.5 s (99.1%), 493 timer wakes: about 8.5 a second, against ADR-009's "about 10 a second" |
| Build | No warning from our code |

**First reading with the turret** (for ADR-014): turret pitch turns into the unit's cant. +4.90° of encoder moved cant from −91.01° to −95.37° (4.36°), and back to −90.59° (4.78°); the unit's own pitch stayed within 0.1°. These are single readings 3 s after each step: accuracy and settling are ADR-014's to measure.

## Found on the way, for ADR-014

- **A unit lying still can wake itself.** The stability poller (`sensor_stability_detector_poller_task` in `low_power_mode.c`) wakes the unit from idle on any report. That includes bit 0, "entered the still state".
  - After every wake, and sometimes right after an idle starts, the BNO085 sends such a report about 0.5 s later.
  - In the Wi-Fi run, the second `idle 20` ended after 0.5 s: "Stability detector report: 1", then "Exiting idle mode".
  - That was 1 of 6 idles across the first two runs (three ended by `wake` over USB, three by the timer); the other five ran their full 20 s.
  - It is the "no wake without a cause" item ADR-009 watched for. Only leaving the still state means movement.
- **An idle unit cannot be reached over Wi-Fi.** Its radio is off until something wakes it: a touch, the button, movement, or `wake` over USB. On the turntable, movement can wake it.

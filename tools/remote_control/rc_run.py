"""Host runner for the serial remote control (ADR-004).

Drives the device over its USB Serial/JTAG console, records the whole session to a log
file, counts failure markers and prints a verdict.

Usage (repo venv):
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py send "state"
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py scenario state_change --min-lvgl-stack 2048 --min-largest-dma 80000

Opening the port may reset the board. After opening, the runner sends `state` every 2 s until
the device answers, so the boot is part of the session and its markers are counted. During that
boot, `rst:` lines are expected, and one abort right after "Failed to detect BNO085" is allowed.
"""

import argparse
import datetime
import pathlib
import sys
import time

import serial

DEFAULT_PORT = "COM11"
BAUD_RATE = 115200
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_LOG_DIR = REPO_ROOT / "build" / "rc_logs"

COMMAND_TIMEOUT_S = 5.0
READY_TIMEOUT_S = 60.0
READY_PROBE_PERIOD_S = 2.0
STATS_PERIOD_S = 30.0
IP_TIMEOUT_S = 90.0
READ_POLL_S = 0.05
HOST_RX_QUEUE_BYTES = 1 << 20   # Windows receive queue requested after open

IP_ADDRESS_MARKER = "Connected with IP Address:"
BNO085_DETECT_MARKER = "Failed to detect BNO085"
ABORT_MARKER = "abort() was called"
RESET_MARKER = "rst:0x"
WATCHDOG_TRIGGER_MARKER = "Task watchdog got triggered"
WATCHDOG_TRIGGER_TIMEOUT_S = 15.0   # the task watchdog timeout is 5 s
CORE_DUMP_SAVED_MARKER = "Core dump has been saved to flash."
CRASH_MARKERS = (RESET_MARKER, "Guru Meditation", ABORT_MARKER)  # expected in the watchdog scenario

# Every @ok reply starts with the command's own prefix, so a stale reply cannot answer the next command
REPLY_PREFIXES = {
    "help": "@ok help",
    "state": "@ok tile=",
    "stats": "@ok free_int=",
    "bt": "@ok bt",
    "tap": "@ok tap",
    "swipe": "@ok swipe",
    "tile": "@ok tile=",
    "rot": "@ok rot=",
    "wifi": "@ok wifi=",
    "save": "@ok save",
    "idle": "@ok idle",
    "wake": "@ok wake",
    "reboot": "@ok reboot",
    "hang": "@ok hang",
}
FAILURE_MARKERS = (
    RESET_MARKER,
    "Guru Meditation",
    ABORT_MARKER,
    "ESP_ERR_NO_MEM",
    "queue) color failed",
    "stack overflow",
    "alloc_failed:",
)

LVGL_TASK_NAME = "taskLVGL"

# Tiles a user can enter (main_tileview.c); (0,0) low power and (0,1) OTA are entered automatically only
ENTERABLE_TILES = ((2, 0), (2, 1), (2, 2), (3, 1), (0, 2))
HOME_TILE = (2, 1)
TILE_DWELL_S = 3.0
IDLE_CYCLES = 3
IDLE_DURATION_S = 20.0
AFTER_WAKE_S = 10.0
AFTER_ROTATION_S = 5.0
WIFI_OFF_S = 10.0
ROTATION_DWELL_S = 120.0
KEEP_AWAKE_S = 20.0       # re-select the home tile this often during long dwells
WATCH_PERIOD_S = 10.0     # watch scenario: one `state` check this often
READY_DRAIN_S = 1.0       # after the first answer, replies to earlier probes may still arrive
FINAL_PUMP_S = 5.0        # keep reading this long after a scenario's last command


class Session:
    """One serial session: port, log file, reply matching, marker counting and readings."""

    def __init__(self, port: str, log_path: pathlib.Path):
        """Open the log file and the serial port without asserting DTR or RTS.

        Args:
            port: Serial port name, e.g. COM11.
            log_path: File that receives every line sent and received, with local timestamps.
        """
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path = log_path
        self.log = open(log_path, "w", encoding="utf-8")

        # Configure before open so pyserial never raises DTR/RTS (which can hold the chip in reset)
        self.port = serial.Serial()
        self.port.port = port
        self.port.baudrate = BAUD_RATE
        self.port.timeout = 0
        self.port.dsrdtr = False
        self.port.rtscts = False
        self.port.dtr = False
        self.port.rts = False
        self.port.open()
        if hasattr(self.port, "set_buffer_size"):
            # pyserial asks Windows for a 4 KB receive queue on open; a burst larger than that
            # between two reads stalls the USB link and the device drops console output
            self.port.set_buffer_size(rx_size=HOST_RX_QUEUE_BYTES, tx_size=4096)

        self.rx_buffer = b""
        self.lines = []           # every received line, in order
        self.replies = []         # received @ok/@err lines not yet matched to a command
        self.failures = []        # verdict reasons
        self.markers = []         # (marker, line) of every counted failure marker
        self.booting = True       # until the device first answers `state`
        self.bno085_detect_seen = False
        self.bno085_abort_allowed = True
        self.min_stack = {}       # task name -> lowest free stack seen (B)
        self.min_largest_dma = None
        self.last_stats_time = None

    def close(self):
        """Close the port and the log file."""
        self.port.close()
        self.log.close()

    def _write_log(self, direction: str, text: str):
        """Append one timestamped line to the session log."""
        stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log.write(f"{stamp} {direction} {text}\n")
        self.log.flush()

    def _check_markers(self, line: str):
        """Count failure markers in one received line, allowing the known boot ones."""
        if BNO085_DETECT_MARKER in line:
            self.bno085_detect_seen = True

        for marker in FAILURE_MARKERS:
            if marker not in line:
                continue
            if self.booting and marker == RESET_MARKER:
                continue  # opening the port resets the board
            if (self.booting and marker == ABORT_MARKER and self.bno085_detect_seen
                    and self.bno085_abort_allowed):
                self.bno085_abort_allowed = False  # the one known abort on first boot after a reset
                continue
            self.markers.append((marker, line))

    def pump(self, duration_s: float):
        """Read and process received lines for duration_s seconds (0 reads what is waiting)."""
        deadline = time.monotonic() + duration_s
        while True:
            chunk = self.port.read(4096)
            if chunk:
                self.rx_buffer += chunk
                *complete, self.rx_buffer = self.rx_buffer.split(b"\n")
                for raw in complete:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r")
                    self._write_log("<", line)
                    self.lines.append(line)
                    self._check_markers(line)
                    if line.startswith("@ok") or line.startswith("@err"):
                        self.replies.append(line)
            if time.monotonic() >= deadline:
                return
            time.sleep(READ_POLL_S)

    def _reply_matches(self, line: str, reply: str) -> bool:
        """True when reply can be the answer to command line (any @err counts)."""
        if reply.startswith("@err"):
            return True
        cmd = line.split()[0] if line.split() else ""
        prefix = REPLY_PREFIXES.get(cmd)
        if prefix is None or not reply.startswith(prefix):
            return False
        # `state` and `tile` share a prefix; only `state` reports the rotation
        if cmd == "state":
            return " rot=" in reply
        if cmd == "tile":
            return " rot=" not in reply
        return True

    def _exchange(self, line: str, timeout_s: float):
        """Send one command line and return its reply line, or None on timeout."""
        # One outstanding command at a time: anything left over belongs to an earlier command
        self.pump(0)
        for stale in self.replies:
            self._write_log("!", f"unmatched reply dropped: {stale}")
        self.replies.clear()

        self._write_log(">", line)
        self.port.write((line + "\n").encode("ascii"))
        self.port.flush()

        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self.pump(READ_POLL_S)
            while self.replies:
                reply = self.replies.pop(0)
                if self._reply_matches(line, reply):
                    return reply
                self._write_log("!", f"reply not matching '{line}' dropped: {reply}")
        return None

    def wait_ready(self) -> bool:
        """Send `state` every 2 s until the device answers @ok, for up to 60 s.

        Returns:
            True when the device answered; False (and a verdict reason) otherwise.
        """
        deadline = time.monotonic() + READY_TIMEOUT_S
        while time.monotonic() < deadline:
            reply = self._exchange("state", READY_PROBE_PERIOD_S)
            if reply is not None and reply.startswith("@ok"):
                self.booting = False
                print(f"ready: {reply}")
                # Probes sent while the device booted may be answered in a burst; drop those answers
                self.pump(READY_DRAIN_S)
                for stale in self.replies:
                    self._write_log("!", f"reply to an earlier probe dropped: {stale}")
                self.replies.clear()
                return True
        self.failures.append(f"device not ready within {READY_TIMEOUT_S:.0f} s")
        return False

    def command(self, line: str) -> str:
        """Send one command, record a failure unless it answers @ok, and return the reply.

        Args:
            line: Command line without line ending, e.g. "rot 1".

        Returns:
            The reply line, or an empty string on timeout.
        """
        reply = self._exchange(line, COMMAND_TIMEOUT_S)
        if reply is None:
            self.failures.append(f"timeout: {line}")
            print(f"> {line}\n  (no reply)")
            return ""
        print(f"> {line}\n  {reply}")
        if not reply.startswith("@ok"):
            self.failures.append(f"{line}: {reply}")
        if line == "stats" and reply.startswith("@ok"):
            self._record_stats(reply)
        return reply

    def _record_stats(self, reply: str):
        """Keep the lowest largest_dma and the lowest free stack per task from a stats reply."""
        for token in reply.split()[1:]:
            key, separator, value = token.partition("=")
            if not separator or not value.isdigit():
                continue
            number = int(value)
            if key == "largest_dma":
                if self.min_largest_dma is None or number < self.min_largest_dma:
                    self.min_largest_dma = number
            elif key.startswith("stack:"):
                task = key[len("stack:"):]
                if task not in self.min_stack or number < self.min_stack[task]:
                    self.min_stack[task] = number

    def stats(self):
        """Take one stats reading now."""
        self.last_stats_time = time.monotonic()
        self.command("stats")

    def dwell(self, duration_s: float):
        """Keep reading the log for duration_s seconds, taking stats every 30 s."""
        deadline = time.monotonic() + duration_s
        while True:
            now = time.monotonic()
            if now >= deadline:
                return
            if self.last_stats_time is None or now - self.last_stats_time >= STATS_PERIOD_S:
                self.stats()
                continue
            next_stats = self.last_stats_time + STATS_PERIOD_S
            self.pump(min(deadline, next_stats) - now)

    def wait_for_log(self, text: str, timeout_s: float, start_index: int) -> bool:
        """Wait for a received line containing text, taking stats every 30 s meanwhile.

        Args:
            text: Substring to look for.
            timeout_s: Longest wait in seconds.
            start_index: Index into the received lines to search from (lines that arrived while
                the triggering commands were answered count too).

        Returns:
            True when the line arrived in time.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self.dwell(1.0)
            if any(text in line for line in self.lines[start_index:]):
                return True
        return False

    def verdict(self, min_lvgl_stack, min_largest_dma) -> bool:
        """Print the readings and the verdict line.

        Args:
            min_lvgl_stack: Floor for taskLVGL's lowest free stack in bytes, or None.
            min_largest_dma: Floor for the lowest largest DMA-capable block in bytes, or None.

        Returns:
            True for PASS.
        """
        reasons = list(self.failures)

        if self.markers:
            kinds = sorted({marker for marker, _ in self.markers})
            reasons.append(f"failure markers={len(self.markers)} ({', '.join(kinds)})")
            for marker, line in self.markers:
                print(f"MARKER {marker}: {line}")

        print(f"READING largest_dma_min={self.min_largest_dma}")
        for task in sorted(self.min_stack):
            print(f"READING stack_min {task}={self.min_stack[task]}")

        if min_lvgl_stack is not None:
            lvgl_stack = self.min_stack.get(LVGL_TASK_NAME)
            if lvgl_stack is None:
                reasons.append(f"no {LVGL_TASK_NAME} stack reading")
            elif lvgl_stack < min_lvgl_stack:
                reasons.append(f"{LVGL_TASK_NAME} stack {lvgl_stack} < {min_lvgl_stack}")

        if min_largest_dma is not None:
            if self.min_largest_dma is None:
                reasons.append("no largest_dma reading")
            elif self.min_largest_dma < min_largest_dma:
                reasons.append(f"largest_dma {self.min_largest_dma} < {min_largest_dma}")

        print(f"log: {self.log_path}")
        if reasons:
            print("VERDICT FAIL " + "; ".join(reasons))
            return False
        print("VERDICT PASS")
        return True


def wait_for_ip_address(session: Session, start_index: int, accept_connected: bool):
    """Wait for the IP address log line; record a failure if it does not come.

    Args:
        session: The open session.
        start_index: Index into the received lines from which the line counts.
        accept_connected: When the line does not come, accept a device that `state` reports as
            already connected (turning Wi-Fi on while it is on and connected prints no new line).
    """
    # Ask first: waiting 90 s for a line that never comes lets the device go idle, which stops Wi-Fi
    if accept_connected and " ip=1" in session.command("state"):
        print("note: state reports the device already connected")
        return
    if session.wait_for_log(IP_ADDRESS_MARKER, IP_TIMEOUT_S, start_index):
        return
    session.failures.append(f"no '{IP_ADDRESS_MARKER}' within {IP_TIMEOUT_S:.0f} s")


def scenario_state_change(session: Session):
    """ADR-001 Stage 5 routine: Wi-Fi on and save, tile tour, idle/wake, rotation, Wi-Fi off/on."""
    session.stats()

    start_index = len(session.lines)
    session.command("wifi on")
    session.command("save wifi")
    wait_for_ip_address(session, start_index, accept_connected=True)

    for col, row in ENTERABLE_TILES + (HOME_TILE,):
        session.command(f"tile {col} {row}")
        session.dwell(TILE_DWELL_S)

    for _ in range(IDLE_CYCLES):
        session.command("idle")
        session.dwell(IDLE_DURATION_S)
        session.command("wake")
        session.dwell(AFTER_WAKE_S)

    for rotation in (1, 2):
        session.command(f"rot {rotation}")
        session.command("save system")
        session.dwell(AFTER_ROTATION_S)

    session.command("wifi off")
    session.dwell(WIFI_OFF_S)
    start_index = len(session.lines)
    session.command("wifi on")
    wait_for_ip_address(session, start_index, accept_connected=False)

    session.stats()


def dwell_awake(session: Session, duration_s: float):
    """Dwell like dwell(), but re-select the home tile every KEEP_AWAKE_S seconds.

    `stats` does not count as user activity, so a long dwell would let the device go idle (the
    owner's unit idles after about 60 s). Re-selecting the tile already shown is a no-op that
    counts as activity, the same job the owner's countdown timer did in the manual runs.
    """
    deadline = time.monotonic() + duration_s
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        session.dwell(min(KEEP_AWAKE_S, remaining))
        if deadline - time.monotonic() > 0:
            session.command(f"tile {HOME_TILE[0]} {HOME_TILE[1]}")


def scenario_rotation_sweep(session: Session):
    """Each rotation 0..3 with 120 s on the digital level tile, stats every 30 s."""
    session.command(f"tile {HOME_TILE[0]} {HOME_TILE[1]}")
    for rotation in range(4):
        session.command(f"rot {rotation}")
        session.stats()
        dwell_awake(session, ROTATION_DWELL_S)
    session.stats()


def scenario_watch(session: Session):
    """state_change, then check `state` every WATCH_PERIOD_S for session.watch_s seconds.

    Hunts the LVGL lock freeze seen on 2026-11-06. With the LVGL watchdog a freeze panics and
    reboots within 5 s: the `rst:` marker fails the run and the core dump stays on the device.
    """
    scenario_state_change(session)

    deadline = time.monotonic() + session.watch_s
    while time.monotonic() < deadline:
        session.dwell(WATCH_PERIOD_S)
        session.command("state")


def wait_for_text(session: Session, text: str, timeout_s: float, start_index: int) -> bool:
    """Read the log (sending nothing) until a line from start_index on contains text."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        session.pump(READ_POLL_S)
        if any(text in line for line in session.lines[start_index:]):
            return True
    return False


def scenario_watchdog(session: Session):
    """Prove the LVGL lock-up watchdog: `hang` must end in a task watchdog panic and a reboot.

    Takes the LVGL lock and never gives it back (the freeze seen on 2026-11-06). Passes when the
    task watchdog names the `lvgl` user, the device boots again, reports the watchdog reset and
    has a core dump stored. The crash's own markers (panic, reset) are expected and not counted.
    """
    if not session.command("hang").startswith("@ok"):
        return

    start_index = len(session.lines)
    markers_before = len(session.markers)
    if not wait_for_text(session, WATCHDOG_TRIGGER_MARKER, WATCHDOG_TRIGGER_TIMEOUT_S, start_index):
        session.failures.append(f"no '{WATCHDOG_TRIGGER_MARKER}' within {WATCHDOG_TRIGGER_TIMEOUT_S:.0f} s of hang")
        return
    if not wait_for_text(session, " - lvgl", 2.0, start_index):
        session.failures.append("task watchdog fired without naming the lvgl user")

    session.booting = True  # the reset that follows is the expected one
    if not session.wait_ready():
        return

    # Forgive only the crash's own markers; anything else (no memory, stack overflow) still counts
    crash = session.markers[markers_before:]
    session.markers[markers_before:] = [(m, l) for m, l in crash if m not in CRASH_MARKERS]
    for marker, line in crash:
        if marker in CRASH_MARKERS:
            print(f"expected crash marker {marker}: {line}")

    crash_lines = session.lines[start_index:]
    reset_lines = [line for line in crash_lines if "Reset reason:" in line]
    print(f"reset: {reset_lines[-1] if reset_lines else '(none)'}")
    if not any("TASK_WDT" in line or "PANIC" in line for line in reset_lines):
        session.failures.append("no watchdog or panic reset reason after the reboot")
    # The dump is never erased, so ask for this crash's save line, not the boot's summary of any dump
    if not any(CORE_DUMP_SAVED_MARKER in line for line in crash_lines):
        session.failures.append(f"no '{CORE_DUMP_SAVED_MARKER}' during the crash")


SCENARIOS = {
    "state_change": scenario_state_change,
    "rotation_sweep": scenario_rotation_sweep,
    "watch": scenario_watch,
    "watchdog": scenario_watchdog,
}


def parse_args(argv):
    """Parse the command line.

    Args:
        argv: Arguments without the program name.

    Returns:
        The parsed argparse namespace.
    """
    # Options go after the action: rc_run.py scenario state_change --min-lvgl-stack 2048
    options = argparse.ArgumentParser(add_help=False)
    options.add_argument("--port", default=DEFAULT_PORT, help="serial port (default COM11)")
    options.add_argument("--min-lvgl-stack", type=int, default=None,
                         help="fail when taskLVGL's lowest free stack (B) is below this")
    options.add_argument("--min-largest-dma", type=int, default=None,
                         help="fail when the lowest largest DMA-capable block (B) is below this")
    options.add_argument("--watch-s", type=float, default=600.0,
                         help="watch scenario: seconds to keep checking after state_change")
    options.add_argument("--log-dir", type=pathlib.Path, default=DEFAULT_LOG_DIR,
                         help="directory for session logs (default build/rc_logs)")

    parser = argparse.ArgumentParser(description="Serial remote control runner (ADR-004)")
    commands = parser.add_subparsers(dest="action", required=True)
    send_parser = commands.add_parser("send", parents=[options],
                                      help="send one command line and print the reply")
    send_parser.add_argument("line", help='command line, e.g. "tile 2 1"')
    scenario_parser = commands.add_parser("scenario", parents=[options], help="run a named scenario")
    scenario_parser.add_argument("name", choices=sorted(SCENARIOS))

    return parser.parse_args(argv)


def main(argv) -> int:
    """Run one send or scenario session and return the process exit code (0 PASS, 1 FAIL)."""
    args = parse_args(argv)

    label = "send" if args.action == "send" else args.name
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    session = Session(args.port, args.log_dir / f"rc_{label}_{stamp}.log")
    session.watch_s = args.watch_s
    try:
        if session.wait_ready():
            if args.action == "send":
                if args.line == "stats":
                    session.stats()
                else:
                    session.command(args.line)
            else:
                SCENARIOS[args.name](session)
                session.pump(FINAL_PUMP_S)  # a crash right after the last command still counts
        passed = session.verdict(args.min_lvgl_stack, args.min_largest_dma)
    finally:
        session.close()

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

"""Host runner for the serial remote control.

Drives the device over its USB Serial/JTAG console, records the whole session to a log
file, counts failure markers and prints a verdict.

Usage (repo venv):
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py send "state"
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py scenario state_change --min-lvgl-stack 2048 --min-largest-dma 80000
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py send snap   (PNG of the screen in logs/rc/snaps/)

Opening the port may reset the board. After opening, the runner sends `state` every 2 s until
the device answers, so the boot is part of the session and its markers are counted. During that
boot, `rst:` lines are expected, and one abort right after "Failed to detect BNO085" is allowed.

Over Wi-Fi (ADR-013; Settings, Advanced: Wi-Fi remote control on), the same commands, snaps and log:
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py find
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py send state --host owmc-8626470.local
    venv\\Scripts\\python.exe tools\\remote_control\\rc_run.py scenario state_change --host 192.168.2.150
The radio stops whenever the unit idles, so over Wi-Fi a scenario idles with `idle <s>` (the unit wakes itself by
a timer) and connects again after it. Connecting never resets the unit; the ROM's reset lines are console only.
"""

import argparse
import base64
import datetime
import pathlib
import re
import socket
import statistics
import struct
import sys
import time
import zlib

import serial

DEFAULT_PORT = "COM11"
RC_NET_PORT = 3333                      # rc_net.h
RC_NET_SERVICE = "_owmc-rc._tcp.local."
NET_CONNECT_TIMEOUT_S = 3.0
NET_RECONNECT_PERIOD_S = 2.0
FIND_S = 4.0
BAUD_RATE = 115200
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
# Outside build/: idf.py fullclean and set-target delete build/, and with it the evidence (ADR-005 B22)
DEFAULT_LOG_DIR = REPO_ROOT / "logs" / "rc"
DEFAULT_SNAP_DIR = DEFAULT_LOG_DIR / "snaps"
# The panel's pixel area has rounded corners: R2.30 mm at 0.078 mm a pixel (DO0164FMST05 datasheet, ADR-006)
PANEL_CORNER_RADIUS_PX = 30

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
    "sleep": "@ok sleep",
    "tear": "@ok tear",
    "grav": "@ok grav",
    "bt": "@ok bt",
    "pmic": "@ok pmic",
    "snap": "@ok snap",
    "outline": "@ok outline",
    "kestrel": "@ok kestrel",
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
    "bnoreset": "@ok bnoreset",
    "settings": "@ok settings",
    "ota": "@ok ota",
    "bno": "@ok bno",
    "top": "@ok top",
    "trace": "@ok trace",
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
WATCHDOG_AFTER_WAKE_S = 2.0  # watchdog scenario: LVGL resumed and feeding before `hang`
AFTER_ROTATION_S = 5.0
WIFI_OFF_S = 10.0
ROTATION_DWELL_S = 120.0
KEEP_AWAKE_S = 20.0       # re-select the home tile this often during long dwells
WATCH_PERIOD_S = 10.0     # watch scenario: one `state` check this often
READY_DRAIN_S = 1.0       # after the first answer, replies to earlier probes may still arrive
FINAL_PUMP_S = 5.0        # keep reading this long after a scenario's last command


class NetPort:
    """The unit's remote control over Wi-Fi, read and written like the serial port.

    The connection drops whenever the unit idles (its radio stops), mostly without a word: reads then return
    nothing until `drop()` and the next read, which connects again (at most every NET_RECONNECT_PERIOD_S).
    """

    def __init__(self, host: str, port: int = RC_NET_PORT):
        self.address = (host, port)
        self.sock = None
        self.next_try = 0.0
        self._connect()

    def _connect(self) -> bool:
        self.next_try = time.monotonic() + NET_RECONNECT_PERIOD_S
        try:
            sock = socket.create_connection(self.address, timeout=NET_CONNECT_TIMEOUT_S)
        except OSError:
            return False
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(0)
        self.sock = sock
        return True

    def drop(self):
        """Close the connection; the next read or write connects again."""
        if self.sock is not None:
            self.sock.close()
        self.sock = None
        self.next_try = 0.0

    def read(self, size: int) -> bytes:
        if self.sock is None:
            if time.monotonic() >= self.next_try:
                self._connect()
            return b""
        try:
            data = self.sock.recv(size)
        except BlockingIOError:
            return b""
        except OSError:
            data = b""
        if not data:
            self.drop()
        return data

    def write(self, data: bytes):
        if self.sock is None and not self._connect():
            return
        try:
            self.sock.settimeout(NET_CONNECT_TIMEOUT_S)
            self.sock.sendall(data)
            self.sock.settimeout(0)
        except OSError:
            self.drop()

    def flush(self):
        pass

    def close(self):
        self.drop()


def find_units() -> list:
    """The units announcing the remote control over Wi-Fi: (host name, "addresses  version") pairs."""
    from zeroconf import ServiceBrowser, Zeroconf

    zc = Zeroconf()
    found = {}

    class Listener:
        def add_service(self, zc, kind, name):
            info = zc.get_service_info(kind, name, 3000)
            if info is not None:
                version = (info.properties or {}).get(b"version", b"?").decode(errors="replace")
                addresses = ", ".join(socket.inet_ntoa(a) for a in info.addresses)
                found[info.server.rstrip(".")] = f"{addresses}  {version}"

        def update_service(self, zc, kind, name):
            self.add_service(zc, kind, name)

        def remove_service(self, zc, kind, name):
            pass

    ServiceBrowser(zc, RC_NET_SERVICE, Listener())
    time.sleep(FIND_S)
    zc.close()
    return sorted(found.items())


class Session:
    """One session, over the serial console or Wi-Fi: port, log file, reply matching, marker counting and readings."""

    def __init__(self, port: str, log_path: pathlib.Path, host: str = None):
        """Open the log file and the serial port without asserting DTR or RTS, or connect to the unit over Wi-Fi.

        Args:
            port: Serial port name, e.g. COM11; not used with host.
            log_path: File that receives every line sent and received, with local timestamps.
            host: The unit's address or mDNS name, for the remote control over Wi-Fi.
        """
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path = log_path
        self.log = open(log_path, "w", encoding="utf-8")
        self.is_net = host is not None
        self._init_state()
        if self.is_net:
            self.port = NetPort(host)
            return

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

    def _init_state(self):
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
        self.snap_header = None   # "@snap <w> <h> <format>" of the screen copy being received
        self.snap_data = []       # its "@sd" base64 lines

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
                    if line.startswith("@sd "):
                        self.snap_data.append(line[4:])  # screen copy data: kept, not logged line by line
                        continue
                    if line.startswith("@snap "):
                        self.snap_header = line
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

    def idle_over_net(self, duration_s: float):
        """Over Wi-Fi: `idle <s>`, then connect again once the unit has woken itself and has its address back.

        The reply to `idle` may be lost with the radio, so a missing one is not a failure.
        """
        line = f"idle {int(duration_s)}"
        reply = self._exchange(line, COMMAND_TIMEOUT_S)
        print(f"> {line}\n  {reply or '(no reply: the radio stopped first)'}")
        if reply is not None and not reply.startswith("@ok"):
            self.failures.append(f"{line}: {reply}")
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            self.pump(READ_POLL_S)
        self.port.drop()
        if not self.wait_ready():
            self.failures.append("no answer after the timed idle")

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

    def snap(self, path: pathlib.Path) -> bool:
        """Copy the screen (`snap`) and save it as a PNG; the panel's rounded corners are tinted red.

        Args:
            path: PNG file to write.

        Returns:
            True when the copy arrived whole and was saved.
        """
        self.snap_header = None
        self.snap_data = []
        reply = self.command("snap")
        if not reply.startswith("@ok snap") or self.snap_header is None:
            return False
        _, width, height, fmt = self.snap_header.split()
        width, height = int(width), int(height)
        lines = int(reply.split("lines=")[1])
        if fmt != "rgb565rle" or len(self.snap_data) != lines:
            self.failures.append(f"snap: {len(self.snap_data)} of {lines} data lines")
            return False

        pairs = b"".join(base64.b64decode(chunk) for chunk in self.snap_data)
        pixels = []
        for count, value in struct.iter_unpack("<HH", pairs):
            pixels.extend([value] * count)
        if len(pixels) != width * height:
            self.failures.append(f"snap: {len(pixels)} pixels, expected {width * height}")
            return False

        write_snap_png(path, width, height, pixels)
        self._write_log("#", f"snap saved: {path}")
        print(f"snap: {path}")
        return True

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


def outside_panel(x: int, y: int, width: int, height: int, radius: int = PANEL_CORNER_RADIUS_PX) -> bool:
    """True when pixel (x, y) lies in a rounded-off corner of the panel, where nothing is shown."""
    cx = radius if x < radius else (width - 1 - radius if x > width - 1 - radius else None)
    cy = radius if y < radius else (height - 1 - radius if y > height - 1 - radius else None)
    if cx is None or cy is None:
        return False
    return (x - cx) ** 2 + (y - cy) ** 2 > radius ** 2


def write_snap_png(path: pathlib.Path, width: int, height: int, pixels: list):
    """Write RGB565 pixels as an RGB PNG; pixels the panel cannot show are tinted red, so content cut by a
    corner stands out."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: none
        for x in range(width):
            v = pixels[y * width + x]
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            if outside_panel(x, y, width, height):
                r, g, b = (r + 255) // 2, g // 2, b // 2
            rows += bytes((r, g, b))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


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
    """State-change routine: Wi-Fi on and save, tile tour, idle/wake, rotation, Wi-Fi off/on."""
    session.stats()

    start_index = len(session.lines)
    session.command("wifi on")
    session.command("save wifi")
    wait_for_ip_address(session, start_index, accept_connected=True)

    for col, row in ENTERABLE_TILES + (HOME_TILE,):
        session.command(f"tile {col} {row}")
        session.dwell(TILE_DWELL_S)

    for _ in range(IDLE_CYCLES):
        if session.is_net:
            session.idle_over_net(IDLE_DURATION_S)
        else:
            session.command("idle")
            session.dwell(IDLE_DURATION_S)
            session.command("wake")
        session.dwell(AFTER_WAKE_S)

    # Turn to another rotation and back to the unit's own, so the saved setting ends as it was found (it used to end
    # on a fixed 2 and overwrote the owner's)
    match = re.search(r"rot=(\d)", session.command("state"))
    own_rotation = int(match.group(1)) if match else 2
    for rotation in ((own_rotation + 1) % 4, own_rotation):
        session.command(f"rot {rotation}")
        session.command("save system")
        session.dwell(AFTER_ROTATION_S)

    if session.is_net:
        # Wi-Fi off would end the session itself; the run over USB covers it
        print("note: Wi-Fi off and on skipped over Wi-Fi")
    else:
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


# Wake profile (ADR-009): which of the level's reports keep running while the BNO085 sleeps, as (grv, la) always-on
WAKE_PROFILE_VARIANTS = ((0, 0), (0, 1))   # the firmware's since ADR-009 step 2, then linear acceleration kept on
WAKE_PROFILE_DEFAULT = (0, 0)
WAKE_PROFILE_CYCLES = 8
WAKE_PROFILE_IDLE_S = 10.0
WAKE_PROFILE_AFTER_WAKE_S = 2.5    # the device reports 1 s after LVGL resumes
WAKE_PROFILE_FIELDS = ("exit", "panel_on", "resumed", "frame", "bno_cmd", "grv_first", "grv_100ms",
                       "la_first", "la_100ms", "te_idle", "te_awake", "idle_ms")


def wake_profile_summary(lines):
    """Median of each wake_prof field over the given log lines; -1 (not seen) values are left out."""
    values = {name: [] for name in WAKE_PROFILE_FIELDS}
    count = 0
    for line in lines:
        if "wake_prof:" not in line:
            continue
        count += 1
        for name in WAKE_PROFILE_FIELDS:
            match = re.search(rf" {name}=(-?[\d.]+)", line)
            if match and float(match.group(1)) >= 0:
                values[name].append(float(match.group(1)))
    medians = {name: (statistics.median(v) if v else None) for name, v in values.items()}
    return count, medians


def scenario_wake_profile(session: Session):
    """Wake from idle on the level tile, timed by the device's `wake_prof:` lines, for each always-on variant.

    Needs a build with WAKE_PROFILING_ENABLED. Each variant: WAKE_PROFILE_CYCLES of idle for WAKE_PROFILE_IDLE_S
    then `wake`. `top` once while active and once while idle (verification builds with run-time stats). Leaves the
    sensor as the firmware sets it.
    """
    session.command(f"tile {HOME_TILE[0]} {HOME_TILE[1]}")
    session.dwell(3.0)
    session.command("top 3000")
    session.command("idle")
    session.dwell(3.0)
    session.command("top 3000")
    session.command("wake")
    session.dwell(WAKE_PROFILE_AFTER_WAKE_S)

    results = []
    for grv, la in WAKE_PROFILE_VARIANTS:
        session.command(f"bno alwayson grv {grv}")
        session.command(f"bno alwayson la {la}")
        session.dwell(1.0)
        start_index = len(session.lines)
        for _ in range(WAKE_PROFILE_CYCLES):
            session.command("idle")
            session.dwell(WAKE_PROFILE_IDLE_S)
            session.command("wake")
            session.dwell(WAKE_PROFILE_AFTER_WAKE_S)
        results.append(((grv, la), wake_profile_summary(session.lines[start_index:])))

    session.command(f"bno alwayson grv {WAKE_PROFILE_DEFAULT[0]}")
    session.command(f"bno alwayson la {WAKE_PROFILE_DEFAULT[1]}")

    print("wake profile (medians, ms from the wake request; bno from its wake command):")
    for (grv, la), (count, medians) in results:
        shown = " ".join(f"{name}={'-' if v is None else f'{v:.1f}'}" for name, v in medians.items())
        print(f"  grv_alwayson={grv} la_alwayson={la} wakes={count}: {shown}")
        if count < WAKE_PROFILE_CYCLES:
            session.failures.append(f"grv={grv} la={la}: {count} of {WAKE_PROFILE_CYCLES} wake_prof lines")


IDLE_CYCLES_DURATION_S = 600.0    # the owner caps cycle tests at 10 minutes
IDLE_CYCLE_IDLE_S = 10.0
IDLE_CYCLE_AWAKE_S = 4.0


def scenario_idle_cycles(session: Session):
    """Idle and wake on the level tile for IDLE_CYCLES_DURATION_S, `state` after each wake.

    A wake that never completes (the LVGL lock held, the LVGL watchdog's reboot) shows as a failure marker or a lost
    `state` reply. With WAKE_PROFILING_ENABLED, the medians of the `wake_prof:` lines are printed as well.
    """
    session.command(f"tile {HOME_TILE[0]} {HOME_TILE[1]}")
    start_index = len(session.lines)
    cycles = 0
    deadline = time.monotonic() + IDLE_CYCLES_DURATION_S
    while time.monotonic() < deadline:
        session.command("idle")
        session.dwell(IDLE_CYCLE_IDLE_S)
        session.command("wake")
        session.dwell(IDLE_CYCLE_AWAKE_S)
        session.command("state")
        cycles += 1
    count, medians = wake_profile_summary(session.lines[start_index:])
    print(f"idle cycles: {cycles}")
    if count:
        shown = " ".join(f"{name}={'-' if v is None else f'{v:.1f}'}" for name, v in medians.items())
        print(f"  wake_prof lines={count}: {shown}")


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
    # The LVGL watchdog is paused while idle by design, so `hang` in idle only locks the unit
    # until the next wake (ADR-005 B23). Wake first, and leave time for LVGL to resume.
    if not session.command("wake").startswith("@ok"):
        return
    session.dwell(WATCHDOG_AFTER_WAKE_S)

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


# Level motion (ADR-009 step 3): the level's drawing while the owner rocks the unit, from the device's
# `display_prof:` and `digital_level_prof:` lines (a build with DISPLAY_PROFILING_ENABLED). The scenario wakes the
# unit, gives the owner LEVEL_MOTION_START_S to pick it up, then records LEVEL_MOTION_RECORD_S. Windows with no
# redraw (idle) are left out. The per-frame figures do not depend on how hard the unit is rocked, so two runs by
# hand compare between builds
LEVEL_MOTION_START_S = 8.0
LEVEL_MOTION_RECORD_S = 30.0
LEVEL_MOTION_MIN_WINDOWS = 20


def prof_pair(line: str, name: str):
    """The `avg/max_name=a/b` field of a profiling line as (avg, max), or None."""
    match = re.search(rf" avg/max_{name}=(\d+)/(\d+)", line)
    return (float(match.group(1)), float(match.group(2))) if match else None


def prof_value(line: str, name: str) -> float:
    """A plain ` name=value` field of a profiling line (0 when missing)."""
    match = re.search(rf" {name}=(\d+)", line)
    return float(match.group(1)) if match else 0.0


def level_motion_summary(lines):
    """Medians over the windows in which the level redrew; max = the worst window.

    draw_us is a frame's render time less its flush callback: the flush mostly sleeps until the panel's scan
    passes, so what is left is the CPU's drawing. draw_pct is that as a share of the window.
    """
    display = [l for l in lines if "display_prof:" in l and prof_value(l, "flush") > 0]
    level = [l for l in lines if "digital_level_prof:" in l and prof_value(l, "ui_try") > 0]

    def med(values):
        return statistics.median(values) if values else None

    def per_s(line, name):
        return prof_value(line, name) * 1000.0 / prof_value(line, "window")

    draw_us = [prof_pair(l, "render")[0] - prof_pair(l, "flush_cb")[0] for l in display]
    return {
        "windows": len(display),
        "flush_per_s": med([per_s(l, "flush") for l in display]),
        "flush_px_avg": med([prof_pair(l, "flush_px")[0] for l in display]),
        "flush_px_max": max([prof_pair(l, "flush_px")[1] for l in display], default=None),
        "render_avg_us": med([prof_pair(l, "render")[0] for l in display]),
        "render_max_us": max([prof_pair(l, "render")[1] for l in display], default=None),
        "flush_cb_avg_us": med([prof_pair(l, "flush_cb")[0] for l in display]),
        "draw_us": med(draw_us),
        "draw_pct": med([d * prof_value(l, "render") / prof_value(l, "window") / 10.0
                         for d, l in zip(draw_us, display)]),
        "wait_max_us": max([prof_pair(l, "wait")[1] for l in display], default=None),
        "game_per_s": med([per_s(l, "game") for l in level]),
        "ui_ok_per_s": med([per_s(l, "ui_ok") for l in level]),
        "lock_fail_per_s": med([per_s(l, "lock_fail") for l in level]),
        "update_avg_us": med([prof_pair(l, "update")[0] for l in level]),
        "update_max_us": max([prof_pair(l, "update")[1] for l in level], default=None),
    }


def scenario_level_motion(session: Session):
    """The level's drawing while the owner rocks the unit by hand. Needs DISPLAY_PROFILING_ENABLED."""
    session.command("wake")
    session.command(f"tile {HOME_TILE[0]} {HOME_TILE[1]}")
    print(f"Pick the unit up and rock it now; recording starts in {LEVEL_MOTION_START_S:.0f} s and lasts "
          f"{LEVEL_MOTION_RECORD_S:.0f} s.", flush=True)
    session.dwell(LEVEL_MOTION_START_S)
    record_index = len(session.lines)
    session.dwell(LEVEL_MOTION_RECORD_S)
    summary = level_motion_summary(session.lines[record_index:])
    print("level motion (medians over redrawing windows; max = worst window):")
    print("  " + " ".join(f"{k}={'-' if v is None else f'{v:.1f}'}" for k, v in summary.items()))
    if summary["windows"] < LEVEL_MOTION_MIN_WINDOWS:
        session.failures.append(f"only {summary['windows']} redrawing windows in {LEVEL_MOTION_RECORD_S:.0f} s")

SCENARIOS = {
    "state_change": scenario_state_change,
    "rotation_sweep": scenario_rotation_sweep,
    "watch": scenario_watch,
    "watchdog": scenario_watchdog,
    "wake_profile": scenario_wake_profile,
    "idle_cycles": scenario_idle_cycles,
    "level_motion": scenario_level_motion,
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
    options.add_argument("--host", default=None,
                         help="the unit's address or mDNS name: the remote control over Wi-Fi instead of --port")
    options.add_argument("--min-lvgl-stack", type=int, default=None,
                         help="fail when taskLVGL's lowest free stack (B) is below this")
    options.add_argument("--min-largest-dma", type=int, default=None,
                         help="fail when the lowest largest DMA-capable block (B) is below this")
    options.add_argument("--watch-s", type=float, default=600.0,
                         help="watch scenario: seconds to keep checking after state_change")
    options.add_argument("--log-dir", type=pathlib.Path, default=DEFAULT_LOG_DIR,
                         help="directory for session logs (default logs/rc)")

    parser = argparse.ArgumentParser(description="Serial remote control runner")
    commands = parser.add_subparsers(dest="action", required=True)
    send_parser = commands.add_parser("send", parents=[options],
                                      help="send one command line and print the reply")
    send_parser.add_argument("line", help='command line, e.g. "tile 2 1"')
    scenario_parser = commands.add_parser("scenario", parents=[options], help="run a named scenario")
    scenario_parser.add_argument("name", choices=sorted(SCENARIOS))
    commands.add_parser("find", help="list the units announcing the remote control over Wi-Fi")

    return parser.parse_args(argv)


def main(argv) -> int:
    """Run one send or scenario session and return the process exit code (0 PASS, 1 FAIL)."""
    args = parse_args(argv)
    if args.action == "find":
        units = find_units()
        for name, addresses in units:
            print(f"{name}  {addresses}")
        print(f"{len(units)} unit(s)")
        return 0 if units else 1

    label = "send" if args.action == "send" else args.name
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    session = Session(args.port, args.log_dir / f"rc_{label}_{stamp}.log", host=args.host)
    session.watch_s = args.watch_s
    try:
        if session.wait_ready():
            if args.action == "send":
                if args.line == "stats":
                    session.stats()
                elif args.line == "snap":
                    session.snap(DEFAULT_SNAP_DIR / f"snap_{stamp}.png")
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

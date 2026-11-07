"""The local OTA host: what the firmware's update check looks for (`main/ota_mode.c`).

After its Wi-Fi connects at boot, the unit asks `owmc_update.local` port 8080 for `/p1/manifest.json`; when the
manifest's version is newer than its own, or `ignore_version` is set, it offers the update, and Install downloads
`path` from the same host and the manifest's `port`. The unit checks once per boot: start this, then restart it.

This announces `owmc_update.local` on the local network (mDNS) and serves the manifest and the build's firmware
(only those two paths). Every request is printed, with the bytes sent.

Usage, from the repo root with the repo venv:
    python scripts/ota_host.py                          # the build, offered to units with an older version
    python scripts/ota_host.py --force                  # offered whatever the unit's version (tests)
    python scripts/ota_host.py --version v0.0.2         # declared as another version
    python scripts/ota_host.py --importance 1           # critical: the unit shows the offer at once
    python scripts/ota_host.py --note-file notes.txt    # a release note of several lines
    python scripts/ota_host.py --manifest-file m.json   # a manifest as written (format tests)
Options: --note, --ip (the address announced; default: the one this machine uses on the network), --port.

Plain HTTP and no image signing, as the firmware is built (CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP): run it only on a
network you trust.
"""
import argparse
import http.server
import json
import socket
import sys
import time
from enum import Enum
from pathlib import Path

from zeroconf import ServiceInfo, Zeroconf

HOSTNAME = "owmc_update.local"
PORT = 8080
MANIFEST_PATH = "/p1/manifest.json"
BUILD = Path(__file__).resolve().parents[1] / "build"


class OtaImportance(Enum):
    NORMAL = 0
    CRITICAL = 1


class OtaPackageType(Enum):
    FIRMWARE = 0
    DATA = 1
    PARTITION_TABLE = 2
    BOOTLOADER = 3


def log(text):
    print(f"{time.strftime('%H:%M:%S')} {text}", flush=True)


def local_ip():
    """The address this machine uses on the network (no packet is sent). gethostbyname(gethostname()) can name
    another adapter (a virtual or Bluetooth one) that the unit cannot reach"""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", help="the version the manifest declares (default: the build's)")
    ap.add_argument("--note", help="the release note the unit shows (up to 1023 bytes; cut with ... beyond)")
    ap.add_argument("--note-file", help="the release note from a file (UTF-8; line breaks kept)")
    ap.add_argument("--importance", type=int, default=OtaImportance.NORMAL.value, help="0 normal, 1 critical")
    ap.add_argument("--force", action="store_true",
                    help="offered whatever the unit's version (ignore_version); by default only to older units")
    ap.add_argument("--manifest-file", help="serve this file as the manifest, as written")
    ap.add_argument("--ip", default=None)
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()

    project = json.loads((BUILD / "project_description.json").read_text(encoding="utf-8"))
    image = (BUILD / project["app_bin"]).read_bytes()
    image_path = "/build/" + project["app_bin"]
    version = args.version or project["project_version"]
    if args.manifest_file:
        manifest = Path(args.manifest_file).read_bytes()
    else:
        manifest = json.dumps({
            "manifest_version": 1,
            "version": version,
            "path": image_path,
            "note": (Path(args.note_file).read_text(encoding="utf-8").strip() if args.note_file else
                     args.note or f"Local build {project['project_version']}."),
            "port": args.port,
            "ignore_version": args.force,
            "type": OtaPackageType.FIRMWARE.value,
            "importance": args.importance,
        }).encode("utf-8")

    class OTARequestHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == MANIFEST_PATH:
                body, kind = manifest, "application/json"
            elif self.path == image_path:
                body, kind = image, "application/octet-stream"
            else:
                log(f"{self.client_address[0]} GET {self.path} -> 404")
                self.send_error(404)
                return
            log(f"{self.client_address[0]} GET {self.path} ({len(body)} bytes)")
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except OSError as e:
                log(f"{self.client_address[0]} {self.path} broke off: {e}")

        def log_message(self, *a):
            pass

    ip = args.ip or local_ip()
    zeroconf = Zeroconf()
    service_info = ServiceInfo("_http._tcp.local.", f"{HOSTNAME}._http._tcp.local.", addresses=[socket.inet_aton(ip)],
                               port=args.port, properties={}, server=f"{HOSTNAME}.")
    zeroconf.register_service(service_info)
    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), OTARequestHandler)
    log(f"http://{HOSTNAME}:{args.port}/ is {ip}; firmware {image_path} ({len(image)} bytes)")
    log("manifest: " + manifest.decode("utf-8", errors="replace"))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("shutting down")
    finally:
        zeroconf.unregister_service(service_info)
        zeroconf.close()
        server.server_close()


if __name__ == "__main__":
    sys.exit(main())

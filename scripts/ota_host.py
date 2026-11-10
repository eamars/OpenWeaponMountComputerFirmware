"""The local OTA host: what the firmware's update check looks for (`main/services/ota_mode.c`).

After its Wi-Fi connects at boot, the unit asks `owmc_update.local` port 8080 for `/p1/manifest.json`; when the
manifest's version is newer than its own, or `ignore_version` is set, it offers the update, and Install downloads
`path` from the same host and the manifest's `port`. The unit checks once per boot: start this, then restart it.
Only when this host does not answer does the unit look at the latest published GitHub release (ADR-015).

This announces `owmc_update.local` on the local network (mDNS) and serves the manifest and the firmware (only those
two paths): the build's, or a release's OTA package. Every request is printed, with the bytes sent.

An OTA package is a zip with the manifest (`manifest.json`) and the firmware (named as the manifest's `path`), so a
release can be served without building it: write it from the build with --write-package, serve it with --package.

A release also carries its manifest on its own (`manifest.json`, --write-manifest), with `url`: where its firmware is
on that release. The package's manifest has no `url`, so a unit given the package downloads from this host.

Usage, from the repo root with the repo venv:
    python scripts/ota_host.py                          # the build, offered to units with an older version
    python scripts/ota_host.py --force                  # offered whatever the unit's version (tests)
    python scripts/ota_host.py --version v0.0.2         # declared as another version
    python scripts/ota_host.py --importance 1           # critical: the unit shows the offer at once
    python scripts/ota_host.py --note-file notes.txt    # a release note of several lines
    python scripts/ota_host.py --manifest-file m.json   # a manifest as written (format tests)
    python scripts/ota_host.py --note-file n.txt --write-package ota.zip   # write the build's OTA package, no serving
    python scripts/ota_host.py --package ota.zip        # serve an OTA package (a release download)
    python scripts/ota_host.py --note-file n.txt --write-manifest manifest.json --url https://.../fw.bin   # a release's
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
import zipfile
from enum import Enum
from pathlib import Path

from zeroconf import ServiceInfo, Zeroconf

HOSTNAME = "owmc_update.local"
PORT = 8080
MANIFEST_PATH = "/p1/manifest.json"
PACKAGE_MANIFEST = "manifest.json"
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
    ap.add_argument("--package", help="serve this OTA package (zip) instead of the build")
    ap.add_argument("--write-package", help="write the build's OTA package (zip) here and exit")
    ap.add_argument("--write-manifest", help="write the build's manifest here, with --url, and exit")
    ap.add_argument("--url", help="with --write-manifest: the firmware's address on its release")
    ap.add_argument("--ip", default=None)
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()
    if bool(args.write_manifest) != bool(args.url):
        ap.error("--write-manifest and --url go together")
    if args.write_manifest and (args.package or args.manifest_file):
        ap.error("--write-manifest writes the build's manifest")

    if args.package:
        with zipfile.ZipFile(args.package) as package:
            manifest = package.read(PACKAGE_MANIFEST)
            image_path = json.loads(manifest)["path"]
            image = package.read(image_path.lstrip("/"))
        args.port = json.loads(manifest).get("port", PORT)  # the unit downloads from the manifest's port
    else:
        project = json.loads((BUILD / "project_description.json").read_text(encoding="utf-8"))
        image = (BUILD / project["app_bin"]).read_bytes()
        image_path = "/build/" + project["app_bin"]
        version = args.version or project["project_version"]
        fields = {
            "manifest_version": 1,
            "version": version,
            "path": image_path,
            "note": (Path(args.note_file).read_text(encoding="utf-8").strip() if args.note_file else
                     args.note or f"Local build {project['project_version']}."),
            "port": args.port,
            "ignore_version": args.force,
            "type": OtaPackageType.FIRMWARE.value,
            "importance": args.importance,
        }
        manifest = Path(args.manifest_file).read_bytes() if args.manifest_file else json.dumps(fields).encode("utf-8")

    if args.write_package or args.write_manifest:
        if args.write_package:
            with zipfile.ZipFile(args.write_package, "w", zipfile.ZIP_DEFLATED) as package:
                package.writestr(PACKAGE_MANIFEST, manifest)
                package.writestr(image_path.lstrip("/"), image)
            log(f"wrote {args.write_package}: {PACKAGE_MANIFEST}, {image_path.lstrip('/')} ({len(image)} bytes)")
        if args.write_manifest:
            Path(args.write_manifest).write_text(json.dumps({**fields, "url": args.url}), encoding="utf-8")
            log(f"wrote {args.write_manifest}: version {version}, url {args.url}")
        return 0

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

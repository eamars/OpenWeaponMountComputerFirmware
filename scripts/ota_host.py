import sys
import socket
from http.server import HTTPServer, SimpleHTTPRequestHandler
from zeroconf import ServiceInfo, Zeroconf
import json
from enum import Enum


HOSTNAME = "owmc_update.local"
PORT = 8080


class OtaImportance(Enum):
    NORMAL = 0
    CRITICAL = 1

class OtaPackageType(Enum):
    FIRMWARE = 0
    DATA = 1
    PARTITION_TABLE = 2
    BOOTLOADER = 3


class OTARequestHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        sys.stdout.write("[HTTP] " + format % args + "\n")

    def do_GET(self):
        if self.path == "/p1/manifest.json":
            manifest = {
                "manifest_version": 1,
                "version": "v0.0.1",
                "path": "/build/OpenWeaponMountComputerFirmware.bin",
                "note": "This version fixes several stability issues, including the screen shattering, lagging and tearing.",
                "port": 8080,
                "ignore_version": True,
                "type": OtaPackageType.FIRMWARE.value,
                "importance": OtaImportance.CRITICAL.value
            }
            body = json.dumps(manifest).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            print(f"[HTTP] Served manifest.json: {body}")

        else:
            super().do_GET()

# Run HTTP server
def run_http_server():
    server = HTTPServer(("0.0.0.0", PORT), OTARequestHandler)
    print(f"[INFO] HTTP server started on port {PORT}")
    return server


# Register mDNS
def register_mdns_service(port):
    zeroconf = Zeroconf()
    ip = socket.inet_aton(socket.gethostbyname(socket.gethostname()))

    service_info = ServiceInfo(
        "_http._tcp.local.",
        f"{HOSTNAME}._http._tcp.local.",
        addresses=[ip],
        port=port,
        properties={},
        server=f"{HOSTNAME}.",
    )

    zeroconf.register_service(service_info)
    print(f"[INFO] mDNS service registered as http://{HOSTNAME}:{port}/")
    return zeroconf, service_info



if __name__ == "__main__":
    server = run_http_server()
    zeroconf, service_info = register_mdns_service(PORT)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[INFO] Shutting down...")
    finally:
        zeroconf.unregister_service(service_info)
        zeroconf.close()
        server.server_close()

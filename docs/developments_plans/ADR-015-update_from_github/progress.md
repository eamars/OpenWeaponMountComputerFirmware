# ADR-015 progress

The turret unit (`owmc-8626470`), on USB (COM11), 2026-11-10. Each step was read from its console; `state_change` ran over Wi-Fi. GitHub's latest release was v1.3.0, published that evening with its `manifest.json` (PR #15).

## Built

- `ota_mode.c`: the manifest fetch takes a source (a URL, or host, path and port) and follows redirects (`esp_http_client_perform`, the final response's body only), with the certificate bundle and 2 KB receive and transmit buffers. Sources in order: `owmc_update.local` (host, path, port), then `https://github.com/eamars/OpenWeaponMountComputerFirmware/releases/latest/download/manifest.json`. `owmc_update.vfnz.pro` removed.
- The manifest's optional `url` (256 bytes): Install downloads the image from it, over HTTPS; without it, from the host, path and port as before. A manifest from GitHub without a `url` is rejected.
- `ota_mode.h`: the repository is one constant (`OTA_GITHUB_REPOSITORY`). Task stacks: `ota_poller` 8 KB (PSRAM), `ota_updater` 7 KB (internal); each logs its unused stack when it ends.
- `main/CMakeLists.txt`: `mbedtls` listed for the certificate bundle. The image is 73 KB larger (2.53 MB), the bundle now being linked.
- `scripts/ota_host.py` docstring and `AGENTS.md`: the unit asks the local host first, then GitHub.
- Build: no warning from our code.

## The first build (`v1.3.0-3-g62f309c`) boot-looped

About 6 s after each boot, as the GitHub check started: "A stack overflow in task ota_poller has been detected", then a reset, every 7 s. Its log also showed "Error parse url http://owmc_update.local:8080/p1/manifest.json". Reflashed over USB (README, F1 and F2).

Measured on a build with 16 KB stacks:
- `ota_poller`, the HTTPS check: 4,584 to 4,588 B used;
- `ota_updater`, an HTTPS update from GitHub: 4,700 B used.

## Proof (`v1.3.0-4-gedb9fd0`, and test builds of the same code reporting `v1.2.9`)

| Check | Result |
|---|---|
| No local host | Local lookup failed after 7 s; GitHub's manifest read (689 B, two redirects, HTTPS); "v1.3.0-4-gedb9fd0 is the same as or newer than the manifest's v1.3.0: not offered". `ota_poller` 3,608 of 8,192 B unused |
| Local override (`ota_host.py --force --importance 1`) | The local manifest read first (no `url`); the offer at once; Install over HTTP from the local host, 2.53 MB in 24.4 s from the tap; restarted on the same build. `ota_updater` 3,900 of 7,168 B unused; `ota_poller` 5,720 unused |
| From GitHub, test build `v1.2.9` with 16 KB stacks | Offered v1.3.0 ("Update available"); Install over HTTPS from the release's `url`, 2.46 MB in 39.5 s from the tap (3 s to connect); image check passed; restarted on v1.3.0 |
| From GitHub, test build `v1.2.9` with the final stacks | The same, 38.4 s; `ota_updater` 2,332 of 7,168 B unused, `ota_poller` 3,608 of 8,192; restarted on v1.3.0 |
| `scenario state_change` over Wi-Fi, `--min-largest-dma 70000` | PASS. Free internal 162,563 B, largest DMA block 86,016 B (v1.3.0: 166,003 and 90,112) |

From the tap, the HTTPS install from GitHub took about 14 s longer than the plain HTTP one from the local host (38 to 40 s for 2.46 MB, against 24 s for 2.53 MB).

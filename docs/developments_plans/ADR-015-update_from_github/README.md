# ADR-015: updates from the latest GitHub release

Status: **Draft** 2026-11-10, waiting for the owner's approval.

## What we want

A unit on Wi-Fi finds the newest published firmware on its own, from this repository's GitHub releases, over a secured connection. It offers it the way it offers an update today. A local host on the bench (`scripts/ota_host.py`) still takes precedence, so a build can be put on a unit before it is released.

## Owner's direction (2026-11-10)

- Look for the latest release on GitHub instead of the hard-coded update site, which never works.
- The GitHub connection is over a secured channel (HTTPS).
- The local override stays, for updates on the bench.
- The check stays once per boot, after Wi-Fi connects. No change to when it runs.

## What we know (checked 2026-11-10)

- **How a unit looks for an update today** (`ota_mode.c`):
  - once per boot, after Wi-Fi connects, `ota_poller_task` tries two hosts in order, `owmc_update.local` and `http://owmc_update.vfnz.pro`, each on port 8080, for `/p1/manifest.json`;
  - the first manifest read ends the search. If its version is newer than the running one (`parse_git_describe_version`, `compare_version`), or `ignore_version` is set, the unit offers it;
  - Install downloads the manifest's `path` from the same host, on the manifest's `port`, with `esp_https_ota`;
  - `owmc_update.vfnz.pro` does not resolve, so a unit away from the bench never finds an update.
- **The manifest** (`ota_manifest_t`, `ota_mode.h`): `manifest_version`, `version`, `path` (128 bytes), `note` (1024 bytes), `port`, `ignore_version`, `type` and `importance`. All are required; a missing field rejects the manifest. It lives in RAM only: nothing is stored.
- **The local host:** `scripts/ota_host.py` announces `owmc_update.local` by mDNS and serves the manifest and the image over plain HTTP. `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` is on for it. ADR-013's turret unit and the release check (docs/releases/README.md, step 4) depend on it.
- **Releases:**
  - a version tag makes the build workflow draft a release with `<name>.bin`, `<name>-ota.zip` (`manifest.json` and the image), the files for flashing over USB, and their SHA-256;
  - the owner publishes the draft after trying it on a unit;
  - v1.0.0, v1.1.0 and v1.2.0 are published. No release has a `manifest.json` of its own.
- **GitHub's fixed link for the newest release:**
  - `https://github.com/eamars/OpenWeaponMountComputerFirmware/releases/latest/download/<file>` redirects (302) to `/releases/download/v1.2.0/<file>`, which redirects again to `release-assets.githubusercontent.com`. Checked with `bootloader.bin`.
  - "Latest" skips drafts and pre-releases, so publishing a draft is what hands it out.
  - It is not the REST API: no rate limit of 60 requests an hour, no JSON of tens of KB, and no User-Agent rule.
  - The last redirect's `Location` header is 919 bytes. The manifest fetch's HTTP client uses the default 512-byte buffer, which would cut it off; the image download already uses 4096 bytes (`OTA_READ_BUFFER_SIZE`).
- **TLS on the unit** (`sdkconfig`):
  - the certificate bundle is in, with the full set (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`);
  - mbedTLS allocates in PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), with dynamic buffers;
  - certificate dates are not checked (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off), so the unit needs no clock (no SNTP).
- **Room:** each app partition is 4 MB; the v1.2.0 image is 2.45 MB.
- **No app rollback** (`CONFIG_APP_ROLLBACK_ENABLE` off), as in ADR-013: a bad image needs USB to recover.

## What we will do

1. **The release carries its manifest.** The build workflow writes `manifest.json` beside the OTA package on every build, with one new field, `url`: the HTTPS address of that version's image on its release (`https://github.com/<repository>/releases/download/<tag>/<name>.bin`). A tag's release uploads it with the other files. `ota_host.py` writes it (`--url`), so the workflow and the bench share one writer.
2. **The unit looks in two places, in order:**
   1. `owmc_update.local`, as today: the local override. If it answers, its manifest decides and GitHub is not asked.
   2. `https://github.com/<repository>/releases/latest/download/manifest.json`, with the certificate bundle and a 2 KB header buffer for the redirects.

   `owmc_update.vfnz.pro` is removed. The repository (`eamars/OpenWeaponMountComputerFirmware`) is one constant; a fork changes that line.
3. **Install:** when the manifest has a `url`, the image is downloaded from it, over HTTPS with the certificate bundle. Without one (`ota_host.py`'s own manifest), it comes from the host, path and port as today. `url` is optional, so the units still on v1.2.0 keep reading `ota_host.py`'s manifests, and can be brought onto this firmware that way.
4. **Nothing else changes:** once per boot after Wi-Fi connects, the offer's screens, the version comparison and the importance rule. No stored settings change.

## How the unit shows it works

On the turret unit, on battery, over the Wi-Fi remote control:
- **The local override:** with `ota_host.py` running, the unit offers the local build, as today.
- **From GitHub, a real release end to end:** a test build that reports `v1.1.9` (a `version.txt`, not committed), with no local host. The unit offers v1.2.0 from GitHub; Install downloads it over HTTPS, and the unit restarts on v1.2.0. `ota_host.py` then puts this ADR's build back.
- **Not offered when newer:** this ADR's build, with no local host, reads GitHub's manifest and offers nothing.
- **The cost:** internal RAM and the largest DMA block (`stats`) after the HTTPS check, and `state_change` with `--min-largest-dma 70000`. The download time over HTTPS against the local host.
- **The workflow:** this branch's build artifact holds `manifest.json`, its `url` pointing at the build's own file name.

## Decision for the owner

**A manifest for v1.2.0.** GitHub's link has nothing to find until the latest published release has a `manifest.json`. The end-to-end proof needs one, and so does every unit on this firmware until the next release.
- Recommended: add `manifest.json` to the published v1.2.0 release (`gh release upload`), written by the same `ota_host.py` from v1.2.0's note, with `url` pointing at v1.2.0's own `.bin`. It is additive: v1.2.0's files and notes stay as they are.
- The alternative: tag a new release after this ADR is merged, and run the GitHub proof then. The ADR would stay open past its PR, against the rule that an ADR closes before its PR.

## Not in this ADR

- Checking more often than once per boot, or a "check now" button.
- Signed images (secure boot) and app rollback.
- Pre-release channels.

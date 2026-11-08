# Releases

Each release has its notes here:
- `<tag>.md`: the release page's text;
- `<tag>_note.txt`: the short note a unit shows when it offers the update (up to 1023 bytes).

How a release is made:
1. Write both files and commit them to `main`.
2. Tag that commit (`git tag -a v1.2.0 -m "Firmware v1.2.0"`) and push the tag.
3. The [build workflow](../../.github/workflows/firmware.yml) builds the tag and checks that the firmware reports exactly the tag's version. It then creates a **draft** release with the notes, the firmware, the OTA package, the files for flashing over USB, and their SHA-256.
4. Try the update on a unit from the draft's OTA package: `python scripts/ota_host.py --package <ota.zip>`, then restart the unit and install.
5. Publish the draft.

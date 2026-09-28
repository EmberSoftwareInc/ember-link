# Hardware smoke test — 2026-09-20

Ember Link `0.1.0-dev` was flashed and tested on one physical ESP32-S3
revision 0.2 dongle previously running EmberConnect `0.5.1`.

Firmware image SHA-256:
`f53e8570968c18a84308c78d96c3e9264319953d175b509f5840131b8eb17782`.

## Passed

- Saved a complete 16 MiB recovery backup locally before flashing. The backup
  is private (mode 0600) under ignored `artifacts/`; it contains device settings
  and must not be published.
- Checked the chip identity and security state. Secure Boot and flash encryption
  were disabled. No eFuses were changed.
- Flashed the bootloader, partition table, initial OTA data, and application with
  esptool. Written-data verification passed. NVS and microSD were preserved.
- After a normal unplug/replug, USB CDC reported Ember Link `0.1.0-dev`, USB
  protocol 1, cloud protocol 1, and a confirmed boot (no pending OTA verification).
- Stored Wi-Fi credentials reconnected successfully. USB Wi-Fi scan returned 22
  networks. The LAN health endpoint reported the new firmware.
- Cloud status reported unconfigured, disabled. Enabling before configuration,
  passing a non-boolean enable value, and configuring an insecure HTTP API origin
  all returned the expected errors without changing that state.
- USB mass storage mounted on macOS. This particular card presents approximately
  126 MB and is FAT16; firmware reported usable storage and free space.
- Through a temporary authenticated LAN pairing, uploaded a new 131,072-byte
  test file and read it back through the USB drive with matching SHA-256.
- Replaced that file with different 65,536-byte content; USB readback hash and
  firmware file inventory both matched the replacement.
- Unmounted the host volume before each device-side write. Automatic USB
  disconnect/reconnect and volume remount worked after each operation.
- Deleted the temporary test file and revoked the temporary pairing. Existing
  non-hidden root files were compared by SHA-256 and remained unchanged.
- All three native suites passed again: protocol validation, file transaction
  recovery, and the mocked cloud worker with sanitizers.

## Not yet validated on hardware

No cloud backend or device credential was configured. Actual HTTPS polling,
download TLS handshakes, cloud job delivery, NVS receipt persistence across power
loss, and backend acknowledgements still require a compatible staging backend.
The LAN transfer above exercises the shared file writer but does not establish
that a complete cloud transfer works on hardware.

No embroidery machine was attached. Machine compatibility, interrupted-power
recovery, invalid cloud payloads on physical hardware, sustained transfer/idle
behavior, and memory/stack headroom remain on the validation checklist. Browser
provisioning was not tested; the existing Wi-Fi configuration was retained.

The device was left running Ember Link with cloud unconfigured and disabled.

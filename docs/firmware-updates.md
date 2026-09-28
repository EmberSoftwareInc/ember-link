# Firmware updates and future cloud transports

Ember Link 0.3.0-dev retains HTTPS polling and adds a signed, customer-approved
firmware delivery path. MQTT/push is not implemented. A later image can add it
without changing the design file delivery contract or requiring a USB cable,
provided the shipped device runs this foundation and the release is compatible.

## Customer experience

The Ember app's Send to Ember Link panel has a Firmware section. It shows the
running version, an operator-published compatible release, release notes, and an
Update firmware action. The customer confirms that the machine is idle and the
dongle may restart. Power must stay connected throughout the update.

The app reserves the device for the update. Its next poll gets an immutable,
time-limited HTTPS download grant. The device downloads to the inactive slot,
checks the exact length, SHA-256, project/chip/version, and RSA signature, records
the pending reboot, then selects the new boot slot and restarts. A later poll
reports installed only after local services are healthy. No design is delivered
while the update's outcome remains unresolved.

Queued installs can be cancelled. Consent expires after ten minutes if the
device has not collected the update. Once offered, the grant lasts ten minutes;
network transfer is separately bounded to five minutes. If no terminal receipt
arrives, the app shows an unknown outcome and requires checking the dongle before
clearing the reservation. It never treats silence as successful installation.

## Boot and power-loss recovery

- The bootloader retains the previous OTA slot. The new image starts a 90-second
  health guard before NVS and SD initialization. Wi-Fi connected + local HTTP/mDNS
  running, or a working setup hotspot + local HTTP, confirms the image. Cloud
  availability is not part of this check. A missing SD card or failure to reach
  those local service states leaves the new image unconfirmed and triggers rollback.
- Failure to mark the image healthy is checked, and does not disable the guard.
- `link_cloud/update_v1` persists the update identity, artifact hash/version,
  previous slot, attempt, ownership generation and outcome. An interrupted
  download becomes failed. A pending reboot that returns to the old slot becomes
  rolled_back. Receipt acknowledgments are persisted, and duplicate offers do not
  reinstall the last update.
- Signature verification and boot selection are separate operations. The
  reboot journal must commit between them. A journal failure cannot select an
  unrecorded image. OTA uses the same operation gate as local/cloud design writes.
- Polls repeat terminal receipts until acknowledged. No reboot or reset erases
  identity or receipts to recover from an NVS error. Persistent NVS errors need
  service; do not erase an enrolled device merely to make a test pass.

## Settings and partition compatibility

The existing `link_cloud/config` and `receipt` schema-1 binary records are frozen
and remain authoritative. Wi-Fi and local pairing storage are unchanged.
`cloud_settings_migrate()` adds a separate JSON `settings_v2` record containing
schema 2 and transport `poll`, without rewriting legacy keys. Unknown schemas or
transports fail closed. This lets an older image still read and modify its own
settings after rollback. Future transport settings must remain additive; do not
replace the schema-1 core or reinterpret its byte layout.

Both 3 MiB OTA slots and all existing partition offsets are unchanged. New full
installations reserve a 64 KiB NVS partition named `link_future` at `0x620000` for
future transport credentials. It is currently unused. An application-only OTA
does **not** update a partition table: older devices report zero reserved bytes,
and cloud updates work on their existing layout. Do not enable a future feature
requiring that partition based only on version; check the capability.

Polls advertise `firmwareUpdate:1`, board `lilygo-t-dongle-s3`, layout `link-v1`,
settings schema, actual inactive-slot capacity, actual reserved partition size,
and public signing-key digests. Diagnostics include current/minimum free heap,
reset reason and pending boot verification. These are metadata, not credentials.
All sizes must be checked again on the device, even after backend filtering.

## Preparing a release

Use the supported ESP-IDF environment from the DIY guide. Build using the
appropriate existing signing key. Do not generate a replacement production key
or publish a development-signed image to production devices.

```sh
idf.py -C firmware build
# Public key export; the private key stays with the release operator.
openssl pkey -in firmware/keys/ota_signing_key.pem -pubout -out /tmp/link-public.pem
python tools/firmware_manifest.py firmware/build/ember-link.bin \
  --public-key /tmp/link-public.pem --release-id link-030-dev \
  --notes 'Describe the changes for customers.' --output /tmp/link-release.json
```

The manifest tool verifies the image signature with the supplied public key,
checks the ESP32-S3/project identity, extracts the embedded version, and records
the artifact length/hash and trusted-key digest. Pass the image and manifest to
the ember-app administrator publisher described in `docs/ember-link-firmware-updates.md`.
Use a unique release ID and version for each distinct artifact. The initial
0.3.0-dev image is the bootstrap foundation; older firmware needs a USB or local
signed update first. Publishing it cannot teach an older image to poll for OTA.

## Signing-key lifecycle

Keep production private keys outside source control and outside Lambda/S3/device
credentials. Keep an offline recovery copy and record the public digest used for
each manufactured batch. The backend only stores signed images and public digests.

ESP-IDF's current update policy trusts signing keys present in the running
signed application's signature blocks. For planned rotation, qualify a transition
image signed with both the old and new keys on hardware, publish it to the old-key
channel, then publish new-key images to the new-key channel after that transition.
The current build defaults sign with one key; the rotation packaging workflow is
an operator task, not automated by this publisher. Qualify the complete old →
transition → new → rollback sequence before rotating a production batch. An
unreachable/lost old key can require physical recovery. Do not assume this is an
emergency revocation mechanism: this development configuration does not enforce
hardware Secure Boot or flash encryption, and has no eFuse anti-rollback policy.

## Required release qualification

`python3 tests/run_native.py` runs production C with ASan/UBSan, including update
streaming, hash/signature rejection paths, NVS failures, reboot/rollback outcomes,
acknowledgment replay, truncated images, and additive settings migration. These
platform fakes do not prove ESP bootloader/power-loss behavior on the board.

Before shipping, test two differently versioned signed images on actual hardware:

1. Bootstrap, enroll, install a cloud release, confirm its version and preserved
   Wi-Fi/account identity, and transfer a design afterward.
2. Disconnect power during download, after verification/before boot selection,
   and while the new image boots; check the eventual receipt and both OTA slots.
3. Use a deliberately unhealthy signed image and verify automatic rollback after
   the health deadline. Retest with Wi-Fi/cloud unavailable and a missing SD card.
4. Reject corrupted, truncated, wrong-key, wrong-board, wrong-version and oversize
   images without changing the running slot. Verify stable free heap during TLS.
5. Retain USB BOOT recovery and a private per-device backup throughout testing.

A future MQTT implementation should send a wake-up notification, then use the
same authenticated fetch/receipt path. Keep polling as a reconnect/fallback path,
introduce capabilities and transport configuration gradually, and preserve the
ability to roll back without losing the polling credentials.

## Guided local updates and a shared release catalog

See [consumer releases](consumer-releases.md) for the 0.3.5-dev USB/local
compatibility fields and the catalog used by both Bridge and cloud publishing.

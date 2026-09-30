# Ember Link

Firmware for a WiFi USB embroidery adapter, initially targeting the LilyGo
T-Dongle-S3 (ESP32-S3, 16 MB flash, no PSRAM, FAT32 microSD). The adapter exposes
its card as USB mass storage to an embroidery machine.

This repository starts from EmberConnect and develops the new Ember Link
product. This iteration focuses on **cloud transfer and browser setup**. The project/image name
is `ember-link` and the display name is `Ember Link`. The current branch's version
is defined in [app_version.h](firmware/main/app_version.h). Legacy firmware/Bridge
identity compatibility is not preserved.

The latest qualified stable release is **0.3.6**. See its
[qualification record](docs/release-qualification-0.3.6.md) for the exact image,
cloud settings tests, and Brother NQ1700E preview checks. Earlier validation
records describe their original builds, not the current qualification status.

## Build your own

Use the [browser installer](https://embersoftwareinc.github.io/ember-link/) for
a qualified prebuilt first installation when available, or start with the
[DIY quick start](docs/diy-quick-start.md) for the parts,
firmware installation path, guided Wi-Fi setup in Bridge, and your first local
transfer. No Ember account is needed for that local workflow.

The [full build and installation guide](docs/diy-build-guide.md) contains the
build commands, manual setup alternatives, recovery instructions, and separate
requirements for cloud enrollment.

## USB setup and machine use

Link starts as USB storage only. To use Web Serial or Bridge USB setup, connect
it to a computer normally, wait for startup, then press and release BOOT twice
quickly. Unplugging returns it to storage-only mode for the machine. Use FAT32;
see [USB modes](docs/usb-modes.md) for timing, reset behavior and test limits.

## Status screen

The LilyGO LCD shows connection status, setup guidance, file-transfer progress,
and firmware updates. It dims after 30 seconds of inactivity. Bridge and web USB
setup can save screen on/off, normal/upside-down orientation, and status LED on/off. See
[status display](docs/status-display.md) for messages, rotation, and validation limits.

Firmware `0.3.6` also accepts these settings through the existing
cloud poll, with persisted revisions and receipts to protect newer USB changes.
See [cloud settings](docs/cloud-settings.md) for the backend contract and rollout
requirements. A working backend/browser reference is available in the private
`EmberSoftwareInc/ember-link-cloud-example` repository. Production cloud rollout
remains a separate deployment.

## Current implementation

```text
Ember backend ← authenticated HTTPS poll ← Ember Link
Private storage ← presigned HTTPS download ← Ember Link → microSD → USB machine
```

- Cloud is unconfigured and disabled on a fresh device. No production URL or
  credential is embedded in source.
- Physical USB configuration sets a device ID, unique bearer credential, HTTPS
  API origin, and exact download-host allowlist.
- The dongle validates one download job's ownership generation, expiry,
  filename, length, and SHA-256, then streams it through a 4 KiB buffer.
- TLS verifies certificates/hostnames; redirects are rejected. Device tokens
  never accompany file downloads.
- Staged writes and backup/commit markers preserve existing files across the
  tested replacement-failure paths. A durable receipt precedes each job.
- Receipts repeat until acknowledged. Interrupted delivery reports
  `needs_reconciliation` instead of silently repeating the job.
- Cloud/local writes and signed updates share an operation gate. HTTP 401/403
  disables cloud persistently until explicitly re-enabled.

The USB `cloud_claim` command confirms a short-lived account setup proof through
the authenticated cloud poll. Consumer browser setup and its backend live in
the separate `ember-app` repo at `/connect`; factory credentials must be
provisioned before consumer setup. Neither the permanent credential nor WiFi
password is sent to the browser account API.

WiFi provisioning, local HTTP transfers, USB setup, LEDs, and signed updates
remain inherited foundations. Enrollment automation, live cloud byte progress,
production cloud firmware distribution, and remote file management remain separate work.
The reference backend has exercised signed cloud firmware delivery on hardware.

## Release workflow

Use the [stable/dev release lifecycle](docs/release-lifecycle.md) for branch rules,
numbered dev builds, signed packages, qualification, stable promotion, and withdrawal.
Pushing code runs checks; publication is always an explicit operator step.

## Build

Tested with ESP-IDF **6.0.2**, pinned in the manifest. Component versions are
locked in `firmware/dependencies.lock`. Load the installed IDF environment:

```sh
source /path/to/esp-idf/export.sh
mkdir -p firmware/keys
# Generate once for development; do not replace an existing key.
espsecure generate-signing-key --version 2 firmware/keys/ota_signing_key.pem
idf.py -C firmware build
```

Signing keys and build output are ignored by Git. A developer key produces an
image trusted by your own DIY units. This is separate from Ember’s official
Development release channel, which uses the same production key as Stable. No
signing key from EmberConnect is included. The image is
`firmware/build/ember-link.bin`. See the [DIY guide](docs/diy-build-guide.md)
for first-install and recovery steps.
Use a stable release tag for a reproducible DIY build. The `dev` branch contains
work toward the next release; a successful local build is not hardware qualification.

## Tests without hardware

After resolving managed dependencies, install a native C compiler, Python 3,
`pkg-config`, and OpenSSL development headers, then run:

```sh
python3 tests/run_native.py
```

Eight suites execute production C with AddressSanitizer and UndefinedBehaviorSanitizer:

1. Protocol validation: malformed jobs, paths/URLs, size/hash/expiry, ownership,
   and receipt acknowledgement matching.
2. Host-file transactions: injected rename failures, digest/length mismatches,
   insufficient space, and simulated reboot states.
3. Cloud worker with simulated ESP-IDF HTTP/NVS/storage: TLS configuration,
   token isolation, journal-before-write, duplicates, unresolved receipts,
   authentication rejection, and reboot recovery.

4. Signed-update state machine: truncated/oversize images, failed verification,
   interrupted downloads, reboot/rollback receipts, NVS faults, and settings migration.

5. USB-mode button timing, double-press setup, and reset behavior.
6. Display status priority, committed-file success, progress bounds, filename
   sanitization, dimming, clock rollover, and framebuffer bounds.

7. Display preferences: persisted orientation/enabled state, invalid values,
   default recovery, and NVS write/commit failures.

8. LED output: suppressed states/blinks, disable during a blink, restoration of
   the current status, and serialized GPIO frames.

See [firmware updates](docs/firmware-updates.md) for customer-approved cloud
installation, release preparation, signing-key lifecycle, and hardware qualification.

These do not validate real TLS handshakes, FAT power loss, USB timing, or
physical-device RAM headroom.

## Configure a development device

Register a device ID and unique lowercase 64-character hex token with a staging
backend implementing [the v1 contract](docs/cloud-protocol.md). Provision WiFi
using the existing USB commands (after entering USB setup mode) or setup hotspot. With `pyserial` installed:

```sh
python3 tools/cloud_configure.py \
  --port /dev/cu.usbmodemEXAMPLE \
  --api-base-url https://api.example.com/ \
  --download-host files.example.com \
  --device-id device-example \
  --enable
```

The token is entered at a hidden prompt. Configuration leaves cloud disabled
unless `--enable` is passed. This tool does not register devices or claim
accounts. Publicly trusted HTTPS certificates are required; verification cannot
be skipped. Close other serial clients before use.

Factory reset clears WiFi/local pairing/name and disables cloud, while retaining
cloud identity and unsettled receipts. It does not unclaim the server-side owner.

## Handoff

- [Implemented cloud protocol](docs/cloud-protocol.md)
- [Hardware validation checklist](docs/hardware-validation.md)
- [Proposed backend architecture](docs/cloud-backend-architecture.md)
- [Broader roadmap](docs/cloud-and-web-setup-implementation-plan.md)

The architecture and roadmap include future work. The implemented protocol is
authoritative for this development iteration. The repository is hosted at
[EmberSoftwareInc/ember-link](https://github.com/EmberSoftwareInc/ember-link).

## License

Ember Link is open source under the [MIT License](LICENSE). Third-party code
and dependencies retain their own licenses and copyright notices. See
[third-party notices](THIRD_PARTY_NOTICES.md) for the LilyGO display attribution.

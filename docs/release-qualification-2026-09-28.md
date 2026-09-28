# Release qualification — 2026-09-28

Status: complete for stable publication. Ember Link 0.3.5 is published as the
latest stable release. Tests use
one LilyGO T-Dongle-S3 prototype and its FAT32 card. Initial trials used the
development key; the stable candidate now uses the separate production key. Neither production AWS nor the web app repository is involved.
Existing root design files were privately backed up and hashed before updates.

## Completed checks

- Bridge 0.5.0 advanced USB recovery installed signed Link `0.3.5-dev` from
  `0.3.4-dev`. Bridge reported a successful restart; independent authenticated
  status confirmed the same device, the other OTA slot, and `pendingVerify:false`.
- The new board/layout/key compatibility fields are available through the local
  firmware endpoint. Guided catalog delivery was subsequently qualified against
  the public production-signed candidate, as recorded below.
- Local Wi-Fi rejected wrong-format, truncated, corrupt, and unrelated-key signed
  images. The confirmed running version/slot did not change. An incomplete
  upload disconnected after 64 KiB also left the running image intact.
- A healthy, separately versioned diagnostic image installed through the local
  Wi-Fi endpoint and confirmed its boot. This validates the device transport,
  not the entire Bridge catalog/download/install UI.
- After the rollback trials, USB inspection verified the same Wi-Fi settings,
  cloud enrollment, device name, screen/LED settings, and both original design
  hashes. The SD card was unmounted again before interruption testing.
- All eight native firmware suites passed before and after the guard change;
  both catalog tests passed. Bridge passed 22 UI tests, 64 Rust tests, its version
  consistency check, and the production frontend build/bundle check.

## Rollback investigation

The deliberately unconfirmed `0.3.5-qa2` fixture retained the production
90-second guard and 3 KiB health-task stack. It returned to the previous healthy
image after about 90 seconds, but reset reason was 5 (`ESP_RST_INT_WDT`). RTC
instrumentation showed that the guard reached the rollback call. The earlier
watchdog observation therefore reproduces with current code.

An otherwise equivalent `0.3.5-qa3` fixture increased only that task's stack to
8 KiB. It returned to the same healthy image with reset reason 3 (`ESP_RST_SW`)
after about 90 seconds. ESP-IDF's rollback API verifies the previous signed
application before marking the current image invalid; a timer-sized stack is
insufficient for that path on the tested build. Watchdog settings and signature
verification were not relaxed. A second `0.3.5-qa4` comparison split the SDK helper into its existing mark-invalid
and software-restart calls solely to measure stack after validation. It also
returned to the healthy image with reset reason 3 after about 90 seconds.
The 8,192-byte task had 4,388 bytes remaining after verification: approximately
3,804 bytes had been used, exceeding the original 3,072-byte allocation.
Production retains the normal SDK rollback-and-reboot helper with an 8 KiB stack.

The initial source candidate was `0.3.5-rc1`, with the 8 KiB guard stack and no QA hooks.
Diagnostic images are temporary test artifacts and must never be released.

## Physical power interruption during USB upload

The device acknowledged 65,536 bytes written to its inactive slot. The host then
streamed slowly and the user unplugged power before the image was complete,
waited five seconds, and reconnected normally. The host recorded a serial
disconnect while the upload was incomplete. The dongle returned to the same
confirmed healthy image/slot with power-on reset reason 1 and storage-only USB.
Both pre-test design hashes matched, and the card was safely unmounted again.

This qualifies interruption during a local USB image write on the test unit.
It does not qualify the separate cloud update journal, a power cut between image
verification and boot selection, or interruption during the new image's boot.

## Exact candidate installation and preservation

Signed `0.3.5-rc1` installed over the authenticated local Wi-Fi update endpoint,
changed from `ota_0` to `ota_1`, and confirmed its healthy boot. Its SHA-256 is
`4bfcfa5e9d7c5aa40e00d62e03ddccd9d505ac7f5add53f1d749143ee25e58e0`.
USB inspection confirmed the saved Wi-Fi, cloud enrollment, device name, screen
orientation/enabled state, and LED setting were unchanged. Both original design
hashes still matched.

A new generated `RC035.PES` (1,371-byte small square) was transferred over local
Wi-Fi and read back byte-for-byte through USB storage. Its SHA-256 is
`c945d4e3c55356f8b15580397c859335e8843d689ebb704cea3beb032ed0a452`.
No existing file was overwritten. The card was safely unmounted before moving Link to the Brother NQ1700E.
The user confirmed that the square previews correctly and the machine remains
responsive on this exact release candidate. No stitching was initiated.

## Stable-publication follow-up

The owner confirmed no customer units have shipped. A separate RSA-3072
production key and verified private recovery copy have been created. Only the
public verification key is stored in the repository. Candidate 0.3.5 is built and signature-verified. The prototype was provisioned
with the production key through an application-only USB recovery flash, then
updated to the exact 0.3.5 binary over the isolated cloud HTTPS path. The boot
confirmed in the other slot and the service received an `installed` receipt.
SHA-256: `364167c2b7ba87658972d040c26ea5a1e154807ea9fefcd252c145a079b46cca`.

The owner intentionally removed ELCHECK.PES and RC035.PES after preview testing.
Those deletions are not data loss. The remaining original design matches the
saved backup hash and the FAT32 filesystem check passed.

Using an isolated HTTPS service and disposable test credentials, physical power
loss after 64 KiB of a cloud firmware write returned to the same confirmed
image/slot and delivered `failed/interrupted` to the service. Power loss after
signature verification and journal persistence, before boot selection, also
returned to the healthy slot and delivered `rolled_back/boot_not_confirmed`.
Download requests did not carry the device's Authorization header.

An initial diagnostic HTTP status hook could settle the journal while the cloud
worker was paused. Production serial/cloud status is protected by the session
mutex; this was a fixture defect, not a production fix. That trial was excluded,
and the boot-selection check was repeated with passive diagnostics. The corrected pending-boot test also passed: the image and journal were both
pending before power loss; within the 300-second diagnostic operator window,
power loss returned to the previous healthy slot (reset reason 1) and delivered
`rolled_back/boot_not_confirmed`. The production 90-second automatic guard had
already been qualified separately. All diagnostic builds remain private.

A dual-signed key-transition trial was rejected without changing the running
image. SDK inspection confirmed that signed updates without eFuse secure boot
verify only the first signature block/digest. Production-key provisioning uses
physical USB recovery; see [signing](production-signing.md).

The exact production 0.3.5 image rejected a development-key update with
`invalid_signature`; the confirmed running version and slot were unchanged.
Anonymous downloads of the public prerelease catalog and image matched the
qualified SHA-256 exactly. The same immutable assets were promoted to the latest stable release after qualification.

## Public guided delivery and storage checks

Both guided Bridge update paths passed against the public 0.3.5 prerelease:
local Wi-Fi and USB. Bridge downloaded the immutable public catalog/image,
installed it, and displayed successful healthy-boot confirmation. Independent
inspection confirmed version 0.3.5, a changed OTA slot, and no pending boot
verification. Each path began on a separately versioned, production-key baseline.
The private Bridge qualification build changed only the catalog URL from the
standard latest-release URL to the prerelease's immutable tag URL. No such
fixture change is included in the Bridge release.

A temporary filler left approximately 32 KiB free on the FAT32 card. An 80,008-byte
upload failed with `insufficient_storage`, with no partial target file and the
original design intact. The filler was removed; filesystem verification passed.
A new generated EL035.PES square was uploaded and read back byte-for-byte, with
the same square SHA-256 recorded above. The original design still matched its
private backup, and intentionally deleted test files were not restored.

The isolated service recorded 360 polls across approximately 63 minutes of
qualification, including intentional firmware installations and resets. A separate
bounded uninterrupted sample covered 94 polls over 782 seconds. This is not a
multi-day endurance qualification. All six signed-image download requests omitted
the device Authorization header. The disposable cloud polling profile was disabled
after testing; saved local Wi-Fi, pairing, and device identity were retained.

Bridge 0.5.1's signed upgrades and application startup passed on macOS, Linux
AppImage, Windows NSIS, and Windows MSI. See the Bridge qualification document
for the harness scope and platform-specific observations.

## Final physical checks and publication

With the card removed while unplugged, the owner confirmed that Link displayed
its missing-card warning. After reinserting the card while unplugged and moving
Link to the Brother NQ1700E, the owner confirmed EL035.PES previewed as a square
and the machine remained responsive. This was the exact production-signed 0.3.5
image. No stitching was initiated.

[Ember Link 0.3.5](https://github.com/EmberSoftwareInc/ember-link/releases/tag/v0.3.5)
and [Ember Bridge 0.5.1](https://github.com/EmberSoftwareInc/ember-bridge/releases/tag/v0.5.1)
are published as stable. Anonymous latest-release downloads returned the qualified
Link catalog/image and Bridge updater feed byte-for-byte (JSON-equivalent for the
Bridge feed). All Bridge platform entries point to immutable 0.5.1 assets.
Production AWS deployment and web app changes remain outside this work.

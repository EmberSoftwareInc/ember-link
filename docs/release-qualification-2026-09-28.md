# Release qualification — 2026-09-28

Status: in progress. No firmware release has been published. Tests use one
LilyGO T-Dongle-S3 development unit, the existing development signing key, and
its FAT32 card. Neither production AWS nor the web app repository is involved.
Existing root design files were privately backed up and hashed before updates.

## Completed checks

- Bridge 0.5.0 advanced USB recovery installed signed Link `0.3.5-dev` from
  `0.3.4-dev`. Bridge reported a successful restart; independent authenticated
  status confirmed the same device, the other OTA slot, and `pendingVerify:false`.
- The new board/layout/key compatibility fields are available through the local
  firmware endpoint. Guided catalog delivery is not yet qualified: the firmware
  repository remains private and has no public latest catalog.
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

The source candidate is `0.3.5-rc1`, with the 8 KiB guard stack and no QA hooks.
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

## Still required before stable publication

- Complete power-interruption checks at the other update boundaries.
- Qualify the complete guided Bridge Wi-Fi and USB update flows using a public
  catalog, including failure and reboot confirmation.
- Complete interrupted cloud download/journal and boot-selection power-loss
  checks, missing/full-card checks, and a longer reliability run.
- Select and back up the production signing key; qualify the production-signed
  artifact. These development-key tests are not manufacturing approval.
- Provide a public account-free firmware distribution location. Publishing a
  draft/private release does not make Bridge's configured public feed usable.
- Qualify Bridge installers and signed app upgrades on macOS, Windows, and Linux.
  All three cross-platform draft installer builds succeeded; no app update has
  been published by this work.

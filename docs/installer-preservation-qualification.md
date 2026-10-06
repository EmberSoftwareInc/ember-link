# Browser installer configuration preservation qualification

Status: implementation on `feature/installer-preserve-settings`; not deployed.
Firmware remains the published 0.3.8 binary. Cloud, Bridge, and web setup are unchanged.

## Review status and deferred checks

The operator approved committing and reviewing this work without purchasing another
board. Physical qualification of the new preloaded-firmware confirmation/erase flow
is deferred because no untouched board is available. Its automated and simulated
browser checks passed, but they are not a substitute for that hardware test.
Existing Bridge pairing retention also remains unverified on hardware because the
spare had no working pairing before testing. These limits must accompany release
review; this document does not assert full hardware qualification or deployment.

## Local checks

On 2026-10-06, the initial 27 installer tests and all 20 Python release-tool tests passed;
the production JavaScript bundle built successfully. The exact published 0.3.8
package passed signature verification and installer partition-table validation.
Chrome loaded the initial local preview with the install button enabled and no
unconditional internal-flash erase checkbox. After adding the preloaded-firmware
path, all 33 installer tests passed and the production bundle built successfully.

- Installer policy and mocked flash tests cover exact partition structure/checksum,
  full-flash blank detection, supported version pairs, read failures, pending OTA
  state, pending cloud receipts, write boundaries, interrupted writes, firmware
  readback and unchanged preserved-region digests.
- NVS parsing is tested against a wholly synthetic 24 KiB image created by
  `esp-idf-nvs-partition-gen` 0.1.9 from ESP-IDF 6.0.2, including a multi-page blob.
  Regenerate it using `installer/tests/fixtures/generate.py` in the IDF Python
  environment. No device dumps or credentials are test fixtures.

## Hardware checks required before publication

- [x] Private backup and identification of the spare before flashing.
      USB identity and healthy 0.3.8 boot checked; 148 card files including metadata
      backed up and card safely unmounted. Internal settings, boot metadata and
      reserved credentials privately backed up with readback checksums. The actual
      flash snapshot passes the new preservation preflight.
- [x] Existing linked 0.3.8: browser reinstall, healthy boot, unchanged cloud identity,
      Wi-Fi, display/LED settings, and card files.
- [x] Cloud design delivery after reinstall; Brother preview confirmed by the operator.
- [x] New cloud settings command after reinstall: LED disabled through production Devices, independently read back over USB, then restored on through cloud and verified over USB (screen on, normal orientation).
- [ ] Existing Bridge pairing remains usable, when a pre-test pairing exists.
- [x] Removed device: preservation and normal account re-linking.
- [x] Interrupted browser write: retry without erasing settings; unsupported partial
      layouts stop without attempting a destructive fallback.
- [x] Verified blank board: first installation, card preparation, and Wi-Fi setup.
- [ ] Recognized preloaded firmware: physical confirmation/erase/install flow (deferred; no untouched board available).

No hardware outcome is implied by the mocked tests. Unknown/custom firmware,
unrecognized layouts, unresolved update receipts, and incompatible downgrades are
intentionally refused. Erasure outside the recognized first-install path and
already-lost credentials remain support work.

The spare had no working Bridge token in the existing Mac token store before this
test; retention of an existing Bridge pairing therefore cannot be claimed from it.

## First browser attempt

The first physical attempt stopped with a serial-stream timeout before the write
phase (zero progress and no write-started error suffix). This is not a passed
installation. Review found that esptool-js 0.7.0's flash-read implementation leaves
the stub's trailing 16-byte MD5 packet unread. The installer now consumes and
verifies that packet before issuing another command; tests cover missing, malformed
and mismatched packets. Errors now identify the failing stage and explicitly say
when no write began. The clean download-mode retry subsequently succeeded as
recorded below; the original timeout was not instrumented sufficiently to prove
that the unread packet caused it.

## Browser reinstall completed

On 2026-10-06, after the clean download-mode retry, Chrome displayed:
"Ember Link 0.3.8 was written and verified. Saved settings were preserved and
verified." This confirms all four writes and the before/after preserved-region
checksums passed in the real browser flow. Post-restart settings, cloud and card
checks are recorded below; successful writing alone was not treated as a healthy-boot claim.

## Post-restart verification

Independent USB readback confirmed healthy 0.3.8 boot with no pending verification,
unchanged display/LED settings including revision, device name, provisioning state,
saved Wi-Fi network, and configured/enabled cloud device ID. Wi-Fi reconnected and
USB status reported cloud online with no pending enrollment. All 75 visible card
files matched their pre-install SHA-256 hashes and the visible file list was unchanged.
The card was safely unmounted after the read-only comparison.

A consistent read of the spare's production DynamoDB device record confirmed an
owner association, no revocation, version 0.3.8, matching current/last-poll ownership
generation, and an authenticated poll less than one second old. No AWS writes or
re-enrollment were performed. This verifies the existing credential still works.
The operator then moved the spare to the Brother, sent a design through Ember,
and confirmed that delivery and the design preview both succeeded. No account
removal or re-enrollment was needed after reinstalling firmware.

## Interrupted browser install and recovery

An isolated localhost-only bundle paused after two acknowledged compressed
application blocks. The pause hook was verified absent from the normal bundle.
The operator unplugged the spare for five seconds, then returned it to ROM download
mode. Read-only inspection verified the spare's MAC and confirmed a genuinely
incomplete application: the first 65,536 bytes matched the release, while the full
application MD5 did not. Its post-cut settings and boot metadata were privately
backed up and checksum-verified. The normal installer preflight accepted that
snapshot in preservation mode. The diagnostic RAM stub was returned to ROM mode
without writing flash, and the normal browser installer retry was requested.
The operator confirmed that the normal browser retry completed with both written-and-verified and saved-settings-preserved messages. Independent USB readback then confirmed healthy 0.3.8 boot, unchanged display, name, Wi-Fi and cloud identity. All 76 visible files matched the fresh pre-interruption hashes, with no file-list changes; the card was safely unmounted. A consistent production record read confirmed the owner association, no revocation, matching ownership generation and an authenticated cloud poll 12 seconds old. No account re-enrollment or AWS writes were needed.

The Devices UI briefly showed an unconfirmed-response/retry message during each cloud settings submission, then resolved to saved on device without a manual retry. The first command reached LED off at revision 3; the restore reached LED on at revision 4. This UI transition is recorded separately from firmware preservation and was not changed in this installer-only work.

## Removed-device reinstall and account re-linking

Spare Link was removed using the production Devices page, which confirmed removal. The operator then reinstalled the published 0.3.8 image with the normal local browser installer and confirmed both firmware and preserved-settings verification. Independent USB checks after restart confirmed healthy 0.3.8, unchanged display settings, name, saved Wi-Fi, and configured cloud device ID. All 76 visible files matched the pre-test hashes and file list; the card was safely unmounted. The operator completed normal USB account setup and confirmed that Spare Link reported ready. A subsequent consistent production record read confirmed an owner association, no revocation, and an authenticated poll from the current ownership generation less than 90 seconds old. This passed without clearing the backend identity or erasing the preserved cloud credential.

## Additional board inspection

The operator supplied another new/disposable board with a fresh microSD. Read-only inspection identified a different ESP32-S3 with 16 MiB flash. Whole-flash MD5 showed it was not blank: its app descriptor identifies arduino-lib-builder (45c1b25), with an Arduino partition layout incompatible with the Link preservation policy. No flash writes or SD changes were performed. The preservation-only installer at that point refused this layout. A new retail board may therefore require a deliberate first-install path for preloaded firmware; the current blank-board test must not be represented as qualification of an untouched retail board. The operator explicitly approved erasing this specific disposable board. The erase tool independently checked its MAC and 16 MiB capacity, erased only internal flash, and verified a whole-flash MD5 matching 16 MiB of 0xFF. Its microSD was untouched. The board was returned to download mode for the normal browser first-install test. The operator completed the normal browser install and reported the expected written-and-verified message without a settings-preserved claim. Independent USB readback after normal restart confirmed healthy 0.3.8 on the new board, no pending boot verification, no provisioning, no configured Wi-Fi or cloud identity, and default screen/LED settings. Initial read-only card status detected a 120 MiB card that was not FAT32. The operator completed browser card preparation and reported FAT32 card prepared and verified as EMBER LINK, then reconnected normally and configured Wi-Fi through Ember setup. Independent USB readback confirmed healthy 0.3.8, configured and connected Wi-Fi, no cloud identity, and the 125,829,120-byte card present as FAT32 with label EMBER LINK and maintenance mode off. This completes the blank-flash first-install and setup check; the new preloaded-firmware path added afterward still needs physical qualification on an untouched matching board.

## Explicit first-install path for recognized preloaded firmware

The installer now recognizes the reviewed Arduino layout and app descriptor and
requires two fresh confirmations before any internal erase. MAC and installation
state are rechecked after approval. Known Link installations never enter this path,
including incompatible versions, pending receipts and damaged settings. All 33
installer tests passed, including refusal/cancel with zero writes, residual Link
markers, changed boards/layouts, read failures, erase verification failure and
power loss after verified erase. The production bundle built successfully.

An isolated localhost-only browser fixture exercised the actual page and installer
logic against simulated flash, with no real USB access. Both confirmations started
unchecked; checking only one left erase disabled; cancellation made zero erases or
writes; restarting cleared consent; approving both completed exactly one erase and
one firmware write with verification. This does not qualify the physical factory
transition. The earlier new board had already been explicitly erased and installed
before this path existed. A matching untouched preloaded board is still needed for
that final hardware check. The configured new Link was not erased again.

Production web setup still describes the public installer as erasing internal
settings. Update that consumer copy after the new installer is deployed; the web
app source was not changed in this task.

# USB card preparation

Implementation status: local working tree, unpublished 0.3.7-dev.2 test build.
Physical formatting, FAT32 detection after a normal reconnect, and recovery
after the controlled formatting interruption have been confirmed on the spare’s
120 MiB card. A generated design preview also passed on the Brother NQ1700E,
with the machine staying responsive. These results cover this spare/card/machine
combination, not all supported card capacities or machine models. Earlier 0.3.6 browser-install checks do not qualify this feature.

## Protocol

All commands are newline-delimited JSON over USB CDC with ordinary request `id`
echoing. `info.cardPreparationProtocolVersion == 1` advertises support. Older
firmware must be shown an upgrade/separate-formatting explanation.

- `card_status`: read-only status containing `serial`, `maintenance`, `present`,
  `readable`, `fat32`, `canFormat`, and `capacityBytes`. Outside maintenance it
  uses the boot-time snapshot to avoid stealing storage from the USB host.
- `card_maintenance` with `driveEjected: true`: accepted only in physical USB
  setup mode, after acquiring the shared operation gate. Replies `rebooting`
  and restarts into a CDC-only session. It does not format anything. The user
  must reconnect the browser's serial session after this restart.
- In maintenance, `card_status` inspects the card and issues a fresh random
  128-bit `challenge` when formatting is supported. It expires after two
  minutes and is invalid after reconnecting USB or asking for new status.
- `card_format` requires `confirm: "ERASE_MICROSD"`, the matching dongle `serial`,
  and that session's `challenge`. Every attempt consumes the challenge. Success
  returns `verified: true` and `filesystem: "FAT32"` only after write/readback.

The browser clears consent on device session changes, status rechecks,
disconnects and attempts. A timeout is an uncertain result, never permission to
retry. Error recovery starts with another read-only check and explicit consent.

## Storage ownership and recovery

Normal machine operation retains the MSC-only profile. Normal computer setup
retains MSC+CDC when the card is usable. Unusable media allow CDC-only setup;
normal mode leaves USB disconnected until the user double-presses BOOT. No card
is automatically formatted. Startup proceeds far enough to run the BOOT gesture,
Wi-Fi setup and USB recovery without a usable filesystem.

Maintenance is latched in RTC memory for software resets and cleared on power
loss. It exposes no MSC interface, creates no MSC storage object, starts no
wireless services, and rejects other mutation commands. Only this mode can call
the raw formatter. Unplugging and reconnecting normally restores usual service.
The card must not be inserted, removed or swapped while powered.

Formatting operates directly on the dongle's SD card, never on another computer
drive. Its MBR/partition settings are described in [the installer guide](browser-installer.md).
A failed write leaves the card unverified. Formatting can be repeated after
another explicit confirmation; partial filesystems are never automatically
repaired by erasing them on boot.

## Automated checks

- `IDF_PATH=/path/to/esp-idf python tests/run_card_fs.py`: production formatter
  plus matching real FatFs, using sparse temporary files and address/undefined
  behavior sanitizers. Tests blank-card inspection without writes; 64 MiB, decimal 128 MB,
  128 MiB, decimal 256 MB, 256 MiB, decimal 512 MB, 512 MiB, 2 GiB, 4 GiB,
  8 GiB and 32 GiB formatting, plus cluster-size transitions; exact MBR layout; unsupported
  capacities/sector sizes; missing cards; injected write failures and retry.
- `python tests/run_native.py`: existing firmware protocol, settings, file,
  cloud, OTA, display and USB gesture regressions.
- `npm test --prefix installer`: package policy and USB JSON framing, request
  correlation, oversized responses, refusal, timeout/no-retry and status typing.

## Required physical checks before qualification

Use only the spare board and a card whose contents may be erased. Confirm:

1. An already formatted card is detected without changing its files.
2. Missing/unformatted cards allow the BOOT gesture and USB setup.
3. Maintenance removes the USB drive and disables wireless transfers.
4. Explicit formatting succeeds; readback, card layout and saved Wi-Fi/pairing
   are correct after normal reconnect.
5. A power cut during formatting leaves USB recovery available; retry succeeds.
6. The Brother reads a known PES design from the resulting card without freezing.
   This check was subsequently completed for dev.2 below; new candidates need their own recorded results.

Do not mark the factory release qualified based only on the host tests.

## Spare-board observations (2026-09-29)

- The user reported that the local 0.3.7-dev.2 image was written and verified.
- USB setup detected a 120.0 MiB card on the spare dongle.
- The user entered maintenance and explicitly selected **Erase and prepare as
  FAT32**. The subsequent card status reported FAT32.
- After instructions to disconnect, reconnect normally and re-enter USB setup,
  the user reported FAT32 again, with the normal-mode maintenance instructions.
  This confirms that the resulting filesystem remains detectable after restart.
- The earlier apparent status discrepancy was explained by the user having
  already clicked the formatting action; it is not evidence of auto-formatting.

The exact formatting-success/readback message was not separately confirmed by
the user, and no independent raw-card inspection has been performed for this
spare. These observations do not complete interruption or machine qualification.

## Controlled browser firmware interruption test

`node tools/make_installer_interruption_test.mjs /private/tmp/new-test-site`
builds a separate local test page from an existing Development preview. Serve
that directory on loopback port 8795. Keep the ordinary installer on port 8794
for recovery. The test page is never part of the Pages deployment.

It retains package/chip/security checks and the exact firmware bytes. Only the
browser transfer is instrumented: after the second compressed application block
is acknowledged, it pauses before further writes. The stub writes the previous
block while receiving the next, so the application is incomplete at this point.
The page then instructs the operator to unplug the spare, wait five seconds and
reconnect holding BOOT. Recover using the ordinary installer; the test page
will pause every time. Record both the observed pause/disconnect and successful
reinstallation plus normal boot. Do not infer a passed test from preparing this
harness. Firmware interruption does not format the SD card.

### Browser firmware interruption recovery

The user followed the separate browser power-cut test, reported reconnecting
in BOOT download mode, then ran the ordinary installer. They confirmed
“Ember Link 0.3.7-dev.2 was written and verified.” After normal reconnect and
double-press BOOT, USB inspection again reported the spare's 120.0 MiB card as
FAT32. This records successful browser reinstall, subsequent boot/setup, and
FAT32 detection after the firmware interruption. It does not qualify an
interrupted SD-card format or independently verify every existing file.

### Controlled SD-format interruption test prepared

A separate local source copy at `/private/tmp/ember-link-card-cut-test/firmware`
adds a diagnostic linker wrapper around `sdmmc_write_sectors`. It arms only
immediately before the existing formatter calls `f_mkfs`, waits for the first
successful write within the partition, reports its sector/count over USB, and
pauses. The original repository firmware has no such hook. The diagnostic page
on loopback port 8796 displays the pause event and requires the diagnostic
capability flag before permitting card commands. Neither artifact is published.

The normal installer on port 8794 retains the ordinary candidate application
SHA-256 `e6c624bef61195b66112c82be3eb4d310d47928dc2ec8ee85bd1cb95fa3dc1bd`.
After the diagnostic power cut, check normal boot/USB recovery, restore that
ordinary image, and explicitly prepare/verify the card again. The diagnostic
image always pauses when formatting and must not be left as normal firmware.
The observed recovery results are recorded below.

### SD-format interruption recovery results (2026-09-29)

The user followed the diagnostic formatting-interruption instructions and
reported that USB SETUP appeared after normal reconnect and double-press BOOT.
Following restoration instructions for the ordinary local 0.3.7-dev.2 image,
the user reported that the 120.0 MiB card was still identified as FAT32.
A mountable FAT32 filesystem alone was not treated as completed formatting.

The user then repeated **Erase and prepare as FAT32** in the ordinary installer's
maintenance flow without interruption and explicitly confirmed the final message:
“FAT32 card prepared and verified.” This confirms completion of the firmware's
format, write/flush, remount, readback and temporary-file removal checks on the
physical spare after the controlled interruption.

The diagnostic pause's exact sector/count was not separately captured in this
record; the interruption sequence is user-reported. This is one controlled
interruption point, not an exhaustive power-loss or media-health qualification.
The subsequent Brother preview result is recorded below. The final ordinary
format leaves the dongle in maintenance until unplugged and reconnected normally.

### Brother NQ1700E preview (2026-09-29)

The Mac identified the spare's 125,829,120-byte media with a FAT32 partition.
The previously used generated square was copied to the card as `DIYTEST.PES`
without replacing any different existing file. Its 1,371 bytes were flushed and
read back successfully. SHA-256:
`c945d4e3c55356f8b15580397c859335e8843d689ebb704cea3beb032ed0a452`.
The card was then successfully unmounted before the user moved the spare to
the Brother normally, without holding BOOT.

The user explicitly confirmed that the design preview opened and the machine
stayed responsive. This passes the previously deferred machine-preview check
for the ordinary 0.3.7-dev.2 candidate and the browser-prepared 120 MiB card.
It is a preview check, not a stitch-out or long-duration reliability test.

An additional read-only `fsck_msdos -n` check could not open the raw device due
to OS permissions; no filesystem repair was attempted or claimed. The evidence
above comprises successful OS mounting, file copy/readback, safe unmount and
physical machine preview.


## Card naming (local 0.3.7-dev.3 candidate)

Card preparation sets the FAT32 volume label to `EMBER LINK` and verifies it after remounting. Existing compatible cards can instead use **Rename to EMBER LINK** in the installer. Renaming preserves files, folders and partition layout; it does not format the card. Both actions require safely ejecting the drive and entering USB card maintenance. Reconnect normally afterward to refresh the name in the operating system.

`info.cardLabelProtocolVersion: 1` advertises naming support. `card_status` adds `label` (up to 11 printable ASCII characters) and `canRename`. In maintenance, `card_rename` requires `confirm: "RENAME_MICROSD"`, the device serial and the fresh single-use challenge from `card_status`. Success returns `verified: true` and `label: "EMBER LINK"` after a remount/readback. The browser never retries automatically. Older firmware can still check/prepare cards, but does not offer rename.

This naming candidate is unpublished. The earlier dev.2 physical qualification above does not establish physical qualification for this change; Finder name and existing-design checks remain pending.

Local validation for card naming passed: the production formatter and real FatFs under address/undefined-behavior sanitizers; nested-file byte preservation and unchanged MBR/boot sector on the 120 MiB layout; repeated rename with no writes; rejection of FAT16, missing and unformatted cards; injected write failure and recovery. All 11 installer tests, 19 Python release/package tests, native firmware suites, installer build and signed ESP-IDF firmware build passed. The local preview offers 0.3.7-dev.3; no GitHub release or recommendation was changed.


### Card naming hardware test (2026-09-30, in progress)

The spare with the 120 MiB card was updated from 0.3.7-dev.2 to the exact
production-signed 0.3.7-dev.3 candidate, SHA-256
`3f15710de3f38fbfb2e5097c05337e6df0dbfd3cfb057fba1c907389fde0a4ab`.
The device verified the image, rebooted, reported 0.3.7-dev.3 with no pending boot
verification, and advertised card-label protocol version 1. Device name, display
settings and provisioned flag matched the private pre-update baseline. All three
backed-up non-system files matched their original sizes and SHA-256 hashes.
The card was safely unmounted before maintenance.

The user entered maintenance through the browser installer, reconnected its USB
session, selected **Rename to EMBER LINK**, and reported successful renaming.
Normal-reconnect label/file checks and the resulting design preview are pending.

Three earlier USB upload attempts did not confirm completion. Bridge was found
holding the same serial port during the third attempt. After the user fully quit
Bridge and the test uploader acquired exclusive USB access, the complete upload
and signature verification succeeded. The incomplete attempts were not treated
as successful installs. Keep competing USB clients closed during qualification.

The user then reconnected normally and confirmed Finder displayed **EMBER LINK**.
Independent macOS inspection agreed: the volume UUID, partition offset, partition
size and FAT32 filesystem were unchanged, and all three backed-up file hashes
matched. The card was safely unmounted for the pending Brother preview.

The user confirmed that `DIYTEST.PES` still previewed as a square on the Brother
NQ1700E and the machine stayed responsive after renaming. This completes the
rename/persistence/file-preservation/preview check for the dev.3 candidate.
The format-and-name path will be checked on the final stable candidate.

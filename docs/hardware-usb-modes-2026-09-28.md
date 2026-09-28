# Integrated Link USB modes — 2026-09-28

## Initial image and behavior

Version `0.3.1-dev`, signed with the existing Link development key, 1,249,280
bytes; SHA-256 `852c7084ac699087d989855a9265e04242eb38a01a05bb37231325da7865bf4d`. Built from the current working tree on
the local development checkout, with TinyUSB pinned to `0.19.0~3` and IDF
`6.0.2`. Includes the earlier TLS cross-signed-chain fix and numeric diagnostics.

Normal power-on uses storage-only USB. Two quick BOOT presses after startup on a
computer request setup mode, deferring a mode-switch reboot while the operation
gate is occupied. An RTC-only marker permits the requested software reboot and
subsequent software restarts to expose MSC+CDC. Other reset reasons clear it.
Single-press pairing and five-second reset remain available. Cloud and local
Wi-Fi operation are retained. See [USB modes](usb-modes.md).

Browser and Bridge setup text now explains the gesture. No account/backend
protocol change or production deployment was made.

## Completed software checks

- All five native C suites pass with ASan/UBSan, including new gesture/session
  coverage: bounce, single/double timing, reset precedence/retry, millisecond
  rollover, corrupt markers, software-restart retention and cold-reset clearing.
- Clean ESP-IDF build succeeds and the signed image verifies against the existing
  Link public key. The initial incremental link contained stale TinyUSB objects;
  preserving the old generated dependency directory and resolving clean pinned
  dependencies fixed the build. No production source was changed to bypass it.
- Read the actual compiled full-speed USB descriptors from the ELF: normal mode
  has one class-8 interface and endpoints 0x01/0x81 (32 bytes); setup mode has
  classes 8/2/10 and five unique endpoints (98 bytes). Lengths and interface counts
  match their descriptor headers.
- Browser serial/account setup tests: 7 pass. Updated setup TSX parses.
- Bridge TypeScript/Vite build and frontend bundle validation pass.

## Hardware preparation and remaining checks

The known working EmberConnect `0.5.1-msctest1` image is preserved privately under
ignored per-device artifacts, alongside the earlier flash backups and verified
pre-format SD image. The FAT32 card and original design hash were checked again
and safely unmounted before requesting recovery mode. The working conventional
thumb drive remains untouched.

Installed the signed image through the ESP32-S3 ROM downloader after checking
the device serial. Bootloader, partition table, OTA selection data and application
writes all passed esptool hash verification. NVS and the SD card were not erased.

First normal cold boot passed: local authenticated info reports `0.3.1-dev` and
`usbMode: storage`; no application CDC port is present; the FAT32 card mounts and
the original design SHA-256 remains unchanged. Local Wi-Fi access works. The
local cloud backend also reports the device online with firmware `0.3.1-dev`.
Two physical attempts left the device in storage mode. The user observed purple
pairing blinks on the retry, indicating single-press recognition. This motivated
the gesture adjustment and successful retry documented below.

See the 0.3.2 results below for physical setup-mode and power-cycle verification.
Cloud-to-Brother preview passed as documented below. A physical busy-transfer
gesture check and broad compatibility/reliability qualification remain; native
coverage does not replace them.

## Gesture refinement — 0.3.2-dev

The initial 100 ms minimum press and 500 ms release-to-release window were too
fussy during physical testing. The revised gesture samples every 10 ms, rejects
presses shorter than 40 ms, and accepts the second press starting within 1,000 ms
of the first release. Long-hold reset precedence remains unchanged. Single-press
pairing consequently waits one second for a possible second press.

All five native suites pass again with sanitizers, including short taps, contact
bounce, the inclusive second-press boundary, long holds and rollover. ESP-IDF
build and RSA signature verification pass. The new signed image is 1,249,280
bytes, SHA-256 `1f449d3a93bbdf7d7e0b8416da68cd510bfd3057d88fd842eba44009d6c455f2`.
Browser, Bridge and DIY instructions now say within one second.

The authenticated local OTA update to `0.3.2-dev` succeeded in slot `ota_1`, with
`pendingVerify: false`. Storage mode, Wi-Fi, FAT32 mount and the original design
hash passed again. The next physical double press successfully entered setup:
the matching USB serial port appeared, `info` responded with `usbMode: setup`,
and cloud status reported enabled/configured/online with HTTP 200 and zero
network/TLS error fields. No Wi-Fi or account re-enrollment was needed.

A physical unplug, five-second wait and normal reconnect returned to
`usbMode: storage` with no application serial port. FAT32 and the original
design hash passed again. `ELCHECK.PES` was confirmed absent before the planned
cloud delivery. The card was safely unmounted before moving to the Brother.

## Brother test with cloud enabled

The user moved the dongle to the Brother NQ1700E and confirmed that it successfully
previewed the original design on `0.3.2-dev`. The dongle remained in the machine
for delivery of a generated `ELCHECK.PES` through the local cloud backend and
the previously authorized temporary HTTPS tunnel. The backend observed the job
progress from queued to offered to done, acknowledging completion while the
dongle was in the machine. The 1,371-byte generated file has SHA-256
`c945d4e3c55356f8b15580397c859335e8843d689ebb704cea3beb032ed0a452`.
The user reopened the USB design list and confirmed that the new square design
previewed successfully. The dongle stayed connected to the Brother throughout
delivery and preview. This validates the local-backend → HTTPS tunnel → Link
cloud download → FAT32 storage → Brother preview path on this hardware. It does
not qualify production AWS infrastructure, sustained reliability, other machine
models, or stitching. The generated test file remains on the card.

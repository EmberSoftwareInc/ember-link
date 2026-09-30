# Browser installer qualification: 0.3.6 bootstrap

Status: in progress. These results do not yet authorize the factory package as
fully qualified for publication.

## Package under test

- Firmware: unchanged published Ember Link 0.3.6 Stable application.
- Firmware source commit: `310dd7ca903a0a408cf682d22a7b70f93e6cca9d`.
- Application SHA-256: `ff49a1069c6986cf80ab3015bc407b977916e457a2e720733e0d3afc25b9b0f9`.
- Factory manifest SHA-256: `dd13931a0e5170e01fc0ee991732ab4c752536fffa5fc0dd36a6820d83db6582`.
- Installer: local preview at `http://127.0.0.1:8794/` from the
  `feature/browser-installer` working tree.
- Hardware: user-confirmed spare original LILYGO T-Dongle-S3.

## Observed results

On 2026-09-29, the user reported that the browser installer displayed
“Ember Link 0.3.6 was written and verified.” This confirms completion of the
installer's write and verification flow on the spare board.

The user then followed the normal reconnect and double-press BOOT instructions
and confirmed that the screen showed USB setup. This confirms that the installed
firmware starts and that the physical USB setup gesture works.

These are user-observed results. Firmware version and device status have not yet
been independently read back over the setup protocol during this test.

The user also reported that the dongle showed strong Wi-Fi after reconnecting.
This confirms that its network configuration survived the power cycle and that
it rejoined Wi-Fi. Local pairing and cloud connectivity are not inferred from
the signal indicator.

## Remaining hardware checks

- Deferred at the user’s request: verify the prepared FAT32 card and preview a
  design on the embroidery machine. This check has not passed for this spare
  board/browser-install combination.
- Interrupt a first installation on the spare and verify recovery by repeating
  the browser install from BOOT download mode.

Keep the existing configured Link separate from these first-install tests. The
installer erases internal settings; it does not erase the microSD card.

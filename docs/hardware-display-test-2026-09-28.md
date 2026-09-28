# Display hardware checks — 2026-09-28

Device: LilyGO T-Dongle-S3, with the previously validated
FAT32 card. Connected to the Mac for this test, not the Brother machine.

## Installed build

- Firmware: `0.3.3-dev`, 1,314,816-byte signed image.
- SHA-256: `9fbe6d89cfc888980af5bdfc8f7467ae150be78b266c2022c1745c48005767ae`.
- RSA signature verified locally before installation.
- First attempt through the local Wi-Fi update endpoint returned HTTP 500.
  The response body was not captured; the precise cause is not established.
  The device remained healthy on `0.3.2-dev` in `ota_0`.
- Installed through the rebuilt Bridge USB setup interface after the user
  enabled USB setup using the BOOT double-press.
- Independently verified `0.3.3-dev`, `ota_1`, `pendingVerify:false`, matching
  device serial and setup USB mode through the authenticated local API.
- Original design's SHA-256 unchanged after installation. Card safely
  unmounted again before subsequent transfer tests.
- Bridge showed the old-firmware update hint before installation, then the
  screen-on checkbox and Normal orientation controls after installation.

## Screen and storage checks

- User confirmed the physical screen is lit and readable after installation.
- User selected **Upside down (180°)** in Bridge and saved; confirmed that
  the physical display rotated correctly. Native menu selection could not be
  driven reliably by UI automation, so this interaction was performed manually.
- Disabled **Screen on** and saved through the actual Bridge UI. User confirmed
  the screen went dark and stayed dark after unplugging/reconnecting normally.
- With the screen disabled, sent generated `DISP033.PES` over authenticated local
  Wi-Fi. Transfer completed and the file appeared in the storage listing.
- After the power cycle, normal USB storage-only mode returned (no setup serial
  interface), and the original design and generated file both matched their
  expected SHA-256 values when read through the Mac's mounted USB volume.
- After BOOT double-press, Bridge read back screen disabled and rotation 180°,
  confirming both saved preferences survived the power cycle.
- Re-enabled the screen through Bridge. Sent invalid USB settings (90°,
  fractional rotation, and a string in place of enabled); each returned
  `invalid_display` without changing saved preferences.
- Restored screen on / normal orientation over USB and read it back. Device
  name and Wi-Fi configuration remained unchanged. User confirmed the physical
  screen was lit and readable in normal orientation again.
- Removed only the generated `DISP033.PES` after readback verification.

## Cloud delivery with LCD enabled

- The existing local cloud backend and previously authorized temporary HTTPS
  tunnel delivered generated `DISPCLD.PES` with the display enabled.
- Backend job reached `done`; USB-volume readback matched the generated design's
  SHA-256. Original design hash remained unchanged.
- Removed the generated cloud test file and verified both display-test filenames
  absent. USB storage re-enumerated normally after the delete; an immediate
  disk lookup briefly raced re-enumeration, then the same card was found again.
- Final preferences: screen on, normal orientation. Updated Bridge test build
  is open to USB setup; this is the repository's debug app bundle, not a
  replacement of the app in `/Applications`.

## Remaining checks

The real browser
setup UI, long-duration reliability, and Brother compatibility with this new
build have not been requalified. Prior Brother tests used 0.3.2-dev. The first
Wi-Fi firmware-upload HTTP 500 remains unexplained; USB installation succeeded.

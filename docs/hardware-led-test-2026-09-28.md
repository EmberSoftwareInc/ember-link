# Status LED hardware test — 2026-09-28

LilyGO T-Dongle-S3 test device, connected to the Mac with its existing FAT32
card. Updated through Bridge USB setup from 0.3.3-dev to signed 0.3.4-dev.
Image SHA-256: `5a1f1ff11947d1ce22eb6627ca42c79e1f529fd437e00c3d98d4061d065a4e30`.
The local API independently confirmed `ota_0` and `pendingVerify:false`.

- Existing screen-on / normal orientation preferences survived the v1→v2
  migration; new LED setting defaulted to on.
- Bridge showed an LED update hint on 0.3.3-dev, then **Status light on** after
  the update. Disabled the LED and saved through Bridge while keeping LCD on.
- Generated `LED034.PES` transferred over authenticated local Wi-Fi with the
  LED disabled. Actual USB-volume readback matched SHA-256. Original design's
  hash remained unchanged. Removed only the generated LED test file afterward.
- User confirmed the LED stayed dark while the screen worked, through unplug /
  reconnect and a BOOT double-press into USB setup.
- USB readback after the power cycle confirmed screen on, rotation 0, LED off.
- A screen-only `set_display` command omitting `ledEnabled` preserved LED off.
  A string instead of boolean was rejected without changing preferences.
- Re-enabled the LED through Bridge; the user confirmed it lit again while
  the screen continued working. Final state is screen on, normal orientation,
  LED on. Card safely unmounted after test cleanup.

Automated validation: eight native firmware suites (ASan/UBSan), 16 Bridge UI
tests, Bridge Rust compile/setup tests and debug app build, ten web setup/serial
tests and clean frontend type-check. Native LED tests capture real encoder frames
for all-off output, disabled blinks, mid-blink disable, latest-state restoration,
and serialized frames. They do not qualify real electrical timing or long-run
reliability. Browser hardware UI and Brother use of 0.3.4-dev remain untested.

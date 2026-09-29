# Ember Link status display

Starting with development firmware 0.3.3-dev, the LilyGO T-Dongle-S3's 160 × 80
ST7735 LCD provides status without needing an app open. It does not add menus or
change the BOOT button gestures. The status LED continues to work.

## Messages

| State | Screen | Meaning/action |
| --- | --- | --- |
| Starting | Starting / Preparing storage | Allow startup to finish. |
| Card missing or initialization failed | Check SD card / Insert a FAT32 card | Disconnect power before checking the card. A filesystem/mount error may still trigger the existing restart behavior. |
| Normal operation | Ready / Wi-Fi strong, fair, or weak | Wi-Fi signal is approximate. Cloud off is a normal local-only configuration. |
| Cloud connected | Local + cloud ready | The most recent cloud poll succeeded; this is not a continuous connection guarantee. |
| Cloud unavailable | Local ready / Cloud down | Local Wi-Fi transfers remain available. Check the account/backend in Ember. |
| Wi-Fi unavailable | Wi-Fi offline / Reconnecting | Existing USB files remain usable. |
| Wi-Fi not configured | Wi-Fi setup / Use Ember or Bridge | Configure through the web app or Bridge. |
| USB setup mode | USB setup / Open Ember or Bridge | Unplug and reconnect normally to return to machine mode. |
| Local pairing | Ready to pair / Open Ember Bridge | A normal short BOOT press opened the existing local pairing window. |
| File delivery | Receiving / filename / Cloud or Local + percentage | Bytes written so far; 99% is the maximum until verification and commit finish. |
| File delivery complete | Saved to Link / filename | The file committed and storage was returned to USB successfully. This does not mean the embroidery machine opened the file or began stitching. |
| File delivery failed | Transfer failed or Card full | Check Ember/Bridge for details; free space or retry as appropriate. |
| Firmware update | Updating / Keep plugged in / percentage | Signed image reception is in progress. |
| Firmware accepted | Restarting / Keep plugged in | The image was verified and selected for the next boot. |
| New firmware confirmed | Update ready | The existing firmware health check confirmed the updated image. |
| Firmware failed | Update rejected, failed, or stopped | Check Ember/Bridge for the detailed result. |

Card problems take priority, then active operations, temporary result notices,
USB setup, Wi-Fi setup/offline, and ordinary ready status. Success notices last
12 seconds, transfer/update errors 20 seconds, and pairing guidance 15 seconds.
The screen dims after 30 seconds without a state change; active transfers and
attention messages stay bright. New state changes brighten it automatically.
There is no new wake gesture that could interfere with pairing or USB setup.

Text uses a compact uppercase font. Filenames are limited to 24 characters on
screen, with an ellipsis for longer names; unsupported characters appear as
question marks. This affects display only, not stored filenames. Credentials,
Wi-Fi passwords, bearer tokens, and signed download URLs are never displayed.

## Screen settings in Bridge and the web app

Connect Link to your computer normally and press BOOT twice after startup to
enter USB setup. In Bridge's **Set up Ember Link** page, or the Ember web app's
USB setup flow, open **Link screen & light**:

- **Screen on** enables/disables the LCD and its backlight. Disabling the screen
  does not disable the status LED, USB storage, network transfers, or updates.
- **Screen orientation** offers **Normal** and **Upside down (180°)**.
- **Status light on** (0.3.4-dev and later) controls the APA102 status LED
  independently of the screen. Off suppresses all colors and blinks, including
  setup, transfer, update, and error indications. Re-enabling restores the current
  device status.
- **Save display settings** applies the choices within the display task's next
  refresh (normally 250 ms). No restart is required. Saved settings survive
  unplugging, Wi-Fi setup, and firmware updates; a factory reset restores defaults.

The options are available while connected over USB, including before Wi-Fi
setup. They are saved directly on the dongle, with no cloud backend request.
Older firmware without this capability shows an update hint instead of controls.
The web flow still requires its existing account/cloud setup prerequisites.

USB `info` includes `"display":{"enabled":true,"rotation":0,"ledEnabled":true}`. To save, send:

```json
{"id":1,"cmd":"set_display","enabled":false,"rotation":180,"ledEnabled":false}
```

`enabled` and `rotation` are required; rotation accepts exactly 0 or 180.
`ledEnabled` is an optional boolean: omission preserves the saved LED preference
so older screen-only clients do not accidentally change it. Older firmware
without `info.display.ledEnabled` shows an LED-specific update hint. A successful reply
contains `ok:true` and the saved `display` object. Invalid values produce
`invalid_display`; an active transfer/update produces `busy`; an NVS write failure
produces `storage_error`. Settings are stored as a versioned blob in the separate
`linkdisplay` NVS namespace. No Wi-Fi or cloud credentials are modified.

Firmware defaults are screen on, status light on, and normal orientation.
The v2 preference blob adds LED state; v1 screen settings migrate with LED on. Builders can set
`CONFIG_LINK_DISPLAY_FLIP=y` through **Ember Link display** in menuconfig to
change the default to 180°. A saved user preference takes precedence.

## Cloud settings (0.3.6-dev)

This development branch adds cloud updates for the same three preferences,
revision checks against stale requests, and durable outcome receipts. USB saves
remain compatible and advance the shared revision. See [the backend contract and
rollback limits](cloud-settings.md). Backend/browser integration and physical
qualification are still required; release 0.3.5 has USB settings only.

## Implementation and validation

A separate low-priority task renders a fixed 25,600-byte DMA framebuffer over
SPI2, refreshing only changed views and at most four times per second. Transfer
and USB code publish small snapshots under a short lock and do not wait for the
LCD. SPI waits are bounded; allocation or driver errors disable the LCD task
without deliberately restarting the device. As with other hardware faults, a
physically failing panel cannot always be detected by this write-only SPI bus.

The driver uses the board manufacturer's initialization sequence, with MIT
attribution in `third_party/lilygo-display/`. No GUI framework is required.
Native ASan/UBSan tests cover state priority, progress bounds, result expiry,
clock rollover, text sanitization, renderer buffer boundaries, persistent
preferences, invalid rotations, and simulated NVS failures. The signed
ESP-IDF 6.0.2 build passes. Physical appearance, rotation, screen-off/on, preference persistence, local
transfers with the screen off, and cloud transfers with it on passed the
[Mac hardware checks](hardware-display-test-2026-09-28.md). Idle dimming, the
real browser setup UI, prolonged reliability, and Brother use with this build
remain to be checked. Prior 0.3.2-dev Brother tests do not qualify this build.

LED unit tests execute the production GPIO encoder, checking disabled states,
blink suppression (including disable during a blink), latest-status restoration,
and frame serialization. Physical off/on, power-cycle persistence, independent LCD operation, and
local-transfer tests passed on 0.3.4-dev; see the
[LED hardware record](hardware-led-test-2026-09-28.md).

# Build your own Ember Link

**New to the project? Start with the [DIY quick start](diy-quick-start.md).**
It covers the parts, installation path, and guided setup through Ember Bridge.
The [browser installer](https://embersoftwareinc.github.io/ember-link/) offers
qualified prebuilt first-install packages when available. This full guide is
the reference for building from source, manual setup, cloud
provisioning, diagnostics, and recovery.

This guide covers buying the parts, installing firmware, checking the USB drive,
and configuring Wi-Fi. The current qualified stable baseline is **Ember Link 0.3.6**
([qualification record](release-qualification-0.3.6.md)). It targets the **original LILYGO
T-Dongle-S3**, and **ESP-IDF 6.0.2**. No soldering or custom PCB is required.

Ember Link presents a microSD card as a USB flash drive to an embroidery machine.
It can receive design files over Wi-Fi. It does not convert file formats, control
stitching, or make an unsupported design readable by a machine.

**Building the hardware does not automatically grant access to Ember's cloud.**
Local USB storage and Wi-Fi configuration work without a cloud account. Cloud
sending and browser account setup additionally require a deployed compatible
backend and a separately enrolled device. See [Cloud setup](#8-cloud-setup).
The firmware repository does not contain production credentials or the backend.

## Contents

1. [Parts and tools](#1-parts-and-tools)
2. [Prepare the hardware and card](#2-prepare-the-hardware-and-card)
3. [Install the development tools](#3-install-the-development-tools)
4. [Download and build the firmware](#4-download-and-build-the-firmware)
5. [Back up and flash the dongle](#5-back-up-and-flash-the-dongle)
6. [Check USB and configure Wi-Fi](#6-check-usb-and-configure-wi-fi)
7. [Try it with your machine](#7-try-it-with-your-machine)
8. [Cloud setup](#8-cloud-setup)
9. [Updates, reset, and recovery](#9-updates-reset-and-recovery)
10. [Troubleshooting](#10-troubleshooting)

## 1. Parts and tools

| Quantity | Item | What to choose |
| --- | --- | --- |
| 1 | **Original LILYGO T-Dongle-S3** | ESP32-S3, 16 MB flash, no PSRAM, built-in USB-A plug and microSD/TF slot. Use the original board pinout; Dual, Plus, and other ESP32 boards are not validated substitutes. |
| 1 | **Working microSD/TF card** | Use a card capacity supported by your machine, formatted as FAT32. An ordinary 4–32 GB SDHC card is a starting choice only if your machine supports it; some machines need much smaller cards. FAT16 passed Mac-only smoke tests, but this card was rejected by the Brother NQ1700E; use FAT32 for the tested Brother configuration. |
| 1 | **Computer** | macOS, Windows, or Linux, with Git, internet access for downloading build dependencies, and a USB data connection. |
| As needed | **USB-C male to USB-A female data adapter** | Needed if your computer has only USB-C. A charging-only adapter will not work. |
| Optional | **microSD reader** | Useful for backups or separate formatting. Firmware with browser card-preparation support can prepare the card inside Link. |
| Optional | Short USB-A extension cable and suitable case | Useful for reaching the BOOT button and reducing strain on the machine's port. Keep the card slot and button accessible. |
| For Wi-Fi | **2.4 GHz Wi-Fi network** | Use a normal home/workshop network. Captive-portal and enterprise sign-in flows are not implemented. Internet access is additionally needed for cloud delivery. |
| For machine use | **Embroidery machine with a USB host port for flash drives** | Its manual must allow USB storage and the chosen card format/capacity. A computer-link/service port is not the same thing. |

Board details and buying reference: [LILYGO product page](https://lilygo.cc/products/t-dongle-s3)
and [original-board documentation](https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/).
Check the selected variant before ordering. LILYGO lists some supplied TF inserts
as nonfunctional; do not assume an included insert is usable storage. Supply a
real card. Prices, accessories, and stock vary.

The LCD shows status, setup guidance, and transfer/update progress; see the
[status display guide](status-display.md). Bluetooth is not used for setup. You do not need a separate programmer, battery, SD module,
external LED, or Ember Bridge installation for these instructions.

## 2. Prepare the hardware and card

1. Back up any files on the microSD card. Formatting erases its contents.
2. In your computer's disk utility, select the **microSD card**, not a computer
   disk. Create a single FAT32 partition with an MBR partition map as a starting
   configuration, subject to your machine's requirements. Do not use exFAT,
   NTFS, or APFS. FAT16 was readable on the Mac but failed the Brother NQ1700E test. Cloud-enabled stable 0.3.6 passed saved-design preview on the Brother NQ1700E with a FAT32 card and the machine stayed responsive. Other machine/card combinations still need validation.
3. Copy a small file onto the card, read it back, then safely eject it.
4. With the dongle unplugged, insert the card using the original board's slot
   and orientation shown in LILYGO's hardware documentation. Do not force it.
5. Keep the dongle connected to the computer during installation. Move it to
   the embroidery machine only after the computer checks succeed.

The firmware does not format the card for you. Storage initializes **before**
the normal USB serial interface, so a missing, unreadable, or unsupported card
can prevent both the drive and the setup port from appearing.

For reference, the existing connections are defined in
[`firmware/main/board.h`](../firmware/main/board.h). No rewiring is needed:

| Function | GPIO |
| --- | --- |
| BOOT button | 0, active low |
| APA102 status LED | Data 40, clock 39 |
| SDMMC | Clock 12, command 16, D0 14, D1 17, D2 21, D3 18 |
| Status LCD | MOSI 3, clock 5, CS 4, DC 2, reset 1, backlight 38 |

## 3. Install the development tools

Use the [ESP-IDF 6.0.2 installation guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/get-started/index.html)
to install **6.0.2** and the ESP32-S3 toolchain for your operating system.
The firmware manifest pins that exact IDF version. This project uses ESP-IDF,
not the Arduino/PlatformIO configuration from LILYGO's demonstration firmware.

- **Windows:** Use Espressif's installer/installation manager and open its
  activated ESP-IDF terminal. The PowerShell examples below assume that terminal.
- **macOS/Linux:** Use the installation manager's activated terminal or the
  activation command it provides. If you installed IDF using its legacy Git
  checkout method, activate it with `source /path/to/esp-idf/export.sh`.

Keep both the ESP-IDF installation and this project's directory in paths
**without spaces**. All subsequent commands run in the activated terminal:

```sh
idf.py --version
python -m esptool version
python -m serial.tools.list_ports -v
```

Expect `ESP-IDF v6.0.2`. The IDF environment supplies esptool, espsecure, and
pyserial; if the last command reports a missing `serial` module, install it in
that environment with `python -m pip install pyserial`. You also need Git.

## 4. Download and build the firmware

From a directory without spaces, clone and enter the repository:

```sh
git clone https://github.com/EmberSoftwareInc/ember-link.git
cd ember-link
git switch --detach v0.3.6
```

The firmware repository is public. These instructions build the stable source
with your own signing key. The standalone `ember-link.bin` release image is an update image, not a complete
first-install flash bundle. The browser installer uses a separate validated set
of bootloader, partition, initial OTA, and application files. Run commands from the **repository root**.
Use the [release lifecycle](release-lifecycle.md) for official stable/dev publishing;
checking out `dev` alone does not select an update channel on your device.

### Create your own firmware signing key once

The build signs updates with a private RSA-3072 key. Keep using the same key for
future builds intended to update this dongle wirelessly. Official Stable and
Development images use Ember’s production key, so a self-signed DIY installation
will not accept them as routine signed updates. Store a private backup;
do not upload the key to GitHub or share it with the cloud service.

**macOS/Linux (bash or zsh):**

```sh
mkdir -p firmware/keys
if [ ! -f firmware/keys/ota_signing_key.pem ]; then
  python -m espsecure generate-signing-key --version 2 --scheme rsa3072 firmware/keys/ota_signing_key.pem
fi
chmod 600 firmware/keys/ota_signing_key.pem
```

**Windows (PowerShell):**

```powershell
New-Item -ItemType Directory -Force firmware/keys | Out-Null
if (-not (Test-Path firmware/keys/ota_signing_key.pem)) {
  python -m espsecure generate-signing-key --version 2 --scheme rsa3072 firmware/keys/ota_signing_key.pem
}
```

These are development settings: signed app updates are enabled, but hardware
Secure Boot, flash encryption, and NVS encryption are not enabled. The guide
does not burn eFuses or configure production device security.

### Build

```sh
idf.py -C firmware build
```

The default target is already `esp32s3`. The first build downloads managed
components and uses [`firmware/dependencies.lock`](../firmware/dependencies.lock).
Do not substitute the default partition layout from another ESP32 project.

A successful build produces these files:

| File | Purpose |
| --- | --- |
| `firmware/build/bootloader/bootloader.bin` | Bootloader |
| `firmware/build/partition_table/partition-table.bin` | Project partition layout |
| `firmware/build/ota_data_initial.bin` | Initial OTA slot selection |
| `firmware/build/ember-link.bin` | Signed Ember Link application |

For first installation, use the complete `idf.py flash` process below. The app
binary alone is an update image, not a complete first-install image at address 0.

## 5. Back up and flash the dongle

Close Ember Bridge, serial terminals, and browser tabs holding a serial
connection. Safely eject any mounted dongle volume before unplugging it.

### Enter download mode and identify its port

1. Unplug the dongle.
2. Hold **BOOT** while plugging it into the computer.
3. Release BOOT, then list ports:

```sh
python -m serial.tools.list_ports -v
```

Compare the list with the device unplugged if necessary. Typical port names are
`/dev/cu.usbmodem101` on macOS, `/dev/ttyACM0` on Linux, and `COM7` on Windows.
These are examples, not fixed names. In the commands below, replace `PORT` with
**your current download-mode port**. The normal firmware port may be different.

```sh
python -m esptool --chip esp32s3 --port PORT flash-id
```

Confirm that the hardware reports an ESP32-S3 and 16 MB flash. Stop if the board
is a different model or reports enabled security restrictions that you have not
planned for. These instructions target ordinary development boards.

### Save the original flash before replacing firmware

This is useful for a board running LILYGO's demo or older firmware. A flash dump
can contain Wi-Fi passwords and device credentials; keep it private. It does
**not** back up the removable microSD card.

```sh
python -c "from pathlib import Path; Path('artifacts').mkdir(exist_ok=True)"
python -m esptool --chip esp32s3 --port PORT read-flash 0x0 0x1000000 artifacts/before-ember-link.bin
```

Use a new filename if you already have a backup. On macOS/Linux, restrict its
permissions with `chmod 600 artifacts/before-ember-link.bin`; on Windows, keep
it in a private user folder. `artifacts/` is excluded from Git.

### First installation on your DIY board

After saving the backup, erase the old board firmware/settings for a clean
first installation. **This erases internal flash, including any device identity
and Wi-Fi settings. Do not use this step for routine updates or an enrolled
Ember device.** It does not erase the microSD card or reset server-side ownership.
Re-enter BOOT/download mode and check the port again between commands if needed.

```sh
idf.py -C firmware -p PORT erase-flash
idf.py -C firmware -p PORT -b 460800 flash
```

`flash` installs the bootloader, partition table, OTA data, and application at
the addresses generated for this build. Do not interrupt it. Wait for successful
write/verification output. If communication is unreliable, retry the flash
command with `-b 115200` and a direct USB connection.

Unplug and reconnect **normally, without holding BOOT**. This normal power cycle
is important: the dongle may remain in download mode after flashing over USB.

## 6. Check USB and configure Wi-Fi

With a usable FAT32 card inserted, normal startup exposes only a USB drive.
While connected to the computer, wait for startup, then **press and release BOOT
twice quickly** (within one second). Link briefly blinks blue and restarts,
adding the **Ember Link Setup** serial interface. Do not hold BOOT while plugging
in; that enters flash recovery instead. Unplugging ends this setup session and
the next power-on returns to storage-only machine mode. See [USB modes](usb-modes.md). The USB product is **Ember Link**; the
mounted volume's label comes from your card and may have a different name.
The LCD shows **USB setup** in this mode. On a fresh install, a blue LED
indicates Wi-Fi setup mode. The screen may briefly show a higher-priority card,
transfer, or update message.

### Recommended: guided setup with Ember Bridge

Once Ember Link firmware is installed and USB setup mode is enabled,
[Ember Bridge](https://github.com/EmberSoftwareInc/ember-bridge/releases/latest)
can configure Wi-Fi and pair the dongle for local transfers. Open **Ember Link**
at the bottom left, choose **Set up Wi-Fi**, select your 2.4 GHz network, enter
its password and a machine name, then click **Connect**. Wait for confirmation
that Link is connected, paired, and saved on the Machines page.

Close any serial terminal or browser USB session before using Bridge. Follow the
[quick start](diy-quick-start.md#5-send-your-first-design) to send your first file.
The manual methods below are alternatives, not additional required steps.

### Verify the firmware over USB

List the ports again to find the normal application port. Substitute that port
for `PORT` here, then wait a moment after opening before sending commands:

```sh
python -m serial.tools.miniterm PORT 115200 --dtr 1 --eol LF
```

Type or paste each JSON command on one line, then press Enter:

```json
{"id":1,"cmd":"info"}
```

The response should have `ok:true`, `name:"Ember Link"`, `version:"0.3.6"` (or the version you built), `usbMode:"setup"`,
`usbProtocolVersion:1`, `setupProtocolVersion:1`, a serial number, and `wifi` and
`cloud` objects. A fresh DIY board normally reports `cloud.configured:false`.
That is expected until enrollment. A temporarily busy cloud worker may return
`cloud.busy:true`; retry `info` after it finishes.

Miniterm does not locally echo typed characters by default. Press **Ctrl+]**
to exit. This serial port is a JSON setup channel, not a normal log console;
using `idf.py monitor` on it is not the setup procedure.

### Configure Wi-Fi directly over USB

You can perform this step without a backend or browser setup service. In the
same serial session, scan and then provision your network:

```json
{"id":2,"cmd":"scan"}
```

```json
{"id":3,"cmd":"provision","ssid":"YOUR_2_4_GHZ_NETWORK","password":"YOUR_WIFI_PASSWORD"}
```

Replace the example values and use valid JSON escaping for any quotes or
backslashes. Use an empty password only for an intentionally open network.
Do not record/share the terminal session containing your password. Provisioning
can take up to 40 seconds; wait for the response before another command. Success
returns `ok:true`; a failed trial returns an error and restores the prior Wi-Fi
configuration. Send `info` again and check `wifi.connected:true` and its IP.

Optionally set a friendly name:

```json
{"id":4,"cmd":"set_name","name":"Sewing room"}
```

Exit the serial terminal before opening a browser serial connection or running
the cloud configuration tool. Only one program can own the port at a time.

### Alternative: initial setup hotspot

On a fresh install, join **`Ember Link-XXXX`** from your computer/phone and open
[the dongle setup page](http://192.168.4.1/) if the captive portal does not appear.
Choose your 2.4 GHz network and save its credentials; the dongle reboots and your
computer then needs to reconnect to its usual network. This is an open local
hotspot with an HTTP setup page; USB provisioning avoids sending the Wi-Fi
password over that hotspot. The hotspot configures Wi-Fi, not cloud ownership.

After it joins your LAN, `http://<dongle-ip>/api/health` provides a simple health
check. mDNS-capable systems may resolve `ember-link-xxxx.local`, using the last
four hex characters of the serial number in lowercase.

## 7. Try it with your machine

1. On the computer, copy a small design in a format your machine already accepts
   to the card's root directory. Use a spare card/test design for first checks.
2. Safely eject the drive and move the dongle to the machine's USB flash-drive
   port. Let it finish booting.
3. Open the machine's USB design browser and check that the design appears and
   its preview is correct. File-format and machine limits still apply.
4. Before trying wireless transfers, return the dongle to your computer and
   confirm it still reads the file correctly.

A green LED means Wi-Fi/local services are ready; it does **not** prove cloud
account setup or delivery succeeded. During a wireless write, the firmware
briefly disconnects/reconnects its USB storage so the host can reread the card.
Do not send or replace files while the machine is reading from it; some machines
may require reopening the USB browser or reinserting the dongle.

Stable 0.3.6 was checked on a LilyGO T-Dongle-S3 with FAT32 storage and a Brother
NQ1700E: a saved design previewed and the machine remained responsive. This is
not certification of other machines, all card capacities, or stitching. See the
[current qualification record](release-qualification-0.3.6.md) and
[hardware validation checklist](hardware-validation.md). Current Ember Bridge
releases support Link setup and local transfers; older EmberConnect-era builds
may not support the new identity.

## 8. Cloud setup

### What is required

A self-built dongle cannot enroll itself into Ember's production cloud just by
knowing an API URL. You need either an operator-authorized device enrollment or
your own compatible backend. There is no public self-service enrollment flow
implemented in this repository.

The backend operator must provide these values for **this device**:

| Value | Requirement |
| --- | --- |
| Device ID | Unique backend-registered ID, 1–63 ASCII letters/digits/hyphens/underscores |
| Device token | Unique 64-character lowercase hexadecimal credential; entered privately |
| Device API root | Publicly trusted HTTPS origin with a trailing slash; no stage/path prefix, custom port, query, or redirect |
| Download hostname | Exact hostname used in file download URLs; no scheme or path |
| Account setup URL | Deployed account website with the matching `/connect` flow enabled |

The matching implementation is in the separate
[`ember-app` feature branch](https://github.com/coryortega/ember-app/tree/feature/ember-link),
which requires its own repository access. Its
[cloud deployment guide](https://github.com/coryortega/ember-app/blob/feature/ember-link/docs/ember-link-cloud.md)
and [consumer setup guide](https://github.com/coryortega/ember-app/blob/feature/ember-link/docs/ember-link-consumer-setup.md)
cover deployment, IAM-authorized `backend/ember-link/enroll.mjs`, and feature flags.
If you do not have that access, the [firmware protocol](cloud-protocol.md) describes
the contract for an independent implementation; running the firmware alone does
not provide an account website or file service.

### Configure the enrolled identity

Enter USB setup mode with the double press, then from this repository root,
using the application serial port, run this
single-line command after replacing all placeholder values:

```sh
python tools/cloud_configure.py --port PORT --api-base-url https://YOUR_DEVICE_API_HOST/ --download-host YOUR_DOWNLOAD_HOST --device-id YOUR_REGISTERED_DEVICE_ID
```

The tool prompts for the permanent device token without displaying it. Do not
put that token on the command line or into a Git-tracked file. The tool configures
an already-enrolled device; it does not create its backend registration or claim
an account. Leave off `--enable` when the next step is consumer browser setup.

### Link an account and send a design

1. Enter USB setup mode with the double press and close any serial terminal/tool.
   Open the operator-provided HTTPS setup URL
   in a desktop browser supporting Web Serial, and sign in.
2. Connect the dongle through the device picker, keep or configure its Wi-Fi,
   name it, and choose **Link to my account**.
3. Wait for cloud confirmation before moving the dongle to the machine.
   The USB command acknowledgement alone does not confirm account ownership.
4. In the matching Ember editor, open a design, choose **Send to machine →
   Ember Link**, select the device, and send. Check both the transfer result
   and the machine's file browser.

Browser setup enables cloud polling. For an operator-managed test without the
browser flow, `cloud_configure.py --enable` enables polling, but ownership must
still be established using that backend's authorized claiming procedure.

Cloud needs outbound HTTPS plus working DNS and time synchronization. Firmware
uses `pool.ntp.org` for its clock and verifies TLS certificates. The backend's
local simulator is useful for software testing, but plain HTTP localhost is not
a valid physical-dongle cloud URL: `localhost` would refer to the dongle itself.

`START HERE.html`, created on the card before Wi-Fi setup, points to
`connect.emberdesign.net`.
It is a convenience link, not enrollment, a firmware installer, or proof that
the hosted setup service has been deployed. Inserting the dongle does not
force a computer to open its browser. Follow your backend operator's current URL.

## 9. Updates, reset, and recovery

- **Cloud updates:** With a compatible backend and an approved release, open the
  Firmware section in the Ember app's Send to Ember Link panel. Confirm that the
  machine is idle and leave the dongle powered on. See [firmware updates](firmware-updates.md).
  Older firmware needs one USB/local bootstrap update to gain this capability.
- **Local wireless updates:** Rebuild with the same signing key. On macOS/Linux with
  bash and curl, run `bash tools/ota-push.sh <dongle-ip-or-hostname>` from the repo
  root. Tap BOOT briefly first to reopen the five-minute local pairing window.
  This script uses the local HTTP API; use it on a trusted LAN. Keep the host
  from accessing the card during updates. It is not a cloud OTA service.
- **USB recovery/reinstallation:** Re-enter BOOT/download mode, then use the full
  `idf.py -C firmware -p PORT flash` command. Omit `erase-flash` for a normal
  reinstallation with the unchanged partition layout. This rewrites initial OTA
  slot selection; do it with stable power and retain your backups. A lost signing
  key requires USB recovery with a new build/key on these development boards.
- **Factory reset:** While running normally, hold BOOT for about five seconds,
  then release it. Reset clears Wi-Fi, local pairing, and name, and disables
  cloud; it retains cloud identity/receipts and does not remove the backend owner.
  A busy operation may prevent reset; wait for it to finish and retry. Holding
  BOOT while plugging in enters the ROM downloader instead.
- **Recover original firmware:** With the same unsecured board in download mode,
  restore its own private backup using the command below, then reconnect
  normally. This overwrites internal flash; never restore another device's dump.

```sh
python -m esptool --chip esp32s3 --port PORT write-flash 0x0 artifacts/before-ember-link.bin
```

Keep signing keys, Wi-Fi credentials, provisioning files, and flash backups out
of Git and public support reports. Production Secure Boot/encryption require a
separate manufacturing and recovery plan; do not enable them as a troubleshooting
step for this development build.

## 10. Troubleshooting

| Symptom | Check |
| --- | --- |
| No application setup port | Connect normally, wait for startup, then press and release BOOT twice quickly. A normal power-on exposes storage only. Check the FAT32 card. |
| No download-mode port | Hold BOOT while reconnecting; use a data-capable adapter/direct port. Compare `serial.tools.list_ports -v` before and after. On Linux check serial-device permissions. |
| Flash cannot connect | Close other serial clients, re-enter BOOT mode, recheck the port, and retry at 115200 baud. |
| Flash succeeds but no drive/setup port | Reconnect normally. Check the real microSD card, format, insertion, and power. USB startup waits for usable storage. |
| Status LED stays dark | Check **Status light on** in Bridge or web USB setup (0.3.4-dev+). A saved off setting suppresses all status colors and blinks. |
| Red LED at startup | Check the card first; other errors can also show red. |
| LCD is blank | Firmware before 0.3.3-dev did not drive the display. With a newer build, allow startup to finish and check power/board model. A display initialization failure is logged and does not stop USB/network operation. |
| Browser does not show a port | Use an HTTPS top-level setup page and a browser with Web Serial; close other serial clients. A missing card may also prevent the port appearing. |
| Browser says device needs cloud preparation | The device identity/API configuration is missing. Complete operator enrollment and `cloud_configure.py`; Wi-Fi alone is insufficient. |
| Wi-Fi scan misses the network | Check 2.4 GHz coverage; enter a hidden SSID manually. Enterprise/captive-portal sign-in is unsupported. |
| Green LED but cloud offline | Check USB `cloud_status`/`info`: configured/enabled state, API origin, device credential, internet, DNS, clock, and backend deployment. Green is a Wi-Fi indicator. |
| Cloud state is `authorization_failed` | Have the operator check/revoke/replace the credential as appropriate. A 401/403 disables cloud until explicitly re-enabled. |
| Update signature rejected | Rebuild with the key trusted by the running firmware, or deliberately reinstall over USB with your replacement key. |
| Machine rejects the drive or design | Check its USB host port, FAT support, capacity limits, supported design format, and filename rules. Test a conventional USB stick first. |
| Account already owns this device | Sign in to that account or ask its operator for help. Factory reset/reflashing is not an ownership-transfer mechanism. |

For support, include board variant, firmware version/commit, OS, card size/format,
LED state, and the exact non-sensitive error. Do not include credentials or dumps.

### Preparing the card in the browser

Firmware with card-preparation support offers an optional **Check your card**
step in the browser installer. Keep an existing FAT32 card, or explicitly erase
and prepare a supported 64 MiB to 32 GiB card without a separate card reader.
This requires the new firmware capability; published 0.3.6 still needs separate
formatting. See [the card preparation flow](browser-installer.md#optional-card-preparation)
for the USB maintenance steps and current test status.

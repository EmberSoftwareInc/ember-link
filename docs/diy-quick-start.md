# Build your own Ember Link: quick start

Turn a LILYGO T-Dongle-S3 into a Wi-Fi USB drive for a compatible
embroidery machine. No soldering is required.

This guide takes you from parts to your first local wireless transfer using
**Ember Bridge**, without an Ember account or cloud enrollment. The one technical
step is installing the firmware. Use the browser installer when a qualified
package is available, or the source-build reference if you want to customize it.
If your device already runs Ember Link, skip that step.

## 1. Gather the parts

- **LILYGO T-Dongle-S3**, with 16 MB flash. The version with a screen is
  recommended and tested. Dual, Plus, and other board variants are not validated substitutes.
- **A real microSD card** with a capacity your embroidery machine supports.
  Do not assume an included card-shaped insert is usable storage.
- **A Mac, Windows, or Linux computer**. Add a USB data adapter if your computer
  has no USB-A port. A separate microSD reader is optional when using the
  installer’s card-preparation feature.
- **A 2.4 GHz Wi-Fi network** and an embroidery machine that reads designs from
  USB flash drives.

See the [parts guide](diy-build-guide.md#1-parts-and-tools) for board links and
compatibility details. Link transfers files; your machine must still support
the design format and size.

## 2. Insert the card

Back up any files you want to keep, then insert the card into the unplugged dongle.
The installer can check the card after firmware installation. Keep an existing
FAT32 card, or explicitly approve preparing it as FAT32. Formatting erases the card;
checking or renaming it does not.

If using older firmware without card preparation, use a card reader to prepare
one FAT32 partition with an MBR partition map, check that files can be written and
read back, then safely eject it before inserting it into the unplugged dongle.
Do not use exFAT.

## 3. Install Ember Link firmware

The [browser installer](https://embersoftwareinc.github.io/ember-link/) provides
a prebuilt official image when a qualified first-install package is available.
Use a desktop browser with Web Serial, such as Chrome or Edge.

1. Safely eject any mounted dongle drive and close other USB setup sessions.
2. Hold **the small button** while plugging the dongle into your computer, then release it.
3. Keep **Stable** selected, then click **Connect and install**.
4. Select the dongle in the browser device picker and leave it connected until
   the page reports the firmware was written and verified.
5. Unplug and reconnect normally. Press the small button twice within one second,
   then use **Connect to check card** in the installer. Follow its maintenance
   instructions if the card needs preparing. An existing FAT32 card can be kept.
   Firmware without this feature requires the separate card-reader method above.
6. Disconnect the installer’s USB session. If you entered maintenance, unplug and
   reconnect normally. Choose [Ember web setup](https://emberdesign.net/link) for an
   eligible cloud device, or continue with Bridge below for local use.

The installer initializes blank boards and preserves settings/cloud identity on
recognized compatible Link installations. It never erases the microSD card during
firmware installation. On a new board with recognized preloaded firmware, it asks
you to confirm that the board has never been set up as Link and approve erasing
its internal firmware/settings. The microSD stays unchanged. Other unrecognized
firmware, unsupported Link versions or unfinished work stop without writing;
follow the support guidance instead of erasing an existing Link.
Installation does not register the device with Ember's cloud or transfer ownership.
Existing Link owners should use normal signed firmware updates for routine updates.

If no qualified package is offered, or you want to modify the firmware, follow
the detailed guide's [tool installation](diy-build-guide.md#3-install-the-development-tools),
[source build](diy-build-guide.md#4-download-and-build-the-firmware), and
[backup/flashing](diy-build-guide.md#5-back-up-and-flash-the-dongle) steps instead.
Self-built firmware uses your own signing key; official browser-installed firmware
uses Ember's production key and can accept compatible official signed updates.

## 4. Connect to Wi-Fi with Bridge

1. [Download and install Ember Bridge](https://github.com/EmberSoftwareInc/ember-bridge/releases/latest).
2. Leave Link connected to your computer with its card installed. Close any
   serial terminal or browser USB session using it.
3. After Link starts, press and release **BOOT twice within one second** to
   enable USB setup. Holding BOOT while plugging in is for firmware installation,
   not this setup step.
4. Open **Ember Link** at the bottom left of Bridge, then **Set up Wi-Fi**.
5. Choose your 2.4 GHz network, enter its password, give Link a name, and click
   **Connect**. Keep your computer on the same local network.
6. Wait for **Wi-Fi connected** and confirmation that Link is paired with Bridge
   and saved on the Machines page.

You can also adjust the screen, orientation, and status light on the Ember Link
page. If you prefer setup without Bridge, the
[detailed guide](diy-build-guide.md#6-check-usb-and-configure-wi-fi) includes manual
USB and setup-hotspot alternatives.

## 5. Send your first design

Safely eject Link's mounted drive, unplug it from the computer, and plug it
normally into the machine's USB storage port. Do not hold BOOT. Leave Bridge
running on your computer and allow Link to reconnect to Wi-Fi.

In Bridge, open **Machines**, find your saved Link, and click **Send** beside it.
Choose a small design your machine already supports and send it while the machine
is idle. Wait for the transfer to finish, then open or refresh the machine's USB
design list and check the preview.

Only start stitching once you have checked the design at the machine. Do not
send or replace files while the machine is reading from Link. If the machine
rejects the media, check its card requirements and try a normal USB drive first.

## What about cloud delivery?

Local sending through Bridge is ready to use without cloud enrollment. A DIY
build does not automatically register itself with Ember's cloud. Cloud use needs
a separately enrolled device and a compatible deployed service; the
[cloud setup reference](diy-build-guide.md#8-cloud-setup) is for that separate step.

## Need help?

Start with the [troubleshooting table](diy-build-guide.md#10-troubleshooting).
For backups, custom builds, updates, and recovery, keep the
[full DIY guide](diy-build-guide.md) handy. Machine and card compatibility vary;
the documented stable build was preview-tested on a Brother NQ1700E with FAT32
storage, rather than validated for every USB embroidery machine.

### Preparing the card in the browser

Firmware with card-preparation support offers an optional **Check your card**
step in the browser installer. Keep an existing FAT32 card, or explicitly erase
and prepare a supported 64 MiB to 32 GiB card without a separate card reader.
This is available in stable 0.3.7 and later; 0.3.6 requires separate formatting. See [the card preparation flow](browser-installer.md#optional-card-preparation)
for the USB maintenance steps and current test status.

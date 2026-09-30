# Browser installer and release synchronization

The installer lives in `installer/` in this repository. GitHub Pages serves its
bundled JavaScript, instructions, and explicitly selected public firmware files.
It does not host accounts, accept credentials, configure Wi-Fi, or enroll cloud
devices. After installation it links to Ember web setup or Bridge's
`ember-bridge://setup` launcher.

## Consumer behavior

Use a desktop browser with Web Serial, such as Chrome or Edge, on HTTPS (or
localhost for testing). The user identifies the original LILYGO T-Dongle-S3,
inserts a microSD card, holds BOOT while connecting USB, and explicitly
approves erasing internal flash. The installer detects ESP32-S3 and 16 MB flash;
it cannot identify the board wiring, so the parts list names the supported
LILYGO T-Dongle-S3. The screen version is recommended and physically tested;
screenless operation has not yet been physically qualified. Hardware security configurations outside the development-board setup
are refused before erasure. No eFuses are changed.

All four files are downloaded with size and SHA-256 checks before any erasure.
The page writes the existing file bytes without patching signed image headers,
and verifies device-side MD5 against each original file after writing. The page
reports **written and verified**, not a confirmed healthy boot. The user must
reconnect normally and check the device through Ember or Bridge. An interrupted
install requires returning to BOOT download mode and repeating installation.

This is a **first-install/recovery** flow. It erases saved Wi-Fi, local pairing,
and cloud identity. The firmware-install step does not erase the microSD card. The separate card
preparation step erases it only after explicit confirmation. Existing Link owners should
use their normal signed update flow to preserve device settings. Reinstallation
does not transfer server-side account ownership.

Stable is the default. Development needs separate experimental consent and uses
only the explicit recommendation in `release-channels/dev.json`. Empty feeds or
legacy releases with no approved factory package have no install button. There
is no fallback to an unrelated version or the newest arbitrary prerelease.

## Optional card preparation

The new firmware exposes `cardPreparationProtocolVersion: 1` in USB `info`.
Stable 0.3.7 includes card preparation and naming. Older 0.3.6 firmware does not
have this capability and requires separate card preparation. The dev.2/dev.3
local candidates in the test history were not published or recommended builds.
See [0.3.7 qualification](release-qualification-0.3.7.md) for the final exact image.

After installation, reconnect normally and double-press BOOT. **Connect to check
card** opens a new Web Serial setup session and reads its status. An existing
FAT32 card can be kept without any destructive action. To prepare a card:

1. Eject the dongle's drive in the operating system and confirm that on the page.
2. Click **Enter card maintenance**. Link restarts without erasing anything.
3. Wait a few seconds, then **Connect to check card** again. Maintenance exposes
   CDC only, with no USB storage interface, Wi-Fi, HTTP server or cloud worker.
4. Review the connected Link serial number and card capacity. Explicitly approve
   deleting all files and partitions, then click **Erase and prepare as FAT32**.
5. Wait for verification, disconnect USB on the page, then unplug and reconnect
   normally. Double-press BOOT again if continuing with Ember or Bridge setup.

The formatter accepts 512-byte sectors and capacities from 64 MiB through
32 GiB. It creates one MBR FAT32 LBA partition at sector 2048, two FAT copies,
and capacity-dependent clusters: 512 bytes through 128 MiB, 1 KiB through
256 MiB, 2 KiB through 512 MiB, 4 KiB through 4 GiB, and 32 KiB above that.
This includes cards labelled 128 MB (decimal capacity), not just 128 MiB cards.
It removes stale
GPT headers/tables, mounts the new filesystem, and writes, flushes, remounts,
reads back and removes a temporary verification file. This is a quick format,
not secure erasure or a complete media-health test. Machine/card compatibility
still requires physical qualification.

No filesystem is formatted automatically. Missing/unreadable cards no longer
block the physical USB setup gesture. Insert or replace cards only while Link
is unplugged. Firmware/Wi-Fi/pairing settings are not erased by card formatting.
After any failure, the browser requires a fresh card check and new confirmation;
it never retries a destructive request automatically. A disconnected browser
cannot cancel formatting already in progress: keep Link powered and reconnect
to check the outcome. After an actual power loss, repeat the maintenance flow.

The command is USB-only. There is no cloud or local HTTP formatting endpoint.
The operation gate blocks concurrent resets/updates; maintenance refuses setup
commands unrelated to card inspection, formatting, or renaming until the next normal power cycle.

Card preparation also names the volume `EMBER LINK`. On compatible firmware,
**Rename to EMBER LINK** sets that name on an existing FAT32 card without formatting
or deleting files. It uses the same maintenance/ejection flow and verifies the
name after remounting. The installer shows the current label and only enables
rename when the connected firmware advertises support.

The setup card offers **Disconnect USB** whenever the page has an active setup
session. Instructions refer to the small button, with a manufacturer photo
available by hover, click, or keyboard focus. The Ember setup link is
<https://emberdesign.net/link>.

See [card preparation protocol and tests](card-preparation.md).

## Release packages

`tools/release.py prepare` now creates schema-2 provenance and ten assets:

- The existing signed `ember-link.bin`, application `manifest.json`,
  `link-releases.json`, `provenance.json`, and `release-notes.md`.
- `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, and
  `factory.json`, containing the fixed flash offsets, sizes, hashes, board,
  source commit, version, and channel.
- `SHA256SUMS`, covering the other nine files.

Only the allowlisted files are copied from the fresh build. Factory validation
requires the original Link partition layout, a DIO/80 MHz/16 MB ESP32-S3 bootloader,
blank initial OTA state, and the exact signed application already in the release.
Device flash dumps, NVS contents, SDK configuration, and keys are never published.
Existing schema-1 packages remain valid for the older update-only release tools.

For a new package, test both ordinary updates and browser installation on a spare
board, then use:

```sh
python tools/release.py publish /path/to/package --qualified --factory-qualified
```

Both flags are operator assertions backed by recorded hardware evidence. They
are not substituted by CI. Verify first boot, USB setup, saved configuration after
setup, machine design preview, and recovery from an interrupted installation.
Publish once; never replace a tagged binary to fix an installer failure.

## Pages deployment

`.github/workflows/installer.yml` builds the website from `main`. It runs on
installer/tooling changes on `main`, and can be dispatched manually on `main`.
Successful release publication and Development recommendation/withdrawal commands
explicitly dispatch it on `main`. This respects the `github-pages` environment’s
main-branch restriction; release events run on tags and cannot deploy there.
The metadata-only `release-channels` branch does not contain workflows either.
If a release is published, edited or removed outside `tools/release.py`, manually
run **Browser installer** on `main` to synchronize the site.

`tools/build_installer.py` resolves the current latest stable release and the
explicit Development feed. It downloads the required assets, checks package
checksums, production application signature, version/channel, and firmware source
tag, then copies files into versioned paths beside the website. It generates
`catalog.json` from those checked packages. Browser requests stay on the site's
origin and need no GitHub token or cross-origin release download permission.

The deploy job runs only after assembly succeeds. A failed job leaves the previous
Pages deployment available. Inspect failures and rerun the workflow; publication
and Pages deployment are separate operations. Website-only edits resolve the same
approved firmware recommendation rather than rebuilding firmware. A cached page
continues to refer to versioned files; missing old files fail before erasure.

Only `installer/dist` is uploaded. Signing keys remain outside GitHub Actions.
The deployed site includes third-party dependency license texts. No telemetry,
account data, Wi-Fi credentials, or USB logs are uploaded by the installer.

## Legacy 0.3.6 bootstrap

The existing stable release predates factory packaging. Do not alter its tag or
assets. An optional, separately qualified companion release named
`installer-v0.3.6` can provide the ten-file factory package. It must be marked a
prerelease and **not latest**, so it cannot replace the firmware stable feed.

`installer/bootstrap.json` explicitly pins the companion's `factory.json` hash
under `v0.3.6`. The site builder also requires its original application manifest
to match published 0.3.6 and its source commit to match `v0.3.6`. A bootstrap is
used only when that exact firmware release is selected and lacks factory assets.
It does not override later stable releases or provide a general fallback.

The initial companion uses the unchanged, production-signed 0.3.6 application.
Its bootloader and partition table are built from the exact 0.3.6 source with
ESP-IDF 6.0.2, and its initial OTA state is blank. A disposable local key may be
needed to satisfy ESP-IDF's configuration dependency during a bootloader-only
build; it is never used to sign or replace the published app, and is not included
in the package. Factory hardware qualification is independent of the older app's
update qualification.

## Local checks

```sh
npm ci --prefix installer
npm test --prefix installer
npm run build --prefix installer
python -m unittest discover -s tests -p 'test_*.py' -v
# Use the ESP-IDF Python environment for signature verification:
python tools/build_installer.py --preview-package /private/path/to/package
python -m http.server 8794 --bind 127.0.0.1 --directory installer/dist
```

Local preview labels the package as a hardware test preview. Pages never uses
`--preview-package`. For a production-equivalent local build, authenticate `gh`
and omit that option. Test cancellation, missing packages, changed channels,
download failures, wrong hardware, and explicit erase consent before testing a
spare board. Do not use a configured customer dongle as a disposable test unit.

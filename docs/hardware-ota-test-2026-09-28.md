# Physical cloud firmware tests — 2026-09-28

## Setup

Same ESP32-S3 Link and FAT16 card as the [delivery smoke test](hardware-cloud-test-2026-09-28.md).
The backend ran locally with the production update state machine and disk-backed
adapters. The user explicitly authorized signed test firmware through a temporary
Cloudflare tunnel. Device/account secrets and signing keys were not embedded in
images or test reports; private account controls stayed on localhost.

Built fixtures in an isolated temporary copy using the existing development key:

- `0.3.0-hwtest1`: healthy image with the TLS fix and diagnostics.
- `0.3.0-hwtest2`: same implementation, except its two local-service health
  callbacks deliberately do not call `ota_confirm()`. The existing 90-second
  guard and bootloader rollback machinery remain unchanged.
- `0.3.0-dev`: the previously tested baseline image for restoration.

Each artifact was signature-verified and matched to its manifest before it was
registered in the local fixture catalogue. No release was published to GitHub,
AWS, or production. The repository's source version was not changed.

## Observed results

| Scenario | Observed outcome | Time including polls/boot |
| --- | --- | ---: |
| Cloud install of healthy image | `installed`, current version `0.3.0-hwtest1`, boot confirmed | 42.13 s |
| Corrupt one download byte | `failed`, `checksum_mismatch`; healthy version unchanged | 46.14 s |
| Install deliberately unconfirmed image | Observed `pendingVerify:true` and `rebooting`; later `rolled_back`, `boot_not_confirmed`, back on `0.3.0-hwtest1` | 140.44 s |
| Deliberately slow download | `failed`, `update_failed`; healthy version unchanged | 28.09 s |
| Restore baseline through cloud | `installed`, current version `0.3.0-dev`, boot confirmed | 50.16 s |

The backend did not prematurely mark the unconfirmed boot installed. Wi-Fi and
test account association survived successful installation and rollback. Existing
SD file content was verified unchanged. After baseline restoration, reset reason
was 3 (software restart), boot verification was complete, and a newly generated
PES design was delivered and read back byte-for-byte through USB.

## Qualifications and follow-up

**Watchdog observation:** after the rollback sequence and before the subsequent
manual power cycle, backend diagnostics reported reset reason 5
(`ESP_RST_INT_WDT`). The rollback outcome succeeded, but the exact reset path
needs investigation with boot logs and task-stack measurements. Do not consider
the 90-second guard fully release-qualified from this run alone. The later
manual power cycle reported reset reason 1, as expected, and baseline restoration
reported reason 3.

**Power interruption is not yet qualified.** The user unplugged and reconnected
the dongle, and it returned with settings intact. However, the backend had already
recorded `update_failed` before that power cut, rather than recovering a durable
`downloading` record as `interrupted`. The temporary tunnel buffered the slowed
response; a separate client received headers but no initial body within 12 seconds.
Changing only the test proxy's streaming headers did not resolve it. Firmware
network timeouts were not weakened. Repeat the partial-download/power-loss test
with a controllable LAN HTTPS server or a staging endpoint that streams promptly.
Cloudflare documents [tunnel response buffering](https://developers.cloudflare.com/tunnel/troubleshooting/)
and [quick-tunnel streaming limitations](https://developers.cloudflare.com/sandbox/api/tunnels/).

Wrong signing key, truncated/mismatched manifests, power loss at boot selection,
missing/full cards, long-run stress, and production AWS validation still require
physical qualification. Software regression coverage does not replace those tests.

## Brother NQ1700E file-read check

Generated `ELCHECK.PES` using the Ember embroidery writer: a 20 mm square,
41 stitch commands, one color, PES version 1, 1,371 bytes. Read-back parsing
confirmed bounds and stitch/color counts. Delivery through the local cloud
backend completed and the USB SD readback matched exactly; existing files were
unchanged. The volume was safely unmounted before asking the user to move the
Link to the Brother NQ1700E and open the design preview. Stitching is not required
for this file-read check.

**Failed on the Brother NQ1700E:** the user reported that inserting the Link
immediately froze the machine, before selecting a file. Recovery required removing
the Link and power-cycling the machine. The same dongle had worked on this machine
before the recent firmware changes. This is a release-blocking compatibility
regression; the cloud/USB readback checks above do not qualify machine support.

After reconnection to the Mac, USB storage and serial enumeration worked. The
original design and generated `ELCHECK.PES` hashes still matched their expected
values. macOS `diskutil verifyVolume` completed its read-only FAT16 check with
exit code 0. Cloud polling was disabled for diagnosis, retaining enrollment and
Wi-Fi configuration.

The previous EmberConnect checkout resolves TinyUSB `0.19.0~3`; Link resolves
`0.21.0~2`. The Espressif MSC adapter remains `2.2.1`, and the application USB
descriptor layout is unchanged apart from identity strings. An isolated signed
`0.3.0-usbtest1` candidate pins TinyUSB `0.19.0~3` while retaining current Link
application code. This dependency change is a hypothesis, not a confirmed cause.
The candidate and machine retest results will be recorded separately.


### USB dependency diagnostic candidate

Installed the signature-verified `0.3.0-usbtest1` image over USB without erasing
settings or the card. Image SHA-256:
`daf363e20cbc1a9ed1bf2e9124d7e52f5f72703601b13debfed49de3c5b3cfb5`.
The component manager resolved TinyUSB `0.19.0~3`; the other resolved component
versions match the baseline. Application code is unchanged except for the
explicit diagnostic version. This candidate is built under a temporary directory;
the repository dependency pin and release version have not been changed.

Mac verification passed: CDC commands, mass-storage enumeration, original design
hash, and byte-identical `ELCHECK.PES`. The image booted in `ota_0`, confirmed its
boot, and reported software-reset reason 3. Wi-Fi remained connected. Cloud was
then restored to its previously enabled state and successfully polled the same
local test backend over verified HTTPS (HTTP 200, no TLS/transport errors).
Keeping cloud enabled and both designs unchanged avoids changing those variables
in the next machine trial. **Brother retest failed:** the user reports the machine still freezes with this
candidate. Reverting TinyUSB alone is therefore not a sufficient fix; do not
promote the diagnostic dependency pin as a resolution.


### Generated-design isolation

The saved pre-Link flash image contains the `EmberConnect Setup` identity and
runtime version string `0.5.1`, consistent with the previous smoke-test record.
Its app descriptor says `0.4.1` because that revision's CMake project version was
not updated alongside its runtime version header. The matching source already
used the MSC+CDC composite layout. The mere presence of CDC is therefore not a
new Link behavior.

An immediate freeze does not establish whether the host fails during enumeration
or scans file contents automatically. After the user reported the failed USB
library trial, a live serial command confirmed the Link was back on the Mac,
running confirmed `0.3.0-usbtest1` with cloud enabled/online. Removed only the
generated `ELCHECK.PES`, whose contents were first checked against the saved local
fixture. The original design hash still matched the pre-test baseline, and the
non-hidden root file inventory now matches that baseline. No firmware or cloud
setting was changed for this isolation. **Brother retest failed again:** the user reports the same freeze after removal
of the generated design. Its presence is therefore not a sufficient explanation.
The original design remained intact throughout.


### Original firmware baseline restoration (in progress)

After both isolated trials failed, prepare a comparison using bytes from the
private pre-Link flash backup rather than another speculative code change.
Extracted the original bootloader (0x0–0x7fff), partition table (0x8000–0x8fff),
OTA selection data (0xf000–0x10fff), and both application slots
(0x20000–0x61ffff). Verified the saved partition table matches those boundaries.
NVS at 0x9000–0xefff, PHY calibration, and SD contents are excluded from writes.
This retains current settings; it is not a full restoration of all pre-Link
state. Saved a fresh complete 16 MiB flash backup before writing, with mode 0600 in the
ignored per-device `artifacts/` directory (`pre-original-restore-2026-09-28.bin`).
Verified embedded checksums and image SHA-256 digests for the original bootloader
and both saved application images. Restored all four firmware regions through
the ROM downloader; all four write hash checks passed. Read back the entire
24 KiB NVS region and verified it was byte-for-byte unchanged from the fresh
backup. No erase-all or eFuse commands were used. Normal boot on the Mac passed: USB CDC reports EmberConnect `0.5.1`, running
`ota_0` with boot verification complete; Wi-Fi is connected. USB storage mounted,
the original design hash matches the pre-test baseline, and `ELCHECK.PES` is
absent. Safely unmounted for the original-firmware Brother comparison. **Brother comparison failed:** the user reports that the restored original
EmberConnect firmware also freezes the machine. A conventional USB thumb drive
was recognized successfully by the same Brother. The issue cannot be attributed
to Link-specific changes alone. This was a firmware-only restoration: SD state,
current settings, and physical hardware were retained, so it does not establish
that all pre-migration conditions were reproduced.


### Separate card-reader comparison

After the original-firmware machine failure, USB diagnostics again confirmed
EmberConnect `0.5.1`, confirmed boot, and connected Wi-Fi on the Mac. The original
design hash still matches. The card presents an MBR layout with one FAT16
partition (about 126 MB, 512-byte logical sectors, 4 KiB clusters). Another
read-only macOS filesystem check completed with exit code 0.

Root entries include the original design, `.fseventsd`, and `.Spotlight-V100`.
macOS denied traversal of `.Spotlight-V100` (operation not permitted); its
contents were not inspected and must not be described as empty. No metadata,
card contents, or firmware were changed in this round.

The user has a USB microSD reader. Next compare the same unchanged card through
that reader on the Brother, with the dongle unplugged. Successful recognition
would narrow the issue toward the dongle USB/power/firmware path. Reader failure
alone would not prove a card defect, since reader compatibility is another
variable. Brother publishes a model-specific
[USB compatibility list](https://download.brother.com/welcome/doch100185/usbmedia_888g0x_g1x_g2x_g3x_g6x_g7x_g8x_g9x_en_r24-12.pdf);
the reader is a diagnostic comparison, not an assertion of official support.


**Reader outcome:** the user reports the Brother stayed responsive but displayed
an approximate “USB media cannot be used” message. It did not reproduce the
freeze, but did not establish that the card is usable on this machine. Reader
compatibility and card/filesystem compatibility remain unresolved.

### Storage-only USB diagnostic profile

Prepared an isolated build from EmberConnect source revision `115d23a` (runtime
`0.5.1`), named `0.5.1-msctest1`. It exposes one MSC interface, removes CDC from
both USB configuration descriptors, sets device class/subclass/protocol to zero
(interface-defined classes), and omits the USB setup task. Wi-Fi, SD handling,
MSC endpoints, requested power, remote-wakeup attribute, and application features
otherwise remain unchanged from that source. Dependencies resolve to the older
TinyUSB `0.19.0~3` and IDF `6.0.2`. No production repository firmware files changed.

Verified the candidate and original saved app signatures with the same existing
EmberConnect public signing key. Candidate size is 1,052,672 bytes; SHA-256:
`ab7bdec309a176850a8d23f20bb4dec15381493474b43a6505b976f9f864a96a`.
Established a temporary authenticated local diagnostic pairing over trusted USB
before installation, storing its token only in a private local test directory.
This preserves a LAN verification/update path while CDC is absent; ROM recovery
and the private full-flash backups remain available.

Installed through signed USB OTA after unmounting the card. On the Mac, local
HTTP reports `0.5.1-msctest1` in `ota_1`, boot verification complete. Wi-Fi API
access works; USB serial is absent, USB storage mounts, the original design hash
matches, and the generated test design remains absent. I/O Registry reports
device class zero. Its interface list was not available in that inspection, so
interface count was not independently verified there. Safely unmounted for a
Brother trial. **Brother result:** the machine stayed responsive and reported unusable media.
This single trial supports the combined storage-only USB profile as a way to
avoid the freeze, but does not establish whether the exact cause is device-level
class descriptors, CDC interfaces, or CDC task behavior. Usable storage is still
unresolved. The same card in a separate reader also produced an unusable-media
message, so comparing storage geometry/format against the working conventional
thumb drive is the next diagnostic step. No card reformat has been authorized or
performed. This is not a permanent decision to remove consumer USB setup.


### Working-drive format comparison and proposed FAT32 test

With both physical devices connected, read-only Disk Utility metadata showed:

| Property | Dongle card | Working thumb drive |
| --- | --- | --- |
| Whole-device capacity | 125,829,120 bytes | 62,914,560,000 bytes |
| Partition scheme | MBR | MBR |
| Filesystem | FAT16 | FAT32 |
| Partition start | 32,256 bytes (sector 63) | 1,048,576 bytes (sector 2048) |
| Logical sector size | 512 bytes | 512 bytes |
| Cluster size | 4,096 bytes | 32,768 bytes |

This is a candidate compatibility difference, not proof that FAT16 caused the
remaining error. Saved a local copy of the original design and verified its
pre-test hash. Also imaged the dongle card using `hdiutil create -srcdevice`;
`hdiutil verify` passed. Mounted the backup read-only and verified the recovered
design against the same hash. A verified copy of the image is retained mode 0600
under ignored per-device artifacts as `sd-before-fat32-2026-09-28.dmg`.

Prepared a separate local test image at the same 125,829,120-byte capacity with
an MBR FAT32 partition starting at 1 MiB. Native macOS formatting selected
512-byte clusters for this small FAT32 volume; copying the working drive's
32 KiB cluster size is inappropriate at this capacity. Restored the original
design into the local image, verified its hash, and passed the read-only
filesystem check. This validates a proposed layout locally, not on the Brother.
Both physical drives remain unchanged. Any format/write of the physical card
requires explicit user approval because it erases its existing filesystem.
Keep the current storage-only diagnostic firmware for that comparison.


### Approved physical FAT32 format and restore

The user explicitly approved reformatting only the dongle card and restoring the
original design. Before erasing, matched the original card UUID, external device
identity (`TEST MSC Storage`), MBR scheme, and exact 125,829,120-byte capacity.
Used macOS Disk Utility's FAT32/MBR erase operation on that card. The working
thumb drive was not formatted or written by this task.

The resulting physical card is `EMBERLINK`, FAT32, UUID
`4D534520-EA62-31B2-B88A-24B40BF3009C`, with 512-byte clusters. **Actual partition
start remains sector 63 (32,256 bytes):** native Disk Utility chose its standard
MBR geometry, rather than the separately prepared local image's 1 MiB alignment.
Do not describe the physical geometry as an exact copy of the working thumb
drive or that local image. The new FAT32 filesystem is the main variable in this
trial; partition geometry remains a possible follow-up if recognition fails.

Kept `0.5.1-msctest1` storage-only firmware unchanged. After safe host unmount,
restored the original design through the authenticated local Wi-Fi upload API.
This exercised firmware mounting/writing of the new FAT32 volume. The upload
succeeded, USB remounted, and the restored design's SHA-256 matched the original
pre-test baseline exactly. No original design was sent through the cloud tunnel.
**Passed on the Brother NQ1700E:** the user reports that the machine recognizes
the FAT32 drive and opens the original design preview. No stitching test was
requested or reported. This establishes a working baseline on this one unit:
EmberConnect `0.5.1-msctest1`, storage-only USB, and the reformatted FAT32 card.

The observed sequence is:

| Firmware / USB profile | Card | Brother result |
| --- | --- | --- |
| Link baseline, composite MSC+CDC | Original FAT16 | Immediate freeze |
| Link with older TinyUSB, composite MSC+CDC | Original FAT16 | Immediate freeze |
| Same Link diagnostic, generated test design removed | Original FAT16 | Immediate freeze |
| Saved original EmberConnect 0.5.1, composite MSC+CDC | Original FAT16 | Immediate freeze |
| Separate USB card reader | Original FAT16 | Responsive; unusable media |
| EmberConnect 0.5.1-msctest1, MSC only | Original FAT16 | Responsive; unusable media |
| Same MSC-only diagnostic firmware | Fresh FAT32, original design restored | Drive recognized; original design preview opens |

Do not overstate causality: the storage-only profile changed both device class
fields and CDC exposure/task startup, and formatting changed filesystem metadata
as well as FAT type. No reverse trial was performed with composite USB on FAT32,
so it is not established that every composite/FAT32 configuration freezes.
Nevertheless, storage-only USB plus FAT32 is the verified working combination.
The partition start is still sector 63, showing that 1 MiB alignment was not
required for this successful test.

This is an EmberConnect diagnostic build, **not the cloud-enabled Ember Link
firmware**. Remaining integration work: bring a machine-compatible storage-only
profile into Link while preserving an explicit USB serial setup path, update the
consumer setup instructions/UI for that path, and repeat delivery/reconnect and
design preview on the Brother with the cloud-enabled firmware. Retain the private
firmware and card backups until that integration is validated.

# Hardware validation still required

This iteration builds for ESP32-S3 and has host tests. Initial physical USB,
Wi-Fi, and shared file-writer smoke tests passed on one dongle; see the
[2026-09-20 results](hardware-smoke-test-2026-09-20.md). Physical HTTPS cloud delivery against the local backend also passed after a TLS
compatibility fix; see the [2026-09-28 results](hardware-cloud-test-2026-09-28.md).
Production integration and the remaining reliability checks below are still
required. These results are not manufacturing approval.

1. Build/sign with a development key; flash a development unit separately.
2. Check SD initialization, USB mass storage/CDC enumeration, and WiFi setup.
3. Enroll a development identity with a staging backend implementing
   [the cloud protocol](cloud-protocol.md), then configure/enable over USB.
4. Verify wrong-host/untrusted TLS certificates fail and tokens never reach the
   download host.
5. Send a real design; confirm the physical machine lists and opens it.
6. Measure minimum free heap, largest free block, task stack headroom, handshake
   memory, SD throughput, and USB reconnect timing.
7. Try invalid digests/lengths, expired grants, full cards, disconnections,
   duplicate offers, lost receipt acknowledgements, and revoked credentials.
8. Interrupt power during staging, replacement, reconnect, and receipt saving.
   Inspect original/new files and confirm uncertain outcomes are reported.
9. Confirm local writes, OTA, and reset cannot race a cloud transfer; verify local
   operation with cloud disabled or unreachable.
10. Run repeated transfers/long idle sessions, looking for heap growth, stalls,
    power instability, and unexpected NVS writes.

USB account proof and physical cloud delivery have been tested with a local
backend. Browser UI setup, production staging integration, and the remaining
hardware reliability scenarios still need qualification. Remote file management
remains separate feature work.

Physical cloud update, corruption rejection, and rollback outcomes are recorded
in the [original OTA report](hardware-ota-test-2026-09-28.md). The latest local
updater and recovery qualification is tracked in the
[release qualification report](release-qualification-2026-09-28.md). Historical
reports describe the builds tested at that time; they are not approval of the
current release candidate.

## Machine integration status

The earlier EmberConnect storage-only/FAT32 diagnostic baseline was subsequently
integrated into cloud-enabled Ember Link. On `0.3.2-dev`, the Brother NQ1700E
recognized the device and previewed a newly cloud-delivered design; see
[USB modes and machine results](hardware-usb-modes-2026-09-28.md). Bridge local
Wi-Fi sending and replacement were also physically previewed on that machine.
The screen and LED settings were tested on later development builds.

This closes the earlier storage-only integration task. It does not establish
broad machine compatibility or qualify the exact current release artifact.
Repeat machine preview, update/recovery, and settings/file-preservation checks
on the candidate before publishing a stable release. Interrupted cloud downloads,
boot-selection power loss, missing/full cards, and sustained reliability remain
separate qualification requirements.

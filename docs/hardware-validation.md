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
in the [2026-09-28 OTA test report](hardware-ota-test-2026-09-28.md). A watchdog
reset observed around rollback and the unqualified interrupted-download test
remain release follow-ups.

**Working machine baseline found:** the Brother NQ1700E recognizes the card and
opens the original design preview with the isolated EmberConnect
`0.5.1-msctest1` storage-only USB profile and a freshly formatted FAT32 card.
The same profile avoided the freeze on the original FAT16 card, but the Brother
reported unusable media. See the OTA report for the complete comparison and its
causal limits. This is one machine/one card and a preview check, not a stitching
or broad compatibility qualification.

**Release blocker remains for Link integration:** the successful firmware is an
EmberConnect diagnostic build, not cloud-enabled Ember Link. Integrate a
machine-compatible storage-only profile while retaining an explicit USB serial
setup path, update consumer setup guidance, and validate cloud delivery and USB
reconnect/preview on the Brother with the integrated image. Do not remove USB
setup as a product feature or claim current production Link is qualified from
this diagnostic result.

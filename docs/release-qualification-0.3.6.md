# Release qualification — 0.3.6

Source: `95cede6cea926541bdb56a72862c557a8c4fec70`.
Version: 0.3.6. Board: LilyGO T-Dongle-S3, partition layout `link-v1`.
Signed image SHA-256: `ff49a1069c6986cf80ab3015bc407b977916e457a2e720733e0d3afc25b9b0f9` (1,314,816 bytes).
The established production verification key is unchanged.

## Automated checks

All eight production-C ASan/UBSan suites and both release-catalog tests passed.
ESP-IDF 6.0.2 built the release. The manifest tool verified the production RSA
signature using the committed public key. Draft release downloads matched all
four local assets byte-for-byte: image, manifest, catalog, and checksum file.

The integration example commit `417dec7` passed 42 Python and 27 Node tests;
GitHub CI passed on Python 3.11 and 3.13. It includes conflict handling, durable
receipts, idempotent retries, expiry, restart recovery, and owner isolation.

## Exact stable binary on hardware

The prototype updated from production-signed 0.3.6-dev to this exact 0.3.6 binary
through authenticated local Wi-Fi. It changed OTA slots and confirmed a healthy
boot, preserving the production trusted digest. USB inspection verified Wi-Fi,
cloud identity/enabled state, device name, screen/LED/orientation preferences and
revision. Both existing PES file hashes matched. The card was safely unmounted.

The current reference backend sent screen off, LED off, and normal orientation
through the HTTPS cloud polling path. The device persisted an applied receipt
at revision 5 and independent USB readback matched. A second cloud request
restored screen on, LED on, and 180-degree orientation at revision 6; its receipt
and USB readback also matched. The prototype retains its disposable example
cloud configuration for further testing.

## Scope

Production AWS and the web app repository were not changed. No prod/dev channel
split was implemented. This release uses the existing stable catalog.

The cloud-settings journal has native injected-failure and restart coverage;
physical power cuts during its NVS write/ack window and downgrade/return through
0.3.5 were not requalified. The earlier 0.3.5 OTA interruption/rollback record
remains historical evidence, not a repeat of those trials on 0.3.6. Older
firmware reads pre-upgrade legacy display preferences; see cloud-settings.md.

## Machine preview and power-cycle persistence

After unplugging from the Mac and reconnecting normally to the Brother NQ1700E,
the owner confirmed a saved design previewed and the machine remained responsive.
A fresh authenticated poll reported 0.3.6 with the restored display settings at
revision 6, confirming persistence across that power cycle. No stitching was
initiated.

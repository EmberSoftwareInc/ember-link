# Ember Link 0.3.7 qualification

Date: 2026-09-30. Release status: [published stable](https://github.com/EmberSoftwareInc/ember-link/releases/tag/v0.3.7).

## Exact package

- Source commit: `b248d6e403b8409f9a6055627f50a88a948085ae`.
- Signed application SHA-256: `7b9b38b260ac0eb2630e185cbb3b8c5ad2194cdb4c6c5b54a7b2492d0f79df82`.
- Board/layout: `lilygo-t-dongle-s3` / `link-v1`, original 16 MB LILYGO board with screen.
- Public verification key: `docs/signing/ember-link-production.pub`.
- First-install package: schema 2, four flash images with a checked factory manifest.
- Hardware: spare dongle, 120 MiB card, Brother NQ1700E.

## Completed checks

- GitHub PR #3 firmware checks passed: native sanitizer suites, real FatFs formatter/rename tests, installer tests/build and Python package/release tests.
- Fresh ESP-IDF 6.0.2 release build and production signature verification passed.
- Signed USB update from dev.3 to this exact 0.3.7 image completed; healthy boot confirmed and all three backed-up card files matched their original hashes. Device name, display settings and provisioning flag matched. The spare was unprovisioned; this does not demonstrate preservation of configured Wi-Fi or cloud credentials.
- User reported the browser wrote and verified 0.3.7. After normal reconnect, independent USB inspection confirmed the version, healthy boot, both card protocols and an unconfigured Wi-Fi/cloud state appropriate to a fresh installation. Card label and all three file hashes were unchanged.
- Dev.3 qualification of the same card-naming implementation covered browser rename, persistence as EMBER LINK in macOS, unchanged volume identity/layout and file bytes, and successful Brother design preview. See [card preparation](card-preparation.md) for the candidate hash and exact scope.

## Final physical checks

- The user ran **Erase and prepare as FAT32** through the browser on 0.3.7 and reported **FAT32 card prepared and verified as EMBER LINK**.
- After normal reconnect, independent USB and macOS checks confirmed 0.3.7, a healthy boot, FAT32, label EMBER LINK, 120 MiB media, a 1 MiB partition offset and 512-byte clusters. The old design was absent, consistent with the explicit format.
- All three backed-up files were restored and their SHA-256 hashes verified. The generated `DIYTEST.PES` square is 1,371 bytes, SHA-256 `c945d4e3c55356f8b15580397c859335e8843d689ebb704cea3beb032ed0a452`.
- The card was safely unmounted. The user moved Link normally to the Brother NQ1700E and confirmed the square preview opened and the machine stayed responsive on the final 0.3.7 image.

## Scope

Browser-install and formatting power-cut recovery were exercised on the earlier dev.2 candidate; their exact evidence is recorded in the card-preparation report. These interruption tests are supporting evidence, not additional tests of this exact 0.3.7 image. The existing 0.3.6 cloud delivery/settings and updater qualifications remain historical. This run is not a new cloud end-to-end qualification, screenless-board qualification, stitch-out, multi-day endurance test, or broad card/machine compatibility certification.

The test uploader initially competed with Bridge for the USB port. After Bridge was fully quit and exclusive USB access was used, updates completed. Installer consumers should release other USB setup clients before installation or card maintenance.

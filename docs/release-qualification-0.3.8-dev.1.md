# Ember Link 0.3.8-dev.1 qualification

Date: 2026-10-02. Status: exact candidate qualified for development publication.

## Exact package

- Source: `d498d7b3822fa6d065ceb8d809686218907f4de4` on `dev`.
- Application SHA-256: `2c31e45a77b33f9a30c09f8a5e116efaba6cb1ca59e1ba22ad848883b94b21d9`.
- Application size: 1,314,816 bytes.
- Board/layout: LILYGO T-Dongle-S3 with screen, `link-v1`.
- Established production signing key; verified with `docs/signing/ember-link-production.pub`.
- Schema-2 first-install package with ten public release assets.
- Hardware: spare dongle with a 120 MiB FAT32 card, Brother NQ1700E for preview qualification.

## Completed checks

- Native ASan/UBSan suites and the complete ESP-IDF 6.0.2 firmware build passed.
- GitHub Firmware checks passed on the exact source commit, including installer tests/build, real FatFs checks, and Python release/package tests.
- Signed USB update from 0.3.7 to the exact candidate completed; the device confirmed a healthy boot and the new enrollment capability.
- Wi-Fi, device name, display settings, and the original design/metadata hashes were preserved. The firmware-generated START HERE.html was removed on the first provisioned boot, as specified by existing storage behavior; the original helper was backed up.
- A temporary device-only backend over an approved HTTPS tunnel rejected an expired enrollment ticket. USB reported `expired` and HTTP 410 without configuring the device.
- A changed service and changed ticket for the same session produced `service_mismatch` and `invalid_enrollment` respectively. Existing enrolled identity produced `already_configured`.
- USB status remained available as a redacted cached snapshot during a blocked HTTPS response, reporting enrollment and network progress.
- After a lost response, the backend accepted an identical retry even after ticket expiry, preserving exactly one device identity. A subsequent power cycle retained that identity and authenticated successfully.
- The initial pause harness allowed a retry to complete before the requested power cut. That run proves lost-response recovery and subsequent reboot persistence, not interruption before enrollment acknowledgement. The harness was corrected to pause every replay; the separate repeat below passed.
- The test backend removed the account association; the device reported `unclaimed` while retaining its credential. USB account relinking associated a second test owner using that existing credential.
- Generated ENR038.PES arrived through the cloud poll/download/receipt flow, matched its expected bytes, and preserved the original design. The USB port re-enumerated during storage transfer, as expected, and was reopened for verification. Test cloud was then disabled.
- The user reported that the localhost browser installer wrote and verified 0.3.8-dev.1 using the candidate's exact factory package.

- Independent USB readback confirmed the fresh installation booted healthily with the production verification key, Wi-Fi and cloud identity cleared, enrollment available, and no pending enrollment. All original card files (including the regenerated START HERE.html) and the generated cloud design matched their saved hashes.

- The corrected physical power-cut repeat passed. Every enrollment acknowledgement was held; after unplugging and rebooting, USB still reported the same pending enrollment and the backend had received zero authenticated polls. After releasing the acknowledgement, the expired consumed ticket recovered the identical credential and binding, and authenticated polling began. Exactly one device identity was created.
- Test cloud was disabled on the spare after qualification. Its disposable test identity remains stored; disabling cloud does not unenroll it. Original design and metadata hashes and the generated square were verified before safely unmounting the card.
- The operator confirmed ENR038.PES previews as a square on the Brother NQ1700E, with the machine staying responsive.

## Publication

Hardware qualification is complete. Prerelease publication, Development recommendation, and hosted installer parity verification are the remaining release operations. Stable must remain on 0.3.7.

## Scope

The temporary qualification backend implements the device contract with disposable accounts and credentials; it is not the production account API or browser enrollment UI. The production backend/frontend remain separate work. No production account, device configuration, cloud infrastructure, or web app repository was changed. Native fault injection supports, but does not replace, the physical evidence above. This is not a stitch-out, endurance test, or broad machine compatibility certification.

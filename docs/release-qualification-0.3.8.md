# Ember Link 0.3.8 stable qualification

Status: candidate preparation in progress on 2026-10-06; not published.

## Scope

Promotes self-service cloud enrollment, cloud removal/re-enrollment recovery,
and authenticated local filesystem operations for Bridge from 0.3.8-dev.2.
Firmware logic is unchanged from the tested development release; the stable
version has a new binary and requires its own exact-package checks.

## Supporting development-image evidence

See [dev.1 qualification](release-qualification-0.3.8-dev.1.md) and
[dev.2 qualification](release-qualification-0.3.8-dev.2.md) for interruption,
factory installation, local file operations, and card integrity results.

Production cloud testing on 2026-10-06 used the spare LILYGO T-Dongle-S3,
0.3.8-dev.2, the verified second FAT32 card, and a Brother NQ1700E:

- Enrollment and removal CORS preflights passed.
- USB self-service enrollment completed and the account reported Link online.
- Cloud LED settings were acknowledged and physically observed; restored on.
- Cloud design delivery completed and the Brother previewed the design.
- Account removal completed; the saved design remained readable on the Brother.
- Same-device enrollment succeeded again, followed by acknowledged cloud settings.
- After unplugging, the account correctly reported offline. A queued LED setting
  remained pending, then applied after normal reconnect without USB setup.
- Wi-Fi and cloud identity survived restart. Another cloud delivery succeeded;
  both earlier and new designs previewed with the Brother responsive.

New design transfers require online status in the current web/backend contract.
Offline settings queuing was tested; offline creation of design jobs was not.
These are development-image results, not stable-image qualification.

## Exact stable package checks

- [ ] Record source commit, image hash, size, and production key ID.
- [ ] Native sanitizer, real FatFs, installer, and release-tool checks pass.
- [ ] Signed update installs and confirms healthy boot on the spare.
- [ ] Wi-Fi, account identity, settings, pairing, and card files are preserved.
- [ ] Production cloud command and design delivery succeed on 0.3.8.
- [ ] Brother previews the design and remains responsive.
- [ ] Exact factory package passes browser install and healthy boot checks.
- [ ] Verify published assets and Stable feed against the qualified package.

## Limits and deferred checks

The user explicitly deferred the longer powered-on reliability test on
2026-10-06. It is not a passed gate. Multi-day reliability, power cuts during FAT
mutations, stitch-out, and other machine models remain untested.
The first test card's corruption remains unexplained, as documented in
[local filesystem operations](local-file-operations.md). Successful second-card
checks do not establish the cause or prove universal card/machine compatibility.
Factory installation resets internal settings and identity; perform it only on
the backed-up spare with explicit operator coordination. The regular dongle
must not be modified.

# Ember Link 0.3.8 stable qualification

Status: candidate preparation in progress on 2026-10-06; not published.

## Scope

Promotes self-service cloud enrollment, cloud removal/re-enrollment recovery,
and authenticated local filesystem operations for Bridge from 0.3.8-dev.2.
Removes automatic first-run setup shortcut creation following factory qualification.
The stable version has a new binary and requires its own exact-package checks.

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

## First candidate checks (superseded; not publication approval)

- [x] Record source commit, image hash, size, and production key ID.
- [x] Native sanitizer, real FatFs, installer, and release-tool checks pass.
- [x] Signed update installs and confirms healthy boot on the spare.
- [x] Wi-Fi, account identity, name/display settings, and all 73 visible card files are preserved.
- [x] Confirm retained local Bridge pairing on the stable image.
- [x] Production cloud command and design delivery succeed on 0.3.8.
- [x] Brother previews the design and remains responsive.
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

## Candidate package identity

- Source commit: `74b6451baa2b295047e749a30130e695ffd3c5bb` on `main`.
- Application size: 1,314,816 bytes.
- SHA-256: `fbb18da6a097e6eab6cdae21600a89ac11985e64ea82970b50add303f16d5297`.
- Production verification key ID:
  `6d0a5aa1c94e21afb66d4985446c3446955c9593345ddf1ae161fb66a1c79455`.
- ESP-IDF 6.0.2 clean build and production signature verification passed.
- Native production-C ASan/UBSan suites, real FatFs formatter tests,
  20 Python release-tool tests, and 11 installer tests passed; installer built.
- Source comparison to `v0.3.8-dev.2` shows no firmware logic changes, only
  the two version declarations. Documentation changes do not alter the binary.
- Before installation, spare identity and healthy dev.2 boot verified; Wi-Fi,
  active production cloud connection, and screen/LED configuration recorded
  privately. Second card mounted read-only for backup: 146 files including host
  metadata, 73 visible files. Card safely unmounted before the signed update.

## Exact update results

The signed USB update verified the full image and rebooted. Independent USB
readback reported 0.3.8 with no pending boot verification; Wi-Fi, cloud device
identity, name, and display/LED settings matched the pre-update snapshot.
A read-only card comparison verified all 73 visible file hashes and an unchanged
visible file list. The card was safely unmounted. The production Devices page
reported the spare online on 0.3.8 and acknowledged a cloud LED-off command.
The retained Bridge pairing was subsequently verified through an authenticated local API request.

The LED was restored through the cloud and acknowledged as saved. The operator
then moved the spare to the Brother, sent a design through the production Ember
cloud, and confirmed delivery and a responsive Brother preview on the exact
0.3.8 image. Browser factory installation remains pending.

Before the approved factory-install check, the existing Bridge token authenticated
`/api/info` on the USB-identified spare running 0.3.8 with normal Wi-Fi connected.
No new pairing was performed. A fresh read-only backup captured 147 files
including metadata after the latest production delivery (`Homer Star.pes`); the
card was safely unmounted. With explicit operator approval, Spare Link was
removed from its account before the factory installer reset. The local browser
preview serves the exact verified factory package; installation is pending.

## Factory check and corrected candidate

The operator reported the first candidate written and verified in Chrome. USB
readback confirmed a healthy fresh 0.3.8 boot, cleared Wi-Fi and cloud identity,
and all 74 pre-factory visible files unchanged. The file-list assertion caught
an additional `START HERE.html`; this is expected for an unprovisioned device.
Inspection found its setup URL still used `connect.emberdesign.net`, which
failed a public DNS/HTTP check on 2026-10-06. The operator requested removing shortcut creation entirely. The corrected
candidate never adds `START HERE.html`; existing post-Wi-Fi cleanup for older
helpers is retained. Setup remains available directly through Ember or Bridge.

The first candidate was never pushed, tagged, drafted, or published. Its exact
image checks above remain historical evidence, not checks of the corrected
binary. The corrected 0.3.8 candidate is rebuilt into a separate local package;
no distributed GitHub assets or tags are replaced.

### Corrected exact-package checks

- [ ] Record corrected source, application hash, and verified production signature.
- [ ] Automated checks pass after removing setup shortcut creation.
- [ ] Browser installation, fresh healthy boot, retained files, and absence of new shortcut creation pass.
- [ ] Fresh Wi-Fi setup and self-service account enrollment pass.
- [ ] Cloud settings and design delivery pass; Brother preview stays responsive.
- [ ] Verify published assets and Stable feed against the corrected qualified package.

# Ember Link 0.3.8 stable qualification

Status: published as latest Stable on 2026-10-06 after explicit operator approval.
Public GitHub downloads and the deployed browser installer match the qualified package.
The limitations below remain open or explicitly deferred.

## Release scope and package

Promotes self-service cloud enrollment and authenticated local filesystem
operations for Bridge from 0.3.8-dev.2. Stops automatically creating
`START HERE.html`; existing cleanup after Wi-Fi setup remains for older helpers.
Normal removal/re-linking preserves device identity. Recovery after a complete
flash erase remains a separate, deferred backend capability.

- Source: `f7ffaeb7d7a69436b0adc9359218d1a9a1fe693b` on `main`.
- Version/channel: `0.3.8`, Stable; application size: 1,314,816 bytes.
- Application SHA-256:
  `57dda28cfc0dec1052f4a8fd0071117e87f663b4a5ce3936a6ceae5d6b223466`.
- Production verification key ID:
  `6d0a5aa1c94e21afb66d4985446c3446955c9593345ddf1ae161fb66a1c79455`.
- Fresh ESP-IDF 6.0.2 build; signature, manifest, factory layout, package checksums,
  source version and channel verified. Only allowlisted public assets are packaged.
- Compared with dev.2, firmware changes are the two version declarations and
  removal of automatic setup-file creation. Qualification documentation added
  afterward does not change the release binary.

## Automated validation

Native production-C ASan/UBSan suites, real ESP-IDF FatFs tests, and all 20 Python
release-tool tests passed after removing setup-file creation. The unchanged
installer passed its 11 tests and build. The assembled local preview's factory
manifest and all four firmware assets matched the final package byte for byte.
A distinct preview asset path prevented reuse of earlier candidate caches.
Release-tool test output about publication is mocked; these tests publish nothing.

## Final candidate hardware results

Hardware: spare LILYGO T-Dongle-S3, verified second 120 MiB FAT32 card, and Brother
NQ1700E. The regular dongle was not modified.

- Chrome installed the final factory package and reported written and verified.
- After normal reconnect, independent USB readback reported 0.3.8, healthy boot
  with no pending verification, and cleared Wi-Fi/cloud identity as expected.
- A read-only card comparison verified all 74 existing visible file hashes and
  an unchanged file list. `START HERE.html` was not recreated. Card safely unmounted.
- Fresh Wi-Fi setup succeeded. Account enrollment encountered the known retained
  identity conflict described below, then succeeded after approved support recovery.
- The production Devices page reported the spare online on 0.3.8. Cloud LED-off
  and LED-on commands were acknowledged as saved; the LED was restored to on.
- The production editor recorded `Leviathan (Clone).pes` saved to Ember Link.
  The operator confirmed its Brother preview opened and the machine stayed responsive.

## Supporting update and development evidence

An earlier unpublished 0.3.8 candidate was installed through the signed USB update
path. Independent USB readback confirmed healthy boot and retention of Wi-Fi,
cloud identity, name and display/LED settings. All 73 then-existing visible files
matched their pre-update hashes. The existing Bridge pairing token authenticated
local API access without re-pairing. Production cloud settings, design delivery,
and a responsive Brother preview passed on that candidate.

Its source was `74b6451baa2b295047e749a30130e695ffd3c5bb` and image SHA-256 was
`fbb18da6a097e6eab6cdae21600a89ac11985e64ea82970b50add303f16d5297`.
Factory checking found its generated helper linked to an unavailable setup
hostname; the operator requested removing helper creation. That candidate was
superseded and must not be published. The final image above was qualified through
factory installation; the signed USB update/preservation check was not repeated
with that final binary. Update machinery is unchanged by the helper removal.

See [dev.1 qualification](release-qualification-0.3.8-dev.1.md) and
[dev.2 qualification](release-qualification-0.3.8-dev.2.md) for enrollment recovery,
interruption tests, local file operations, and card-integrity evidence. Additional
production tests on dev.2 on 2026-10-06 passed enrollment, account removal, same-
device re-linking with retained credentials, cloud settings, design delivery and
Brother preview, automatic cloud reconnection, and queued offline settings after
restart. Old and newly delivered designs remained readable. Current frontend and
backend require online status to create new design transfers; offline design-job
creation is not supported and was not tested.

## Factory-reset recovery issue and explicit deferral

After account removal and factory erasure, linking showed: "This pending
connection needs attention. Reconnect with the original account or contact Ember
support." An isolated backend reproduction confirmed HTTP 409
`enrollment_conflict`: removal retains the old credential and advances ownership
generation, while a full flash erase makes the device generate a different token.
Fresh enrollment deliberately refuses to replace the existing identity. Earlier
normal re-linking passed because the old device credential was retained.

On 2026-10-06 the operator approved a one-time support reset for this spare and
explicitly deferred self-service factory-reset recovery. The correct AWS account
and table were verified. The single identity record was privately backed up and
confirmed unowned, not administratively revoked, and without active work.
Deletion required the exact backed-up data and version plus no owner index
attribute. Returned data matched the backup; a consistent read verified absence.
Only that identity record was deleted. Enrollment/removal audit records, design
objects, infrastructure, and other devices were unchanged. After restart the
spare enrolled successfully and completed the final cloud checks above.

This does not fix the product limitation or establish a general automatic-reset
policy. Previously enrolled devices that lose credentials through full flashing
still require authorized support recovery. Prefer signed updates that preserve
configuration. [Cloud enrollment](cloud-enrollment.md#known-limitation-enrollment-after-a-full-flash-erase)
records the deferred work and required ownership/replay safeguards. Release notes
include this limitation. Private backups and device records remain outside Git.

## Other limits and deferred checks

The operator explicitly deferred the longer powered-on reliability test on
2026-10-06. It is not passed. Multi-day reliability, deliberate power cuts during
FAT mutations, stitch-out, and other embroidery-machine models remain untested.
The first card's corruption remains unexplained; successful second-card checks
neither identify the cause nor prove universal card/machine compatibility. See
[local filesystem operations](local-file-operations.md). These limits are disclosed
in the release notes. No claim of power-loss-atomic FAT operations is made.

## Publication verification (completed 2026-10-06)

- [x] Push reviewed source and qualification documentation to GitHub.
- [x] Source CI passes; draft targets the final package's source commit.
- [x] Draft assets exactly match the verified local package.
- [x] Publish v0.3.8 as latest Stable and verify the immutable tag/source binding.
- [x] Anonymous Stable catalog and image match the qualified package.
- [x] Browser installer deployment succeeds and serves the final factory assets.

[Source CI](https://github.com/EmberSoftwareInc/ember-link/actions/runs/37508376840)
passed before publication. [Release v0.3.8](https://github.com/EmberSoftwareInc/ember-link/releases/tag/v0.3.8)
was published at 18:11:36 UTC; its tag resolves to the source commit above.
All ten anonymously downloaded release assets and the latest Stable catalog
matched the local qualified package byte for byte.

[Browser installer deployment](https://github.com/EmberSoftwareInc/ember-link/actions/runs/37509402805)
passed. At 18:15 UTC, the public Pages catalog selected Stable 0.3.8; its factory
manifest and all four firmware parts matched the qualified package byte for byte.
Development remains on 0.3.8-dev.2. The [public installer](https://embersoftwareinc.github.io/ember-link/)
is ready for new DIY boards and deliberate recovery.

Production AWS firmware-catalog synchronization is a separate cloud deployment;
GitHub publication does not by itself update that catalog.

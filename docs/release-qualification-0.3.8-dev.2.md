# Ember Link 0.3.8-dev.2 development qualification

Status: exact package qualified for opt-in development publication on 2026-10-02.
Publication and Development recommendation verification are recorded below once complete.

Adds authenticated local filesystem operations for Bridge's Files page: listing,
create folder, move/rename and file/empty-folder deletion. Shared operations are
transport-independent; no cloud filesystem commands or backend changes are added.
Requires the matching Bridge file-browser development build.

Supporting hardware results and limitations are in [local filesystem operations](local-file-operations.md)
and Bridge's `docs/local-file-browser.md`. The earlier local test image was
`0.3.8-dev.1-files.2`; these results must not be relabeled as exact-image checks.
The first test card's corruption remains unexplained; a second card passed all
feature checks, nested Brother preview, file hashes and FAT integrity inspection.
Use backed-up designs and a verified card during development testing.

## Exact package checks

- [x] Record source commit, application SHA-256, size and production signing key ID.
- [x] Source/CI checks pass on release source.
- [x] Signed update installs and confirms a healthy boot.
- [x] Wi-Fi, name, display/LED settings, cloud-disabled state and card files retained.
- [x] Final image passes local file operations through matching Bridge build.
- [x] Exact factory package passes browser installation and healthy boot checks.
- [x] Nested design previews on Brother NQ1700E; machine remains responsive.
- [x] Record remaining limitations; approved for prerelease publication only.

Stable firmware remains 0.3.7. Stable Bridge remains 0.5.2. Multi-day reliability
and final stable-package qualification are separate; do not claim they passed.

## Package identity and completed checks

- Source: `60032687703b9092c9df00d10c60b2cba4c0ea73` on `dev`.
- Application: 1,314,816 bytes; SHA-256
  `85530922319a21ab71d4e2fd0a564258ea779948217e0073f4a069537083a43e`.
- Production verification key ID:
  `6d0a5aa1c94e21afb66d4985446c3446955c9593345ddf1ae161fb66a1c79455`.
- [Source CI](https://github.com/EmberSoftwareInc/ember-link/actions/runs/37070345514)
  passed on the exact source commit. Local native sanitizer and release-tool
  checks passed before packaging.
- Signed USB update from the earlier local test image installed the exact image,
  confirmed a healthy boot, and retained Wi-Fi, name, display/LED preferences,
  cloud-disabled state and all 70 existing generated designs.
- Chrome installed the exact factory package using the local installer preview.
  The operator confirmed written-and-verified success. Independent USB readback
  confirmed 0.3.8-dev.2, healthy boot, cleared Wi-Fi/cloud configuration and identity,
  and all 70 existing designs unchanged. Factory installation deliberately resets
  internal configuration; it is distinct from the settings-preserving update.
- The packaged Bridge 0.5.3-dev.1 app configured Wi-Fi and paired the spare again.
  Twelve final checks passed: authenticated listing; 34-file pagination; delivery
  of two generated designs; folder creation; stale-revision refusal; file move and
  rename; collision refusal; nonempty-folder refusal; empty-folder deletion;
  populated-folder rename/restore; disposable-file deletion; final nested listing.
- With Link in the Brother NQ1700E, the operator previewed the generated square at
  `FBREL2/REL038.PES`; the machine remained responsive.
- After another normal power cycle, USB readback confirmed healthy boot and Wi-Fi
  retention. A read-only mounted-volume comparison verified all 71 retained
  designs against expected SHA-256 hashes, including the new nested design and
  every original file. The card was safely unmounted afterward.

The earlier independent-reader FAT integrity audit remains supporting evidence
from the local test image, not a new raw FAT audit of the final image. Final-image
checks above used a second 120 MiB FAT32 card and a spare LILYGO T-Dongle-S3.
The production-connected device was not modified.

Multi-day reliability, deliberate power cuts during FAT mutations, stitch-out,
other embroidery machines and stable-package qualification remain untested.
The first card's unresolved corruption remains a stable-promotion review item.

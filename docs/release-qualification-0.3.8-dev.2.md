# Ember Link 0.3.8-dev.2 development qualification

Status: preparation in progress; not approved for publication yet.

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

- [ ] Record source commit, application SHA-256, size and production signing key ID.
- [ ] Source/CI checks pass on release source.
- [ ] Signed update installs and confirms a healthy boot.
- [ ] Wi-Fi, name, display/LED settings, cloud-disabled state and card files retained.
- [ ] Final image passes local file operations through matching Bridge build.
- [ ] Exact factory package passes browser installation and healthy boot checks.
- [ ] Nested design previews on Brother NQ1700E; machine remains responsive.
- [ ] Record remaining limitations and publish as prerelease only.

Stable firmware remains 0.3.7. Stable Bridge remains 0.5.2. Multi-day reliability
and final stable-package qualification are separate; do not claim they passed.

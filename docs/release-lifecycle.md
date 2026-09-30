# Stable and development release lifecycle

This is the operator runbook for Ember Link firmware. Branches, catalogs, and
`tools/release.py` implement the GitHub lifecycle described here. Bridge and the reference cloud example implement separate per-device
Stable/Development preferences. Production cloud deployment is also separate from GitHub publication.

## Branches and feeds

| Name | Purpose | What a push does |
|---|---|---|
| `main` | Reviewed code for stable releases | Runs checks; publishes nothing |
| `dev` | Integration branch for the next release | Runs checks; publishes nothing |
| Feature branches | Individual changes, merged into `dev` | PR checks when targeting `dev` or `main` |
| `release-channels` | Metadata only: development recommendation and its history | Updates the dev catalog; contains no firmware source |

`main` remains the default branch. The dev branch initially starts from the same
stable source, with the next numbered development version. It is persistent;
feature branches can be deleted after merging. Never merge `release-channels`
into either code branch.

- Stable feed (existing URL):
  <https://github.com/EmberSoftwareInc/ember-link/releases/latest/download/link-releases.json>
- Development feed:
  <https://raw.githubusercontent.com/EmberSoftwareInc/ember-link/release-channels/dev.json>

Both use catalog schema 1. New catalogs also identify `channel: stable` or
`channel: dev`; existing stable clients ignore the additive field. The dev feed
starts empty. Firmware artifacts always use immutable version-tagged GitHub
release URLs. The mutable dev recommendation is updated in one Git commit with
a compare-and-swap check on the previous file SHA. Raw GitHub responses may be
cached for several minutes; clients should retain their last valid catalog during
transient failures and must not install automatically.

A GitHub prerelease is published and downloadable, but does not become the latest
stable release. Publishing a dev prerelease and recommending it are separate
steps, so there can be multiple downloadable test builds and only one currently
recommended build per board/layout/signing key. These releases are public, like
the source repo; do not put credentials or private signing material in assets.

## 1. Develop a feature

```sh
git switch dev
git pull --ff-only
git switch -c feature/my-change
# Make and test changes, then commit and push this feature branch.
git push -u origin feature/my-change
# Open a PR targeting dev. Review and merge after checks pass.
```

Use small feature branches when work should be reviewed independently. A direct
commit to `dev` is also possible. The CI workflow has read-only repository access
and runs the eight native production-C sanitizer suites and Python release-tool
tests. It never signs, publishes, or changes channel recommendations. It is not a
full ESP-IDF build or physical-device qualification.

No GitHub branch-protection rules are installed by this setup. Review/PR checks
are the working convention; maintainers can add repository rules later if desired.

## 2. Select a development snapshot

```sh
git switch dev
git pull --ff-only
python3 tools/release.py version --channel dev 0.3.7-dev.1
git diff
git add firmware/CMakeLists.txt firmware/main/app_version.h
git commit -m 'Prepare 0.3.7-dev.1'
git push origin dev
```

If that version is already set and committed, skip the version/commit steps.
Every distributed build gets a unique version: `0.3.7-dev.1`, `0.3.7-dev.2`, etc.
The tool updates both firmware version declarations together. It refuses the
wrong branch, a dirty checkout, unnumbered dev versions, and a dev version for
stable publication. Do not move a published tag or replace its binary assets.

## 3. Build and prepare a signed package

Load ESP-IDF 6.0.2 and authenticate the GitHub CLI with the release operator's
normal account. Write release notes in a file outside the checkout, using at most
4,000 characters. Include changes, compatibility, migration/downgrade caveats,
and the scope of the qualification you will perform.

```sh
source /path/to/esp-idf/export.sh
python tools/release.py prepare --channel dev \
  --key /private/path/ember-link-production.pem \
  --out /tmp/link-0.3.7-dev.1 \
  --notes-file /tmp/link-0.3.7-dev.1-notes.md
```

Use the established production key for both channels. This preserves the update
path for production-key devices. The key remains outside Git and GitHub Actions.
The tool uploads only the allowlisted public release files below. Keys, build
logs, SDK configuration, ELF files, and device data are excluded.

`prepare` makes a fresh ESP-IDF build from the current clean checkout with the
committed defaults and the supplied signing-key path. It rejects an existing
output directory or an output path inside the repository. It verifies the final
application signature with the committed production public key and checks the
image version against the source version. Source changes during the build stop
packaging. The package records the full source commit and branch.

New packages publish these ten files:

- `ember-link.bin`: signed application image.
- `manifest.json`: verified image compatibility, hash, size, and tagged URL.
- `link-releases.json`: channel-specific recommendation.
- `provenance.json`: source commit, branch, version, channel, and image hash.
- `release-notes.md`: the prepared release notes.
- `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`: first-install components.
- `factory.json`: validated board, offsets, file hashes, and source/version binding.
- `SHA256SUMS`: checksums for the preceding nine files.

Legacy schema-1 releases retain their original six assets. New schema-2 packages
require browser first-install qualification as well as update qualification;
see [browser installer](browser-installer.md) for packaging and Pages deployment.

Keep the full local package for qualification and later recommendation changes.
An ordinary build or successful CI check does not approve installation or release.

## 4. Create a draft and qualify the exact image

```sh
python tools/release.py draft /tmp/link-0.3.7-dev.1
```

The tool requires the source commit to exist on the corresponding remote branch,
checks the production signature again, and creates a draft targeting that exact
commit. A dev draft is marked prerelease; a stable draft is not. Neither changes
an update feed. The tool refuses an existing remote version tag and never replaces
existing release assets.

Install the package's exact signed image on a test unit. Record its SHA-256 and
results: healthy boot, settings/identity/file preservation, changed features,
power-cycle persistence, and machine preview. Add interruption, rollback, migration,
and conflict tests appropriate to the changes. See the hardware validation and
prior release qualification documents. Native fault injection complements physical
tests but does not replace them.

If fixes are needed after distributing a build, create a new dev version. If a
command fails, inspect the remote release before retrying; do not delete or
force-move a published tag to make a retry work. Failed private draft preparation
can be reviewed and corrected before publication.

## 5. Publish the qualified dev build, then recommend it

```sh
python tools/release.py publish /tmp/link-0.3.7-dev.1 --qualified --factory-qualified
python tools/release.py recommend-dev /tmp/link-0.3.7-dev.1
```

`--factory-qualified` additionally asserts fresh-board browser installation passed.
`--qualified` is the operator's assertion that the exact image passed its recorded
checks. The tool verifies signature, package checksums, source ancestry, draft
channel/target, and downloaded draft assets before publishing. It then verifies
the published tag points to the recorded commit. Dev publication explicitly uses
prerelease=true and latest=false. Successful publication dispatches the Browser
installer workflow on `main`; a deployment-start failure reports a retry instruction
without repeating or undoing publication.

`recommend-dev` separately checks the published release and downloaded assets,
then updates only `release-channels/dev.json`. It never writes `main`, a stable
release, or the stable catalog. The catalog currently supports one production
board/layout/key combination; adding another requires extending this packaging
flow to preserve all supported recommendations together.

Verify the dev URL returns the expected version/hash and the stable URL still
returns the previous stable recommendation. The reference implementation must
add opt-in filtering before users can consume the dev feed through its UI.

## 6. Promote ready code to stable

Review `dev` and make sure everything being merged is ready for production. Do not
merge an integration branch containing features that should remain experimental.
Use a release branch or select the approved changes if necessary.

```sh
git switch main
git pull --ff-only
git merge --no-ff dev
python3 tools/release.py version --channel stable 0.3.7
git add firmware/CMakeLists.txt firmware/main/app_version.h
git commit -m 'Prepare stable 0.3.7'
git push origin main
```

A PR from `dev` into `main` can replace the merge command. Review any conflicts,
especially version files. Then prepare a fresh stable package, create its draft,
and qualify its exact binary:

```sh
python tools/release.py prepare --channel stable \
  --key /private/path/ember-link-production.pem \
  --out /tmp/link-0.3.7 \
  --notes-file /tmp/link-0.3.7-notes.md
python tools/release.py draft /tmp/link-0.3.7
# Install and qualify this exact stable binary; record the evidence.
python tools/release.py publish /tmp/link-0.3.7 --qualified --factory-qualified
```

This publishes `v0.3.7` with prerelease=false and latest=true, updating the existing
stable feed. It does not relabel or replace `v0.3.7-dev.N`: the embedded version and
binary hash change, so stable needs its own build and final checks. Verify anonymous
catalog/image downloads match the local package. Production AWS catalog/object
publication remains a separate operator deployment, followed by parity checks.

## 7. Start the next cycle

```sh
git switch dev
git merge main
python3 tools/release.py version --channel dev 0.3.8-dev.1
git add firmware/CMakeLists.txt firmware/main/app_version.h
git commit -m 'Start 0.3.8 development'
git push origin dev
```

Resolve version conflicts deliberately. Stable hotfixes start from `main`, get a
new stable patch release, and are merged back into `dev` so they are not lost.
Do not use a lower stable version merely because `dev` is farther ahead.

## Withdrawal and return to stable

```sh
python tools/release.py withdraw-dev
```

This clears the dev recommendation atomically while preserving prior releases and
feed history. Recommending a previously qualified dev package uses `recommend-dev`
again; it does not rebuild or replace that version's image. Withdrawal prevents
new recommendations after caches refresh; it cannot cancel a previously approved
in-progress update or uninstall firmware already running on a device.

Returning a dongle to Stable requires an explicit install through a
channel-aware updater. Selecting a channel must never silently downgrade. Check
settings-journal compatibility before offering an older stable image. For example,
0.3.5 reads pre-upgrade legacy display preferences rather than the 0.3.6 journal.

## Consumer integration

The Bridge and standalone cloud example implementations support independent
per-device Stable/Development preferences. Bridge stores its choice by hardware
serial; the example stores an owner-authorized cloud preference. Both default to
Stable, require Development opt-in, and retain signature/board/layout checks.
Their install flows distinguish return-to-stable and require explicit replacement
approval. Selecting a channel alone does not change installed firmware.

See Bridge's `docs/consumer-updates.md` and the private
`ember-link-cloud-example/docs/release-channels.md` for implementation and rollout
limits. The example imports verified packages manually; it does not automatically
mirror GitHub recommendations or withdrawals. Production cloud integration and
consumer release qualification remain separate steps. This does not introduce a
Development update channel for the Bridge desktop application itself.

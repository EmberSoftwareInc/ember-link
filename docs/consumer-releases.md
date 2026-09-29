# Publishing firmware for Bridge and the Ember web app

For new releases, follow the [stable/dev operator lifecycle](release-lifecycle.md).
It builds clean signed packages, records provenance, and separates dev recommendations
from the existing stable feed. The lower-level commands below describe the artifact
format and production AWS handoff; they are not automatic deployment.

Link 0.3.5 (introduced in 0.3.5-dev) reports the same public board/layout/signing-key compatibility
metadata over USB `info` and authenticated `GET /api/update` that cloud polls
already report. No cloud enrollment is needed for local discovery or installation.
Local compatibility reporting includes no cloud receipts or credentials. A Bridge
running the new updater can install over local Wi-Fi or USB and confirm the serial,
requested version, changed slot, and healthy boot. Earlier firmware needs a signed
USB bootstrap once to expose these local fields; existing cloud OTA still works.

## Prepare once, distribute to both

Use the established production signing key for production hardware. Keep it private.
Build and qualify the image on hardware, including rollback and interrupted-power
scenarios. Then, in the ESP-IDF Python environment:

```sh
python tools/firmware_manifest.py firmware/build/ember-link.bin \
  --public-key /path/to/public.pem --release-id link-035 \
  --notes 'Release notes for users.' \
  --download-url https://github.com/EmberSoftwareInc/ember-link/releases/download/v0.3.5/ember-link.bin \
  --output /tmp/link-035.json
python tools/release_catalog.py --manifest /tmp/link-035.json \
  --output /tmp/link-releases.json
```

These URLs identify the published 0.3.5 release. For a new release, change the
version, release ID and tag together. Each distinct
artifact needs a unique version and release ID. The manifest tool cryptographically
verifies the image using the public key. The catalog tool validates and combines
manifests; it does not sign or publish. Repeat `--manifest` for other signing-key
channels. There must be one recommendation per board/layout/key. Include all
supported channels in each public catalog. An empty catalog withdraws all public
recommendations. The catalog may point to an older tagged release for a deliberate
rollback; it is an approved recommendation, not “largest version wins.”

1. Prepare a tagged GitHub release with signed `.bin`, verified manifest, release
   notes, and `link-releases.json`. The artifact URLs must use immutable tag paths,
   never `latest`. Do not replace binary assets in place.
2. Validate the exact same artifacts in ember-app without AWS writes:

```sh
node backend/ember-link/publish-firmware.mjs \
  --catalog /tmp/link-releases.json --manifest /tmp/link-035.json \
  --image /path/to/ember-link.bin --dry-run
```

3. Publish the cloud copy using operator IAM and the deployed stack's table/bucket:

```sh
node backend/ember-link/publish-firmware.mjs \
  --catalog /tmp/link-releases.json --manifest /tmp/link-035.json \
  --image /path/to/ember-link.bin --table LINK_TABLE --bucket LINK_BUCKET \
  --region AWS_REGION --approve-release
```

4. Publish the matching GitHub release as the latest stable release and verify
   `releases/latest/download/link-releases.json`, each image, and the web app's
   recommendation agree. GitHub prereleases do not serve as the stable latest
   release. GitHub and AWS publication are separate steps: coordinate them and
   verify parity; the tools do not provide a cross-service atomic commit.

Bridge's catalog is public and requires no Ember account. The cloud copy stays in
private, versioned S3. Neither catalog publication nor device polling grants consent
for installation. The device independently verifies the signed image in every path.
Public GitHub publication for firmware is currently an operator step; no credentials
or production signing keys were added to CI.

## Qualification status

Version 0.3.6 is the latest stable release and adds cloud screen/LED/orientation
settings. Its exact signed image passed healthy-boot confirmation, settings and
design preservation, cloud changes with USB readback, power-cycle persistence,
and a Brother NQ1700E design preview. See [0.3.6 qualification](release-qualification-0.3.6.md)
for artifacts and test limits. Cloud controls require the corresponding backend
extension; production AWS publication remains a separate operator step.

### Previous 0.3.5 qualification

See [release qualification](release-qualification-2026-09-28.md) for measured
hardware results. Production-signed 0.3.5 is published as stable and passed
cloud HTTPS installation, guided Bridge Wi-Fi/USB installation, healthy-boot
confirmation, physical recovery checks, and Brother design preview. Cloud recovery
tests used an isolated local service, not production AWS. Public latest-release
endpoints were verified against the exact qualified artifacts.

See [production signing](production-signing.md) for the public key and the
verified SDK limitation on key rotation. Development-key prototypes require USB
recovery to establish production trust; ordinary production updates use the
same production key. Private/draft release assets are not publicly available.

# Production firmware signing

The first production key was established before any customer dongles shipped.
Its public verification key is [ember-link-production.pub](signing/ember-link-production.pub).
The ESP Secure Boot V2 public-key digest (catalog `signingKeyId`) is:

`6d0a5aa1c94e21afb66d4985446c3446955c9593345ddf1ae161fb66a1c79455`

Private signing material is kept outside this repository. The operator has a
separate private recovery copy. Neither private keys nor prototype transition
images belong in Git, public release assets, or CI logs. Before shipping customer
units, retain an access-controlled off-machine backup and document its custodian.

## Build and verify

Configure `CONFIG_SECURE_BOOT_SIGNING_KEY` in the local ignored SDK configuration
to the absolute path of the production RSA-3072 private key, then build with
ESP-IDF. Do not replace the development default with a machine-specific path.
Verify the resulting signed application with the committed public key:

```sh
python tools/firmware_manifest.py firmware/build/ember-link.bin \
  --public-key docs/signing/ember-link-production.pub \
  --release-id link-035 \
  --download-url https://github.com/EmberSoftwareInc/ember-link/releases/download/v0.3.5/ember-link.bin \
  --output /tmp/link-035.json
```

A successful build alone is insufficient: verify the manifest's signing key,
version, SHA-256 and size against the exact qualified binary. Preserve immutable
versioned release assets. The public key cannot sign updates.

## Existing prototypes and future rotation

Development-key prototypes need a physical USB recovery flash to establish the
production key. In ESP-IDF 6.0.2 with
`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, update verification considers
only the first signature block and first trusted digest. A dual-signed image
therefore does **not** provide an OTA key-rotation path in this configuration.
The prototype correctly rejected a transition whose first signature used the
new key. Do not advertise additional signature blocks as supported trust channels.

Verify that the final running image reports only the production digest, accepts
production-signed updates, and rejects old development-key images. Keep private
prototype transition and diagnostic images out of public releases.

Customer updates must continue using this production key. A future OTA key
rotation mechanism requires a separately implemented and qualified migration;
do not assume adding a second signature is sufficient. Maintain an off-machine
private-key backup so ordinary updates remain possible. Physical USB recovery
is the fallback if the key is lost.

This mechanism authenticates firmware updates; it does not enable eFuse secure
boot or prevent a device owner from reflashing through physical USB recovery.

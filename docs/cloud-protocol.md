# Ember Link firmware cloud protocol v1

Implemented cloud-transfer and USB account-setup contract. The matching backend,
enrollment and account website are implemented in the separate `ember-app` repo.
Firmware supports **download jobs only**.

## Configuration and trust

NVS namespace: `link_cloud`. Configuration and receipts are separate schema-v1
blobs. NVS is not encrypted in this build.

Physical USB `cloud_configure` accepts:

```json
{
  "id": 1,
  "cmd": "cloud_configure",
  "apiBaseUrl": "https://api.example.com/",
  "downloadHost": "files.example.com",
  "deviceId": "device-example",
  "token": "<64 lowercase hexadecimal characters>"
}
```

Configuration disables cloud until `{"cmd":"cloud_enable","enabled":true}`.
`cloud_status` exposes enabled/configured state, device ID, and last receipt,
never credentials or signed URLs. USB `info` also includes protocol versions and
cloud status. An active session may return `{"protocolVersion":1,"busy":true}`.

This is a factory/developer provisioning surface, not account claiming. Normal
browser setup must not receive the production device credential. There is no
LAN cloud-configuration route. Unsettled receipts block reconfiguration.

API origins must be HTTPS with a trailing slash, no path prefix, userinfo,
explicit port, query, or fragment, and at most 240 bytes. Device/job/attempt IDs
allow ASCII letters, digits, hyphens, and underscores, up to 63 bytes. Tokens
are exactly 64 lowercase hex characters (32 random bytes generated externally).

TLS uses ESP-IDF's certificate bundle and hostname verification, with cross-signed
certificate verification enabled. Cloud waits
for a station IP and a clock at least as recent as January 2025; SNTP uses
`pool.ntp.org`. Redirects are disabled. Download hosts must match the configured
hostname exactly, case-insensitively, using HTTPS port 443. Device Authorization
is never attached to downloads.

## USB mode selection

Firmware `0.3.1-dev` starts with storage-only USB. On a computer, two quick BOOT
presses after startup request a reboot into MSC+CDC setup mode, deferred until
any guarded transfer/update finishes. Power-cycling returns to storage-only.
`info` adds `usbMode:"setup"`; LAN health/info report either `setup` or `storage`.
The JSON setup/account protocol remains version 1. See [USB modes](usb-modes.md).

## Consumer setup over USB (setup protocol v1)

Firmware `0.2.0-dev` adds `setupProtocolVersion:1` to USB `info`. Browser setup
lives in the Ember account website (`ember-app/next_frontend`, `/connect`).
Wi-Fi uses the existing `scan` and `provision` commands; no Bridge is required.
Factory enrollment and `cloud_configure` must precede consumer setup.

After creating an authenticated account setup session, the browser sends:

```json
{"id":2,"cmd":"cloud_claim","sessionId":"random-session-id","secret":"<64 lowercase hex characters>"}
```

The secret is a random, short-lived proof, **not** the device credential. The
account API receives SHA-256 of the UTF-8 secret string. Firmware accepts setup
only when configured and not transferring or holding an unsettled receipt. It
persists cloud enablement, holds the proof only in RAM, and wakes the worker.
Each authenticated device poll includes:

```json
{"setup":{"sessionId":"random-session-id","secret":"<ephemeral secret>"},"readyForJob":false}
```

The backend atomically verifies the proof, identity, ownership and expiry before
claiming. Poll response adds `setupResult` with the same `sessionId` and a
`status` of `linked`, `blocked`, `expired`, or `invalid`. A matching terminal
result clears the proof; a result for another session does not. USB status
exposes only `setupSessionId` and `setupState`. The backend's owner-private
session status is the browser's source of confirmation, not the USB command ack.

Proofs expire after ten minutes locally and on the server. Repeating the same
USB request does not extend that deadline. Reset, cloud disable, reconfiguration,
and authorization failure clear the proof. Setup cannot transfer a device from
another account; factory reset retains the server-side owner. There is no change
to the cloud job/receipt protocol version or NVS configuration schema.

## Poll request

`POST {apiBaseUrl}v1/device/poll`

Headers: `Authorization: Bearer <device token>`, `Content-Type: application/json`.
Resolve identity from the credential and check that the body device ID matches.

```json
{
  "protocolVersion": 1,
  "deviceId": "device-example",
  "firmwareVersion": "0.1.0-dev",
  "readyForJob": true,
  "storage": {"totalBytes": 16000000000, "freeBytes": 15000000000}
}
```

Subsequent polls include `receipt` when one exists, even after acknowledgement.
`readyForJob` means no unsettled cloud receipt. Another local operation can
still prevent acquiring the shared operation gate when a job is offered.

The response must be HTTP 200 with a JSON object of at most 8 KiB:

```json
{
  "protocolVersion": 1,
  "claimed": true,
  "ownershipGeneration": 2,
  "nextPollSeconds": 10,
  "job": {
    "type": "download",
    "jobId": "job-123",
    "attemptId": "attempt-1",
    "ownershipGeneration": 2,
    "filename": "rose.pes",
    "size": 12345,
    "sha256": "<64 lowercase hexadecimal characters>",
    "expiresAt": 2000000300,
    "downloadUrl": "https://files.example.com/object?signature=..."
  }
}
```

Omit `job` when none is ready. `claimed:false` prevents new downloads. The
backend must persist assignments and re-offer the same job until receiving a
receipt: a busy device or lost response may mean an offer was never executed.
Invalid jobs are rejected and surfaced through USB as `invalid_job`; validate
contracts server-side. Arbitrary command types are not supported.

The backend must authorize the job for this device/current owner. The job's
ownership generation must equal the response generation. Download URLs should
reference immutable/version-pinned objects verified during upload finalize.

## Limits and download behavior

- Root filenames only, printable ASCII, at most 127 bytes, with an extension.
  Reserved names, separators, control characters, trailing spaces/dots,
  internal/hidden filenames, and START HERE files are rejected.
- Size: 1–67,108,864 bytes. HTTP 200 and an exact Content-Length are required.
  Chunked transfer and redirects are not accepted for design downloads.
- Expiry: integer Unix seconds, future and at most one hour ahead at acceptance.
  It is checked during download and again before commit.
- Streaming uses 4 KiB chunks and SHA-256. Card free space must cover the entire
  new file plus 64 KiB; the old file remains until staging completes.
- API/network read timeouts are 15 seconds. The download loop budget is five
  minutes, plus bounded connection/cleanup overhead.

The worker handles one transfer synchronously. **No polling or live byte-progress
reports occur during download.** The backend should show an acquired transfer
as delivering and allow appropriate heartbeat grace (initially at least six
minutes), rather than declaring it offline after 30 seconds. This iteration has
no lease-renewal endpoint. Choose supported file sizes using measured throughput.

After integrity verification, firmware installs the file, refreshes storage
information, and reconnects USB. The saved receipt goes out on the next poll,
normally about five seconds later:

```json
{
  "jobId": "job-123",
  "attemptId": "attempt-1",
  "state": "done",
  "filename": "rose.pes",
  "sha256": "<expected hash>",
  "size": 12345,
  "bytesReceived": 12345,
  "ownershipGeneration": 2
}
```

Other states: `failed` (known pre-commit failure) and `needs_reconciliation`
(interrupted/potentially committed operation). `errorCode` may be
`checksum_mismatch`, `insufficient_storage`, `transfer_expired`, `size_mismatch`,
`download_failed`, `storage_failed`, `journal_error`, or `interrupted`.

A failed receipt contains the expected hash, not proof of verified bytes.
`done` means verification, storage, and USB reconnect succeeded. It does not
prove the embroidery machine opened the design or started stitching.

## Acknowledgement and recovery

Persist the receipt atomically before acknowledging it in the poll response:

```json
{
  "protocolVersion": 1,
  "claimed": true,
  "ownershipGeneration": 2,
  "receiptAck": {
    "jobId": "job-123",
    "attemptId": "attempt-1",
    "state": "done"
  }
}
```

All three fields must match. Unsettled receipts block new cloud jobs.
Acknowledgement is persisted. The immediately previous job ID is retained to
reject replays after acknowledgement; the backend must not reissue older
completed jobs. Unlimited historical deduplication belongs to the backend.

Before touching storage, firmware saves `delivering`. Booting with that state
changes it to `needs_reconciliation`/`interrupted`. Do not retry blindly or clear
an uncertain job on timeout. An ambiguous receipt requires matching acknowledgement
fields plus `"resolved":true`, meaning the backend/operator explicitly resolved
the ambiguity. This must not be added automatically to every acknowledgement.

Card transactions use `~link.upload`, `~link.commit`, and `~link.backup`. Recovery
restores the backup when the destination is absent, or retains an installed
destination. Corrupt recovery metadata fails closed, preserving possible originals.
This does not make FAT power-loss proof; damaged metadata/card corruption may
require computer recovery. Physical power-loss testing is still required.

Disable/factory reset preserves receipts and cloud identity, while disabling
cloud. NVS errors do not automatically erase identity. Disable/reset can return
busy during a transfer. There is no remote post-acquisition cancellation yet.

## Retry and backend obligations

Idle polling defaults to ten seconds plus up to one second jitter. Valid
`nextPollSeconds` is 5–300. Network/server failures back off from 5 to 300 seconds.
Verified HTTP 401/403 responses disable cloud persistently; USB access is needed
to re-enable/reconfigure. HTTP 429 uses exponential backoff; Retry-After is not
consumed in this iteration.

The backend owns authorization, revocation, one active assignment, immutable
objects, receipt persistence, and offered-but-unaccepted jobs. An already-issued
grant cannot be revoked mid-download by this polling client: unclaim blocks
future jobs, but an in-flight transfer can finish. Use short-lived grants and
reflect that boundary in the product.

No service endpoint is assumed to exist. Agree this development contract before
implementing production APIs or enrolling users.

## Optional firmware delivery extension

Protocol 1 now supports capability-gated `firmwareUpdate`, `firmwareReceipt`,
and `firmwareAck` fields. Existing design transfers remain compatible. See
[firmware updates](firmware-updates.md) for behavior and release qualification;
the companion ember-app document `docs/ember-link-firmware-updates.md` defines
the backend routes, fields and release catalogue.

## USB connection diagnostics

`cloud_status` and USB `info.cloud` include a `network` object describing the
last API request. `stage` is `idle`, `init`, `connect`, `write`, `headers`, `read`,
`parse`, or `complete`. Numeric fields are `httpStatus`, `error` (the connection
open result), `socketErrno`, `transportError`, `tlsError`, and `tlsFlags`. Errors
are reset before the next attempt; `complete` means a JSON object was received,
not that an account claim or file transfer completed. No URL, header, credential
or response body is exposed. During an active session, the existing `busy`
response still applies. See the [physical test results](hardware-cloud-test-2026-09-28.md).

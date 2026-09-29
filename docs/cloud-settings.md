# Cloud screen and LED settings — protocol extension v1

Implemented in development firmware `0.3.6` on
`feature/cloud-based-settings-updates`. This is a firmware extension to the
existing authenticated HTTPS poll, not a new endpoint or transport. Production
backend and browser/example changes are separate work; none are made by this
branch. Release `0.3.5` does not accept cloud settings commands.

## Supported settings and direct setup

Only screen `enabled`, `rotation` (0 or 180), and `ledEnabled` are supported.
No remote Wi-Fi changes, credential changes, reset, or firmware installation are
included in this command. The existing USB `set_display` command still works,
including the optional LED field for older clients. USB requires no cloud claim.

USB info, successful USB saves, and cloud polls now report:

```json
{
  "settingsProtocolVersion": 1,
  "settingsWritable": true,
  "display": {"enabled": true, "rotation": 0, "ledEnabled": true, "revision": 12}
}
```

Both paths write the same persisted preferences. Each accepted USB save advances
`revision`, even when values are unchanged or reset to defaults. Each applied
cloud change also advances it. Local USB remains usable with cloud disabled or
with a cloud receipt awaiting acknowledgement. A short shared operation gate
serializes settings writes with LAN/cloud transfers, OTA, and USB operations.
No SD card access, USB media refresh, or device restart is needed for settings.
The display worker picks up the saved settings on its next refresh; LED state is
updated immediately. A receipt confirms saved software state, not visual proof
that a physical screen/LED is functioning.

## Backend offer

Poll requests also contain `readyForSettings`. Offer only when it is true and
`settingsProtocolVersion` is exactly 1. Missing capabilities mean unsupported;
do not infer support from the device name or a version-string comparison.

After checking the requesting account owns the device, queue an immutable command
with a unique `commandId` and the last device-reported revision. The response to
`POST v1/device/poll` may include:

```json
{
  "protocolVersion": 1,
  "claimed": true,
  "ownershipGeneration": 7,
  "nextPollSeconds": 5,
  "settingsUpdate": {
    "commandId": "settings-unique-uuid",
    "ownershipGeneration": 7,
    "expectedRevision": 12,
    "expiresAt": 1800000060,
    "display": {"enabled": false, "rotation": 180, "ledEnabled": false}
  }
}
```

The expiry is an illustrative Unix timestamp; generate it from the current time.
It must be strictly in the future and at most one hour ahead. IDs follow existing
Link ID rules (1–63 ASCII letters/digits, hyphen, underscore). Ownership generation
must match the enclosing claimed poll response. `expectedRevision` must be an
integer from 0 through 9007199254740990; revisions never wrap. Both booleans and
rotation are required for cloud changes. Unknown or duplicate properties in
`settingsUpdate` or its `display` object are rejected.

Use a separate owner-authorized browser API, request idempotency keys, and an
atomic per-device work reservation. Keep the device credential out of the browser.
Queue at most one settings command per device; do not offer it alongside a file
job or firmware update. Defensive firmware precedence is firmware update,
settings, then file. Unacknowledged receipts or account-claim setup block new
cloud work. A busy local operation defers execution with no receipt or write;
the backend may re-offer the same immutable command after checking readiness.

## Durable outcome and acknowledgement

The next poll repeats the current display snapshot and this separate receipt:

```json
{
  "settingsReceipt": {
    "commandId": "settings-unique-uuid",
    "ownershipGeneration": 7,
    "expectedRevision": 12,
    "revision": 13,
    "status": "applied",
    "display": {"enabled": false, "rotation": 180, "ledEnabled": false, "revision": 13}
  }
}
```

If USB changed revision 12 to 13 while this request was queued or while the HTTPS
poll was in flight, it instead reports `status:"conflict"`, revision 13 and the
actual settings at the conflict. **A conflict never applies the queued values.**
The browser should show the new reported values and require a fresh user decision;
do not automatically rebase or retry the desired settings with a newer revision.

Store the outcome durably before returning:

```json
{
  "settingsAck": {
    "commandId": "settings-unique-uuid",
    "ownershipGeneration": 7,
    "expectedRevision": 12,
    "revision": 13,
    "status": "applied"
  }
}
```

All five fields must exactly match the receipt, including `conflict` when that is
the outcome. A mismatched or unpersisted acknowledgement does not unblock work.
The device retains the last receipt after acknowledgement; backend handling must
be idempotent. Duplicate command ID + generation does not write preferences again.
Never reuse an ID with different contents. Older successful commands cannot be
reapplied after a later change because their expected revision is now stale.

A USB change after cloud application does not modify the historical cloud
receipt. For example, the receipt can say applied at revision 13 while the poll's
current `display` says revision 14. Show revision 14 as current state; the receipt
only proves what happened to its specific command. Both snapshots are captured
together for each poll. Record receipt ownership separately from current device
ownership: settle old work on unlink/reassignment without exposing it to another
account. Device reconfiguration remains blocked while any receipt is unsettled.

## Failure and offline behavior

- Offline devices wait for their next successful poll. The browser must distinguish
  requested values from the last reported values and show pending/offline state.
- A lost response or reboot after the settings commit preserves settings, revision,
  and receipt together. Poll again and acknowledge; do not blindly create a new ID.
- A future (never-observed) revision, invalid/expired payload, or ownership mismatch
  causes no write or receipt. Malformed command status is `invalid_settings`.
  Expired unoffered work may be cancelled by the backend; an already offered command
  without a receipt is **unknown**, not proved failed. Preserve its reservation and
  reconcile before sending more work. A missing receipt is not evidence of success.
- An NVS write/commit error returns no applied receipt. Because commit failure may
  be ambiguous, `settingsWritable` and `readyForSettings` become false until reboot
  reloads the authoritative journal. New cloud work and identity reconfiguration
  also pause during this uncertainty. Do not treat the old in-memory snapshot as
  proof that an offered command was not saved. An NVS open error before writing is
  safe to retry. No Wi-Fi, device credential, file, or firmware receipt is erased.
- A pending receipt must be processed even when readiness is false. It can be
  acknowledged and a subsequent command offered in the same poll response, but
  prefer a fresh readiness report before scheduling more work.
- Existing TLS, bearer authentication, backoff, and 401/403 cloud-disable behavior
  apply unchanged. No additional connection or faster idle polling is introduced.

## Persistence and rollback boundary

`linkdisplay/state_v1` is one 112-byte record with explicit little-endian integer
encoding, settings, revision, and the last receipt. An atomic NVS blob update is
the commit point. The cache and hardware are updated only after NVS success.
Unknown/truncated/invalid journals fail initialization rather than resetting the
revision and making old requests eligible. Revision exhaustion refuses writes.
Factory reset saves defaults as another local revision and keeps receipt history;
a full flash/NVS erase requires retiring the old cloud identity and pending work.

On first use, existing `linkdisplay/prefs` v1/v2 choices are imported at revision
zero. The legacy blob is left untouched, as are the frozen cloud/OTA blobs. An
older firmware rollback therefore sees its **pre-upgrade preferences**, not the
new cloud-adjusted ones. Returning to this firmware resumes the new journal;
settings changed while running old firmware are not merged. Backends must stop
settings offers whenever capability reporting disappears. The feature was tested on a physical prototype, including cloud writes and
independent USB readback. Physical downgrade/rollback of the new settings journal
has not been qualified.

## Validation

`python3 tests/run_native.py` runs production C under ASan/UBSan. Settings tests
cover legacy import, reboot persistence, applied/conflict receipts, lost responses,
USB writes before and during a cloud poll, stale/duplicate commands, mismatched
acks, busy transfers/updates, malformed payloads, wrong ownership, expired commands,
integer limits, NVS write/commit failures (including an ambiguous committed write),
and corrupt journals. The cloud-worker tests use the actual poll parser and
settings journal, with platform I/O substituted.

Before release, exercise a real device through USB and cloud with both indicators,
reboot persistence, a local edit racing a queued command, and power cuts around
the journal write/receipt acknowledgement. Verify the Brother stays responsive.
A host test or successful ESP-IDF build does not replace these checks.

Local verification: all eight native sanitizer suites and both Python release
catalog tests passed. The production-signed `0.3.6-dev` prototype build passed
cloud screen/LED/orientation writes and independent USB readback, then restored
the original settings with a second confirmed cloud request. The reference backend
and UI passed 42 Python and 27 Node tests, including browser conflict handling.
These tests used a disposable local backend and HTTPS tunnel; production AWS was
not deployed. Stable-binary qualification and hashes are recorded in the 0.3.6
release notes.

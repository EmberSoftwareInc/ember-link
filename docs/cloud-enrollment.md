# Self-service cloud enrollment (enrollment protocol v1)

Firmware support is implemented on `feature/self-service-cloud-enrollment`, based
on `0.3.8-dev.1`. The production account API and browser flow still need the
implementation described here. This is an additive capability, not a change to
cloud poll protocol 1 or account-claim protocol 1. No enrollment endpoint is
embedded in the firmware image.

## Consumer flow

1. Sign in at `/link/connect`, connect Link over USB, and configure or reuse Wi-Fi.
2. Read USB `info`. If `cloud.configured` is true, keep using the existing
   `cloud_claim` flow. Never ask an already configured device to enroll again.
3. For an unconfigured device advertising `enrollmentProtocolVersion:1`, request
   an enrollment ticket from the authenticated account backend.
4. Pass that ticket and the service endpoints to `cloud_enroll` over USB. A USB
   success response means the request was saved, not that enrollment finished.
5. Link generates a private 256-bit credential, saves it locally, and sends it
   directly to the bootstrap API over verified HTTPS. The backend atomically
   enrolls the device and associates it with the ticket's account.
6. The browser watches its account-private enrollment status. Confirm the device
   has polled successfully before saying it is online and ready for delivery.

The user never handles a permanent token. Enrollment and local setup are free;
the backend continues to check Pro entitlement when accepting cloud design jobs.
Prebuilt devices with factory identities continue to use account claiming.

## USB contract (implemented)

Top-level `info.enrollmentProtocolVersion` is `1`, even if the cloud worker is
busy. Wi-Fi must be configured first. The browser obtains these parameters from
its authenticated backend, not from user-entered credential fields:

```json
{
  "id": 7,
  "cmd": "cloud_enroll",
  "apiBaseUrl": "https://api.example.com/",
  "downloadHost": "files.example.com",
  "sessionId": "enrollment-session-id",
  "ticket": "<64 lowercase hexadecimal characters>"
}
```

API origin and download host use the existing strict validators: HTTPS origin
with trailing slash, no path prefix, port, query, credentials or fragment;
download host is an exact hostname. `sessionId` is 1–63 ASCII alphanumeric,
underscore or hyphen characters. The ticket is 32 random bytes encoded as
64 lowercase hex characters.

The firmware derives `deviceId` from its hardware Wi-Fi MAC using the USB
serial's existing convention: `50787D2C5A1C` becomes `link-50787d2c5a1c`.
It ignores any device ID supplied by the browser. This is an identifier, not
cryptographic proof that the device is genuine or owned by the user.

The command fails for a configured device, unsettled receipts, unavailable
settings journal, active operation, cloud-session contention, or storage fault.
No LAN enrollment route exists. USB enrollment cannot overwrite a provisioned
identity, even if cloud is disabled or its credential was rejected.

USB command failures have specific `error.code` values; these are separate from
the asynchronous enrollment states reported after an accepted command:

| Error code | Frontend action |
|---|---|
| `invalid_enrollment` | Refresh/check the ticket and request parameters. Never reuse a session ID with a different ticket. |
| `busy` | Keep showing progress and retry with a short delay; an operation or receipt is outstanding. |
| `already_configured` | Read device status and use the existing account-linking flow. |
| `service_mismatch` | Resume the pending enrollment with its original service. Do not erase credentials or silently switch endpoints. |
| `storage_error` | Ask the user to restart Link before retrying. Settings may have reached flash despite a reported failure. |

Messages contain no ticket, credential or service URL. Do not parse message text
to determine frontend behavior; use `error.code`.

The asynchronous worker waits for Wi-Fi and a usable clock. With Wi-Fi active,
it generates a new credential using the ESP32 hardware RNG. It commits the
credential to NVS **before** its first network request. Existing identities and
other transfer/update/settings receipts are not repurposed for enrollment.

`cloud_status` and `info.cloud` add:

```json
{
  "enrollmentProtocolVersion": 1,
  "enrollmentState": "pending",
  "enrollmentPending": true,
  "enrollmentSessionId": "enrollment-session-id",
  "enrollmentDeviceId": "link-50787d2c5a1c"
}
```

Neither tickets nor credentials nor endpoint URLs appear in status. During a
network operation, status returns `busy:true,cached:true` with the most recent
redacted snapshot, including configured/enabled state, enrollment and account
setup state/IDs, general cloud state and numeric network diagnostics. The
worker publishes a snapshot before each blocking HTTPS phase, so the browser
can still show `enrollmentState:"registering"`, `state:"enrolling"` and the
current `network.stage` while waiting.

`snapshotAgeMs` measures time since this snapshot was captured, not time since
the last successful connection. A fresh read returns `busy:false,cached:false`.
Cached responses omit transfer receipts and firmware-update details; their
absence must not clear previously displayed data. Poll again for fresh details.
Cached state is progress information, not authorization or proof of completion;
the account backend remains the authority. Do not issue replacement tickets
merely because the worker is busy.

| Enrollment state | Meaning / UI behavior |
|---|---|
| `idle` | No current enrollment; inspect `configured` before starting. |
| `pending`, `registering` | Working; general cloud `state` identifies Wi-Fi/clock waits. |
| `connection_error`, `rate_limited`, `invalid_response` | Automatic retries with backoff; show progress/retry status. |
| `linked` | Valid acknowledgement and local configuration saved; check backend ownership and online status. |
| `expired` | Backend returned 410; obtain a fresh ticket. |
| `blocked` | Backend returned 409; explain conflict without revealing another account. |
| `rejected` | Backend returned 400/401/403/404; fix ticket/service configuration. |
| `retry_required` | Local retry window ended, or a paused attempt was reloaded on boot. |
| `paused` | USB cloud-disable/reset paused enrollment without forgetting its identity. |
| `storage_error` | Stop; reboot to reload the authoritative journal before retrying. |

USB state strings are diagnostic, not an account authorization decision. On a
normal later reboot, a completed enrollment reports `configured:true` and may
report enrollment `idle`; the permanent configuration is the durable result.

## Device bootstrap endpoint (backend implementation required)

`POST {apiBaseUrl}v1/device/enroll`, JSON body, **no Authorization header**:

```json
{
  "protocolVersion": 1,
  "sessionId": "enrollment-session-id",
  "ticket": "<temporary ticket>",
  "deviceId": "link-50787d2c5a1c",
  "deviceToken": "<private 64-character device-generated token>",
  "firmwareVersion": "0.3.8-dev.1"
}
```

This endpoint is unauthenticated by device bearer token but authorized by the
single-use ticket. It must not be routed through the existing device-token
middleware that assumes an enrolled record already exists. Never log request
bodies: both ticket and deviceToken are secrets. Store only hashes of those
secrets using the existing token hashing convention, not raw tokens.

Return **HTTP 200**, bounded JSON (firmware limit 8 KiB):

```json
{
  "protocolVersion": 1,
  "sessionId": "enrollment-session-id",
  "deviceId": "link-50787d2c5a1c",
  "downloadHost": "files.example.com",
  "claimed": true,
  "ownershipGeneration": 1
}
```

The firmware requires the exact session, device ID and download host, a boolean
`claimed:true`, protocol version 1, and a positive safe-integer generation. It
will not follow redirects, accept another API origin from the response, trust
unverified TLS, or accept a partial response. Do not use HTTP 201 or 204.

The backend must implement these rules in conditional/transactional writes:

- An authenticated account creates a short-lived ticket (recommended ten
  minutes). Bind it to owner, expected device ID, approved service endpoints and
  intended name. Server-generate the session ID and ticket; only return the
  ticket once. Limit outstanding tickets and rate-limit issuance per account
  and bootstrap attempts per source/session. Public status must not disclose
  account existence, device owners, token hashes, or credentials.
- Validate schema, ticket hash, expiration and device ID. Derive ownership and
  name from the ticket record, never from the unauthenticated request.
- Atomically consume the ticket, create the unique device identity with its
  token hash, and assign the owner. A conflicting identity/owner, revoked device
  or administrative factory record must never be overwritten by this route.
- **Look up an already-consumed enrollment before applying ticket expiration.**
  An exact repeat with the original ticket, same device ID and same token hash
  returns the same acknowledgement even after the original ticket expired. This
  is replay of one operation, not reuse to create another device.
- Preserve a durable enrollment binding alongside the device; do not rely on
  the short-lived ticket TTL row for recovery. Replays must still reject revoked
  devices or ownership changes. A fresh valid ticket may resume a previous
  enrollment only for the same owner, device ID, token hash and service. This
  supports a browser that lost its previous session without minting a new key.
- The signed-in browser polls an owner-private status route for its own ticket.
  Return completion/device ID, then verify current ownership and a successful
  device poll. Never return the permanent credential to the browser.

Suggested account routes are `POST /link/enrollments` and
`GET /link/enrollments/{sessionId}`; those names are not compiled into firmware.
Use the app's authenticated API namespace, CSRF/origin controls and existing
ownership checks when implementing them. Ticket issuance does not require Pro.

HTTP 410 means expired; 409 means conflict; 400/401/403/404 pause with a rejected
state. These responses retain the local candidate credential. HTTP 429, 5xx,
transport errors and malformed responses retry with jittered 5–300 second
backoff. The local active attempt stops after ten minutes measured from request
acceptance or boot, checked when networking is ready. The backend's absolute
ticket expiry remains authoritative across device power cycles.

## Recovery and security boundaries

The `link_cloud/enrollment` schema-1 blob is independent of the existing config
and receipt schemas. Persisted stages are:

1. USB request saved with a pinned origin and an empty candidate token.
2. Candidate token generated and saved before any bootstrap request.
3. Backend atomically accepts; identical requests are safe to replay.
4. Firmware validates the response and saves enabled cloud configuration.
5. Firmware clears the enrollment journal, including the ticket, then uses the
   existing authenticated poll. If power fails between 4 and 5, boot recognizes
   the matching configuration and clears the journal without re-enrollment.

An ambiguous NVS failure stops the worker and provisioning until reboot reloads
flash. The firmware never assumes a failed commit means nothing was written.
Conflicting persisted identities fail closed. Power loss before an acknowledgement
replays the same credential, not a newly generated one.

While pending, duplicate USB requests for the same session/ticket are idempotent
and do not extend its active timeout. An explicit retry of a paused attempt or
a fresh ticket can resume it. Origin and download host cannot change while a
candidate exists. Even after an expired/rejected response, its token is retained
because an earlier server acknowledgement may have been lost. There is no
automatic discard/rekey action. Service migrations or abandoning an uncertain
enrollment require administrative reconciliation before erasing configuration.

USB `cloud_enable:false` and local factory reset pause enrollment, retaining
its journal. The USB reset response explicitly says local settings were reset
and cloud disabled, while cloud identity, account association and SD-card files
are retained; account removal is a separate backend action. Enabling alone does not resume it; `cloud_enroll` does. Existing
factory provisioning remains available when there is no pending enrollment.
Finish or reconcile enrollment before downgrading to firmware without this
capability: older firmware does not understand the pending journal.

The browser is trusted to obtain the correct endpoints for a **fresh** device.
Physical USB access authorizes starting enrollment, not replacing an existing
account. A compromised setup site can choose a malicious service for a blank
device; Web Serial permission is not service attestation. Pinning the origin
prevents that service selection from redirecting an existing candidate token.
NVS remains unencrypted, as in previous firmware; this feature does not claim
protection against physical flash extraction or cloned hardware identities.

## Known limitation: enrollment after a full flash erase

Self-service recovery after a full browser/factory flash erase is deferred as of
2026-10-06. Account removal intentionally preserves the device credential in the
backend, and normal USB re-linking works while the device retains that credential.
A full flash erase generates a new credential; the existing backend identity then
rejects fresh enrollment with `409 enrollment_conflict`, including for the same
account after removal. The web page may report that the pending connection needs
attention. Repeating the installation does not fix the retained backend identity.

This differs from the firmware's local-settings reset, which retains cloud
identity. Prefer signed firmware updates for an already configured device.

Until account-authorized recovery is implemented, previously enrolled devices
that lose their identity require support intervention. During 0.3.8 qualification,
the operator explicitly approved a one-time reset of the known spare's released
identity record. It was privately backed up and checked for no owner, no active
work, and no administrative revocation. Deletion was conditional on the exact
backed-up data and version, and absence was verified with a consistent read.
Only that identity item was removed; enrollment/removal history and other devices
were preserved. This was an exceptional support action, not an automatic recovery
rule or a general instruction to delete production identity records.

Future recovery must authenticate the authorized account, explicitly reconcile
ownership and in-flight work, invalidate old credentials and stale requests, and
handle replay/races safely. Do not relax enrollment conflict checks or automatically
delete records to work around this limitation. The deferral is a known release
limitation; one-time support recovery is not proof of self-service recovery.

## Validation and rollout

`python3 tests/run_native.py` exercises the production enrollment C code with
fault-injected HTTPS and NVS: retries after lost responses, boot recovery,
expired/replaced tickets, redirects and TLS policy, malformed acknowledgements,
status redaction, operation conflicts, commit failures (including ambiguous
ones), and the config/journal power-loss window. It also runs the existing
transfer, settings and update suites. These are firmware tests, not proof of a
deployed backend's authorization or transaction semantics.

Next implement the account/bootstrap routes and browser flow against this
contract, test concurrent/replayed/tampered tickets and owner isolation, then
qualify a development build on a spare unenrolled Link. Verify first enrollment,
power interruption, normal file delivery, and that the already-connected device
continues to work. Publish through the normal development and stable lifecycle
only after that integration and hardware qualification.

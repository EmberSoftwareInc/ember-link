# Ember Link cloud and browser setup: implementation plan

> Planning context. The current firmware implementation focuses on cloud transfer
> only and uses new Ember Link identifiers without legacy compatibility. The
> executable v1 contract is documented in [cloud-protocol.md](cloud-protocol.md);
> claiming, browser setup, and remote file management remain future work.

Status: proposed implementation sequence. No firmware, service, or UI changes
have been implemented by this plan.

This plan implements the product direction discussed in September 2026. The
[backend architecture overview](cloud-backend-architecture.md) describes the
proposed API, authentication, data model, and transfer recovery rules.

Product naming decision: **Ember Link** is the dongle's customer-facing name.
`EmberConnect` below refers to the existing repository or source path. Ember
Bridge retains its name. The proposed setup URL is still `connect.emberdesign.net`.

## Intended customer experience

1. Plug Ember Link into a computer and open `connect.emberdesign.net`.
2. Sign into Ember, authorize USB access, select WiFi, and name the machine.
3. The website verifies connectivity and links the dongle to the account.
4. Move the dongle to the embroidery machine.
5. Select the machine in Ember and send a design through the cloud.

Ember Bridge remains the desktop connection option for supported Brother WiFi
machines and an optional local companion for Ember Link. Dongle users with a
supported setup browser need no desktop app. A later WiFi setup increment
provides the same independence for unsupported USB browsers.

The interface presents one machine picker and one send action. Connection
details can explain local/cloud availability. A successful transfer means the
file reached the dongle and USB was reconnected, not that stitching started.

## Scope and release boundaries

**Core beta:** browser USB setup, signed USB updates where required, secure
account claiming, cloud delivery to online devices, one owner per dongle,
one active transfer per dongle, progress/errors, and unchanged local capability.

**Broader onboarding release:** WiFi-hotspot account claiming, tested mobile
setup, optional USB landing-page notifications, and finished packaging/help.

**Deferred:** shared devices, long-lived offline queues, automatic retry through
a different transport, cloud firmware rollout management, and machine control.

Polling over HTTPS plus private S3 file downloads is the initial architecture
candidate. Validate cost and latency before committing. Keep job contracts
independent of polling so push signaling can be added later.

## Phase 0 — Freeze contracts and release decisions

Owners: product, cloud, firmware, and web developers.

Deliverables:

- Agree the backend overview's claim, ownership, job, receipt, and error model.
- Define versioned JSON schemas and example request/response fixtures shared
  between backend, browser, and firmware work. Use explicit capability fields
  rather than relying solely on firmware version comparisons.
- Choose target file-size limits, allowed filename rules, replacement behavior,
  online freshness, polling interval, and transfer deadlines.
- Choose device enrollment and credential rotation; define how existing units
  without cloud credentials become enrolled. Never use a serial number alone
  as proof of ownership or production-device authenticity.
- Specify factory reset, account unclaim, resale, and lost-account recovery.
- Decide the setup deployment boundary. Prefer reusing the existing Next.js
  application and account components, with a top-level setup page rather than
  placing Web Serial inside the editor iframe.
- Confirm how authentication works on `connect.emberdesign.net`. Existing login
  storage does not automatically imply a session on another origin. Use an
  explicit sign-in or established redirect flow; do not pass account tokens in
  URL parameters.
- Agree a backend feature flag and beta enrollment policy. The current editor's
  Bridge rollout gate must not accidentally block cloud sends or become the
  authorization mechanism for device ownership.

Acceptance: all three implementation areas can work from the same protocol
fixtures, and no unresolved identity/reset decision changes their basic design.

## Phase 1 — Prove USB and HTTPS on real hardware

Owners: firmware and web developers, with a minimal cloud test endpoint.

Build two small feasibility tests before investing in full UI:

1. A browser opens the current CDC interface, identifies the dongle, scans WiFi,
   and provisions it using the existing JSON protocol.
2. The dongle downloads a representative design over verified HTTPS, checks its
   size/hash, writes it to microSD, and reconnects USB. A physical embroidery
   machine must list and open the design.

Measure minimum free heap, largest available allocation, stack headroom,
download throughput, flash image size, and power/reset behavior. Test while the
USB interface and local HTTP service are active. The board has no PSRAM and two
3 MB application slots; successful desktop compilation is not sufficient.

Check browser serial behavior on Windows and macOS: opening/closing the port,
DTR, split/coalesced JSON lines, long scan responses, unplugging, and reconnecting
after a firmware reboot. Bridge must not hold the serial port during these tests.

Acceptance: repeatable browser provisioning and a verified HTTPS-to-machine
transfer, with recorded resource headroom and a supported-platform test matrix.
If memory or power margins are insufficient, resolve them before proceeding.

## Phase 2 — Build the cloud foundation

Owner: cloud developer. Starts after Phase 0; production design incorporates
Phase 1 measurements.

In `ember-app`:

- Extend SAM resources for isolated development/staging, device credentials,
  device ownership, claims, transfer metadata, and private temporary S3 storage.
- Implement separate user and device authentication, revocation, scoped IAM,
  rate limits, and structured errors.
- Implement device enrollment tooling with an explicit factory/operator identity.
- Implement claim creation/redemption, device listing/rename, and unclaim.
- Implement transfer creation, direct upload authorization, finalize, poll/lease,
  progress/receipt reporting, pre-delivery cancellation, and reconciliation.
- Make claims, active-slot reservation, and transitions atomic. Pin the validated
  object version and bind jobs to an ownership generation.
- Separate operational expiry from TTL cleanup; handle abandoned uploads,
  obsolete claims, and ambiguous delivery outcomes.
- Add metrics for delivery outcomes, latency, last-seen behavior, authorization
  failures, throttling, and polling volume. Redact credentials and signed URLs.
- Reuse existing Cognito/S3/job patterns without inheriting AI-generation quotas
  or permanent artifact retention.

Acceptance: a simulated device completes the full lifecycle; tests reject
cross-account operations, conflicting claims, duplicate sends, stale attempts,
changed objects, and revoked credentials. Provide API fixtures and staging
configuration for the firmware and web developers.

## Phase 3 — Implement firmware cloud delivery and recovery

Owner: firmware developer. Protocol work can proceed alongside Phase 2.

In `EmberConnect/firmware/main`:

- Extract a transport-independent file-transfer component from `http_api.c`.
  LAN uploads and cloud downloads share validation, storage arbitration, staged
  writes, integrity checks, and USB reconnect behavior.
- Design recoverable replacement of an existing filename; add a durable job
  journal and receipt reconciliation across reboot. Do not treat the current
  temporary-file/rename handler as sufficient for power-loss recovery.
- Add cloud credential/configuration storage separate from LAN pairing tokens
  and signed firmware images. Distinguish factory identity from clearable WiFi
  settings and cloud enablement.
- Add TLS validation, time initialization, authenticated polling, streaming
  downloads, bounded buffers, progress reporting, and reconnect/backoff.
- Add USB commands/capabilities for beginning account claim, reading cloud
  readiness, and disabling cloud. Existing Bridge commands remain compatible.
- Prevent simultaneous local/cloud writes and firmware updates. Return a clear
  busy state rather than interleaving operations.
- Reconcile uncertain jobs before receiving new work. Duplicate messages should
  replay receipts rather than blindly downloading/reconnecting again.
- Keep local operation usable when cloud service or internet connectivity fails.

Acceptance: the real dongle performs an authenticated staged-cloud transfer,
survives router outages and selected power interruptions, and preserves existing
designs. The same firmware still accepts local Bridge uploads and USB setup.

## Phase 4 — Build browser setup and signed update flow

Owner: web developer, with firmware support.

In the existing `ember-app/next_frontend`:

- Build the setup page with browser capability detection and sign-in.
- Implement a reusable typed Web Serial client for JSON commands and binary
  firmware streaming, request IDs, progress events, timeouts, and cancellation.
- Permit only one serial conversation/session owner at a time; handle another
  tab or Bridge holding the port with a helpful retry message.
- Build identify → WiFi scan → live credential trial → name → claim → ready.
- Handle already-provisioned, already-owned, offline-cloud, wrong-password,
  missing-card, unsupported-firmware, and interrupted-setup states explicitly.
- Keep passwords out of logs/analytics and clear them from page state after use.
- Add an HTTPS release manifest and signed-image download path if updates are
  required. Show progress, reconnect after reboot, and verify the running version
  and health confirmation before declaring the update complete.
- Define how legacy units obtain credentials during migration. An ordinary
  firmware update cannot, by itself, establish trusted cloud enrollment.

Acceptance: a new user configures a production-enrolled dongle from a supported
browser without Bridge, sees cloud readiness, and completes setup after one
wrong-password retry. An interrupted update recovers or gives a tested recovery
path. Browser/OS support is based on device tests, not feature detection alone.

## Phase 5 — Integrate cloud into Ember's send experience

Owner: web developer, with backend and Bridge support.

- Refactor `frontend/src/components/editor-modals/SendToMachinePanel.tsx` so cloud
  sends do not begin by checking whether Bridge is installed.
- Introduce transport-neutral device and transfer types. Preserve the existing
  export generator and Bridge client behind a local transport adapter.
- Load account-owned cloud devices; display names, last-seen information,
  storage/capabilities, and actionable setup links.
- Upload the exported design, finalize the transfer, and poll its outcome.
  Separate browser-to-cloud upload progress from dongle delivery progress.
- Deduplicate a dongle discovered through both routes using its stable hardware
  identity. Inspect and extend Bridge's neutral API if it does not expose enough
  identity; never merge solely by nickname or IP address.
- Choose the route before creating a job. Proposed beta behavior: cloud for a
  cloud-enabled dongle and Bridge for direct Brother/local-only targets; retain
  explicit local use in connection settings or the Bridge app. Revisit automatic
  local preference after shared identity and job behavior are proven.
- Never automatically fall back to another route after an ambiguous send.
- Show “Delivered to Ember Link” accurately; keep the embroidery machine's
  supported file format explicit because the dongle cannot identify it over USB.

Acceptance: cloud sending works with Bridge absent, direct Brother sending
still works, one dongle appears once, and no install prompt blocks cloud users.
A finalized transfer continues if the user closes the browser. Once configured,
cloud sending also works in browsers without Web Serial.

## Phase 6 — Complete discovery and fallback onboarding

Owners: firmware, web, and product developers.

- Firmware 0.3.8 removes automatic START HERE file creation. Keep the existing
  post-provisioning cleanup for helpers from older firmware; direct users to
  the web setup URL or Bridge.
- Update factory QA assertions and quick-start materials to match the new URL.
  Print the setup URL and QR code; plugging in must not be advertised as a
  guarantee that a website opens automatically.
- Optionally add a WebUSB landing-page descriptor as a convenience. Test the
  resulting composite device on Windows/macOS and physical embroidery machines.
  This descriptor does not replace the Web Serial setup transport.
- Extend the current WiFi-hotspot portal with an account-claim handoff: provision
  WiFi, restore internet access, then redeem a physically obtained short-lived
  claim in Ember. Design expiry/retry behavior and avoid making users sign into
  their Ember account inside an unreliable captive-portal window.
- Verify Safari and mobile fallback on real devices before promising universal
  no-download setup. Provide clear fallback instructions until this ships.

Acceptance: instructions remain usable with no USB notification; supported USB
users and tested WiFi-fallback users can both finish without Bridge. Existing
machines still recognize the USB drive after descriptor changes.

## Phase 7 — Validate and release a small beta

Owners: all developers, plus the hardware QA operator.

- Extend `QA/device_qa.py` and release manifests for unique credential enrollment,
  cloud capability versions, staging/production separation, and clean packing
  state. Test reset preserves only the intended factory identity.
- Test power loss during download, file replacement, USB reconnect, and receipt
  submission; expired authorization; full/unreadable card; duplicate messages;
  concurrent local upload; stale ownership; firmware rollback; and factory reset.
- Run sustained polling/transfer tests and load-test the expected fleet. Record
  polling cost estimates and tune cadence before broader availability.
- Add support guidance for cloud offline, pending reconciliation, claim conflicts,
  and safe recovery. Record versions and IDs without exposing secrets or designs.
- Release behind account/device flags with a known firmware/web protocol matrix.
  Prefer capability flags over forced firmware downgrade when disabling cloud.
- Define alarms, operational owner, credential-revocation procedure, release
  rollback, and criteria for expanding the beta.

Acceptance: a fresh user completes setup and a real-machine transfer, the failure
matrix passes, operating costs are understood, and disabling cloud preserves the
local path. Keep evidence from physical-machine checks in private QA records.

## Repository cleanup and organization move

This is a separate release-preparation workstream, not a prerequisite for the
hardware feasibility tests:

- Apply Ember Link to customer-facing app copy, setup pages, packaging, help,
  USB display names, and release descriptions. Audit persisted names, discovery
  service identifiers, firmware image/project validation, and USB identity before
  changing them; preserve compatibility with existing Bridge and dongle versions.
  Plan any repository rename alongside the organization transfer.
- Update both READMEs around the new product relationship and link the setup page.
- Replace stale architecture/roadmap sections, including the older relay proposal
  once the transport decision is accepted and Bridge's outdated tray roadmap.
- Audit tracked files and full Git history for credentials, signing material,
  personal QA records, and internal account data; rotate exposed secrets if found.
- Decide licenses, attribution, contribution/security guidance, and the hosted
  service versus self-hosted enrollment policy.
- Document reproducible builds, development signing, factory enrollment boundaries,
  supported hardware, protocol fixtures, and compatibility validation.
- Inventory repository links, download URLs, release hosting, CI secrets and
  permissions before preparing the GitHub organization transfer.

Repository transfer and public visibility are separate release actions after
review. This plan does not perform either action or rewrite Git history.

## Dependency order and effort

```text
Contracts → hardware feasibility
         → backend foundation ↔ firmware implementation
         → browser setup → cloud claiming / end-to-end setup
Backend + firmware → unified send experience → core beta validation
Core flow → fallback onboarding → broader onboarding release
Repository preparation can run alongside implementation.
```

Initial planning range for the core beta remains roughly 5–8 engineer-weeks,
including integration and physical-device validation. It is total effort, not a
promise of calendar duration. Dedicated developers can overlap backend, firmware,
and UI after contracts are agreed; hardware and integration gates remain shared.

Budget separately for WiFi-hotspot cloud claiming, optional USB notification
work, unusual legacy-device recovery, and open-source release preparation.
Re-estimate after Phase 1 and again after the first authenticated vertical slice.

The first implementation milestone is deliberately small: **configure the
existing dongle from a browser, then prove a verified HTTPS download reaches a
physical embroidery machine.** That validates the two largest assumptions
before committing to a complete cloud product.

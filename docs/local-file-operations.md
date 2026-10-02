# Local filesystem operations (protocol 1)

Branch: `feature/local-file-browser`. The shared implementation is
`firmware/main/local_files.c`; the authenticated LAN adapter is `POST /api/fs`.
Health/info advertise `fileSystemProtocolVersion: 1`. Existing cloud and USB
setup protocols and root upload APIs are unchanged.

The caller must hold both the global operation gate and APP ownership of the
card. Commands are `list`, `mkdir`, `move` (also rename), and `delete`; root path
is an empty string. Every HTTP request requires the paired bearer token and
`confirmedIdle:true`, even in captive setup mode. It has a 1 KiB request limit.
This is not authorization to format the card and never invokes card formatting.

List returns `path`, `revision`, `entries` (`name`, `kind`, `size`), `total`,
`hidden`, `nextOffset`. Pages contain at most 32 entries. Use `offset` and the
same revision for continuation. Each explicit request remounts the card; clients
must not poll this endpoint. Existing cached `/api/files` stays unchanged.

Mutation examples (revision comes from the source parent; mkdir uses its parent):

```json
{"op":"mkdir","path":"Projects","revision":"0123456789abcdef","confirmedIdle":true}
{"op":"move","path":"rose.pes","destination":"Projects/rose.pes","revision":"0123456789abcdef","confirmedIdle":true}
{"op":"delete","path":"Projects/rose.pes","revision":"0123456789abcdef","confirmedIdle":true}
```

Success returns a refreshed first page of that parent during the same card
ownership session. Mutation failure is never automatically retried. Stable error
codes include `stale_listing`, `already_exists`, `folder_not_empty`,
`invalid_request`, `invalid_destination`, `unsupported_folder_tree`,
`folder_too_large`, `storage_error`, and `result_unknown`.

Paths are relative printable ASCII, <=255 bytes, <=8 components, each <=127
bytes. Reject FAT-forbidden and reserved names, dot/tilde-prefixed internal names,
traversal, absolute paths, noncanonical case and short-name aliases. Native tests
also reject symlinks; the device FAT volume cannot contain POSIX symlinks. There
is no overwrite, recursive delete, or case-only rename. A populated folder move
validates descendant paths before renaming. Scans are bounded to 4,096 entries;
unsupported/hidden entries are counted, not exposed as actions. They are excluded
from the revision so macOS metadata churn on USB reconnect does not block changes.
Destination collisions and nonempty folders are still checked against live storage.

The revision combines actionable directory-entry metadata and a per-boot nonce, and is
rechecked under exclusive card ownership. It is not a content hash and cannot
identify same-size changes that preserve FAT timestamps. Keep other hosts idle
and refresh after external modifications. Filename collisions are rejected
case-insensitively. A move uses FAT's same-volume rename; it does not copy/delete
file contents. After any operation the card is returned to USB and reconnected.

These metadata operations are not power-loss-atomic filesystem transactions.
Never claim that a power cut cannot damage FAT or that a failed HTTP response
means the operation did not happen. Clients must refresh/inspect uncertain
results. Firmware performs no replay or destructive recovery for these requests.
Cloud operations, durable command receipts and idempotent cloud replay remain a
separate future addition; no new cloud command is advertised here.

Validation: native production-code tests under ASan/UBSan, fault-injected rename,
ESP-IDF build. The Bridge counterpart documents the end-to-end local API and
spare-card hardware qualification in `docs/local-file-browser.md`.

Hardware qualification on 2026-10-02 used local firmware
`0.3.8-dev.1-files.2`, a LILYGO T-Dongle-S3 and Brother NQ1700E. On the second
120 MiB FAT32 card, reader/dongle USB write tests preserved 71 generated files;
15 Bridge operation checks and native UI checks passed; and the moved nested
square previewed on the Brother with the machine responsive. A final independent
reader audit found matching FAT copies, no structural fsck findings, and correct
names/hashes for all 70 remaining generated files. Native regressions cover the
hidden-host-metadata revision fix and Bridge tests cover status-poll interference.

The first card developed filesystem corruption during the larger USB-write test.
Its original designs were preserved and backed up. Independent-reader inspection
and an immutable private raw image confirmed stored damage; subsequent reads were
consistent. The cause remains unresolved. It was not repaired or formatted.
See Bridge's qualification notes for details and the multi-day reliability plan.
Do not infer power-cut safety or universal machine compatibility from these tests.

The numbered `0.3.8-dev.2` release is a separate package. Its exact image and
factory installer must pass qualification before publication; earlier hardware
checks are supporting evidence, not a substitute for those checks.

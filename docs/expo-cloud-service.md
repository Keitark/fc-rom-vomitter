# Operator-dispatched Expo cloud service

> **Status:** the Cloudflare service and a compile-checked ESP32 queue client
> are implemented. This queue client is **not yet validated on the physical
> cartridge**. The older fixed-URL pull and local SoftAP upload remain separate
> modes. Do not interpret a successful web upload as a physical game change.

This v2 service lets a visitor upload a compatible homebrew ROM from a public
web page without a ChatGPT account. The service validates and queues the ROM;
the cartridge then negotiates a compatible job, downloads it over HTTPS,
verifies it, installs it only in a safe hardware state, and reports the result.

The initial public implementation is intended for one supervised cartridge at
an exhibition, not as an unrestricted ROM-hosting service. Only ROMs that the
uploader is authorized to use may be submitted.

## Roles and trust boundary

| Role | Responsibility | Must not do |
|---|---|---|
| Anonymous visitor | Upload one ROM and retain the returned status URL | Address the cartridge directly or bypass validation |
| Public web service | Validate, deduplicate, queue, expire, and display status | Expose private object URLs or device credentials |
| Cartridge | Advertise capabilities, claim a compatible job, verify bytes, install safely, acknowledge result | Trust browser-supplied metadata or install while the console bus is unsafe |
| Operator | Release the next valid upload at a suitable point, pause/cancel the queue, and supervise console RESET | Release a second job before the first is resolved |

The visitor is anonymous to ChatGPT, but not unbounded: the service issues a
random, HttpOnly session cookie and a non-guessable status token. Device API
requests use a separate per-device secret and HMAC authentication.

## Visitor flow

1. Open the public Site and upload a `.nes` file.
2. The server parses the iNES header and validates the complete payload.
3. A valid, supported image is admitted to the FIFO queue, but not released
   to the cartridge yet.
4. At a play boundary, the operator presses **Send next game** on `/operator`.
   The cartridge's next signed poll claims that one released item.
5. The page receives a non-guessable status URL and polls for progress.
6. Status advances through `queued`, `claimed`, `downloaded`, `installed`, or
   a terminal `rejected`, `failed`, `expired`, or `duplicate` state.

No ChatGPT sign-in is required for this visitor flow. Operator controls and
deployment administration remain authenticated separately.

### HTTP surface

| Method and path | Caller | Result |
|---|---|---|
| `POST /api/public/jobs` | Visitor browser | Validate upload and return `202` with a status token, or a bounded `4xx` rejection |
| `GET /api/public/jobs/{status_token}` | Visitor browser | Return public job state without exposing the ROM or device identity |
| `POST /api/device/v2/next` | Authenticated cartridge | Report capabilities/state and receive `204` or one leased manifest |
| `GET /api/device/v2/jobs/{job_id}/rom` | Authenticated cartridge | Download bytes only for the device holding the active lease |
| `POST /api/device/v2/jobs/{job_id}/result` | Authenticated cartridge | Idempotently acknowledge `installed`, `unchanged`, `deferred`, or `failed` |
| `GET /api/operator/status` | Authenticated operator | Inspect queue counts, pause state, and last device state |
| `GET /api/operator/queue` | Authenticated operator | List active metadata without ROM bytes or uploader identity |
| `POST /api/operator/advance` | Authenticated operator | Release exactly one waiting job; reject while one is active |
| `POST /api/operator/pause` | Authenticated operator | Pause or resume device claims |
| `POST /api/operator/jobs/{job_id}/cancel` | Authenticated operator | Cancel a queued item |
| `POST /api/operator/clear-next` | Authenticated operator | Cancel and delete the oldest queued item |
| `POST /api/operator/clear-all` | Authenticated operator | Cancel and delete all queued items, bounded per request |

The public status token and the internal job ID are different values. A leaked
status URL must not authorize download, cancellation, or device operations.

## Admission gate

Automatic admission means automatic **validation**, not accepting arbitrary bytes
or authorizing an immediate change on the cartridge.
Before writing to private object storage, the service must verify:

- exact upload size limit and complete iNES header;
- supported mapper and PRG/CHR geometry reported by the cartridge capability
  profile;
- no trainer or NES 2.0 features unless that exact format is supported;
- SHA-256 and CRC32 computed by the service, never trusted from the browser;
- duplicate suppression against queued and active images;
- per-session/IP cooldown and a bounded queue (initial target: 20 jobs);
- content expiry and deletion (initial target: one hour after completion or
  expiry).

Rejected uploads must never receive a downloadable private-object URL.

## Device negotiation

The cartridge polls every 2–3 seconds in the exhibition profile. Each request
reports the state that affects compatibility and safe installation:

```json
{
  "device_id": "demo-cart-01",
  "firmware": "2.0.0-dev",
  "protocol": 2,
  "mappers": [0],
  "max_rom_bytes": 41488,
  "active_sha256": "...",
  "console_power": false,
  "console_exposed": false,
  "can_interrupt_console": false
}
```

The service returns `204 No Content` when no compatible work exists, or a
short-lived manifest:

```json
{
  "job_id": "01J...",
  "download_url": "https://service.example/api/device/v2/jobs/{job_id}/rom",
  "bytes": 40976,
  "sha256": "...",
  "crc32": "d98313b2",
  "ines": { "mapper": 0, "prg_kib": 32, "chr_kib": 8 },
  "expires_at": "2026-09-22T12:34:56Z"
}
```

The download endpoint is private and available only to the device holding the
short lease; it is not a public R2 object URL. The device must verify TLS,
byte count, SHA-256, CRC32, and parsed iNES fields.
It then passes the image through the existing inactive-slot commit, flash CRC,
SRAM readback, and console-isolation gates. If the cartridge has not explicitly
enabled supervised powered-console reload, the service leaves the job waiting
while console power is present. A claimed job can be `deferred` if power state
changes during the transfer. With powered reload enabled, gameplay freezes
during installation; staff must press Famicom RESET once the cartridge reports
READY. This configuration requires physical bus-safety testing before use.

After processing, the cartridge posts one idempotent result:

- `installed` — verified and made active;
- `unchanged` — already active with the same hash;
- `deferred` — valid but waiting for a safe console-power state;
- `failed` — rejected locally, with a bounded machine-readable error code.

Claims and acknowledgements need leases and idempotency keys so a reset or
lost response cannot install one queue item twice or strand it permanently.

## Cloudflare staging storage layout

The staging implementation uses a Cloudflare Worker, D1 for metadata and queue
state, and private R2 objects for ROM bytes. The API remains hosting-neutral;
the visitor UI can later be presented through ChatGPT Sites without moving the
device control plane.

| Store | Data |
|---|---|
| D1 | sessions, jobs, hashes, validation result, lease, device status, acknowledgement, expiry |
| R2 private bucket | validated ROM payload keyed by job ID/hash |

The public browser never receives permanent R2 credentials. The cartridge
receives only a short-lived download URL after an authenticated claim.

## Operational controls

- Staff-only release-next, pause/resume, and cancel controls. Clear-next and
  clear-all API endpoints are retained for recovery; a dedicated emergency-stop
  control is future work.
- Health view for last device poll, firmware/capabilities, active hash, queue
  depth, last error, and whether installation is deferred.
- Strict request/body limits, rate limiting, origin checks, structured audit
  events, and no storage of account, payment, Wi-Fi, or device secrets in logs.
- SoftAP browser upload remains the local recovery path if the cloud service or
  venue network is unavailable.

See [`service/README.md`](../service/README.md) for local verification and
staging deployment instructions.

## Implementation acceptance gates

This v2 design is ready to demonstrate only when:

1. invalid, oversized, unsupported, duplicate, and rate-limited uploads have
   automated negative tests;
2. device HMAC, expiry, replay, lease recovery, and idempotent acknowledgement
   tests pass;
3. power-on-console cases defer by default; a supervised interrupting profile
   has passed real-board isolation, SRAM, and RESET tests before use;
4. a queued job survives browser refresh and displays the final device result;
5. private ROM bytes cannot be listed or fetched anonymously;
6. operator release, pause, and powered-console behavior are tested on the real
   cartridge; and
7. the fixed-URL v1 mode and SoftAP fallback remain usable.

# Cloudflare exhibition service

This Worker implements the planned anonymous ROM queue in
[`docs/expo-cloud-service.md`](../docs/expo-cloud-service.md). It stores queue
metadata in D1 and validated iNES payloads in a private R2 bucket. No ROM is
included in this repository or its tests; the integration test synthesizes a
minimal byte-pattern fixture.

## Visitor page

The public page is **Famicom Game Drop**. It defaults to Japanese, has an
English switch and explains the three-step upload flow. It translates upload
errors and job states while keeping
the existing `/api/public/jobs` protocol. A status URL can be saved and
reopened; the token stays in the URL fragment.

Visitor form uploads appear in the public gallery by default. The uploader
can instead check **Keep this game private**; raw API uploads remain private.
An optional comment is displayed only for public games. Names are visible to
staff only unless the uploader separately opts to publish one. The gallery
does not expose ROM download URLs. Existing one-hour expiry is unchanged.

The page explicitly says that physical cartridge pickup is unverified. A
successful upload proves validation and queue admission, not that the game
has changed on a physical Famicom.

## Operator-controlled dispatch

Open `/operator` and enter the operator token. The page is Japanese-first with
an English switch; the token stays only in page memory. A valid visitor upload
remains queued but **unreleased**. The operator presses **Send next game** to
release exactly one waiting item. A second item cannot be released while one
is queued for the cartridge, claimed, downloading, or deferred. The cartridge
polls `/api/device/v2/next` and claims only released work. Staff may pause the
queue or cancel a waiting item. A powered-on console may be interrupted only
when the cartridge advertises that its locally tested configuration permits
it; staff must then press the Famicom RESET button after the new image is
verified. This last step is not yet physically validated.

The operator can also withdraw a public-gallery listing without cancelling
the source ROM submission or already queued plays. Names and comments in the
operator queue require the operator token and are cleared when the source job
expires or is cancelled.

## Local verification

```powershell
cd service
npm install
npm run check
npm test
```

The integration test starts `wrangler dev` with local D1/R2 emulation and
exercises upload, validation, duplicate rejection, HMAC authentication, replay
rejection, operator release, duplicate-release prevention, powered-console
policy, job lease, private download, idempotent result, public status,
default-public/private upload options, name privacy, gallery withdrawal, and
operator pause/resume.

## Local interactive service

Create an ignored `service/.dev.vars`:

```dotenv
DEVICE_HMAC_SECRET=replace-with-at-least-32-random-characters
OPERATOR_TOKEN=replace-with-a-separate-random-token
RATE_LIMIT_SALT=replace-with-a-third-random-value
```

Then run:

```powershell
npm run db:local
npm run dev
```

Open `http://127.0.0.1:8787`. Do not use real credentials or copyrighted ROM
images in repository tests.

## Staging deployment

Provision one D1 database and one private R2 bucket, put the D1 identifier in
`wrangler.toml`, apply all migrations remotely, set all three secrets with
`wrangler secret put`, then deploy. The `workers.dev` hostname is the staging
endpoint; attaching a custom domain is a separate, reviewed step.

After deployment, verify the real D1/R2/HMAC path using a generated mapper-0
fixture. Supply secrets only through the process environment; the script does
not print them or use a copyrighted ROM:

```powershell
$env:SERVICE_URL = "https://your-staging-worker.workers.dev"
$env:DEVICE_HMAC_SECRET = "..."
$env:OPERATOR_TOKEN = "..."
npm run test:remote
```

The device signs every request using the canonical string defined in
`src/auth.ts`. Secrets are per-device deployment configuration and must not be
compiled into public firmware artifacts.

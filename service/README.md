# Cloudflare exhibition service

This Worker implements the planned anonymous ROM queue in
[`docs/expo-cloud-service.md`](../docs/expo-cloud-service.md). It stores queue
metadata in D1 and validated iNES payloads in a private R2 bucket. No ROM is
included in this repository or its tests; the integration test synthesizes a
minimal byte-pattern fixture.

## Local verification

```powershell
cd service
npm install
npm run check
npm test
```

The integration test starts `wrangler dev` with local D1/R2 emulation and
exercises upload, validation, duplicate rejection, HMAC authentication, replay
rejection, job lease, private download, idempotent result, public status and
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
`wrangler.toml`, apply the migration remotely, set all three secrets with
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

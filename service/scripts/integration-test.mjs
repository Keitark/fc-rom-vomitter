import assert from "node:assert/strict";
import { createHmac, createHash, randomBytes } from "node:crypto";
import { spawn } from "node:child_process";
import { rmSync } from "node:fs";
import { setTimeout as delay } from "node:timers/promises";
import { fileURLToPath } from "node:url";

const base = "http://127.0.0.1:8787";
const deviceId = "test-cart-01";
const deviceSecret = "test-device-secret-32-characters";
const operatorToken = "test-operator-token-32-characters";

rmSync(new URL("../.wrangler", import.meta.url), { recursive: true, force: true });

function makeRom({ mapper = 0, fill = 0x42, prgBanks = 2, chrBanks = 1 } = {}) {
  const bytes = Buffer.alloc(16 + prgBanks * 16 * 1024 + chrBanks * 8 * 1024, fill);
  bytes.set([0x4e, 0x45, 0x53, 0x1a, prgBanks, chrBanks, (mapper & 0x0f) << 4, mapper & 0xf0], 0);
  return bytes;
}

function signed(pathname, method = "POST", body = Buffer.alloc(0), nonce) {
  const timestamp = String(Math.floor(Date.now() / 1000));
  const actualNonce = nonce ?? randomBytes(18).toString("base64url");
  const bodyHash = createHash("sha256").update(body).digest("hex");
  const canonical = `${method}\n${pathname}\n${timestamp}\n${actualNonce}\n${bodyHash}`;
  const signature = createHmac("sha256", deviceSecret).update(canonical).digest("hex");
  return {
    "X-RV-Device": deviceId,
    "X-RV-Timestamp": timestamp,
    "X-RV-Nonce": actualNonce,
    "X-RV-Body-SHA256": bodyHash,
    Authorization: `RV-HMAC-SHA256 ${signature}`,
  };
}

async function waitForServer() {
  for (let attempt = 0; attempt < 60; attempt += 1) {
    try {
      const response = await fetch(`${base}/api/health`);
      if (response.ok) return;
    } catch {}
    await delay(250);
  }
  throw new Error("wrangler dev did not become ready");
}

async function upload(bytes, session = randomBytes(18).toString("base64url"), ip = "192.0.2.10") {
  return fetch(`${base}/api/public/jobs`, {
    method: "POST",
    headers: {
      "Content-Type": "application/octet-stream",
      "X-RV-Authorized": "true",
      Cookie: `rv_session=${session}`,
      "CF-Connecting-IP": ip,
    },
    body: bytes,
  });
}

const wrangler = fileURLToPath(new URL("../node_modules/wrangler/bin/wrangler.js", import.meta.url));
const migrate = spawn(process.execPath, [wrangler, "d1", "migrations", "apply", "DB", "--local"], { stdio: "inherit" });
assert.equal(await new Promise((resolve) => migrate.on("exit", resolve)), 0, "local D1 migration failed");

const server = spawn(process.execPath, [
  wrangler, "dev", "--local", "--port", "8787",
  "--var", `DEVICE_ID:${deviceId}`,
  "--var", `DEVICE_HMAC_SECRET:${deviceSecret}`,
  "--var", `OPERATOR_TOKEN:${operatorToken}`,
  "--var", "RATE_LIMIT_SALT:test-rate-limit-salt-32-chars",
  "--var", "UPLOAD_COOLDOWN_SECONDS:10",
  "--var", "LEASE_SECONDS:1",
  "--var", "JOB_TTL_SECONDS:30",
  "--var", "MAX_QUEUE:2",
], { stdio: ["ignore", "pipe", "pipe"] });
let logs = "";
server.stdout.on("data", chunk => { logs += chunk; });
server.stderr.on("data", chunk => { logs += chunk; });

try {
  await waitForServer();
  const health = await (await fetch(`${base}/api/health`)).json();
  assert.equal(health.protocol, 2);

  const invalid = await upload(Buffer.from("not a rom"), "invalid-session-00000001", "192.0.2.11");
  assert.equal(invalid.status, 422);
  assert.equal((await invalid.json()).error, "short_header");

  const unsupported = await upload(makeRom({ mapper: 3 }), "unsupported-session-001", "192.0.2.12");
  assert.equal(unsupported.status, 422);
  assert.equal((await unsupported.json()).error, "mapper_unsupported");

  const oversized = await upload(Buffer.alloc(41489), "oversized-session-00001", "192.0.2.15");
  assert.equal(oversized.status, 413);
  assert.equal((await oversized.json()).error, "too_large");

  const noAuthorization = await fetch(`${base}/api/public/jobs`, {
    method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: makeRom({ fill: 0x16 }),
  });
  assert.equal(noAuthorization.status, 400);
  assert.equal((await noAuthorization.json()).error, "authorization_required");

  const rom = makeRom();
  const accepted = await upload(rom, "accepted-session-000001", "192.0.2.13");
  assert.equal(accepted.status, 202);
  const queued = await accepted.json();
  assert.match(queued.status_token, /^[A-Za-z0-9_-]{24,96}$/);

  const duplicate = await upload(rom, "duplicate-session-00001", "192.0.2.14");
  assert.equal(duplicate.status, 409);
  assert.equal((await duplicate.json()).error, "duplicate");

  const cooldown = await upload(makeRom({ fill: 0x43 }), "accepted-session-000001", "192.0.2.13");
  assert.equal(cooldown.status, 429);
  assert.equal((await cooldown.json()).error, "cooldown");

  const statusQueued = await (await fetch(queued.status_url)).json();
  assert.equal(statusQueued.state, "queued");
  assert.equal(statusQueued.queue_position, 1);

  const capabilities = Buffer.from(JSON.stringify({
    firmware: "integration-test", protocol: 2, mappers: [0], max_rom_bytes: 41488,
    active_sha256: "0".repeat(64), console_power: false, console_exposed: false,
  }));
  const nextPath = "/api/device/v2/next";
  const fixedNonce = randomBytes(18).toString("base64url");
  const next = await fetch(`${base}${nextPath}`, { method: "POST", headers: { ...signed(nextPath, "POST", capabilities, fixedNonce), "Content-Type": "application/json" }, body: capabilities });
  assert.equal(next.status, 200);
  const manifest = await next.json();
  assert.equal(manifest.ines.mapper, 0);
  assert.equal(manifest.install_policy, "safe_now");
  assert.equal(manifest.bytes, rom.length);

  const replay = await fetch(`${base}${nextPath}`, { method: "POST", headers: { ...signed(nextPath, "POST", capabilities, fixedNonce), "Content-Type": "application/json" }, body: capabilities });
  assert.equal(replay.status, 401);
  assert.equal((await replay.json()).error, "device_replay");

  const romPath = new URL(manifest.download_url).pathname;
  const anonymousDownload = await fetch(`${base}${romPath}`);
  assert.equal(anonymousDownload.status, 401);
  assert.equal((await anonymousDownload.json()).error, "device_auth_invalid");

  const downloaded = await fetch(`${base}${romPath}`, { headers: signed(romPath, "GET") });
  assert.equal(downloaded.status, 200);
  assert.deepEqual(Buffer.from(await downloaded.arrayBuffer()), rom);

  const resultPath = `/api/device/v2/jobs/${manifest.job_id}/result`;
  const resultBody = Buffer.from(JSON.stringify({ result: "installed", idempotency_key: "install-result-00000001", code: "ok", message: "Verified and active." }));
  const installed = await fetch(`${base}${resultPath}`, { method: "POST", headers: { ...signed(resultPath, "POST", resultBody), "Content-Type": "application/json" }, body: resultBody });
  assert.equal(installed.status, 200);
  assert.equal((await installed.json()).state, "installed");

  const repeated = await fetch(`${base}${resultPath}`, { method: "POST", headers: { ...signed(resultPath, "POST", resultBody), "Content-Type": "application/json" }, body: resultBody });
  assert.equal(repeated.status, 200);
  assert.equal((await repeated.json()).idempotent, true);

  const conflictingBody = Buffer.from(JSON.stringify({ result: "failed", idempotency_key: "install-result-00000001", code: "conflict" }));
  const conflicting = await fetch(`${base}${resultPath}`, { method: "POST", headers: { ...signed(resultPath, "POST", conflictingBody), "Content-Type": "application/json" }, body: conflictingBody });
  assert.equal(conflicting.status, 409);
  assert.equal((await conflicting.json()).error, "idempotency_conflict");

  const statusInstalled = await (await fetch(queued.status_url)).json();
  assert.equal(statusInstalled.state, "installed");
  assert.equal(statusInstalled.result_code, "ok");

  const operatorHeaders = { Authorization: `Bearer ${operatorToken}`, "Content-Type": "application/json" };
  const pause = await fetch(`${base}/api/operator/pause`, { method: "POST", headers: operatorHeaders, body: JSON.stringify({ paused: true }) });
  assert.equal(pause.status, 200);
  const operator = await (await fetch(`${base}/api/operator/status`, { headers: operatorHeaders })).json();
  assert.equal(operator.paused, true);
  const pausedPoll = await fetch(`${base}${nextPath}`, { method: "POST", headers: { ...signed(nextPath, "POST", capabilities), "Content-Type": "application/json" }, body: capabilities });
  assert.equal(pausedPoll.status, 204);
  await fetch(`${base}/api/operator/pause`, { method: "POST", headers: operatorHeaders, body: JSON.stringify({ paused: false }) });

  const unsafeRom = makeRom({ fill: 0x44 });
  const unsafeAccepted = await upload(unsafeRom, "unsafe-session-00000001", "192.0.2.16");
  assert.equal(unsafeAccepted.status, 202);
  const unsafeQueued = await unsafeAccepted.json();
  const unsafeCapabilities = Buffer.from(JSON.stringify({
    firmware: "integration-test", protocol: 2, mappers: [0], max_rom_bytes: 41488,
    active_sha256: "1".repeat(64), console_power: true, console_exposed: true,
  }));
  const firstUnsafePoll = await fetch(`${base}${nextPath}`, { method: "POST", headers: { ...signed(nextPath, "POST", unsafeCapabilities), "Content-Type": "application/json" }, body: unsafeCapabilities });
  assert.equal(firstUnsafePoll.status, 200);
  const firstUnsafeManifest = await firstUnsafePoll.json();
  assert.equal(firstUnsafeManifest.install_policy, "defer_until_safe");

  await delay(2100);
  const leaseRecoveryPoll = await fetch(`${base}${nextPath}`, { method: "POST", headers: { ...signed(nextPath, "POST", unsafeCapabilities), "Content-Type": "application/json" }, body: unsafeCapabilities });
  assert.equal(leaseRecoveryPoll.status, 200);
  const recoveredManifest = await leaseRecoveryPoll.json();
  assert.equal(recoveredManifest.job_id, firstUnsafeManifest.job_id);

  const unsafeRomPath = new URL(recoveredManifest.download_url).pathname;
  const unsafeDownload = await fetch(`${base}${unsafeRomPath}`, { headers: signed(unsafeRomPath, "GET") });
  assert.equal(unsafeDownload.status, 200);
  assert.deepEqual(Buffer.from(await unsafeDownload.arrayBuffer()), unsafeRom);

  const unsafeResultPath = `/api/device/v2/jobs/${recoveredManifest.job_id}/result`;
  const deferredBody = Buffer.from(JSON.stringify({ result: "deferred", idempotency_key: "deferred-result-000001", code: "console_unsafe" }));
  const deferred = await fetch(`${base}${unsafeResultPath}`, { method: "POST", headers: { ...signed(unsafeResultPath, "POST", deferredBody), "Content-Type": "application/json" }, body: deferredBody });
  assert.equal(deferred.status, 200);
  assert.equal((await deferred.json()).state, "deferred");

  const thirdRom = makeRom({ fill: 0x45 });
  const thirdAccepted = await upload(thirdRom, "third-session-000000001", "192.0.2.17");
  assert.equal(thirdAccepted.status, 202);
  const thirdQueued = await thirdAccepted.json();
  const queueFull = await upload(makeRom({ fill: 0x46 }), "full-session-0000000001", "192.0.2.18");
  assert.equal(queueFull.status, 503);
  assert.equal((await queueFull.json()).error, "queue_full");

  const installedAfterDeferredBody = Buffer.from(JSON.stringify({ result: "installed", idempotency_key: "installed-after-defer-01", code: "ok" }));
  const installedAfterDeferred = await fetch(`${base}${unsafeResultPath}`, { method: "POST", headers: { ...signed(unsafeResultPath, "POST", installedAfterDeferredBody), "Content-Type": "application/json" }, body: installedAfterDeferredBody });
  assert.equal(installedAfterDeferred.status, 200);
  assert.equal((await installedAfterDeferred.json()).state, "installed");

  const expiryWaitMs = Math.max(0, Date.parse(thirdQueued.expires_at) - Date.now() + 1200);
  await delay(expiryWaitMs);
  const expiredStatus = await (await fetch(thirdQueued.status_url)).json();
  assert.equal(expiredStatus.state, "expired");

  console.log("integration PASS: upload -> lease -> authenticated download -> installed status");
  console.log("negative PASS: invalid, unauthorized, oversize, mapper, duplicate, cooldown, queue, replay, and pause gates");
  console.log("lifecycle PASS: lease recovery, unsafe deferral, later install, idempotency conflict, and expiry cleanup");
} finally {
  server.kill("SIGTERM");
  await Promise.race([new Promise(resolve => server.once("exit", resolve)), delay(3000)]);
  if (process.exitCode) console.error(logs);
}

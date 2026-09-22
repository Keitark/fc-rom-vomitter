import assert from "node:assert/strict";
import { createHash, createHmac, randomBytes } from "node:crypto";

const serviceUrl = required("SERVICE_URL").replace(/\/$/, "");
const deviceId = process.env.DEVICE_ID ?? "demo-cart-01";
const deviceSecret = required("DEVICE_HMAC_SECRET");
const operatorToken = required("OPERATOR_TOKEN");

function required(name) {
  const value = process.env[name];
  if (!value) throw new Error(`${name} is required`);
  return value;
}

function syntheticRom() {
  const bytes = Buffer.alloc(16 + 32 * 1024 + 8 * 1024);
  bytes.set([0x4e, 0x45, 0x53, 0x1a, 2, 1, 0, 0], 0);
  randomBytes(bytes.length - 16).copy(bytes, 16);
  return bytes;
}

function signed(pathname, method = "POST", body = Buffer.alloc(0)) {
  const timestamp = String(Math.floor(Date.now() / 1000));
  const nonce = randomBytes(18).toString("base64url");
  const bodyHash = createHash("sha256").update(body).digest("hex");
  const canonical = `${method}\n${pathname}\n${timestamp}\n${nonce}\n${bodyHash}`;
  const signature = createHmac("sha256", deviceSecret).update(canonical).digest("hex");
  return {
    "X-RV-Device": deviceId,
    "X-RV-Timestamp": timestamp,
    "X-RV-Nonce": nonce,
    "X-RV-Body-SHA256": bodyHash,
    Authorization: `RV-HMAC-SHA256 ${signature}`,
  };
}

async function expectJson(response, expectedStatus) {
  const text = await response.text();
  assert.equal(response.status, expectedStatus, `${response.url}: ${text}`);
  return text ? JSON.parse(text) : null;
}

const health = await expectJson(await fetch(`${serviceUrl}/api/health`), 200);
assert.equal(health.ok, true);
assert.equal(health.protocol, 2);

const operator = await expectJson(await fetch(`${serviceUrl}/api/operator/status`, {
  headers: { Authorization: `Bearer ${operatorToken}` },
}), 200);
assert.equal(operator.paused, false, "staging queue must be resumed before smoke testing");

const rom = syntheticRom();
const accepted = await expectJson(await fetch(`${serviceUrl}/api/public/jobs`, {
  method: "POST",
  headers: {
    "Content-Type": "application/octet-stream",
    "X-RV-Authorized": "true",
  },
  body: rom,
}), 202);
assert.equal(accepted.state, "queued");

const capabilities = Buffer.from(JSON.stringify({
  firmware: "remote-smoke",
  protocol: 2,
  mappers: [0],
  max_rom_bytes: 41488,
  active_sha256: "0".repeat(64),
  console_power: false,
  console_exposed: false,
}));
const nextPath = "/api/device/v2/next";
const manifest = await expectJson(await fetch(`${serviceUrl}${nextPath}`, {
  method: "POST",
  headers: { ...signed(nextPath, "POST", capabilities), "Content-Type": "application/json" },
  body: capabilities,
}), 200);
assert.equal(manifest.ines.mapper, 0);
assert.equal(manifest.install_policy, "safe_now");

const romPath = new URL(manifest.download_url).pathname;
const downloaded = await fetch(`${serviceUrl}${romPath}`, { headers: signed(romPath, "GET") });
assert.equal(downloaded.status, 200);
assert.deepEqual(Buffer.from(await downloaded.arrayBuffer()), rom);

const resultPath = `/api/device/v2/jobs/${manifest.job_id}/result`;
const resultBody = Buffer.from(JSON.stringify({
  result: "installed",
  idempotency_key: randomBytes(18).toString("base64url"),
  code: "remote_smoke_ok",
  message: "Synthetic deployment verification completed.",
}));
const result = await expectJson(await fetch(`${serviceUrl}${resultPath}`, {
  method: "POST",
  headers: { ...signed(resultPath, "POST", resultBody), "Content-Type": "application/json" },
  body: resultBody,
}), 200);
assert.equal(result.state, "installed");

const publicStatus = await expectJson(await fetch(accepted.status_url), 200);
assert.equal(publicStatus.state, "installed");
assert.equal(publicStatus.result_code, "remote_smoke_ok");

console.log(`remote smoke PASS: ${serviceUrl}`);
console.log("health -> anonymous upload -> HMAC lease -> private R2 download -> installed status");

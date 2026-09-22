import { authenticateDevice, authenticateOperator } from "./auth";
import { crc32Hex, InesError, MAX_INES_SIZE, parseSupportedInes, sha256Hex } from "./ines";
import { visitorPage } from "./page";

export interface Env {
  DB: D1Database;
  ROMS: R2Bucket;
  DEVICE_ID: string;
  DEVICE_HMAC_SECRET: string;
  OPERATOR_TOKEN: string;
  RATE_LIMIT_SALT: string;
  JOB_TTL_SECONDS: string;
  LEASE_SECONDS: string;
  MAX_QUEUE: string;
  UPLOAD_COOLDOWN_SECONDS: string;
}

type JobRow = {
  id: string;
  status_token: string;
  state: string;
  object_key: string;
  sha256: string;
  crc32: string;
  bytes: number;
  mapper: number;
  prg_kib: number;
  chr_kib: number;
  mirroring: string;
  lease_owner: string | null;
  lease_expires_at: number | null;
  result_code: string | null;
  result_message: string | null;
  result_idempotency_key: string | null;
  created_at: number;
  updated_at: number;
  expires_at: number;
};

const encoder = new TextEncoder();
const terminalStates = new Set(["installed", "unchanged", "failed", "expired", "cancelled"]);

function configurationNumber(raw: string, fallback: number, minimum: number, maximum: number): number {
  const value = Number(raw);
  return Number.isSafeInteger(value) && value >= minimum && value <= maximum ? value : fallback;
}

function jsonError(status: number, error: string, message: string): Response {
  return Response.json({ error, message }, { status });
}

function randomToken(bytes = 24): string {
  const buffer = crypto.getRandomValues(new Uint8Array(bytes));
  let binary = "";
  for (const value of buffer) binary += String.fromCharCode(value);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replaceAll("=", "");
}

function cookieValue(request: Request, name: string): string | null {
  const cookies = request.headers.get("Cookie") ?? "";
  for (const item of cookies.split(";")) {
    const [key, ...value] = item.trim().split("=");
    if (key === name) return value.join("=");
  }
  return null;
}

async function saltedHash(value: string, salt: string): Promise<string> {
  return sha256Hex(encoder.encode(`${salt}\n${value}`));
}

async function readUpload(request: Request): Promise<{ bytes: Uint8Array; authorized: boolean }> {
  const contentType = request.headers.get("Content-Type") ?? "";
  const declaredLength = Number(request.headers.get("Content-Length") ?? "0");
  if (declaredLength > MAX_INES_SIZE + 4096) throw new InesError("Upload is too large.", "too_large");

  if (contentType.startsWith("multipart/form-data")) {
    const form = await request.formData();
    const file = form.get("rom");
    if (!(file instanceof File)) throw new InesError("Select a .nes file.", "missing_file");
    if (file.size > MAX_INES_SIZE) throw new InesError("Upload is too large.", "too_large");
    return { bytes: new Uint8Array(await file.arrayBuffer()), authorized: form.get("authorized") === "on" };
  }

  const bytes = new Uint8Array(await request.arrayBuffer());
  if (bytes.byteLength > MAX_INES_SIZE) throw new InesError("Upload is too large.", "too_large");
  return { bytes, authorized: request.headers.get("X-RV-Authorized") === "true" };
}

async function expireObjects(env: Env, now: number): Promise<void> {
  const expired = await env.DB.prepare(
    "SELECT id, object_key FROM jobs WHERE expires_at <= ? AND object_deleted = 0 LIMIT 10",
  ).bind(now).all<{ id: string; object_key: string }>();
  for (const job of expired.results) {
    await env.ROMS.delete(job.object_key);
    await env.DB.prepare(
      "UPDATE jobs SET state = CASE WHEN state IN ('queued','claimed','downloaded','deferred') THEN 'expired' ELSE state END, object_deleted = 1, updated_at = ? WHERE id = ?",
    ).bind(now, job.id).run();
  }
  await env.DB.prepare(
    "UPDATE jobs SET state = 'queued', lease_owner = NULL, lease_expires_at = NULL, updated_at = ? WHERE state IN ('claimed','downloaded') AND lease_expires_at < ? AND expires_at > ?",
  ).bind(now, now, now).run();
}

async function uploadJob(request: Request, env: Env, now: number): Promise<Response> {
  let upload: { bytes: Uint8Array; authorized: boolean };
  try {
    upload = await readUpload(request);
  } catch (error) {
    if (error instanceof InesError) return jsonError(error.code === "too_large" ? 413 : 400, error.code, error.message);
    return jsonError(400, "upload_invalid", "The upload could not be read.");
  }
  if (!upload.authorized) return jsonError(400, "authorization_required", "Confirm that you are authorized to use this ROM.");

  let metadata;
  try {
    metadata = parseSupportedInes(upload.bytes);
  } catch (error) {
    if (error instanceof InesError) return jsonError(422, error.code, error.message);
    throw error;
  }

  const sha256 = await sha256Hex(upload.bytes);
  const duplicate = await env.DB.prepare(
    "SELECT state FROM jobs WHERE sha256 = ? AND state IN ('queued','claimed','downloaded','deferred','installed') AND expires_at > ? LIMIT 1",
  ).bind(sha256, now).first<{ state: string }>();
  if (duplicate) return jsonError(409, "duplicate", `This image is already ${duplicate.state}.`);

  const session = cookieValue(request, "rv_session") ?? randomToken(18);
  const sessionHash = await saltedHash(session, env.RATE_LIMIT_SALT);
  const ip = request.headers.get("CF-Connecting-IP") ?? request.headers.get("X-Forwarded-For") ?? "local";
  const ipHash = await saltedHash(ip, env.RATE_LIMIT_SALT);
  const cooldown = configurationNumber(env.UPLOAD_COOLDOWN_SECONDS, 10, 0, 3600);
  const recent = await env.DB.prepare(
    "SELECT created_at FROM jobs WHERE (session_hash = ? OR ip_hash = ?) AND created_at > ? ORDER BY created_at DESC LIMIT 1",
  ).bind(sessionHash, ipHash, now - cooldown).first<{ created_at: number }>();
  if (recent) return jsonError(429, "cooldown", "Please wait before submitting another image.");

  const maxQueue = configurationNumber(env.MAX_QUEUE, 20, 1, 1000);
  const count = await env.DB.prepare(
    "SELECT COUNT(*) AS count FROM jobs WHERE state IN ('queued','claimed','downloaded','deferred') AND expires_at > ?",
  ).bind(now).first<{ count: number }>();
  if ((count?.count ?? 0) >= maxQueue) return jsonError(503, "queue_full", "The exhibition queue is full.");

  const id = crypto.randomUUID();
  const statusToken = randomToken();
  const objectKey = `jobs/${id}.nes`;
  const ttl = configurationNumber(env.JOB_TTL_SECONDS, 3600, 1, 86400);
  const expiresAt = now + ttl;
  const crc32 = crc32Hex(upload.bytes);

  await env.ROMS.put(objectKey, upload.bytes, {
    httpMetadata: { contentType: "application/octet-stream" },
    customMetadata: { sha256, crc32 },
  });
  try {
    await env.DB.prepare(
      `INSERT INTO jobs (
        id, status_token, state, object_key, sha256, crc32, bytes,
        mapper, prg_kib, chr_kib, mirroring, session_hash, ip_hash,
        created_at, updated_at, expires_at
      ) VALUES (?, ?, 'queued', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    ).bind(
      id, statusToken, objectKey, sha256, crc32, upload.bytes.byteLength,
      metadata.mapper, metadata.prgKib, metadata.chrKib, metadata.mirroring,
      sessionHash, ipHash, now, now, expiresAt,
    ).run();
  } catch (error) {
    await env.ROMS.delete(objectKey);
    throw error;
  }

  const origin = new URL(request.url).origin;
  return Response.json({
    state: "queued",
    status_token: statusToken,
    status_url: `${origin}/api/public/jobs/${statusToken}`,
    expires_at: new Date(expiresAt * 1000).toISOString(),
  }, {
    status: 202,
    headers: { "Set-Cookie": `rv_session=${session}; Path=/; Max-Age=3600; HttpOnly; Secure; SameSite=Strict` },
  });
}

async function publicStatus(token: string, env: Env, now: number): Promise<Response> {
  if (!/^[A-Za-z0-9_-]{24,96}$/.test(token)) return jsonError(404, "not_found", "Job not found.");
  const job = await env.DB.prepare(
    "SELECT state, result_code, result_message, created_at, updated_at, expires_at FROM jobs WHERE status_token = ?",
  ).bind(token).first<Pick<JobRow, "state" | "result_code" | "result_message" | "created_at" | "updated_at" | "expires_at">>();
  if (!job) return jsonError(404, "not_found", "Job not found.");
  const position = job.state === "queued"
    ? await env.DB.prepare("SELECT COUNT(*) AS count FROM jobs WHERE state = 'queued' AND created_at < ? AND expires_at > ?")
      .bind(job.created_at, now).first<{ count: number }>()
    : null;
  return Response.json({
    state: job.state,
    queue_position: position ? (position.count + 1) : null,
    result_code: job.result_code,
    message: job.result_message,
    created_at: new Date(job.created_at * 1000).toISOString(),
    updated_at: new Date(job.updated_at * 1000).toISOString(),
    expires_at: new Date(job.expires_at * 1000).toISOString(),
  });
}

function validCapabilities(value: unknown): value is {
  firmware: string; protocol: number; mappers: number[]; max_rom_bytes: number;
  active_sha256?: string; console_power: boolean; console_exposed: boolean;
} {
  if (!value || typeof value !== "object") return false;
  const item = value as Record<string, unknown>;
  return typeof item.firmware === "string" && item.firmware.length <= 64 && item.protocol === 2
    && Array.isArray(item.mappers) && item.mappers.every((mapper) => Number.isInteger(mapper))
    && Number.isInteger(item.max_rom_bytes) && Number(item.max_rom_bytes) > 0
    && typeof item.console_power === "boolean" && typeof item.console_exposed === "boolean"
    && (item.active_sha256 === undefined || /^[0-9a-f]{64}$/.test(String(item.active_sha256)));
}

async function nextJob(request: Request, body: Uint8Array, env: Env, deviceId: string, now: number): Promise<Response> {
  let capabilities: unknown;
  try { capabilities = JSON.parse(new TextDecoder().decode(body)); }
  catch { return jsonError(400, "capabilities_invalid", "Invalid JSON capability report."); }
  if (!validCapabilities(capabilities)) return jsonError(422, "capabilities_invalid", "Capability report is incomplete.");

  await env.DB.prepare(
    `INSERT INTO devices (device_id, firmware, protocol, mappers_json, max_rom_bytes, active_sha256, console_power, console_exposed, last_seen_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(device_id) DO UPDATE SET firmware=excluded.firmware, protocol=excluded.protocol,
       mappers_json=excluded.mappers_json, max_rom_bytes=excluded.max_rom_bytes,
       active_sha256=excluded.active_sha256, console_power=excluded.console_power,
       console_exposed=excluded.console_exposed, last_seen_at=excluded.last_seen_at`,
  ).bind(
    deviceId, capabilities.firmware, capabilities.protocol, JSON.stringify(capabilities.mappers),
    capabilities.max_rom_bytes, capabilities.active_sha256 ?? null,
    capabilities.console_power ? 1 : 0, capabilities.console_exposed ? 1 : 0, now,
  ).run();

  const paused = await env.DB.prepare("SELECT value FROM service_settings WHERE key = 'paused'").first<{ value: string }>();
  if (paused?.value === "1") return new Response(null, { status: 204 });

  const placeholders = capabilities.mappers.map(() => "?").join(",");
  if (!placeholders) return new Response(null, { status: 204 });
  const job = await env.DB.prepare(
    `SELECT * FROM jobs WHERE state = 'queued' AND expires_at > ? AND bytes <= ? AND mapper IN (${placeholders}) ORDER BY created_at LIMIT 1`,
  ).bind(now, capabilities.max_rom_bytes, ...capabilities.mappers).first<JobRow>();
  if (!job) return new Response(null, { status: 204 });

  if (capabilities.active_sha256 && capabilities.active_sha256 === job.sha256) {
    await env.DB.prepare(
      "UPDATE jobs SET state='unchanged', result_code='already_active', result_message='Image was already active on the cartridge.', updated_at=? WHERE id=? AND state='queued'",
    ).bind(now, job.id).run();
    return new Response(null, { status: 204 });
  }

  const leaseSeconds = configurationNumber(env.LEASE_SECONDS, 60, 1, 600);
  const leaseExpiresAt = now + leaseSeconds;
  const claimed = await env.DB.prepare(
    "UPDATE jobs SET state='claimed', lease_owner=?, lease_expires_at=?, updated_at=? WHERE id=? AND state='queued'",
  ).bind(deviceId, leaseExpiresAt, now, job.id).run();
  if ((claimed.meta.changes ?? 0) !== 1) return new Response(null, { status: 204 });

  const origin = new URL(request.url).origin;
  return Response.json({
    job_id: job.id,
    download_url: `${origin}/api/device/v2/jobs/${job.id}/rom`,
    bytes: job.bytes,
    sha256: job.sha256,
    crc32: job.crc32,
    ines: { mapper: job.mapper, prg_kib: job.prg_kib, chr_kib: job.chr_kib, mirroring: job.mirroring },
    lease_expires_at: new Date(leaseExpiresAt * 1000).toISOString(),
    expires_at: new Date(job.expires_at * 1000).toISOString(),
    install_policy: capabilities.console_power || capabilities.console_exposed ? "defer_until_safe" : "safe_now",
  });
}

async function downloadRom(jobId: string, env: Env, deviceId: string, now: number): Promise<Response> {
  const job = await env.DB.prepare(
    "SELECT * FROM jobs WHERE id=? AND lease_owner=? AND state IN ('claimed','downloaded','deferred') AND expires_at>?",
  ).bind(jobId, deviceId, now).first<JobRow>();
  if (!job) return jsonError(404, "job_unavailable", "The job is not leased to this device.");
  const object = await env.ROMS.get(job.object_key);
  if (!object) return jsonError(410, "object_missing", "The ROM payload is no longer available.");
  await env.DB.prepare("UPDATE jobs SET state='downloaded', updated_at=? WHERE id=? AND state='claimed'").bind(now, jobId).run();
  const headers = new Headers();
  object.writeHttpMetadata(headers);
  headers.set("Content-Type", "application/octet-stream");
  headers.set("Content-Length", String(object.size));
  headers.set("ETag", object.httpEtag);
  headers.set("X-RV-SHA256", job.sha256);
  headers.set("X-RV-CRC32", job.crc32);
  headers.set("Cache-Control", "private, no-store");
  return new Response(object.body, { headers });
}

async function reportResult(requestBody: Uint8Array, jobId: string, env: Env, deviceId: string, now: number): Promise<Response> {
  let value: Record<string, unknown>;
  try { value = JSON.parse(new TextDecoder().decode(requestBody)) as Record<string, unknown>; }
  catch { return jsonError(400, "result_invalid", "Invalid JSON result."); }
  const result = String(value.result ?? "");
  const idempotencyKey = String(value.idempotency_key ?? "");
  const code = String(value.code ?? "").slice(0, 64);
  const message = String(value.message ?? "").slice(0, 240);
  if (!["installed", "unchanged", "deferred", "failed"].includes(result) || !/^[A-Za-z0-9_-]{16,96}$/.test(idempotencyKey)) {
    return jsonError(422, "result_invalid", "Result or idempotency key is invalid.");
  }

  const job = await env.DB.prepare("SELECT * FROM jobs WHERE id=? AND lease_owner=?").bind(jobId, deviceId).first<JobRow>();
  if (!job) return jsonError(404, "job_unavailable", "The job is not assigned to this device.");
  if (job.result_idempotency_key === idempotencyKey) {
    if (job.state !== result) return jsonError(409, "idempotency_conflict", "The idempotency key was already used for another result.");
    return Response.json({ ok: true, idempotent: true, state: job.state });
  }
  if (terminalStates.has(job.state)) return jsonError(409, "job_terminal", `Job is already ${job.state}.`);
  if (job.state === "deferred" && result === "deferred") {
    return Response.json({ ok: true, idempotent: true, state: "deferred" });
  }

  await env.DB.prepare(
    "UPDATE jobs SET state=?, result_code=?, result_message=?, result_idempotency_key=?, updated_at=? WHERE id=? AND lease_owner=?",
  ).bind(result, code || null, message || null, idempotencyKey, now, jobId, deviceId).run();
  return Response.json({ ok: true, idempotent: false, state: result });
}

async function operatorRoute(request: Request, pathname: string, env: Env, now: number): Promise<Response> {
  if (!authenticateOperator(request, env.OPERATOR_TOKEN)) return jsonError(401, "operator_auth_invalid", "Operator authentication failed.");
  if (request.method === "GET" && pathname === "/api/operator/status") {
    const settings = await env.DB.prepare("SELECT value FROM service_settings WHERE key='paused'").first<{ value: string }>();
    const states = await env.DB.prepare("SELECT state, COUNT(*) AS count FROM jobs GROUP BY state").all<{ state: string; count: number }>();
    const device = await env.DB.prepare("SELECT firmware, protocol, mappers_json, max_rom_bytes, console_power, console_exposed, last_seen_at FROM devices ORDER BY last_seen_at DESC LIMIT 1").first();
    return Response.json({ paused: settings?.value === "1", jobs: states.results, device });
  }
  if (request.method === "POST" && pathname === "/api/operator/pause") {
    const value: { paused?: boolean } = await request.json<{ paused?: boolean }>().catch(() => ({}));
    if (typeof value.paused !== "boolean") return jsonError(422, "pause_invalid", "Provide a boolean paused value.");
    await env.DB.prepare("UPDATE service_settings SET value=?, updated_at=? WHERE key='paused'").bind(value.paused ? "1" : "0", now).run();
    return Response.json({ paused: value.paused });
  }
  if (request.method === "POST" && (pathname === "/api/operator/clear-next" || pathname === "/api/operator/clear-all")) {
    const limit = pathname.endsWith("clear-next") ? 1 : 20;
    const jobs = await env.DB.prepare("SELECT id, object_key FROM jobs WHERE state='queued' ORDER BY created_at LIMIT ?").bind(limit).all<{ id: string; object_key: string }>();
    for (const job of jobs.results) {
      await env.ROMS.delete(job.object_key);
      await env.DB.prepare("UPDATE jobs SET state='cancelled', object_deleted=1, updated_at=? WHERE id=? AND state='queued'").bind(now, job.id).run();
    }
    return Response.json({ cancelled: jobs.results.length });
  }
  return jsonError(404, "not_found", "Route not found.");
}

async function route(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const pathname = url.pathname;
  const now = Math.floor(Date.now() / 1000);
  await expireObjects(env, now);

  if (request.method === "GET" && pathname === "/") {
    return new Response(visitorPage, { headers: { "Content-Type": "text/html; charset=utf-8" } });
  }
  if (request.method === "GET" && pathname === "/api/health") {
    return Response.json({ ok: true, service: "fc-rom-vomitter-cloud", protocol: 2, time: new Date(now * 1000).toISOString() });
  }
  if (request.method === "POST" && pathname === "/api/public/jobs") return uploadJob(request, env, now);
  const publicMatch = pathname.match(/^\/api\/public\/jobs\/([A-Za-z0-9_-]+)$/);
  if (request.method === "GET" && publicMatch) return publicStatus(publicMatch[1], env, now);
  if (pathname.startsWith("/api/operator/")) return operatorRoute(request, pathname, env, now);

  if (pathname.startsWith("/api/device/v2/")) {
    const body = request.method === "GET" ? new Uint8Array() : new Uint8Array(await request.arrayBuffer());
    const authentication = await authenticateDevice(request, body, env, now);
    if (!authentication.ok) return authentication.response;
    if (request.method === "POST" && pathname === "/api/device/v2/next") {
      return nextJob(request, body, env, authentication.deviceId, now);
    }
    const romMatch = pathname.match(/^\/api\/device\/v2\/jobs\/([0-9a-f-]{36})\/rom$/);
    if (request.method === "GET" && romMatch) return downloadRom(romMatch[1], env, authentication.deviceId, now);
    const resultMatch = pathname.match(/^\/api\/device\/v2\/jobs\/([0-9a-f-]{36})\/result$/);
    if (request.method === "POST" && resultMatch) return reportResult(body, resultMatch[1], env, authentication.deviceId, now);
  }
  return jsonError(404, "not_found", "Route not found.");
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      const response = await route(request, env);
      const headers = new Headers(response.headers);
      headers.set("X-Content-Type-Options", "nosniff");
      headers.set("Referrer-Policy", "no-referrer");
      headers.set("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
      if ((headers.get("Content-Type") ?? "").startsWith("text/html")) {
        headers.set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
      }
      if (new URL(request.url).pathname.startsWith("/api/")) headers.set("Cache-Control", "no-store");
      return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
    } catch (error) {
      console.error("request failed", error instanceof Error ? error.message : "unknown error");
      return jsonError(500, "internal_error", "The service could not complete the request.");
    }
  },
};

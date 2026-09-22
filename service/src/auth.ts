export type DeviceAuthEnv = {
  DB: D1Database;
  DEVICE_ID: string;
  DEVICE_HMAC_SECRET: string;
};

const encoder = new TextEncoder();

function bytesToHex(bytes: ArrayBuffer): string {
  return [...new Uint8Array(bytes)].map((value) => value.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqual(left: string, right: string): boolean {
  if (left.length !== right.length) return false;
  let difference = 0;
  for (let index = 0; index < left.length; index += 1) {
    difference |= left.charCodeAt(index) ^ right.charCodeAt(index);
  }
  return difference === 0;
}

export async function hmacHex(secret: string, message: string): Promise<string> {
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  return bytesToHex(await crypto.subtle.sign("HMAC", key, encoder.encode(message)));
}

export function canonicalDeviceRequest(
  method: string,
  pathname: string,
  timestamp: string,
  nonce: string,
  bodySha256: string,
): string {
  return `${method.toUpperCase()}\n${pathname}\n${timestamp}\n${nonce}\n${bodySha256}`;
}

export async function authenticateDevice(
  request: Request,
  body: Uint8Array,
  env: DeviceAuthEnv,
  nowSeconds: number,
): Promise<{ ok: true; deviceId: string } | { ok: false; response: Response }> {
  const deviceId = request.headers.get("X-RV-Device") ?? "";
  const timestamp = request.headers.get("X-RV-Timestamp") ?? "";
  const nonce = request.headers.get("X-RV-Nonce") ?? "";
  const claimedBodyHash = (request.headers.get("X-RV-Body-SHA256") ?? "").toLowerCase();
  const authorization = request.headers.get("Authorization") ?? "";
  const signature = authorization.startsWith("RV-HMAC-SHA256 ")
    ? authorization.slice("RV-HMAC-SHA256 ".length).toLowerCase()
    : "";

  if (deviceId !== env.DEVICE_ID || !/^\d{10}$/.test(timestamp) || !/^[A-Za-z0-9_-]{16,96}$/.test(nonce)) {
    return { ok: false, response: Response.json({ error: "device_auth_invalid" }, { status: 401 }) };
  }
  const numericTimestamp = Number(timestamp);
  if (!Number.isSafeInteger(numericTimestamp) || Math.abs(nowSeconds - numericTimestamp) > 300) {
    return { ok: false, response: Response.json({ error: "device_timestamp_invalid" }, { status: 401 }) };
  }

  const actualBodyHash = await crypto.subtle.digest("SHA-256", Uint8Array.from(body).buffer);
  const actualBodyHashHex = bytesToHex(actualBodyHash);
  if (!timingSafeEqual(actualBodyHashHex, claimedBodyHash)) {
    return { ok: false, response: Response.json({ error: "device_body_hash_invalid" }, { status: 401 }) };
  }

  const pathname = new URL(request.url).pathname;
  const expected = await hmacHex(
    env.DEVICE_HMAC_SECRET,
    canonicalDeviceRequest(request.method, pathname, timestamp, nonce, claimedBodyHash),
  );
  if (!timingSafeEqual(expected, signature)) {
    return { ok: false, response: Response.json({ error: "device_signature_invalid" }, { status: 401 }) };
  }

  await env.DB.prepare("DELETE FROM device_nonces WHERE seen_at < ?").bind(nowSeconds - 600).run();
  try {
    await env.DB.prepare("INSERT INTO device_nonces (device_id, nonce, seen_at) VALUES (?, ?, ?)")
      .bind(deviceId, nonce, nowSeconds)
      .run();
  } catch {
    return { ok: false, response: Response.json({ error: "device_replay" }, { status: 401 }) };
  }

  return { ok: true, deviceId };
}

export function authenticateOperator(request: Request, token: string): boolean {
  const authorization = request.headers.get("Authorization") ?? "";
  return token.length >= 24 && timingSafeEqual(authorization, `Bearer ${token}`);
}

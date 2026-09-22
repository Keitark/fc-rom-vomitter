CREATE TABLE IF NOT EXISTS service_settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);

INSERT OR IGNORE INTO service_settings (key, value, updated_at)
VALUES ('paused', '0', unixepoch());

CREATE TABLE IF NOT EXISTS jobs (
  id TEXT PRIMARY KEY,
  status_token TEXT NOT NULL UNIQUE,
  state TEXT NOT NULL CHECK (state IN (
    'queued', 'claimed', 'downloaded', 'deferred',
    'installed', 'unchanged', 'failed', 'expired', 'cancelled'
  )),
  object_key TEXT NOT NULL UNIQUE,
  sha256 TEXT NOT NULL,
  crc32 TEXT NOT NULL,
  bytes INTEGER NOT NULL,
  mapper INTEGER NOT NULL,
  prg_kib INTEGER NOT NULL,
  chr_kib INTEGER NOT NULL,
  mirroring TEXT NOT NULL,
  session_hash TEXT NOT NULL,
  ip_hash TEXT NOT NULL,
  lease_owner TEXT,
  lease_expires_at INTEGER,
  result_code TEXT,
  result_message TEXT,
  result_idempotency_key TEXT,
  object_deleted INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_jobs_queue
ON jobs (state, mapper, created_at);

CREATE INDEX IF NOT EXISTS idx_jobs_sha_state
ON jobs (sha256, state);

CREATE INDEX IF NOT EXISTS idx_jobs_session_created
ON jobs (session_hash, created_at);

CREATE INDEX IF NOT EXISTS idx_jobs_expiry
ON jobs (expires_at, object_deleted);

CREATE TABLE IF NOT EXISTS devices (
  device_id TEXT PRIMARY KEY,
  firmware TEXT NOT NULL,
  protocol INTEGER NOT NULL,
  mappers_json TEXT NOT NULL,
  max_rom_bytes INTEGER NOT NULL,
  active_sha256 TEXT,
  console_power INTEGER NOT NULL,
  console_exposed INTEGER NOT NULL,
  last_seen_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS device_nonces (
  device_id TEXT NOT NULL,
  nonce TEXT NOT NULL,
  seen_at INTEGER NOT NULL,
  PRIMARY KEY (device_id, nonce)
);

CREATE INDEX IF NOT EXISTS idx_device_nonces_seen
ON device_nonces (seen_at);

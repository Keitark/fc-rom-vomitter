CREATE TABLE IF NOT EXISTS gallery_items (
  id TEXT PRIMARY KEY,
  source_job_id TEXT NOT NULL,
  title TEXT NOT NULL,
  object_key TEXT NOT NULL UNIQUE,
  sha256 TEXT NOT NULL,
  crc32 TEXT NOT NULL,
  bytes INTEGER NOT NULL,
  mapper INTEGER NOT NULL,
  prg_kib INTEGER NOT NULL,
  chr_kib INTEGER NOT NULL,
  mirroring TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  object_deleted INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS gallery_active_idx ON gallery_items(expires_at, created_at);

CREATE TABLE IF NOT EXISTS operator_sessions (
  session_hash TEXT PRIMARY KEY,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_operator_sessions_expiry
ON operator_sessions (expires_at);

-- Existing queued jobs remain unreleased until an operator explicitly sends one.
ALTER TABLE jobs ADD COLUMN released_at INTEGER;

CREATE INDEX IF NOT EXISTS idx_jobs_dispatch
ON jobs (state, released_at, created_at);

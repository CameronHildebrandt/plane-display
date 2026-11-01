-- D1 stores hashes, never reusable bearer secrets.
CREATE TABLE devices (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  api_key_hash TEXT NOT NULL UNIQUE,
  previous_api_key_hash TEXT,
  previous_key_expires_at INTEGER,
  latitude REAL NOT NULL,
  longitude REAL NOT NULL,
  radius_km REAL NOT NULL CHECK(radius_km > 0 AND radius_km <= 300),
  include_grounded INTEGER NOT NULL DEFAULT 0,
  rotation_requested INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE pairing_codes (
  token_hash TEXT PRIMARY KEY,
  device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  expires_at INTEGER NOT NULL,
  used_at INTEGER
);
CREATE INDEX pairing_codes_expires_at ON pairing_codes(expires_at);

CREATE TABLE sessions (
  token_hash TEXT PRIMARY KEY,
  device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  expires_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX sessions_expires_at ON sessions(expires_at);

CREATE TABLE flight_snapshots (
  device_id TEXT PRIMARY KEY REFERENCES devices(id) ON DELETE CASCADE,
  payload_json TEXT NOT NULL,
  fetched_at INTEGER NOT NULL
);

-- Fleet metadata and WebAuthn credentials are deliberately separate from the
-- short-lived browser pairing sessions used by an individual display.
ALTER TABLE devices ADD COLUMN firmware_channel TEXT NOT NULL DEFAULT 'stable'
  CHECK(firmware_channel IN ('stable', 'preview', 'local'));
ALTER TABLE devices ADD COLUMN last_firmware_version INTEGER NOT NULL DEFAULT 0;
ALTER TABLE devices ADD COLUMN last_seen_at INTEGER;

CREATE TABLE admin_passkeys (
  credential_id TEXT PRIMARY KEY,
  public_key TEXT NOT NULL,
  counter INTEGER NOT NULL,
  transports_json TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  last_used_at INTEGER
);

CREATE TABLE admin_webauthn_challenges (
  challenge TEXT PRIMARY KEY,
  kind TEXT NOT NULL CHECK(kind IN ('registration', 'authentication')),
  expires_at INTEGER NOT NULL
);
CREATE INDEX admin_webauthn_challenges_expires_at ON admin_webauthn_challenges(expires_at);

CREATE TABLE admin_sessions (
  token_hash TEXT PRIMARY KEY,
  credential_id TEXT NOT NULL REFERENCES admin_passkeys(credential_id) ON DELETE CASCADE,
  expires_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX admin_sessions_expires_at ON admin_sessions(expires_at);

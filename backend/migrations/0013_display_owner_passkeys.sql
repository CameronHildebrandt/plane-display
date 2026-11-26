-- Passkeys enrolled from a display's paired configuration page are scoped to
-- that display. They are intentionally separate from fleet-admin credentials.
CREATE TABLE device_passkeys (
  credential_id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  public_key TEXT NOT NULL,
  counter INTEGER NOT NULL,
  transports_json TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  last_used_at INTEGER
);
CREATE INDEX device_passkeys_device_id ON device_passkeys(device_id);

CREATE TABLE device_webauthn_challenges (
  challenge TEXT PRIMARY KEY,
  device_id TEXT REFERENCES devices(id) ON DELETE CASCADE,
  kind TEXT NOT NULL CHECK(kind IN ('registration', 'authentication')),
  expires_at INTEGER NOT NULL
);
CREATE INDEX device_webauthn_challenges_expires_at ON device_webauthn_challenges(expires_at);

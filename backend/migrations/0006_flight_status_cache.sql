ALTER TABLE tracked_flights ADD COLUMN status_identifier TEXT;

CREATE TABLE flight_status_cache (
  tracked_flight_id TEXT PRIMARY KEY REFERENCES tracked_flights(id) ON DELETE CASCADE,
  payload_json TEXT NOT NULL,
  fetched_at INTEGER NOT NULL
);

CREATE TABLE provider_usage (
  provider TEXT NOT NULL,
  usage_month TEXT NOT NULL,
  requests INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (provider, usage_month)
);

-- Cache published AirLabs routes separately from short-lived live snapshots.
-- The worker applies a seven-day TTL, keyed by callsign and weekday.
CREATE TABLE flight_route_cache (
  route_key TEXT PRIMARY KEY,
  payload_json TEXT NOT NULL,
  fetched_at INTEGER NOT NULL
);

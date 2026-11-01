-- Static-ish registration data, keyed by the ICAO 24-bit transponder address.
CREATE TABLE aircraft_cache (
  icao24 TEXT PRIMARY KEY,
  type_name TEXT,
  manufacturer TEXT,
  owner_name TEXT,
  owner_country TEXT,
  fetched_at INTEGER NOT NULL
);

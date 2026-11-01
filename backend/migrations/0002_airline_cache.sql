-- Airline names are reference data, cached for a month to keep free API usage tiny.
CREATE TABLE airline_cache (
  icao_code TEXT PRIMARY KEY,
  name TEXT,
  country TEXT,
  fetched_at INTEGER NOT NULL
);

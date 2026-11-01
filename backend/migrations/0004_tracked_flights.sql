CREATE TABLE tracked_flights (
  id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  flight_number TEXT NOT NULL,
  title TEXT NOT NULL,
  flight_date TEXT NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE INDEX tracked_flights_device_date ON tracked_flights(device_id, flight_date);

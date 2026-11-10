-- Keep the registry's canonical ICAO type and operator flag alongside its
-- human-readable owner details. Existing rows are refreshed once to populate
-- these columns, then resume the normal long-lived cache behaviour.
ALTER TABLE aircraft_cache ADD COLUMN type_code TEXT;
ALTER TABLE aircraft_cache ADD COLUMN owner_code TEXT;
ALTER TABLE aircraft_cache ADD COLUMN identity_version INTEGER NOT NULL DEFAULT 0;

-- Opt-in filter: preserve today's complete live-feed behavior by default.
ALTER TABLE devices ADD COLUMN hide_incomplete_data INTEGER NOT NULL DEFAULT 0;

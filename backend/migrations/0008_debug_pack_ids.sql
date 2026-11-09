-- Debug-mode pack identifiers are strings (for example, "canadian-scheduled").
-- Version 0007 provisioned the column as INTEGER and the original write path
-- consequently stored every selected pack as 1. SQLite accepts text in this
-- affinity column, so retain the schema and repair the unusable legacy value.
UPDATE devices SET debug_mode = '' WHERE debug_mode = 1;

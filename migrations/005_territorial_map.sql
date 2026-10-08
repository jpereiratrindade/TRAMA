ALTER TABLE units ADD COLUMN latitude REAL CHECK(latitude IS NULL OR (latitude BETWEEN -90 AND 90));
ALTER TABLE units ADD COLUMN longitude REAL CHECK(longitude IS NULL OR (longitude BETWEEN -180 AND 180));
CREATE INDEX IF NOT EXISTS idx_territories_kind_name ON territories(kind,name);

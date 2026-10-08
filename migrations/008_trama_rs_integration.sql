-- TRAMA-RS is the sole territorial reference service from this migration on.
-- Remove the legacy generated catalog because its COREDE/biome values were inferred.
DELETE FROM territorial_catalog;

CREATE TABLE IF NOT EXISTS integration_state (
    integration_id TEXT PRIMARY KEY,
    endpoint TEXT NOT NULL,
    status TEXT NOT NULL,
    checked_at TEXT,
    dataset_version TEXT,
    validation_status TEXT,
    details_json TEXT
);

INSERT OR REPLACE INTO integration_state(
    integration_id, endpoint, status, checked_at, dataset_version,
    validation_status, details_json
) VALUES (
    'trama-rs', 'http://10.163.80.176:8080', 'configured', NULL, NULL,
    'PRELIMINAR_NAO_HOMOLOGADO',
    '{"note":"Runtime endpoint may be overridden with TRAMA_RS_URL; no inferred fallback is permitted."}'
);

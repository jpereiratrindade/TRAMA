-- Migration 006: Territorial Service Refactor & Dual-Axis Hierarchy
-- Separate administrative planning hierarchy (RF -> COREDE -> Município) from ecological dimension (Bioma)
-- Add canonical IBGE code and offline territorial reference catalog

ALTER TABLE units ADD COLUMN ibge_code TEXT;
ALTER TABLE units ADD COLUMN biome_predominant TEXT;
ALTER TABLE units ADD COLUMN biomes_occurring TEXT;

CREATE TABLE IF NOT EXISTS territorial_catalog (
    ibge_code TEXT PRIMARY KEY,
    municipality_name TEXT NOT NULL,
    uf TEXT NOT NULL DEFAULT 'RS',
    corede_code TEXT,
    corede_name TEXT NOT NULL,
    rf_code TEXT NOT NULL,
    rf_name TEXT NOT NULL,
    biome_predominant TEXT NOT NULL,
    biomes_occurring TEXT NOT NULL,
    latitude REAL,
    longitude REAL,
    dataset_version TEXT NOT NULL DEFAULT 'rs-territorial-2026.1',
    source_planning TEXT NOT NULL DEFAULT 'SPGG/RS - Atlas Socioeconômico',
    source_ecology TEXT NOT NULL DEFAULT 'IBGE - Biomas do Brasil',
    verified_at TEXT NOT NULL DEFAULT '2026-10-08T00:00:00Z'
);

CREATE INDEX IF NOT EXISTS idx_territorial_catalog_name ON territorial_catalog(municipality_name);
CREATE INDEX IF NOT EXISTS idx_territorial_catalog_corede ON territorial_catalog(corede_name);
CREATE INDEX IF NOT EXISTS idx_territorial_catalog_rf ON territorial_catalog(rf_name);
CREATE INDEX IF NOT EXISTS idx_territorial_catalog_biome ON territorial_catalog(biome_predominant);
CREATE INDEX IF NOT EXISTS idx_units_ibge ON units(ibge_code);

-- Update v_attendance view to include territorial attributes
DROP VIEW IF EXISTS v_attendance;
CREATE VIEW v_attendance AS 
SELECT 
    aa.*,
    a.project_id,
    a.unit_id,
    u.name AS unit_name,
    u.project_group_id,
    g.code AS group_code,
    g.label AS group_label,
    t.name AS municipality,
    u.ibge_code,
    u.biome_predominant,
    u.biomes_occurring,
    c.name AS corede,
    rf.name AS functional_region
FROM attendance_aggregates aa 
JOIN activities a ON a.id = aa.activity_id 
JOIN units u ON u.id = a.unit_id 
JOIN territories t ON t.id = u.territory_id 
LEFT JOIN territories c ON c.id = t.parent_id AND c.kind = 'corede'
LEFT JOIN territories rf ON rf.id = c.parent_id AND rf.kind = 'functional_region'
LEFT JOIN project_groups g ON g.id = u.project_group_id 
WHERE a.deleted_at IS NULL;

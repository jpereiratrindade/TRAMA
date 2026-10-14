-- Migration 009: Temporal Series & Qualitative Analytics
-- Enriches v_attendance with activity temporal markers (occurred_at, kind, title)
-- Adds performance indices for temporal queries and thematic frequency aggregations

DROP VIEW IF EXISTS v_attendance;
CREATE VIEW v_attendance AS 
SELECT 
    aa.id,
    aa.activity_id,
    aa.total,
    aa.women,
    aa.men,
    aa.youth,
    aa.source_id,
    aa.source_locator,
    aa.quality_status,
    aa.revision,
    a.project_id,
    a.unit_id,
    a.kind AS activity_kind,
    a.title AS activity_title,
    a.occurred_at,
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

CREATE INDEX IF NOT EXISTS idx_activities_occurred ON activities(occurred_at);
CREATE INDEX IF NOT EXISTS idx_activities_kind ON activities(kind);
CREATE INDEX IF NOT EXISTS idx_observations_topic ON observations(topic);

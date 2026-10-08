ALTER TABLE projects ADD COLUMN revision INTEGER NOT NULL DEFAULT 1;
ALTER TABLE projects ADD COLUMN deleted_at TEXT;
ALTER TABLE units ADD COLUMN revision INTEGER NOT NULL DEFAULT 1;
ALTER TABLE units ADD COLUMN deleted_at TEXT;
CREATE INDEX IF NOT EXISTS idx_projects_deleted ON projects(deleted_at);
CREATE INDEX IF NOT EXISTS idx_units_deleted ON units(project_id,deleted_at);

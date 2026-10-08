CREATE VIEW IF NOT EXISTS v_attendance AS SELECT aa.*,a.project_id,a.unit_id,u.name unit_name,u.project_group_id,g.code group_code,g.label group_label,t.name municipality FROM attendance_aggregates aa JOIN activities a ON a.id=aa.activity_id JOIN units u ON u.id=a.unit_id JOIN territories t ON t.id=u.territory_id LEFT JOIN project_groups g ON g.id=u.project_group_id WHERE a.deleted_at IS NULL;


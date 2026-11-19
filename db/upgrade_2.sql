DO $upgrade$
DECLARE
    version text;
BEGIN
    PERFORM pg_catalog.pg_advisory_xact_lock(pg_catalog.hashtext('sqlscan.schema'), 0);
    SELECT value INTO STRICT version FROM sqlscan.setting WHERE key = 'schema_version';
    IF version = '2' THEN RETURN; END IF;
    IF version <> '1' THEN RAISE EXCEPTION 'Unsupported SqlScan schema version'; END IF;

    -- Старые строки имеют NULL в полях, которые первая схема не измеряла.
    ALTER TABLE sqlscan.scan
        ADD COLUMN risk text,
        ADD COLUMN line_count integer CHECK (line_count > 0),
        ADD COLUMN hit_count bigint CHECK (hit_count >= 0),
        ADD COLUMN analyzed_bytes bigint CHECK (analyzed_bytes >= 0);
    ALTER TABLE sqlscan.hit ADD COLUMN category_applied text;
    UPDATE sqlscan.setting SET value = '2' WHERE key = 'schema_version';
END;
$upgrade$;

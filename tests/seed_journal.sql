\set ON_ERROR_STOP on
\ir ../db/schema.sql
DO $grants$ BEGIN
    IF has_schema_privilege(current_user,'public','CREATE')
       OR NOT has_schema_privilege(current_user,'sqlscan','CREATE')
       OR has_database_privilege(current_user,current_database(),'CREATE')
       OR (SELECT rolsuper OR rolcreaterole OR rolcreatedb FROM pg_roles WHERE rolname=current_user) THEN
        RAISE EXCEPTION 'Unexpected application role grants';
    END IF;
END $grants$;
-- Keep a populated v1 record through the migration to check backwards compatibility.
INSERT INTO sqlscan.scan (source_type,input_sha256,input_bytes,total_score,suspicious,threshold_used,truncated)
VALUES ('manual',decode(repeat('00',32),'hex'),0,0,false,5,false);
\ir ../db/upgrade_2.sql
DO $$ BEGIN
    IF NOT EXISTS (SELECT FROM sqlscan.scan WHERE analyzed_bytes IS NULL) THEN
        RAISE EXCEPTION 'Historical record missing after migration';
    END IF;
END $$;
INSERT INTO sqlscan.scan(source_type,source_name,input_sha256,input_bytes,total_score,suspicious,threshold_used,truncated)
SELECT 'manual','QA',decode(repeat('00',32),'hex'),0,0,false,5,false FROM generate_series(1,9999);

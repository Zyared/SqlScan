\set ON_ERROR_STOP on
-- Запускать через psql в пустой тестовой БД. Даже создание таблиц откатывается.
BEGIN;
DO $$
BEGIN
    IF to_regclass('sqlscan.setting') IS NOT NULL THEN
        RAISE EXCEPTION 'Use an empty test database';
    END IF;
END;
$$;

\ir schema.sql

DO $$
DECLARE
    saved_rule integer;
    saved_scan bigint;
BEGIN
    IF (SELECT count(*) FROM sqlscan.rule WHERE enabled) <> 18 OR
       (SELECT count(*) FROM sqlscan.rule_category) <> 8 THEN
        RAISE EXCEPTION 'Unexpected initial rules or categories';
    END IF;

    SELECT id INTO STRICT saved_rule FROM sqlscan.rule WHERE pattern = '1=1';
    INSERT INTO sqlscan.scan (source_type, input_sha256, input_bytes, total_score,
                              suspicious, threshold_used, truncated)
    VALUES ('manual', decode(repeat('00', 32), 'hex'), 3, 2, false, 5, false)
    RETURNING id INTO saved_scan;
    INSERT INTO sqlscan.hit (scan_id, rule_id, line_no, position, pattern_applied,
                             weight_applied, high_risk_applied, context)
    SELECT saved_scan, id, 1, 0, pattern, weight, high_risk, '1=1'
    FROM sqlscan.rule WHERE id = saved_rule;

    BEGIN
        UPDATE sqlscan.rule SET weight = 6 WHERE id = saved_rule;
        RAISE EXCEPTION 'Out-of-range weight accepted';
    EXCEPTION WHEN check_violation THEN NULL;
    END;
    BEGIN
        UPDATE sqlscan.rule SET pattern = E'\tUNION  SELECT\n' WHERE id = saved_rule;
        RAISE EXCEPTION 'Duplicate normalized pattern accepted';
    EXCEPTION WHEN unique_violation THEN NULL;
    END;
    BEGIN
        UPDATE sqlscan.hit SET context = repeat('я', 201) WHERE scan_id = saved_scan;
        RAISE EXCEPTION 'Oversized context accepted';
    EXCEPTION WHEN string_data_right_truncation THEN NULL;
    END;
    BEGIN
        DELETE FROM sqlscan.rule WHERE id = saved_rule;
        RAISE EXCEPTION 'Deleted a rule used in a report';
    EXCEPTION WHEN foreign_key_violation THEN NULL;
    END;

    UPDATE sqlscan.rule SET pattern = E'\tReNamed  Rule\n', weight = 4,
                            high_risk = true, enabled = false WHERE id = saved_rule;
    DELETE FROM sqlscan.rule WHERE pattern = ';';
    UPDATE sqlscan.setting SET value = '7' WHERE key = 'suspicious_score_threshold';
END;
$$;

\ir schema.sql

DO $$
BEGIN
    IF (SELECT count(*) FROM sqlscan.rule) <> 17 OR
       EXISTS (SELECT FROM sqlscan.rule WHERE pattern IN ('1=1', ';')) OR
       NOT EXISTS (SELECT FROM sqlscan.rule WHERE pattern = 'renamed rule'
                   AND weight = 4 AND high_risk AND NOT enabled) OR
       NOT EXISTS (SELECT FROM sqlscan.setting WHERE key = 'suspicious_score_threshold' AND value = '7') THEN
        RAISE EXCEPTION 'Reinitialization changed user data';
    END IF;

    IF NOT EXISTS (SELECT FROM sqlscan.hit AS h JOIN sqlscan.scan AS s ON s.id = h.scan_id
                   WHERE h.pattern_applied = '1=1' AND h.weight_applied = 2
                   AND NOT h.high_risk_applied AND s.threshold_used = 5) THEN
        RAISE EXCEPTION 'Historical report changed';
    END IF;

    DELETE FROM sqlscan.scan WHERE id = (SELECT scan_id FROM sqlscan.hit);
    IF EXISTS (SELECT FROM sqlscan.hit) THEN
        RAISE EXCEPTION 'Cascade did not remove the hit';
    END IF;
END;
$$;
ROLLBACK;

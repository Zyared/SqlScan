-- Инициализация выполняется одним оператором PostgreSQL, включая начальные данные.
DO $install$
DECLARE
    previous_search_path text := pg_catalog.current_setting('search_path');
BEGIN
    PERFORM pg_catalog.pg_advisory_xact_lock(pg_catalog.hashtext('sqlscan.schema'), 0);

    IF pg_catalog.to_regclass('sqlscan.setting') IS NOT NULL THEN
        IF EXISTS (SELECT FROM sqlscan.setting WHERE key = 'schema_version' AND value IN ('1', '2')) THEN
            RETURN;
        END IF;
        RAISE EXCEPTION 'Unsupported SqlScan schema version';
    END IF;

    SET LOCAL search_path = pg_catalog;
    IF to_regnamespace('sqlscan') IS NULL THEN
        CREATE SCHEMA sqlscan;
    END IF;

    CREATE TABLE sqlscan.rule_category (
        id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
        name text NOT NULL UNIQUE CHECK (btrim(name) <> ''),
        description text NOT NULL
    );

    CREATE TABLE sqlscan.rule (
        id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
        category_id integer NOT NULL REFERENCES sqlscan.rule_category,
        pattern text COLLATE "C" NOT NULL UNIQUE CHECK (char_length(pattern) BETWEEN 1 AND 64),
        weight smallint NOT NULL CHECK (weight BETWEEN 1 AND 5),
        high_risk boolean NOT NULL,
        enabled boolean NOT NULL DEFAULT true,
        created_at timestamptz NOT NULL DEFAULT statement_timestamp(),
        updated_at timestamptz NOT NULL DEFAULT statement_timestamp()
    );

    CREATE FUNCTION sqlscan.prepare_rule() RETURNS trigger
    LANGUAGE plpgsql SET search_path = pg_catalog AS $fn$
    BEGIN
        -- Те же ASCII-буквы и шесть видов пробелов, что в SqlInjectionDetector::Normalize.
        NEW.pattern := btrim(regexp_replace(
            translate(NEW.pattern, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz'),
            E'[ \t\n\r\f\013]+', ' ', 'g'));
        IF TG_OP = 'UPDATE' THEN
            NEW.updated_at := statement_timestamp();
        END IF;
        RETURN NEW;
    END;
    $fn$;

    CREATE TRIGGER prepare_rule BEFORE INSERT OR UPDATE ON sqlscan.rule
        FOR EACH ROW EXECUTE FUNCTION sqlscan.prepare_rule();

    CREATE TABLE sqlscan.scan (
        id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
        started_at timestamptz NOT NULL DEFAULT statement_timestamp(),
        source_type text NOT NULL CHECK (source_type IN ('manual', 'file', 'batch')),
        source_name text,
        input_sha256 bytea NOT NULL CHECK (octet_length(input_sha256) = 32),
        input_bytes bigint NOT NULL CHECK (input_bytes >= 0),
        total_score integer NOT NULL CHECK (total_score >= 0),
        suspicious boolean NOT NULL,
        threshold_used integer NOT NULL CHECK (threshold_used > 0),
        truncated boolean NOT NULL
    );
    CREATE INDEX scan_started_at_idx ON sqlscan.scan (started_at DESC, id DESC);
    CREATE INDEX scan_suspicious_started_at_idx ON sqlscan.scan (suspicious, started_at DESC, id DESC);
    CREATE INDEX scan_input_sha256_idx ON sqlscan.scan (input_sha256);

    CREATE TABLE sqlscan.hit (
        id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
        scan_id bigint NOT NULL REFERENCES sqlscan.scan ON DELETE CASCADE,
        rule_id integer NOT NULL REFERENCES sqlscan.rule ON DELETE RESTRICT,
        line_no integer NOT NULL CHECK (line_no >= 1),
        position integer NOT NULL CHECK (position >= 0),
        pattern_applied varchar(64) NOT NULL CHECK (pattern_applied <> ''),
        weight_applied smallint NOT NULL CHECK (weight_applied BETWEEN 1 AND 5),
        high_risk_applied boolean NOT NULL,
        context varchar(200) NOT NULL
    );
    CREATE INDEX hit_scan_id_idx ON sqlscan.hit (scan_id);
    CREATE INDEX hit_rule_id_idx ON sqlscan.hit (rule_id);

    CREATE TABLE sqlscan.whitelist (
        id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
        fragment text NOT NULL UNIQUE CHECK (btrim(fragment) <> ''),
        comment text
    );

    CREATE TABLE sqlscan.setting (
        key text PRIMARY KEY CHECK (btrim(key) <> ''),
        value text NOT NULL
    );

    INSERT INTO sqlscan.rule_category (name, description) VALUES
        ('tautology', 'Логические условия и тавтологии'),
        ('union', 'Объединение результатов запросов'),
        ('comment', 'Маркеры комментариев'),
        ('stacked', 'Разделители последовательных команд'),
        ('time_based', 'Функции задержки и измерения времени'),
        ('os_command', 'Запуск команд операционной системы'),
        ('schema_info', 'Обращение к метаданным СУБД'),
        ('data_change', 'Изменение данных или структуры таблиц');

    INSERT INTO sqlscan.rule (category_id, pattern, weight, high_risk)
    SELECT c.id, seed.pattern, seed.weight, seed.high_risk
    FROM (VALUES
        ('tautology',   ''' or',              3, true),
        ('tautology',   '" or',               3, true),
        ('tautology',   'or 1=1',             4, true),
        ('tautology',   '1=1',                2, false),
        ('stacked',     ';',                  1, false),
        ('comment',     '--',                 3, true),
        ('comment',     '/*',                 2, false),
        ('comment',     '*/',                 2, false),
        ('stacked',     ';--',                4, true),
        ('union',       'union select',       5, true),
        ('time_based',  'sleep(',             4, true),
        ('time_based',  'benchmark(',         4, true),
        ('os_command',  'xp_cmdshell',        5, true),
        ('schema_info', 'information_schema', 3, false),
        ('data_change', 'drop table',         5, true),
        ('data_change', 'insert into',        3, false),
        ('data_change', 'update set',         3, false),
        ('data_change', 'delete from',        3, false)
    ) AS seed(category, pattern, weight, high_risk)
    JOIN sqlscan.rule_category AS c ON c.name = seed.category;

    INSERT INTO sqlscan.setting (key, value) VALUES
        ('schema_version', '1'),
        ('suspicious_score_threshold', '5'),
        ('max_analyze_input_bytes', '524288'),
        ('max_file_load_bytes', '2097152');
    PERFORM set_config('search_path', previous_search_path, true);
END;
$install$;

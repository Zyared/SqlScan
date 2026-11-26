#include "pch.h"
#include "Database.h"
#include "ReportIO.h"
#include "resource.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <wincrypt.h>
#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "crypt32.lib")

namespace {
void Check(SQLRETURN rc, SQLSMALLINT kind, SQLHANDLE handle) {
    if (SQL_SUCCEEDED(rc))
        return;
    SQLWCHAR state[6]{}, message[1024]{};
    SQLINTEGER native{};
    SQLSMALLINT n{};
    if (handle && SQL_SUCCEEDED(SQLGetDiagRecW(kind, handle, 1, state, &native, message, 1024, &n)))
        throw DbError(io::Utf8(reinterpret_cast<wchar_t *>(state)) + ": " +
                                 io::Utf8(reinterpret_cast<wchar_t *>(message)));
    throw DbError("Ошибка ODBC");
}
class Statement {
  public:
    SQLHSTMT value{};
    explicit Statement(SQLHDBC db) { Check(SQLAllocHandle(SQL_HANDLE_STMT, db, &value), SQL_HANDLE_DBC, db); }
    ~Statement() { SQLFreeHandle(SQL_HANDLE_STMT, value); }
};
std::wstring ResourceSql(int id) {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ResourceSql), &module))
        throw std::runtime_error("Schema module unavailable");
    auto res = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res)
        throw std::runtime_error("Ресурс схемы БД отсутствует");
    auto bytes = static_cast<const char *>(LockResource(LoadResource(module, res)));
    if (!bytes)
        throw std::runtime_error("Ошибка загрузки схемы");
    return io::Wide({bytes, SizeofResource(module, res)});
}
std::string Flag(bool v) { return v ? "true" : "false"; }
int PositiveSetting(const Row &setting, int maximum) {
    std::string_view text = setting[1];
    constexpr auto whitespace = " \t\r\n\v\f";
    const auto first = text.find_first_not_of(whitespace);
    if (first != text.npos) {
        text = text.substr(first, text.find_last_not_of(whitespace) - first + 1);
        if (text.front() == '+')
            text.remove_prefix(1);
        int value{};
        const auto end = text.data() + text.size();
        const auto [ptr, error] = std::from_chars(text.data(), end, value);
        if (ptr == end && error == std::errc{} && value > 0 && value <= maximum)
            return value;
    }
    throw DbError("Недопустимое значение настройки " + setting[0]);
}
constexpr wchar_t kSettings[] = L"Software\\SqlScanCoursework\\SqlScan2";
} // namespace
DbConnection::~DbConnection() { Close(); }
void DbConnection::Swap(DbConnection &other) noexcept {
    std::swap(env_, other.env_);
    std::swap(dbc_, other.dbc_);
    std::swap(loggedIn_, other.loggedIn_);
}
void DbConnection::Close() noexcept {
    if (dbc_) {
        if (loggedIn_)
            SQLDisconnect(dbc_);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc_);
    }
    if (env_)
        SQLFreeHandle(SQL_HANDLE_ENV, env_);
    dbc_ = nullptr;
    env_ = nullptr;
    loggedIn_ = false;
}
void DbConnection::Open(const std::wstring &connection) {
    Close();
    try {
        Check(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env_), SQL_HANDLE_ENV, env_);
        Check(SQLSetEnvAttr(env_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0),
              SQL_HANDLE_ENV, env_);
        Check(SQLAllocHandle(SQL_HANDLE_DBC, env_, &dbc_), SQL_HANDLE_ENV, env_);
        Check(SQLSetConnectAttrW(dbc_, SQL_LOGIN_TIMEOUT, reinterpret_cast<SQLPOINTER>(5), 0), SQL_HANDLE_DBC,
              dbc_);
        auto rc = SQLDriverConnectW(dbc_, nullptr, (SQLWCHAR *)connection.c_str(), SQL_NTS, nullptr, 0,
                                    nullptr, SQL_DRIVER_NOPROMPT);
        Check(rc, SQL_HANDLE_DBC, dbc_);
        loggedIn_ = true;
        Transaction([&] {
            Query(ResourceSql(IDR_SCHEMA));
            Query(ResourceSql(IDR_UPGRADE));
        });
    } catch (...) {
        Close();
        throw;
    }
}
Rows DbConnection::Query(const std::wstring &sql, const Row &args) {
    if (!Connected())
        throw DbError("Нет подключения к базе");
    Statement stmt(dbc_);
    Check(SQLPrepareW(stmt.value, (SQLWCHAR *)sql.c_str(), SQL_NTS), SQL_HANDLE_STMT, stmt.value);
    std::vector<std::wstring> values;
    for (const auto &a : args)
        values.push_back(io::Wide(a));
    std::vector<SQLLEN> sizes(values.size(), SQL_NTS);
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto &v = values[i];
        Check(SQLBindParameter(stmt.value, static_cast<SQLUSMALLINT>(i + 1), SQL_PARAM_INPUT, SQL_C_WCHAR,
                               v.size() > 4000 ? SQL_WLONGVARCHAR : SQL_WVARCHAR,
                               (std::max)(v.size(), std::size_t(1)), 0, v.data(),
                               static_cast<SQLLEN>((v.size() + 1) * sizeof(wchar_t)), &sizes[i]),
              SQL_HANDLE_STMT, stmt.value);
    }
    auto rc = SQLExecute(stmt.value);
    if (rc == SQL_NO_DATA)
        return {};
    Check(rc, SQL_HANDLE_STMT, stmt.value);
    SQLSMALLINT columns{};
    Check(SQLNumResultCols(stmt.value, &columns), SQL_HANDLE_STMT, stmt.value);
    Rows rows;
    if (!columns)
        return rows;
    while ((rc = SQLFetch(stmt.value)) != SQL_NO_DATA) {
        Check(rc, SQL_HANDLE_STMT, stmt.value);
        Row row;
        for (SQLUSMALLINT col = 1; col <= columns; ++col) {
            std::wstring value;
            for (;;) {
                wchar_t chunk[512]{};
                SQLLEN len{};
                rc = SQLGetData(stmt.value, col, SQL_C_WCHAR, chunk, sizeof(chunk), &len);
                if (rc == SQL_NO_DATA)
                    break;
                Check(rc, SQL_HANDLE_STMT, stmt.value);
                if (len == SQL_NULL_DATA)
                    break;
                value += chunk;
                if (rc == SQL_SUCCESS)
                    break;
            }
            row.push_back(io::Utf8(value));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
void DbConnection::Transaction(const std::function<void()> &work) {
    Check(SQLSetConnectAttrW(dbc_, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0), SQL_HANDLE_DBC,
          dbc_);
    try {
        work();
        Check(SQLEndTran(SQL_HANDLE_DBC, dbc_, SQL_COMMIT), SQL_HANDLE_DBC, dbc_);
    } catch (...) {
        if (!SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, dbc_, SQL_ROLLBACK)))
            Close();
        if (dbc_ &&
            !SQL_SUCCEEDED(SQLSetConnectAttrW(dbc_, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0)))
            Close();
        throw;
    }
    if (!SQL_SUCCEEDED(SQLSetConnectAttrW(dbc_, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0)))
        Close();
}
Rows RuleRepository::Rules() {
    return db_.Query(L"SELECT r.id,r.pattern,r.weight,r.high_risk::int,r.enabled::int,c.name,c.id FROM "
                     L"sqlscan.rule r JOIN sqlscan.rule_category c ON c.id=r.category_id ORDER BY r.id");
}
Rows RuleRepository::Categories() {
    return db_.Query(L"SELECT id,description FROM sqlscan.rule_category ORDER BY id");
}
SqlInjectionDetector RuleRepository::Detector() { return SqlInjectionDetector(LoadActiveRules()); }
DetectorSettings RuleRepository::LoadActiveRules() {
    DetectorSettings result;
    for (const auto &a : Rules())
        if (a[4] == "1")
            result.rules.push_back({a[1], std::stoi(a[2]), a[3] == "1", std::stoi(a[0]), a[5]});
    constexpr int maximumBytes = 2 * 1024 * 1024;
    for (const auto &a : db_.Query(L"SELECT key,value FROM sqlscan.setting")) {
        if (a[0] == "suspicious_score_threshold")
            result.threshold = PositiveSetting(a, (std::numeric_limits<int>::max)());
        else if (a[0] == "max_analyze_input_bytes")
            result.maxBytes = PositiveSetting(a, maximumBytes);
        else if (a[0] == "max_file_load_bytes")
            result.maxFileBytes = PositiveSetting(a, maximumBytes);
    }
    return result;
}
void RuleRepository::EditRule(int id, int category, std::string pattern, int weight, bool high,
                              bool enabled) {
    Row params = {std::to_string(category), std::move(pattern), std::to_string(weight), Flag(high),
                  Flag(enabled)};
    if (!id)
        db_.Query(L"INSERT INTO sqlscan.rule(category_id,pattern,weight,high_risk,enabled) VALUES "
                  L"(?::int,?,?::smallint,?::boolean,?::boolean)",
                  params);
    else {
        params.push_back(std::to_string(id));
        if (db_.Query(
                   L"UPDATE sqlscan.rule SET "
                   L"category_id=?::int,pattern=?,weight=?::smallint,high_risk=?::boolean,enabled=?::boolean "
                   L"WHERE id=?::int RETURNING id",
                   params)
                .empty())
            throw std::runtime_error("Правило уже удалено");
    }
}
void RuleRepository::DeleteRule(int id) {
    db_.Query(L"DELETE FROM sqlscan.rule WHERE id=?::int", {std::to_string(id)});
}
std::string ScanRepository::Previous(std::string hash) {
    auto rows = db_.Query(L"SELECT id FROM sqlscan.scan WHERE input_sha256=decode(?,'hex') AND "
                          L"source_type='file' ORDER BY started_at DESC,id DESC LIMIT 1",
                          {hash});
    return rows.empty() ? "" : rows[0][0];
}
std::string ScanRepository::Save(const DetectionReport &r, std::string type, std::string name,
                                 std::string hash, std::size_t sourceBytes) {
    std::string id;
    db_.Transaction([&] {
        id =
            db_.Query(
                   L"INSERT INTO "
                   L"sqlscan.scan(source_type,source_name,input_sha256,input_bytes,total_score,suspicious,"
                   L"threshold_used,truncated,risk,line_count,hit_count,analyzed_bytes) VALUES "
                   L"(?,?,decode(?,'hex'),?::bigint,?::int,?::boolean,?::int,?::boolean,?,?::int,?::bigint,?:"
                   L":bigint) RETURNING id",
                   {type, name, hash, std::to_string(sourceBytes), std::to_string(r.totalScore),
                    Flag(r.suspicious), std::to_string(r.threshold), Flag(r.inputTruncated), RiskLevel(r),
                    std::to_string(r.lines.size()), std::to_string(r.totalHits),
                    std::to_string(r.analyzedInputBytes)})
                .at(0)
                .at(0);
        std::string data = "[";
        std::size_t pending = 0;
        auto flush = [&] {
            if (!pending)
                return;
            data += ']';
            db_.Query(
                L"INSERT INTO "
                L"sqlscan.hit(scan_id,rule_id,line_no,position,pattern_applied,weight_applied,high_risk_"
                L"applied,context,category_applied) SELECT "
                L"?::bigint,h.rule_id,h.line_no,h.pos,h.pattern,h.weight,h.high,h.ctx,h.category FROM "
                L"jsonb_to_recordset(?::jsonb) AS h(rule_id int,line_no int,pos int,pattern text,weight "
                L"int,high boolean,ctx text,category text)",
                {id, data});
            data.assign("[");
            pending = 0;
        };
        r.ForEachHit([&](const DetectionHit &h) {
            if (pending)
                data += ',';
            data += "{\"rule_id\":" + std::to_string(h.ruleId) +
                    ",\"line_no\":" + std::to_string(h.line) + ",\"pos\":" + std::to_string(h.position) +
                    ",\"weight\":" + std::to_string(h.weight) + ",\"pattern\":" + io::Json(h.pattern) +
                    ",\"high\":" + Flag(h.highRisk) + ",\"ctx\":" + io::Json(h.context) +
                    ",\"category\":" + io::Json(h.category) + "}";
            if (++pending == 500)
                flush();
        });
        flush();
    });
    return id;
}
Rows ScanRepository::History(const HistoryFilter &f, int offset, int limit) {
    static const std::array<const wchar_t *, 7> order = {L"s.id",          L"s.started_at", L"s.source_type",
                                                         L"s.source_name", L"s.suspicious", L"s.total_score",
                                                         L"CASE s.risk WHEN 'Низкий' THEN 1 WHEN 'Средний' THEN 2 "
                                                         L"WHEN 'Высокий' THEN 3 WHEN 'Критический' THEN 4 END"};
    std::wstring sql =
        L"SELECT s.id,to_char(s.started_at,'YYYY-MM-DD HH24:MI:SS'),s.source_type,s.source_name,CASE WHEN "
        L"s.suspicious THEN 'Да' ELSE 'Нет' END,s.total_score,s.risk FROM sqlscan.scan s WHERE true";
    Row args;
    auto filter = [&](const std::string &value, const wchar_t *clause) {
        if (!value.empty()) {
            sql += clause;
            args.push_back(value);
        }
    };
    filter(f.text, L" AND strpos(lower(coalesce(s.source_name,'')),lower(?))>0");
    filter(f.from, L" AND s.started_at>=?::date");
    filter(f.until, L" AND s.started_at<(?::date + 1)");
    filter(f.verdict, L" AND s.suspicious=?::boolean");
    filter(f.minScore, L" AND s.total_score>=?::int");
    filter(f.source, L" AND s.source_type=?");
    sql += L" ORDER BY ";
    sql += order.at(f.sortColumn);
    sql += f.ascending ? L" ASC" : L" DESC";
    sql += L" NULLS LAST,s.id DESC LIMIT ?::int OFFSET ?::int";
    args.push_back(std::to_string(limit));
    args.push_back(std::to_string(offset));
    return db_.Query(sql, args);
}
Rows ScanRepository::ScanSummary(std::string id) {
    return db_.Query(
        L"SELECT s.id,to_char(started_at,'YYYY-MM-DD "
        L"HH24:MI:SS'),source_type,source_name,total_score,threshold_used,suspicious::int,risk,line_"
        L"count,hit_count,input_bytes,analyzed_bytes,truncated::int,encode(input_sha256,'hex'), "
        L"(SELECT count(*) FROM sqlscan.hit h WHERE h.scan_id=s.id) FROM "
        L"sqlscan.scan s WHERE id=?::bigint",
        {id});
}
Rows ScanRepository::ScanLines(std::string id, int limit) {
    return db_.Query(LR"sql(
        WITH target AS (
            SELECT id, threshold_used, least(line_count, nullif(?::int, -1)) AS lines
            FROM sqlscan.scan s
            WHERE id = ?::bigint AND line_count IS NOT NULL AND
                hit_count = (SELECT count(*) FROM sqlscan.hit WHERE scan_id = s.id)
        ), scores AS (
            SELECT h.line_no, least(sum(h.weight_applied), 2147483647) AS score,
                   bool_or(h.high_risk_applied) AS high
            FROM target t JOIN sqlscan.hit h ON h.scan_id = t.id
            WHERE h.line_no <= t.lines GROUP BY h.line_no
        )
        SELECT n, coalesce(score, 0),
               (coalesce(high, false) OR coalesce(score, 0) >= threshold_used)::int
        FROM target CROSS JOIN LATERAL generate_series(1, lines) AS line(n)
        LEFT JOIN scores ON line_no = n ORDER BY n
    )sql", {std::to_string(limit), id});
}
Rows ScanRepository::ScanHits(std::string id, int limit) {
    return db_.Query(
        L"SELECT "
        L"line_no,position,pattern_applied,weight_applied,high_risk_applied::int,context,category_"
        L"applied FROM sqlscan.hit WHERE scan_id=?::bigint ORDER BY line_no,position,id LIMIT NULLIF(?::int,-1)",
        {id, std::to_string(limit)});
}
void ScanRepository::DeleteScan(std::string id) {
    db_.Query(L"DELETE FROM sqlscan.scan WHERE id=?::bigint", {id});
}
Rows ScanRepository::Days() {
    return db_.Query(L"SELECT started_at::date,count(*),count(*) FILTER(WHERE suspicious) FROM sqlscan.scan "
                     L"GROUP BY 1 ORDER BY 1 DESC LIMIT 30");
}
Rows ScanRepository::TopRules() {
    return db_.Query(L"SELECT r.pattern, count(*) FROM sqlscan.hit h JOIN sqlscan.rule r ON r.id=h.rule_id "
                     L"GROUP BY r.id ORDER BY count(*) DESC, r.pattern LIMIT 10");
}
Rows ScanRepository::Totals() {
    return db_.Query(L"SELECT count(*),count(*) FILTER(WHERE suspicious) FROM sqlscan.scan");
}
std::wstring SavedConnection() {
    DWORD n = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettings, L"Connection", RRF_RT_REG_BINARY, nullptr, nullptr, &n) !=
        ERROR_SUCCESS)
        return {};
    std::vector<BYTE> bytes(n);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettings, L"Connection", RRF_RT_REG_BINARY, nullptr, bytes.data(),
                     &n) != ERROR_SUCCESS)
        return {};
    DATA_BLOB in{n, bytes.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw std::runtime_error("Не удалось прочитать настройки DPAPI");
    std::wstring value(reinterpret_cast<wchar_t *>(out.pbData), out.cbData / sizeof(wchar_t));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return value;
}
void SaveConnection(const std::wstring &s) {
    if (s.empty()) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettings, L"Connection");
        return;
    }
    DATA_BLOB in{static_cast<DWORD>(s.size() * sizeof(wchar_t)), (BYTE *)s.data()}, out{};
    if (!CryptProtectData(&in, L"SqlScan", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        throw std::runtime_error("Ошибка сохранения настроек DPAPI");
    auto result =
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettings, L"Connection", REG_BINARY, out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Не удалось записать настройки");
}

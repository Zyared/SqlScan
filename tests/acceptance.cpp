#include "pch.h"
#include "Database.h"
#include "Checks.h"
#include "ReportIO.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
using Clock = std::chrono::steady_clock;
void Require(bool condition, const char *name) {
    if (!condition)
        throw std::runtime_error(name);
}
void CheckProtectedSettings() {
    const auto keyName = L"Software\\SqlScan-QA-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                         std::to_wstring(GetTickCount64());
    HKEY root{};
    DWORD disposition{};
    Require(RegCreateKeyExW(HKEY_CURRENT_USER, keyName.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, nullptr, &root, &disposition) == ERROR_SUCCESS &&
                disposition == REG_CREATED_NEW_KEY, "private test registry root");
    const auto restore = [&] {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, keyName.c_str());
    };
    try {
        Require(RegOverridePredefKey(HKEY_CURRENT_USER, root) == ERROR_SUCCESS, "isolate HKCU");
        const std::wstring sample = L"DSN=SqlScan-QA;Uid=fixture;";
        SaveConnection(sample);
        Require(SavedConnection() == sample, "DPAPI round trip");
        SaveConnection(L"");
        Require(SavedConnection().empty(), "reset protected settings");
    } catch (...) {
        restore();
        throw;
    }
    restore();
}
void CheckCore() {
    CheckProtectedSettings();
    SqlInjectionDetector stock;
    Require(stock.Rules().size() == 18, "built-in rule count");
    Require(!stock.Analyze("catalog item").suspicious, "benign input");
    auto split = stock.Analyze("x; ; ;\r\ny; ;");
    Require(!split.suspicious && split.totalScore == 5 && split.lines.size() == 2, "per-line thresholds");
    auto match = stock.Analyze("ok\r\nitem;--");
    Require(match.suspicious && match.hits[0].line == 2, "CRLF line coordinates");
    SqlInjectionDetector marker({{"marker token", 5, false, 1, "test"}}, 5, 128);
    for (auto s :
         {"MARKER\tTOKEN", "mar/**/ker token", "marker%2520token", "marker&#32;token", "marker+token"})
        Require(marker.Analyze(s).suspicious, "normalization");
    Require(!marker.Analyze("marker\ntoken").suspicious, "cross-line boundary");
    const std::string call = "timing: sleep ( 0";
    auto layout = stock.Analyze(call);
    Require(layout.totalHits == 1 && layout.totalScore == 4 && layout.hits[0].position == 8 &&
            layout.hits[0].matchedBytes == 7 && !stock.Analyze(call, true).suspicious, "call layout and legacy");
    const auto span = io::HighRiskSpans(layout, 200);
    Require(span.size() == 1 && span.front().begin == 8 && span.front().end == 15, "actual spelling highlighted");
    Require(!stock.Analyze("notes: asleep ( 0").suspicious && !stock.Analyze("sleep ( hours").suspicious,
            "identifier and argument boundaries");
    Require(!stock.Analyze("union all select name,city").suspicious &&
            !stock.Analyze("union all select null,null_key").suspicious, "ordinary set operations");
    SqlInjectionDetector literal({{"sleep(", 4, true, 1, "custom"}}, 5, 128);
    Require(!literal.Analyze(call).suspicious, "custom-category literal keeps literal matching");
    for (bool legacy : {false, true}) {
        auto report = stock.Analyze(std::string(kMaxSavedHits + 1, ';') + " sleep ( 0", legacy);
        std::size_t count = 0, expanded = 0;
        report.ForEachHit([&](const DetectionHit &h) {
            ++count;
            if (h.pattern == "sleep(") ++expanded;
        });
        Require(count == report.totalHits && expanded == (legacy ? 0u : 1u), "dense replay respects matching mode");
    }
    const std::string projected = "(select case when (2 < 3) then 4 else 8 end)";
    const auto value = "fixture) and ? + " + projected;
    auto structured = stock.Analyze(value);
    Require(structured.totalHits == 1 && structured.totalScore == 4 && structured.suspicious &&
            structured.contributions[0].pattern == structured.hits[0].pattern &&
            structured.hits[0].position == structured.normalizedInput.find("and") &&
            structured.hits[0].position + structured.hits[0].matchedBytes == structured.normalizedInput.size(),
            "auditable scalar evidence and span");
    Require(!stock.Analyze(value, true).suspicious && !stock.Analyze("SELECT\n" + value).suspicious &&
            !stock.Analyze("fixture) and '" + projected + "'").suspicious, "legacy, multiline and quoted text");
    auto records = stock.AnalyzeLog("ordinary record\r\n" + value + "\n" + value);
    Require(records.totalHits == 2 && records.totalScore == 8 && !records.lines[0].suspicious &&
                records.hits[0].line == 2 && records.hits[1].line == 3 &&
                records.contributions.size() == 1 && records.contributions[0].occurrences == 2,
            "independent log records and grouped score contributions");
    const std::string denseLog = std::string(kMaxSavedHits, ';') + "\r\n" + value;
    auto replay = stock.AnalyzeLog(denseLog);
    std::size_t enumerated = 0;
    replay.ForEachHit([&](const DetectionHit &hit) {
        if (++enumerated == kMaxSavedHits + 1)
            Require(hit.line == 2 && hit.pattern == structured.hits[0].pattern &&
                        hit.position == structured.hits[0].position, "dense log structural detail");
    });
    Require(enumerated == replay.totalHits && replay.totalHits == kMaxSavedHits + 1, "dense log count");
    SqlInjectionDetector capped(stock.Rules(), 5, value.size());
    Require(capped.AnalyzeLog(value + "\nnext record").suspicious &&
                !capped.AnalyzeLog(value + " unfinished").suspicious, "complete log record at byte cap");
    Require(marker.Analyze(std::string("marker\0token", 12)).suspicious,
            "embedded NUL normalized consistently in the public engine");
    SqlInjectionDetector adjusted({{"or 1=1", 2, false, 17, "tautology"}}, 5, 1024);
    auto weighted = adjusted.Analyze(value);
    Require(weighted.hits[0].ruleId == 17 && weighted.totalScore == 2 && !weighted.suspicious,
            "structural evidence respects configured weight and risk");
    SqlInjectionDetector disabled({{"or 1=1", 4, true, 17, "tautology", false}}, 5, 1024);
    Require(disabled.Analyze(value).totalHits == 0, "disabled rule disables its variant");
    auto dense = stock.Analyze(std::string(kMaxSavedHits + 2, ';'));
    Require(dense.hits.size() == kMaxSavedHits && dense.totalHits == kMaxSavedHits + 2 &&
                dense.totalScore == static_cast<int>(dense.totalHits),
            "bounded details preserve the total");
    std::size_t streamed = 0;
    dense.ForEachHit([&](const DetectionHit &hit) {
        Require(hit.position == streamed++ && hit.line == 1, "complete dense report positions");
    });
    Require(streamed == dense.totalHits, "all dense details streamed");
    SqlInjectionDetector plus({{"a+b c", 5, false, 2, "text"}}, 5, 128);
    Require(plus.Analyze("a%2Bb%2520c").suspicious, "percent-encoded literal plus survives the second pass");
    auto edge = stock.Analyze(std::string(kMaxAnalyzeInputBytes-1, 'x') + "🙂;");
    Require(edge.inputTruncated && edge.analyzedInputBytes == kMaxAnalyzeInputBytes-1 &&
            edge.totalHits == 0, "UTF-8 boundary at input cap");
    auto explained = stock.Analyze(";--");
    const auto proof = io::Report(explained, "txt");
    Require(proof.find("1 × 4 = 4") != std::string::npos && proof.find("8 ≥ 5") != std::string::npos &&
            proof.find("сигнатура высокого риска") != std::string::npos, "human-readable score calculation");
    auto spans = io::HighRiskSpans(stock.Analyze("текст\nabc;--"), 4096);
    Require(spans.size() == 1 && spans.front().end - spans.front().begin == 3, "merged highlight spans");
    auto bounded = stock.Analyze(std::string(kMaxAnalyzeInputBytes + 1, 'a'));
    Require(bounded.inputTruncated && bounded.analyzedInputBytes == kMaxAnalyzeInputBytes, "input cap");
    Require(io::Hash("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256");
    Require(io::DecodeFile("\xef\xbb\xbf"
                           "abc") == "abc",
            "BOM");
    Require(io::DecodeFile(std::string("a\0b", 3)) == "a b", "NUL conversion");
    Require(io::Html("<&>") == "&lt;&amp;&gt;" && io::Csv("=1") == "\"'=1\"", "report escaping");
    auto escaped = SqlInjectionDetector({{"<m>", 3, true, 1, "section&name"}}, 5, 256)
                       .Analyze("one\n<m><m>\nthree");
    auto clipped = io::HighRiskSpans(escaped, 8);
    Require(clipped.size() == 1 && clipped.front().begin == 4 && clipped.front().end == 8,
            "adjacent highlight spans clipped to preview");
    for (const auto fmt : {"html", "csv", "txt"}) {
        std::ostringstream stream;
        io::WriteReport(stream, escaped, fmt);
        Require(stream.str() == io::Report(escaped, fmt), "streamed full report");
    }
    const auto html = io::Report(escaped, "html");
    Require(html.find("<mark>&lt;m&gt;&lt;m&gt;</mark>") != std::string::npos &&
            html.find("section&amp;name") != std::string::npos, "HTML escape before highlighting");
    std::string input(kMaxAnalyzeInputBytes, 'a');
    auto start = Clock::now();
    auto large = stock.Analyze(input);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    std::cout << "analyze_512KiB_ms=" << ms << "\n";
    Require(ms <= 100 && !large.suspicious, "512 KiB time limit");
    auto stressStart = Clock::now();
    auto stress = stock.Analyze(std::string(kMaxAnalyzeInputBytes, ';'));
    auto stressMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - stressStart).count();
    Require(stress.totalHits == kMaxAnalyzeInputBytes && stress.totalScore == static_cast<int>(stress.totalHits),
            "maximum-density score and hits");
    std::cout << "dense_512KiB_ms=" << stressMs << "\n";
    Require(stressMs <= 100, "512 KiB dense analysis time");
}
bool CheckDatabase() {
    wchar_t *raw = nullptr;
    std::size_t n = 0;
    _wdupenv_s(&raw, &n, L"SQLSCAN_TEST_CONNECTION");
    if (!raw)
        return false;
    {
        std::wstring connection(raw);
        free(raw);
        DbConnection db;
        RuleRepository rules(db);
        ScanRepository scans(db);
        db.Open(connection);
        db.Transaction([&] {
            const auto old = db.Query(L"SELECT value FROM sqlscan.setting WHERE key='suspicious_score_threshold'").at(0);
            const auto setThreshold = L"UPDATE sqlscan.setting SET value=? WHERE key='suspicious_score_threshold'";
            db.Query(setThreshold, {" +7 "});
            Require(rules.LoadActiveRules().threshold == 7, "decimal threshold with whitespace");
            db.Query(setThreshold, {"7 points"});
            bool badSettingRejected = false;
            try {
                rules.LoadActiveRules();
            } catch (const DbError &) {
                badSettingRejected = true;
            }
            Require(badSettingRejected, "reject partially numeric stored threshold");
            db.Query(setThreshold, old);
        });
        auto detector = rules.Detector();
        const std::string markerValue = "fixture) and ? + (select case when (2 < 3) then 4 else 8 end)";
        auto nested = detector.Analyze(markerValue);
        Require(nested.totalScore == 4 && nested.hits.size() == 1 && nested.hits[0].ruleId > 0,
                "configured rule owns structural evidence");
        auto nestedId = scans.Save(nested, "manual", "", io::Hash(markerValue), markerValue.size());
        auto details = scans.ScanHits(nestedId);
        Require(details.size() == 1 && details[0][2] == nested.hits[0].pattern && details[0][3] == "4",
                "historical snapshot stores structural reason");
        Require(scans.ScanLines(nestedId) == Rows{{"1", "4", "1"}}, "archived high-risk hit below threshold");
        scans.DeleteScan(nestedId);
        auto unique = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        auto pattern = "sqlscan-test-" + unique;
        auto cats = rules.Categories();
        rules.EditRule(0, std::stoi(cats.at(0)[0]), pattern, 3, false, true);
        auto active = rules.Detector();
        auto test = active.Analyze(pattern);
        Require(test.totalHits == 1, "custom rule");
        int ruleId = test.hits.at(0).ruleId;
        auto id = scans.Save(test, "file", unique, io::Hash(pattern), pattern.size());
        Require(scans.Previous(io::Hash(pattern)) == id, "file deduplication");
        const auto renamed = pattern + "-new";
        rules.EditRule(ruleId, std::stoi(cats.at(0)[0]), renamed, 3, false, true);
        const auto input = renamed + "\nclean\n" + renamed + " " + renamed;
        const auto multiline = rules.Detector().Analyze(input);
        const auto multilineId = scans.Save(multiline, "manual", unique, io::Hash(input), input.size());
        const Rows expectedLines = {{"1", "3", "0"}, {"2", "0", "0"}, {"3", "6", "1"}};
        Require(scans.ScanLines(multilineId) == expectedLines &&
                scans.ScanLines(multilineId, 1) == Rows{expectedLines.front()}, "archived line scores and preview");
        const auto top = scans.TopRules();
        Require(std::find(top.begin(), top.end(), Row{renamed, "4"}) != top.end(),
                "statistics combines matches before and after a rule rename");
        rules.EditRule(ruleId, std::stoi(cats.at(0)[0]), pattern, 4, true, false);
        Require(scans.ScanLines(multilineId) == expectedLines, "archived lines retain applied rule settings");
        db.Query(L"UPDATE sqlscan.scan SET hit_count=4 WHERE id=?::bigint", {multilineId});
        Require(scans.ScanLines(multilineId).empty(), "do not invent line scores for a partial archive");
        scans.DeleteScan(multilineId);
        Require(rules.Detector().Analyze(pattern).hits.empty(), "disabled rule");
        Require(scans.ScanHits(id).at(0)[3] == "3", "saved weight");
        bool rejected = false;
        try {
            rules.DeleteRule(ruleId);
        } catch (const DbError &error) {
            Require(std::string(error.what()).size() > 5, "ODBC diagnostics");
            rejected = true;
        }
        Require(rejected, "historical rule deletion protection");
        HistoryFilter f;
        f.text = unique;
        auto count = scans.History(f, 0).size();
        Require(count == 1, "file name filter");
        auto filtered = f;
        filtered.verdict = "false";
        filtered.source = "file";
        filtered.minScore = "3";
        filtered.from = db.Query(L"SELECT CURRENT_DATE::text")[0][0];
        filtered.until = filtered.from;
        for (int column = 0; column < 7; ++column) {
            filtered.sortColumn = column;
            filtered.ascending = column % 2 == 0;
            Require(scans.History(filtered, 0, 1).size() == 1, "combined history filters and sorting");
        }
        filtered.minScore = "4";
        Require(scans.History(filtered, 0).empty() && scans.History(f, 1).empty(), "minimum score and offset");
        HistoryFilter byRisk;
        byRisk.text = unique + "-severity";
        byRisk.sortColumn = 6;
        db.Query(L"INSERT INTO sqlscan.scan(source_type, source_name, input_sha256, input_bytes, total_score, "
                 L"suspicious, threshold_used, truncated, risk) SELECT 'manual', ?, decode(repeat('00',32),'hex'), "
                 L"0, 0, false, 5, false, risk FROM (VALUES (NULL),('Критический'),('Низкий'),('Высокий'),"
                 L"('Средний')) AS fixture(risk)", {byRisk.text});
        byRisk.ascending = true;
        const auto asc = scans.History(byRisk, 0);
        byRisk.ascending = false;
        const auto desc = scans.History(byRisk, 0);
        const Row severity{"Низкий", "Средний", "Высокий", "Критический"};
        Require(asc.size() == 5 && desc.size() == 5 && asc.back()[6].empty() && desc.back()[6].empty(),
                "unknown historical risk follows known levels");
        for (std::size_t i = 0; i < severity.size(); ++i)
            Require(asc[i][6] == severity[i] && desc[i][6] == severity[severity.size() - i - 1],
                    "journal sorts by severity");
        Require(scans.ScanLines(asc.back()[0]).empty(), "unknown legacy line count stays unavailable");
        db.Query(L"DELETE FROM sqlscan.scan WHERE source_name=?", {byRisk.text});
        test.hits.at(0).ruleId = -1;
        rejected = false;
        try {
            scans.Save(test, "file", unique, io::Hash(pattern), pattern.size());
        } catch (const std::exception &) {
            rejected = true;
        }
        Require(rejected && scans.History(f, 0).size() == count, "atomic save rollback");
        scans.DeleteScan(id);
        Require(scans.ScanHits(id).empty(), "cascade delete");
        rules.DeleteRule(ruleId);
        std::string many(1000, ';');
        auto hits = detector.Analyze(many);
        Require(hits.hits.size() == 1000, "1000 matches");
        auto start = Clock::now();
        id = scans.Save(hits, "manual", unique, io::Hash(many), many.size());
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        Require(scans.ScanHits(id).size() == 1000 && ms <= 1000, "1000-hit persistence");
        std::cout << "save_1000_hits_ms=" << ms << "\n";
        scans.DeleteScan(id);
        std::string denseInput(kMaxSavedHits + 37, ';');
        auto dense = detector.Analyze(denseInput);
        auto denseId = scans.Save(dense, "manual", unique, io::Hash(denseInput), denseInput.size());
        const auto detail = db.Query(L"SELECT count(*),max(position) FROM sqlscan.hit WHERE scan_id=?::bigint", {denseId});
        Require(std::stoull(detail[0][0]) == dense.totalHits &&
                std::stoull(detail[0][1]) == denseInput.size() - 1, "dense SQL history is complete");
        const auto fullCsv = io::Report(dense, "csv");
        Require(fullCsv.find("1," + std::to_string(denseInput.size() - 1) + ",") != std::string::npos,
                "dense export includes the final position");
        scans.DeleteScan(denseId);
        start = Clock::now();
        auto page = scans.History({}, 0, 50);
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        Require(ms <= 1000, "journal page time");
        std::cout << "history_page_ms=" << ms << " returned=" << page.size() << "\n";
        std::cout << "PostgreSQL / ODBC checks passed\n";
    }
    return true;
}
#ifndef SQLSCAN_NATIVE_TEST
int main() {
    try {
        CheckCore();
        CheckDatabase();
        std::cout << "Core checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

#endif

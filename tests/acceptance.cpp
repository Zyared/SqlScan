#include "pch.h"
#include "Database.h"
#include "Checks.h"
#include "ReportIO.h"
#include <algorithm>
#include <chrono>
#include <iostream>
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
        auto detector = rules.Detector();
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
        rules.EditRule(ruleId, std::stoi(cats.at(0)[0]), pattern, 4, true, false);
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

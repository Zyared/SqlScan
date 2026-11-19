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
void CheckCore() {
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
        } catch (const std::exception &) {
            rejected = true;
        }
        Require(rejected, "historical rule deletion protection");
        HistoryFilter f;
        f.text = unique;
        auto count = scans.History(f, 0).size();
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

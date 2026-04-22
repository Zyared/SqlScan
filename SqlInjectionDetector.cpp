#include "pch.h"
#include "SqlInjectionDetector.h"

#include <cstring>

namespace {

bool IsSpaceChar(const char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

char ToLowerAscii(const char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

} // namespace

SqlInjectionDetector::SqlInjectionDetector() {
    rules_ = {
        {"' or",              3, true},
        {"\" or",             3, true},
        {"or 1=1",            4, true},
        {"1=1",               2, false},
        {";",                 1, false},
        {"--",                3, true},
        {"/*",                2, false},
        {"*/",                2, false},
        {";--",               4, true},
        {"union select",      5, true},
        {"sleep(",            4, true},
        {"benchmark(",        4, true},
        {"xp_cmdshell",       5, true},
        {"information_schema",3, false},
        {"drop table",        5, true},
        {"insert into",       3, false},
        {"update set",        3, false},
        {"delete from",       3, false},
    };
}

DetectionReport SqlInjectionDetector::Analyze(std::string_view input) const {
    DetectionReport report;
    report.originalInputBytes = input.size();

    std::string_view chunk = input;
    if (chunk.size() > kMaxAnalyzeInputBytes) {
        chunk = chunk.substr(0, kMaxAnalyzeInputBytes);
        report.inputTruncated = true;
    }
    report.analyzedInputBytes = chunk.size();
    report.normalizedInput = Normalize(chunk);

    bool hasHighRiskPattern = false;
    for (const PatternRule& rule : rules_) {
        const std::vector<std::size_t> positions =
            FindAllOccurrences(report.normalizedInput, rule.pattern);
        for (const std::size_t pos : positions) {
            report.hits.push_back({rule.pattern, pos, rule.weight, rule.highRisk});
            report.totalScore += rule.weight;
            if (rule.highRisk) {
                hasHighRiskPattern = true;
            }
        }
    }

    report.suspicious =
        hasHighRiskPattern || report.totalScore >= kSuspiciousScoreThreshold;
    return report;
}

std::string SqlInjectionDetector::Normalize(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    bool previousWasSpace = false;
    for (const char raw : input) {
        const char c = ToLowerAscii(raw);
        if (IsSpaceChar(c)) {
            if (!previousWasSpace) {
                result.push_back(' ');
                previousWasSpace = true;
            }
            continue;
        }
        result.push_back(c);
        previousWasSpace = false;
    }

    if (!result.empty() && result.front() == ' ') {
        result.erase(result.begin());
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

std::vector<std::size_t> SqlInjectionDetector::FindAllOccurrences(
    std::string_view text,
    std::string_view pattern) {
    std::vector<std::size_t> positions;
    // Guard against unsigned underflow: text.size() - pattern.size() wraps if pattern is longer
    if (pattern.empty() || text.size() < pattern.size()) {
        return positions;
    }

    const char* const hay = text.data();
    const char* const ndl = pattern.data();
    const std::size_t plen = pattern.size();
    const std::size_t last = text.size() - plen;
    for (std::size_t i = 0; i <= last; ++i) {
        if (memcmp(hay + i, ndl, plen) == 0) {
            positions.push_back(i);
        }
    }

    return positions;
}

#include "SqlInjectionDetector.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace {
bool Space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

int Hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Decode ASCII syntax only. Keep non-ASCII URL octets intact so a malformed
// escape cannot inject an invalid UTF-8 byte into a saved report.
std::string Decode(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '%' && i + 2 < input.size()) {
            int a = Hex(input[i + 1]), b = Hex(input[i + 2]);
            if (a >= 0 && a < 8 && b >= 0) {
                c = static_cast<char>(16 * a + b);
                i += 2;
            }
        } else if (c == '+') {
            c = ' ';
        } else if (c == '&') {
            const auto token = input.substr(i, 10);
            auto end = token.find(';');
            if (end != std::string_view::npos) {
                auto entity = token.substr(1, end - 1);
                unsigned value = 0;
                if (entity == "apos")
                    value = 39;
                else if (entity == "quot")
                    value = 34;
                else if (entity == "amp")
                    value = 38;
                else if (entity.starts_with('#')) {
                    entity.remove_prefix(1);
                    bool hex = entity.starts_with('x') || entity.starts_with('X');
                    if (hex)
                        entity.remove_prefix(1);
                    auto r =
                        std::from_chars(entity.data(), entity.data() + entity.size(), value, hex ? 16 : 10);
                    if (r.ec != std::errc{} || r.ptr != entity.data() + entity.size())
                        value = 0;
                }
                if (value > 0 && value < 128) {
                    c = static_cast<char>(value);
                    i += end;
                }
            }
        }
        out += c == '\0' ? ' ' : c;
    }
    return out;
}

bool Word(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::string Normalize(std::string_view input, bool legacy) {
    std::string decoded;
    if (!legacy && input.find_first_of("%+&") != input.npos) {
        decoded = Decode(input);
        if (decoded.find_first_of("%&") != decoded.npos)
            decoded = Decode(decoded);
        input = decoded;
    }
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (!legacy && c == '/' && i > 0 && i + 4 < input.size() && input.substr(i, 4) == "/**/" &&
            Word(input[i - 1]) && Word(input[i + 4])) {
            i += 3;
            continue;
        }
        if (Space(c)) {
            if (!out.empty() && out.back() != ' ')
                out += ' ';
        } else {
            out += c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c;
        }
    }
    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}
bool Continuation(char c) { return (static_cast<unsigned char>(c) & 0xc0) == 0x80; }

std::string Context(const std::string &line, std::size_t pos) {
    auto begin = pos > 48 ? pos - 48 : 0;
    auto end = (std::min)(line.size(), pos + 144);
    while (begin < pos && Continuation(line[begin]))
        ++begin;
    while (end < line.size() && Continuation(line[end]))
        --end;
    return line.substr(begin, end - begin);
}
} // namespace

DetectorSettings BuiltinRuleSource::LoadActiveRules() {
    return {{{"' or", 3, true, 0, "tautology"},
             {"\" or", 3, true, 0, "tautology"},
             {"or 1=1", 4, true, 0, "tautology"},
             {"1=1", 2, false, 0, "tautology"},
             {";", 1, false, 0, "stacked"},
             {"--", 3, true, 0, "comment"},
             {"/*", 2, false, 0, "comment"},
             {"*/", 2, false, 0, "comment"},
             {";--", 4, true, 0, "stacked"},
             {"union select", 5, true, 0, "union"},
             {"sleep(", 4, true, 0, "time_based"},
             {"benchmark(", 4, true, 0, "time_based"},
             {"xp_cmdshell", 5, true, 0, "os_command"},
             {"information_schema", 3, false, 0, "schema_info"},
             {"drop table", 5, true, 0, "data_change"},
             {"insert into", 3, false, 0, "data_change"},
             {"update set", 3, false, 0, "data_change"},
             {"delete from", 3, false, 0, "data_change"}}};
}

SqlInjectionDetector::SqlInjectionDetector() : SqlInjectionDetector(BuiltinRuleSource().LoadActiveRules()) {}
SqlInjectionDetector::SqlInjectionDetector(DetectorSettings s)
    : SqlInjectionDetector(std::move(s.rules), s.threshold, s.maxBytes) {
    maxFileBytes_ = s.maxFileBytes;
}

SqlInjectionDetector::SqlInjectionDetector(std::vector<PatternRule> rules, int threshold,
                                           std::size_t maxBytes)
    : rules_(std::move(rules)), threshold_(threshold), maxBytes_(maxBytes) {
    if (threshold <= 0 || !maxBytes || maxBytes > 2 * 1024 * 1024)
        throw std::runtime_error("Недопустимый порог или лимит анализа в БД");
    for (const auto &rule : rules_)
        if (rule.pattern.empty() || rule.weight < 1 || rule.weight > 5)
            throw std::runtime_error("Некорректная сигнатура");
}

DetectionReport SqlInjectionDetector::Analyze(std::string_view input, bool legacy) const {
    DetectionReport r;
    r.threshold = threshold_;
    r.originalInputBytes = input.size();
    if (input.size() > maxBytes_) {
        auto end = maxBytes_;
        while (end && Continuation(input[end]))
            --end;
        input = input.substr(0, end);
        r.inputTruncated = true;
    }
    r.analyzedInputBytes = input.size();
    std::size_t start = 0, number = 1;
    do {
        const auto end = legacy ? input.npos : input.find_first_of("\r\n", start);
        std::string line = Normalize(input.substr(start, end - start), legacy);
        if (number > 1)
            r.normalizedInput += '\n';
        r.normalizedInput += line;
        int score = 0;
        bool high = false;
        for (const auto &rule : rules_) {
            if (!rule.enabled)
                continue;
            for (auto pos = line.find(rule.pattern); pos != line.npos;
                 pos = line.find(rule.pattern, pos + 1)) {
                score = (std::min)(score, (std::numeric_limits<int>::max)() - rule.weight) + rule.weight;
                high |= rule.highRisk;
                ++r.totalHits;
                if (r.hits.size() < kMaxSavedHits)
                    r.hits.push_back({rule.pattern, pos, rule.weight, rule.highRisk, rule.id, number,
                                      rule.category, Context(line, pos)});
            }
        }
        const bool suspicious = high || score >= threshold_;
        r.totalScore = (std::min)(r.totalScore, (std::numeric_limits<int>::max)() - score) + score;
        r.suspicious |= suspicious;
        r.lines.push_back({number++, score, suspicious});
        if (end == input.npos)
            break;
        start = end + 1;
        if (input[end] == '\r' && start < input.size() && input[start] == '\n')
            ++start;
    } while (start < input.size());
    return r;
}

std::string RiskLevel(const DetectionReport &r) {
    if (!r.suspicious)
        return r.totalScore ? "Средний" : "Низкий";
    for (const auto &l : r.lines)
        if (l.suspicious && l.score >= 2LL * r.threshold)
            return "Критический";
    return "Высокий";
}

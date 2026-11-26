#include "SqlInjectionDetector.h"
#include "ScalarPredicate.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace {
bool Space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == '\0';
}

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
std::string Decode(std::string_view input, bool formPlus) {
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
        } else if (c == '+' && formPlus) {
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
        decoded = Decode(input, true);
        if (decoded.find_first_of("%&") != decoded.npos)
            decoded = Decode(decoded, false);
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

std::string Context(std::string_view line, std::size_t pos) {
    auto begin = pos > 48 ? pos - 48 : 0;
    auto end = (std::min)(line.size(), pos + 144);
    while (begin < pos && Continuation(line[begin]))
        ++begin;
    while (end > pos && end < line.size() && Continuation(line[end]))
        --end;
    return std::string(line.substr(begin, end - begin));
}
bool IdentifierByte(char c) {
    return Word(c) || (c >= '0' && c <= '9') || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

bool Consume(std::string_view &text, std::string_view prefix) {
    if (!text.starts_with(prefix))
        return false;
    text.remove_prefix(prefix.size());
    return true;
}

void SkipSpace(std::string_view &text) {
    if (text.starts_with(' ')) // Normalize already collapsed whitespace.
        text.remove_prefix(1);
}

std::size_t AdditionalSpellingLength(std::string_view line, std::size_t pos, std::string_view prefix) {
    auto begin = pos;
    if (prefix == "sleep" && pos >= 3 && line.substr(pos - 3, 3) == "pg_")
        begin -= 3;
    if (begin && IdentifierByte(line[begin - 1]))
        return 0;
    auto rest = line.substr(pos + prefix.size());
    if (prefix == "union") {
        if (!Consume(rest, " all select null") && !Consume(rest, " distinct select null"))
            return 0;
        SkipSpace(rest);
        if (!Consume(rest, ","))
            return 0;
        SkipSpace(rest);
        if (Consume(rest, "null") && (rest.empty() || !IdentifierByte(rest.front())))
            return line.size() - rest.size() - pos;
        return 0;
    }
    if (!Consume(rest, " ("))
        return 0;
    const auto matchedBytes = line.size() - rest.size() - pos;
    SkipSpace(rest);
    return !rest.empty() && rest.front() >= '0' && rest.front() <= '9' ? matchedBytes : 0;
}

auto ScalarFamily(const std::vector<PatternRule> &rules) {
    return std::find_if(rules.begin(), rules.end(), [](const PatternRule &rule) {
        return rule.enabled && rule.category == "tautology" && rule.pattern == "or 1=1";
    });
}

template <typename Visitor>
void MatchLine(std::string_view line, const std::vector<PatternRule> &rules, bool legacy,
               std::vector<PatternRule>::const_iterator scalarFamily, Visitor visit) {
    if (line.empty())
        return;
    bool literalFound = false;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto &rule = rules[index];
        if (!rule.enabled)
            continue;
        std::string_view prefix = rule.pattern;
        if (!legacy && rule.category == "time_based" && (prefix == "sleep(" || prefix == "benchmark("))
            prefix.remove_suffix(1);
        else if (!legacy && rule.category == "union" && prefix == "union select")
            prefix = "union";
        const bool extended = prefix.size() != rule.pattern.size();
        for (auto pos = line.find(prefix); pos != line.npos; pos = line.find(prefix, pos + 1)) {
            const auto length = !extended || line.substr(pos).starts_with(rule.pattern)
                                    ? rule.pattern.size() : AdditionalSpellingLength(line, pos, prefix);
            if (length) {
                literalFound = true;
                visit(index, pos, length, nullptr);
            }
        }
    }
    if (!literalFound && scalarFamily != rules.end()) {
        const auto evidence = scalar_predicate::Match(line);
        if (evidence.length)
            visit(static_cast<std::size_t>(scalarFamily - rules.begin()), evidence.position,
                  evidence.length, evidence.description);
    }
}

DetectionHit Detail(const PatternRule &rule, std::string_view line, std::size_t number, std::size_t pos,
                    std::size_t length, const char *description) {
    return {description ? std::string(description) : rule.pattern, pos, rule.weight, rule.highRisk,
            rule.id, number, rule.category, Context(line, pos), length};
}

int AddScore(int current, int extra) {
    return (std::min)(current, (std::numeric_limits<int>::max)() - extra) + extra;
}
} // namespace

void DetectionReport::ForEachHit(const std::function<void(const DetectionHit &)> &visit) const {
    if (replayRules.empty()) {
        for (const auto &hit : hits)
            visit(hit);
        return;
    }
    std::string_view text = normalizedInput;
    std::size_t begin = 0;
    const auto family = ScalarFamily(replayRules);
    for (const auto &score : lines) {
        auto end = text.find('\n', begin);
        auto line = text.substr(begin, end - begin);
        MatchLine(line, replayRules, replayLegacy, score.line <= replayScalarThroughLine ? family : replayRules.end(),
                  [&](std::size_t index, std::size_t pos, std::size_t length, const char *description) {
                      visit(Detail(replayRules[index], line, score.line, pos, length, description));
                  });
        if (end == text.npos)
            break;
        begin = end + 1;
    }
}

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
    return AnalyzeInput(input, legacy ? InputMode::Legacy : InputMode::Sample);
}
DetectionReport SqlInjectionDetector::AnalyzeLog(std::string_view input) const {
    return AnalyzeInput(input, InputMode::Log);
}

DetectionReport SqlInjectionDetector::AnalyzeInput(std::string_view input, InputMode mode) const {
    DetectionReport r;
    r.threshold = threshold_;
    r.originalInputBytes = input.size();
    bool lastLineComplete = true;
    if (input.size() > maxBytes_) {
        auto end = maxBytes_;
        while (end && Continuation(input[end]))
            --end;
        lastLineComplete = input[end] == '\r' || input[end] == '\n';
        input = input.substr(0, end);
        r.inputTruncated = true;
    }
    r.analyzedInputBytes = input.size();
    std::size_t start = 0, number = 1;
    std::vector<std::size_t> counts(rules_.size());
    const bool legacy = mode == InputMode::Legacy;
    const auto family = ScalarFamily(rules_);
    do {
        const auto end = legacy ? input.npos : input.find_first_of("\r\n", start);
        std::string line = Normalize(input.substr(start, end - start), legacy);
        if (number > 1)
            r.normalizedInput += '\n';
        r.normalizedInput += line;
        int score = 0;
        bool high = false;
        // A file contains independent records. A sample can be one formatted SQL statement.
        // Structural evidence needs a complete record and must survive dense-report replay.
        const bool completeRecord = mode == InputMode::Log ? (end != input.npos || lastLineComplete)
            : (!legacy && !r.inputTruncated && number == 1 &&
               (end == input.npos || input.find_first_not_of("\r\n", end) == input.npos));
        if (completeRecord)
            r.replayScalarThroughLine = number;
        MatchLine(line, rules_, legacy, completeRecord ? family : rules_.end(),
                  [&](std::size_t index, std::size_t pos, std::size_t length, const char *description) {
            const auto &rule = rules_[index];
            score = AddScore(score, rule.weight);
            high |= rule.highRisk;
            ++r.totalHits;
            if (description) {
                const auto found = std::find_if(r.contributions.begin(), r.contributions.end(),
                    [&](const ScoreContribution &entry) { return entry.pattern == description; });
                if (found != r.contributions.end())
                    ++found->occurrences;
                else
                    r.contributions.push_back({description, rule.weight, 1});
            } else {
                ++counts[index];
            }
            if (r.hits.size() < kMaxSavedHits)
                r.hits.push_back(Detail(rule, line, number, pos, length, description));
        });
        const bool suspicious = high || score >= threshold_;
        r.totalScore = AddScore(r.totalScore, score);
        r.suspicious |= suspicious;
        r.lines.push_back({number++, score, suspicious, high});
        if (end == input.npos)
            break;
        start = end + 1;
        if (input[end] == '\r' && start < input.size() && input[start] == '\n')
            ++start;
    } while (start < input.size());
    for (std::size_t i = 0; i < counts.size(); ++i)
        if (counts[i])
            r.contributions.push_back({rules_[i].pattern, rules_[i].weight, counts[i]});
    if (r.totalHits > r.hits.size()) {
        r.replayRules = rules_;
        r.replayLegacy = legacy;
    }
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

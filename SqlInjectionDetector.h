#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

inline constexpr int kSuspiciousScoreThreshold = 5;
inline constexpr std::size_t kMaxAnalyzeInputBytes = 512 * 1024;

struct DetectionHit {
    std::string pattern;
    std::size_t position = 0;
    int weight = 0;
    bool highRisk = false;
};

struct DetectionReport {
    bool suspicious = false;
    int totalScore = 0;
    std::string normalizedInput;
    std::vector<DetectionHit> hits;
    bool inputTruncated = false;
    std::size_t originalInputBytes = 0;
    std::size_t analyzedInputBytes = 0;
};

class SqlInjectionDetector {
public:
    SqlInjectionDetector();
    DetectionReport Analyze(std::string_view input) const;

private:
    struct PatternRule {
        std::string pattern;
        int weight = 1;
        bool highRisk = false;
    };

    std::vector<PatternRule> rules_;

    static std::string Normalize(std::string_view input);
    static std::vector<std::size_t> FindAllOccurrences(
        std::string_view text,
        std::string_view pattern);
};

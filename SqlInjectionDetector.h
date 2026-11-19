#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

inline constexpr int kSuspiciousScoreThreshold = 5;
inline constexpr std::size_t kMaxAnalyzeInputBytes = 512 * 1024;
inline constexpr std::size_t kMaxSavedHits = 16384;

struct PatternRule {
    std::string pattern;
    int weight;
    bool highRisk;
    int id = 0;
    std::string category;
    bool enabled = true;
};

struct DetectorSettings {
    std::vector<PatternRule> rules;
    int threshold = kSuspiciousScoreThreshold;
    std::size_t maxBytes = kMaxAnalyzeInputBytes;
    std::size_t maxFileBytes = 2 * 1024 * 1024;
};

class IRuleSource {
  public:
    virtual ~IRuleSource() = default;
    virtual DetectorSettings LoadActiveRules() = 0;
};

class BuiltinRuleSource final : public IRuleSource {
  public:
    DetectorSettings LoadActiveRules() override;
};

struct DetectionHit {
    std::string pattern;
    std::size_t position;
    int weight;
    bool highRisk;
    int ruleId;
    std::size_t line;
    std::string category;
    std::string context;
};

struct LineScore {
    std::size_t line;
    int score;
    bool suspicious;
};

struct DetectionReport {
    bool suspicious = false;
    int totalScore = 0;
    int threshold = kSuspiciousScoreThreshold;
    std::string normalizedInput;
    std::vector<DetectionHit> hits;
    std::vector<LineScore> lines;
    bool inputTruncated = false;
    std::size_t originalInputBytes = 0;
    std::size_t analyzedInputBytes = 0;
    std::size_t totalHits = 0;
};

class SqlInjectionDetector {
  public:
    SqlInjectionDetector();
    explicit SqlInjectionDetector(DetectorSettings settings);
    SqlInjectionDetector(std::vector<PatternRule> rules, int threshold, std::size_t maxBytes);
    std::size_t FileLimit() const { return maxFileBytes_; }
    DetectionReport Analyze(std::string_view input, bool legacy = false) const;
    const std::vector<PatternRule> &Rules() const { return rules_; }
    int Threshold() const { return threshold_; }

  private:
    std::vector<PatternRule> rules_;
    int threshold_ = kSuspiciousScoreThreshold;
    std::size_t maxBytes_ = kMaxAnalyzeInputBytes;
    std::size_t maxFileBytes_ = 2 * 1024 * 1024;
};

std::string RiskLevel(const DetectionReport &report);

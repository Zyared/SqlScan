#pragma once
#include "SqlInjectionDetector.h"
#include <functional>
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

using Row = std::vector<std::string>;
using Rows = std::vector<Row>;
struct HistoryFilter {
    std::string text, from, until, verdict, minScore, source;
    int sortColumn = 0;
    bool ascending = false;
};

class DbConnection {
  public:
    DbConnection() = default;
    DbConnection(const DbConnection &) = delete;
    DbConnection &operator=(const DbConnection &) = delete;
    ~DbConnection();
    void Open(const std::wstring &connection);
    void Close() noexcept;
    void Swap(DbConnection &other) noexcept;
    bool Connected() const { return dbc_ != SQL_NULL_HDBC; }
    Rows Query(const std::wstring &sql, const Row &args = {});
    void Transaction(const std::function<void()> &work);

  private:
    SQLHENV env_{};
    SQLHDBC dbc_{};
    bool loggedIn_ = false;
};

class RuleRepository final : public IRuleSource {
  public:
    explicit RuleRepository(DbConnection &db) : db_(db) {}
    SqlInjectionDetector Detector();
    DetectorSettings LoadActiveRules() override;
    Rows Rules();
    Rows Categories();
    void EditRule(int id, int category, std::string pattern, int weight, bool high, bool enabled);
    void DeleteRule(int id);

  private:
    DbConnection &db_;
};

class ScanRepository {
  public:
    explicit ScanRepository(DbConnection &db) : db_(db) {}
    std::string Save(const DetectionReport &r, std::string type, std::string name, std::string hash,
                     std::size_t sourceBytes);
    std::string Previous(std::string hash);
    Rows History(const HistoryFilter &filter, int offset, int limit = 50);
    Rows ScanSummary(std::string id);
    Rows ScanHits(std::string id);
    void DeleteScan(std::string id);
    Rows Days();
    Rows TopRules();
    Rows Totals();

  private:
    DbConnection &db_;
};

std::wstring SavedConnection();
void SaveConnection(const std::wstring &connection);

#pragma once
#include "Database.h"
#include "resource.h"
#include <optional>

class ActivityChart : public CWnd {
  public:
    Rows days;

  protected:
    afx_msg void OnPaint();
    DECLARE_MESSAGE_MAP()
};

class CSqlScanDlg : public CDialogEx {
  public:
    explicit CSqlScanDlg(CWnd *parent = nullptr);

  protected:
    void DoDataExchange(CDataExchange *) override;
    BOOL OnInitDialog() override;
    void OnOK() override {}
    afx_msg void OnSize(UINT, int, int);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO *);
    afx_msg void OnTab(NMHDR *, LRESULT *);
    afx_msg void OnTableOpen(NMHDR *, LRESULT *);
    afx_msg void OnSort(NMHDR *, LRESULT *);
    afx_msg void OnAction(UINT);
    afx_msg void OnChanged();
    afx_msg void OnScan();
    afx_msg void OnLoad();
    afx_msg void OnExport();
    afx_msg void OnConnect();
    afx_msg void OnOffline();
    afx_msg void OnAbout();
    DECLARE_MESSAGE_MAP()
  private:
    CTabCtrl tabs_;
    CListCtrl table_;
    CRichEditCtrl output_;
    ActivityChart chart_;
    DbConnection db_;
    RuleRepository rules_{db_};
    ScanRepository scans_{db_};
    SqlInjectionDetector detector_;
    std::wstring connection_;
    HistoryFilter filter_;
    Rows rows_;
    int page_ = 0;
    bool more_ = false, ready_ = false, loading_ = false;
    std::string filename_, fileText_, fileRaw_, notice_;
    std::optional<DetectionReport> report_;
    int Tab() const { return tabs_.GetCurSel(); }
    void Layout();
    void Refresh();
    void Status();
    void Connect();
    void Detail(std::string id);
    void Rule(bool add);
    void Filters();
    void ExportHistory();
    void SetRows(const Rows &data, const std::vector<std::pair<std::wstring, int>> &columns);
    void RenderResult();
};

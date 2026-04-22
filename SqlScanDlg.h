#pragma once

#include "resource.h"
#include "SqlInjectionDetector.h"

class CSqlScanDlg : public CDialogEx {
public:
    explicit CSqlScanDlg(CWnd* pParent = nullptr);

    enum { IDD = IDD_SQLSCAN_DIALOG };

protected:
    void DoDataExchange(CDataExchange* pDX) override;
    BOOL OnInitDialog() override;
    afx_msg void OnBnClickedCheck();
    afx_msg void OnBnClickedLoad();
    afx_msg void OnBnClickedSave();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);

    DECLARE_MESSAGE_MAP()

private:
    SqlInjectionDetector detector_;
    bool layoutReady_ = false;
    CRichEditCtrl m_editOutput;

    void LayoutControls(int cx, int cy);
    void FillReportRich(const DetectionReport& r);
    void AppendRichText(COLORREF cr, const CString& text);
    void AppendNormalizedWithHighlights(
        const std::string& normalized,
        const std::vector<DetectionHit>& hits);
    static COLORREF ScoreToGradientColor(int score);

    static std::string CStringToUtf8(const CString& s);
    static CString Utf8ToCString(const std::string& s);
    static BOOL LoadTextFile(const CString& path, CString& out, CString& err);
    static BOOL SaveTextFileUtf8(const CString& path, const CString& text);
};

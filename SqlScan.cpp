#include "pch.h"
#include "SqlScan.h"
#include "SqlScanDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CSqlScanApp theApp;

BEGIN_MESSAGE_MAP(CSqlScanApp, CWinApp)
END_MESSAGE_MAP()

CSqlScanApp::CSqlScanApp() noexcept = default;

BOOL CSqlScanApp::InitInstance() {
    CWinApp::InitInstance();

    if (!AfxInitRichEdit2()) {
        AfxMessageBox(_T("Не удалось загрузить Rich Edit (Riched20.dll)."), MB_ICONERROR);
        return FALSE;
    }

    SetRegistryKey(_T("SqlScanCoursework"));

    CSqlScanDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();

    return FALSE;
}

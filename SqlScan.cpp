#include "pch.h"
#include "SqlScan.h"
#include "SqlScanDlg.h"
#include "ReportIO.h"
#include <shellapi.h>
#include <cstdio>
#pragma comment(                                                                                             \
    linker,                                                                                                  \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CSqlScanApp theApp;
BEGIN_MESSAGE_MAP(CSqlScanApp, CWinApp)
END_MESSAGE_MAP()
CSqlScanApp::CSqlScanApp() noexcept = default;
BOOL CSqlScanApp::InitInstance() {
    CWinApp::InitInstance();
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv && count > 1) {
        try {
            if ((count != 5 && count != 6) || std::wstring_view(argv[1]) != L"--batch" ||
                std::wstring_view(argv[3]) != L"--out" ||
                (count == 6 && std::wstring_view(argv[5]) != L"--legacy"))
                throw std::runtime_error(
                    "Usage: SqlScan.exe --batch dataset.csv --out result.csv [--legacy]");
            exitCode_ = RunBatch(argv[2], argv[4], count == 6);
        } catch (const std::exception &e) {
            auto handle = GetStdHandle(STD_ERROR_HANDLE);
            DWORD written{};
            std::string msg = std::string(e.what()) + "\r\n";
            WriteFile(handle, msg.data(), (DWORD)msg.size(), &written, nullptr);
            exitCode_ = 2;
        }
        LocalFree(argv);
        return FALSE;
    }
    if (argv)
        LocalFree(argv);
    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    if (!AfxInitRichEdit2()) {
        AfxMessageBox(L"Не удалось загрузить Rich Edit.");
        exitCode_ = 1;
        return FALSE;
    }
    SetRegistryKey(L"SqlScanCoursework");
    CSqlScanDlg dlg;
    m_pMainWnd = &dlg;
    if (dlg.DoModal() == -1)
        exitCode_ = 1;
    m_pMainWnd = nullptr;
    return FALSE;
}
int CSqlScanApp::ExitInstance() {
    CWinApp::ExitInstance();
    return exitCode_;
}

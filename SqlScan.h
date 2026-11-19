#pragma once
#include "resource.h"

class CSqlScanApp : public CWinApp {
  public:
    CSqlScanApp() noexcept;
    BOOL InitInstance() override;
    int ExitInstance() override;
    DECLARE_MESSAGE_MAP()
  private:
    int exitCode_ = 0;
};
extern CSqlScanApp theApp;

#pragma once

#include "resource.h"

class CSqlScanApp : public CWinApp {
public:
    CSqlScanApp() noexcept;
    BOOL InitInstance() override;
    DECLARE_MESSAGE_MAP()
};

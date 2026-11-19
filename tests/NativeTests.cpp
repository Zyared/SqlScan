#include "CppUnitTest.h"
#include "Checks.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SqlScanTests {
TEST_CLASS(Acceptance){public : TEST_METHOD(TextAnalysisAndReports){CheckCore();
}
TEST_METHOD(PostgreSqlRepository) {
    if (!CheckDatabase())
        Assert::Fail(L"Set SQLSCAN_TEST_CONNECTION to a disposable PostgreSQL database.");
}
}
;
}

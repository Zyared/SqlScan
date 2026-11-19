# Run in an x64 Visual Studio Developer PowerShell with C++20, MFC and Windows SDK.
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'x64\Release'))
$ErrorActionPreference = 'Stop'
$out = [IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputDirectory)).FullName
Push-Location $PSScriptRoot
try {
    & rc.exe /nologo /fo "$out\SqlScan.res" SqlScan.rc
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    $sources = 'SqlScan.cpp','SqlScanDlg.cpp','SqlInjectionDetector.cpp','ReportIO.cpp','Database.cpp','Batch.cpp'
    & cl.exe /nologo /MP /EHsc /permissive- /std:c++20 /utf-8 /W4 /O2 /MT /D_UNICODE /DUNICODE /D_WINDOWS /DNDEBUG "/Fo$out\" "/Fe$out\SqlScan.exe" $sources "$out\SqlScan.res" /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /MANIFEST:EMBED /INCREMENTAL:NO shell32.lib
    if ($LASTEXITCODE) { throw 'Release build failed.' }
    Get-Item -LiteralPath "$out\SqlScan.exe" | Select-Object FullName,Length
} finally { Pop-Location }

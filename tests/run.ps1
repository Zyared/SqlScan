# Run from an x64 Visual Studio Developer PowerShell.
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\x64\Validation'))
$ErrorActionPreference = 'Stop'
$out = [IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputDirectory)).FullName
Push-Location (Join-Path $PSScriptRoot '..')
try {
    & rc.exe /nologo /fo "$out\schema.res" SqlScan.rc
    if ($LASTEXITCODE) { throw 'Resource compilation failed' }
    & cl.exe /nologo /EHsc /std:c++20 /utf-8 /O2 /MT /D_UNICODE /DUNICODE /DNDEBUG /I. "/Fo$out\" "/Fe$out\Acceptance.exe" tests\acceptance.cpp SqlInjectionDetector.cpp Database.cpp ReportIO.cpp "$out\schema.res" /link /SUBSYSTEM:CONSOLE
    if ($LASTEXITCODE) { throw 'Test compilation failed' }
    & "$out\Acceptance.exe"
    if ($LASTEXITCODE) { throw 'Acceptance tests failed' }
} finally { Pop-Location }

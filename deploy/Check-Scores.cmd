@echo off
setlocal
set "out=%LOCALAPPDATA%\SqlScan2\Checks\Scores-%RANDOM%-%RANDOM%"
mkdir "%out%" || exit /b 1
"%~dp0validation\python\python.exe" -B "%~dp0source\tests\score_audit.py" "%~dp0." "%out%\scores.json" --html "%out%\scores.html" --fresh
if errorlevel 1 (
    if not defined SQLSCAN_NONINTERACTIVE pause
    exit /b 1
)
echo %out%\scores.html
if not defined SQLSCAN_NONINTERACTIVE start "" "%out%\scores.html"
exit /b 0

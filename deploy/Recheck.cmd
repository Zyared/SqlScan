@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0Recheck.ps1" %*
set "result=%errorlevel%"
echo Exit code %result%
if not defined SQLSCAN_NONINTERACTIVE pause
exit /b %result%

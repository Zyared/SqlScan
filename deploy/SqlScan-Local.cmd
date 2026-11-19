@echo off
setlocal
set "shell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if defined PROCESSOR_ARCHITEW6432 set "shell=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%shell%" -NoLogo -NoProfile -ExecutionPolicy RemoteSigned -File "%~dp0Start-Local.ps1" %*
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%

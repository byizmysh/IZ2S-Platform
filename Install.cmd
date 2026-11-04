@echo off
setlocal DisableDelayedExpansion
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0distribution\install-windows.ps1" %*
set "IZ2S_RESULT=%ERRORLEVEL%"
if not "%IZ2S_RESULT%"=="0" echo Setup failed. Read the message above and FAQ.md.
pause
exit /b %IZ2S_RESULT%

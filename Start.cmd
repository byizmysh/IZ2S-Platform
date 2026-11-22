@echo off
setlocal DisableDelayedExpansion
pushd "%~dp0"
set "IZ2S_NODE=%~dp0bootstrap\node\node.exe"
if not exist "%IZ2S_NODE%" set "IZ2S_NODE=%~dp0data\runtime\node\node.exe"
if exist "%IZ2S_NODE%" (
  "%IZ2S_NODE%" distribution\start-windows.cjs %*
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0distribution\install-windows.ps1" -Portable %*
)
set "IZ2S_RESULT=%ERRORLEVEL%"
popd
if not "%IZ2S_RESULT%"=="0" pause
exit /b %IZ2S_RESULT%

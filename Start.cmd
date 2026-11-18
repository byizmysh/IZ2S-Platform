@echo off
setlocal DisableDelayedExpansion
set "IZ2S_NODE=%~dp0data\runtime\node\node.exe"
if not exist "%IZ2S_NODE%" (
  echo IZ2S is not installed in this folder. Open Install.cmd first.
  pause
  exit /b 1
)
pushd "%~dp0"
"%IZ2S_NODE%" distribution\start-local.cjs %*
set "IZ2S_RESULT=%ERRORLEVEL%"
popd
if not "%IZ2S_RESULT%"=="0" pause
exit /b %IZ2S_RESULT%

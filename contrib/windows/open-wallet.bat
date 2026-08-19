@echo off
setlocal
rem Opens the bundled VIZ wallet straight from disk.
rem
rem Same reason as the Forecaster launcher: an https:// page is not allowed to
rem reach 127.0.0.1, so use this local copy to work against your own node.
rem
rem In the wallet: Settings -> Node.

cd /d "%~dp0"

if not exist "client\wallet\index.html" (
  echo.
  echo   Wallet client not found next to this script.
  echo   Expected: %~dp0client\wallet\index.html
  echo   Re-download the release archive - the client ships inside it.
  echo.
  pause
  exit /b 1
)

start "" "%~dp0client\wallet\index.html"

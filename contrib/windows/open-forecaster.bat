@echo off
setlocal
rem Opens the bundled Forecaster client straight from disk.
rem
rem It has to be the local copy, not the hosted one: browsers refuse to let an
rem https:// page talk to 127.0.0.1 (Local Network Access), so the public client
rem cannot reach a node running on this machine. Opened from disk it can.
rem
rem In the client: Settings -> Node and connection -> "My own node".

cd /d "%~dp0"

if not exist "client\forecaster\index.html" (
  echo.
  echo   Forecaster client not found next to this script.
  echo   Expected: %~dp0client\forecaster\index.html
  echo   Re-download the release archive - the client ships inside it.
  echo.
  pause
  exit /b 1
)

start "" "%~dp0client\forecaster\index.html"

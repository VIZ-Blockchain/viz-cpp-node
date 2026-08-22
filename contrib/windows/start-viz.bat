@echo off
setlocal
rem VIZ node launcher for Windows.
rem Keeps the chain data next to this file, in the "data" subfolder, and starts
rem vizd.exe with it. First start downloads a snapshot from trusted peers.

cd /d "%~dp0"

if not exist "vizd.exe" (
  echo.
  echo   vizd.exe not found next to this script.
  echo   Download it from the latest "Windows Build" release:
  echo   https://github.com/VIZ-Blockchain/viz-cpp-node/releases
  echo   and put vizd.exe in this folder.
  echo.
  pause
  exit /b 1
)

if not exist "data" mkdir "data"

if not exist "data\config.ini" (
  if not exist "config.ini" (
    echo   config.ini not found. Keep it next to this script.
    pause
    exit /b 1
  )
  copy /y "config.ini" "data\config.ini" >nul
  echo   Created data\config.ini - edit that copy if you want to change settings.
)

echo.
echo   Starting the VIZ node.
echo   First start downloads a snapshot from trusted peers - this takes a few
echo   minutes and a few GB of disk. After that the node keeps up with the chain
echo   on its own.
echo.
echo   Your local API will be at  http://127.0.0.1:8090
echo   Run open-forecaster.bat or open-wallet.bat to use that address from
echo   the bundled clients (the public web versions cannot reach it).
echo.
echo   Close this window (or press Ctrl+C) to stop the node.
echo.

vizd.exe -d "data"

set EXITCODE=%ERRORLEVEL%
echo.
if not "%EXITCODE%"=="0" (
  echo   The node exited with code %EXITCODE%. The lines above explain why.
) else (
  echo   The node stopped.
)
pause
exit /b %EXITCODE%

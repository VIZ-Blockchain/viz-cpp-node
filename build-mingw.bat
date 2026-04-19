@echo off
REM ===========================================================================
REM  VIZ Windows Build Script (MinGW)
REM
REM  Prerequisites:
REM    - MinGW-w64 with g++ supporting C++11 and SSE4.2
REM    - CMake 3.16+
REM    - Git for Windows
REM    - Boost 1.71+ built with: link=static threading=multi runtime-link=shared
REM    - OpenSSL for Windows (https://slproweb.com/products/Win32OpenSSL.html)
REM
REM  Required Environment Variables:
REM    BOOST_ROOT        - Path to Boost installation (e.g. C:\Boost)
REM    OPENSSL_ROOT_DIR  - Path to OpenSSL installation (e.g. C:\OpenSSL-Win64)
REM
REM  Optional Environment Variables:
REM    VIZ_BUILD_TYPE    - Release or Debug (default: Release)
REM    VIZ_LOW_MEMORY    - ON or OFF (default: OFF)
REM    VIZ_BUILD_TESTNET - ON or OFF (default: OFF)
REM    VIZ_FULL_STATIC   - ON or OFF (default: OFF, produces static exe)
REM    VIZ_CMAKE_EXTRA   - Additional CMake options
REM ===========================================================================

setlocal enabledelayedexpansion

REM --- Defaults ---
if not defined VIZ_BUILD_TYPE set VIZ_BUILD_TYPE=Release
if not defined VIZ_LOW_MEMORY set VIZ_LOW_MEMORY=OFF
if not defined VIZ_BUILD_TESTNET set VIZ_BUILD_TESTNET=OFF
if not defined VIZ_FULL_STATIC set VIZ_FULL_STATIC=OFF

REM --- Validate required environment variables ---
if not defined BOOST_ROOT (
    echo ERROR: BOOST_ROOT is not set. Point it to your Boost installation.
    echo   Example: set BOOST_ROOT=C:\Boost
    exit /b 1
)

if not defined OPENSSL_ROOT_DIR (
    echo ERROR: OPENSSL_ROOT_DIR is not set. Point it to your OpenSSL installation.
    echo   Example: set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
    exit /b 1
)

REM --- Validate paths exist ---
if not exist "%BOOST_ROOT%\include" (
    echo ERROR: BOOST_ROOT does not appear to be a valid Boost installation.
    echo   Looking for: %BOOST_ROOT%\include
    exit /b 1
)

if not exist "%OPENSSL_ROOT_DIR%\include" (
    echo ERROR: OPENSSL_ROOT_DIR does not appear to be a valid OpenSSL installation.
    echo   Looking for: %OPENSSL_ROOT_DIR%\include
    exit /b 1
)

REM --- Check MinGW is available ---
where mingw32-make >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: mingw32-make not found in PATH.
    echo   Make sure MinGW-w64 bin directory is in your PATH.
    exit /b 1
)

REM --- Determine source directory (parent of this script's location) ---
set SCRIPT_DIR=%~dp0
set SOURCE_DIR=%SCRIPT_DIR%..

REM --- Display configuration ---
echo.
echo ============================================
echo  VIZ Windows Build (MinGW)
echo ============================================
echo  Build Type:      %VIZ_BUILD_TYPE%
echo  Low Memory Node: %VIZ_LOW_MEMORY%
echo  Build Testnet:   %VIZ_BUILD_TESTNET%
echo  Full Static:     %VIZ_FULL_STATIC%
echo  BOOST_ROOT:      %BOOST_ROOT%
echo  OPENSSL_ROOT:    %OPENSSL_ROOT_DIR%
echo  Source Dir:      %SOURCE_DIR%
echo ============================================
echo.

REM --- Create build directory ---
set BUILD_DIR=%SOURCE_DIR%\build
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM --- Configure ---
echo [1/2] Configuring with CMake...
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=%VIZ_BUILD_TYPE% ^
    -DBOOST_ROOT="%BOOST_ROOT%" ^
    -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT_DIR%" ^
    -DLOW_MEMORY_NODE=%VIZ_LOW_MEMORY% ^
    -DBUILD_TESTNET=%VIZ_BUILD_TESTNET% ^
    -DENABLE_MONGO_PLUGIN=OFF ^
    -DBUILD_SHARED_LIBRARIES=OFF ^
    -DFULL_STATIC_BUILD=%VIZ_FULL_STATIC% ^
    %VIZ_CMAKE_EXTRA%

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b %ERRORLEVEL%
)

REM --- Build ---
echo.
echo [2/2] Building...
mingw32-make -C "%BUILD_DIR%" -j%NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed.
    exit /b %ERRORLEVEL%
)

echo.
echo ============================================
echo  Build completed successfully!
echo  Output directory: %BUILD_DIR%
echo ============================================

endlocal

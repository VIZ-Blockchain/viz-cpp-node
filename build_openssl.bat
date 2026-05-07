@echo off
set PATH=C:\perl_strawberry\perl\bin;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" x64

cd /d F:\openssl-3.0.16

echo === Configuring OpenSSL 3.0.16 for MSVC x64 ===
perl Configure VC-WIN64A no-asm --prefix=D:\OpenSSL --openssldir=D:\OpenSSL\ssl

if %ERRORLEVEL% neq 0 (
    echo ERROR: Configure failed
    exit /b %ERRORLEVEL%
)

echo === Building OpenSSL ===
nmake /I

if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b %ERRORLEVEL%
)

echo === Installing OpenSSL to D:\OpenSSL ===
nmake install_sw

if %ERRORLEVEL% neq 0 (
    echo ERROR: Install failed
    exit /b %ERRORLEVEL%
)

echo === Done ===

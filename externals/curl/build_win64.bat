@echo off
REM ====================================================================
REM build_win64.bat -- Build a static libcurl for Windows x64.
REM
REM Run from a "x64 Native Tools Command Prompt for VS 20xx".
REM Requires CMake on PATH.
REM
REM This builds a static libcurl with:
REM   - Windows Schannel for TLS (no OpenSSL dependency)
REM   - SSPI for NTLM/Kerberos authentication
REM   - IPv6 enabled
REM   - zlib compression support
REM   - Minimal: no SSH, no HTTP/2, no brotli, no zstd
REM
REM Output:
REM   externals\curl\lib\x64\libcurl_a.lib
REM
REM Usage (from the TH8 root directory):
REM   externals\curl\build_win64.bat
REM   externals\curl\build_win64.bat debug
REM
REM The curl source must be cloned into the src/ subdirectory:
REM   cd externals\curl
REM   git clone --depth 1 https://github.com/curl/curl.git src
REM
REM Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
REM
REM See the file "license.terms" for information on usage and redistribution of
REM this file, and for a DISCLAIMER OF ALL WARRANTIES.
REM
REM ====================================================================

setlocal

set CURL_SRC=%~dp0src
set CURL_BLD=%~dp0src\_build_x64
set CURL_OUT=%~dp0lib\x64

REM Resolve zlib paths relative to the TH8 root (two levels up from here).
set ZLIB_INC=%~dp0..\zlib\include
set ZLIB_LIB=%~dp0..\zlib\lib\x64\zlib.lib

if not exist "%CURL_SRC%\CMakeLists.txt" (
    echo ERROR: curl source not found at %CURL_SRC%
    echo.
    echo Clone it first:
    echo   cd externals\curl
    echo   git clone --depth 1 https://github.com/curl/curl.git src
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake not found on PATH.
    exit /b 1
)

if /i "%1"=="debug" (
    set BUILD_TYPE=Debug
) else (
    set BUILD_TYPE=Release
)

REM Determine zlib availability.
set ZLIB_FLAG=OFF
set ZLIB_CMAKE_ARGS=
if exist "%ZLIB_INC%\zlib.h" (
    if exist "%ZLIB_LIB%" (
        set ZLIB_FLAG=ON
        set ZLIB_CMAKE_ARGS=-DZLIB_INCLUDE_DIR="%ZLIB_INC%" -DZLIB_LIBRARY="%ZLIB_LIB%"
        echo Found zlib: %ZLIB_INC% / %ZLIB_LIB%
    )
)
if "%ZLIB_FLAG%"=="OFF" (
    echo NOTE: zlib not found; building without compression.
)

echo.
echo Configuring libcurl x64 %BUILD_TYPE% (static, Schannel, zlib=%ZLIB_FLAG%)...

cmake -S "%CURL_SRC%" -B "%CURL_BLD%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DBUILD_CURL_EXE=OFF ^
    -DBUILD_TESTING=OFF ^
    -DCURL_USE_SCHANNEL=ON ^
    -DCURL_USE_OPENSSL=OFF ^
    -DCURL_ZLIB=%ZLIB_FLAG% ^
    %ZLIB_CMAKE_ARGS% ^
    -DCURL_BROTLI=OFF ^
    -DCURL_ZSTD=OFF ^
    -DUSE_LIBIDN2=OFF ^
    -DCURL_USE_LIBSSH2=OFF ^
    -DCURL_USE_LIBSSH=OFF ^
    -DCURL_USE_LIBPSL=OFF ^
    -DUSE_NGHTTP2=OFF ^
    -DENABLE_IPV6=ON

if errorlevel 1 (
    echo.
    echo CMAKE CONFIGURE FAILED.
    exit /b 1
)

echo.
echo Building...

cmake --build "%CURL_BLD%" --config %BUILD_TYPE%

if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo BUILD SUCCEEDED.  Copying to %CURL_OUT%...

if not exist "%CURL_OUT%" mkdir "%CURL_OUT%"

REM CMake puts the static lib in lib/ under the build directory.
if exist "%CURL_BLD%\lib\libcurl_a.lib" (
    copy /y "%CURL_BLD%\lib\libcurl_a.lib" "%CURL_OUT%\"
    echo Copied libcurl_a.lib
) else if exist "%CURL_BLD%\lib\%BUILD_TYPE%\libcurl_a.lib" (
    copy /y "%CURL_BLD%\lib\%BUILD_TYPE%\libcurl_a.lib" "%CURL_OUT%\"
    echo Copied libcurl_a.lib
) else if exist "%CURL_BLD%\lib\libcurl.lib" (
    copy /y "%CURL_BLD%\lib\libcurl.lib" "%CURL_OUT%\libcurl_a.lib"
    echo Copied libcurl.lib as libcurl_a.lib
) else (
    echo WARNING: Could not find the built library.
    echo Check %CURL_BLD%\lib\ for the output.
    dir /b /s "%CURL_BLD%\lib\*.lib" 2>nul
)

echo.
echo Done.  Build TH8 with: nmake /f Makefile.msc ENABLE_LIBCURL=1
echo.

endlocal

@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "ROOT=%cd%"
set "SHIPDIR=%ROOT%\ship"
if not exist "%SHIPDIR%" mkdir "%SHIPDIR%"

echo [ship] OpenDoc Windows shipper
echo.

rem ---- version from CMakeLists.txt ---------------------------------------
set "VERSION="
for /f "tokens=3" %%A in ('findstr /b /c:"project(OpenDoc VERSION" "%ROOT%\CMakeLists.txt"') do set "VERSION=%%A"
if not defined VERSION (
    echo [ship] ERROR: could not read project version from CMakeLists.txt
    exit /b 1
)
echo [ship] Version: %VERSION%

rem ---- tool discovery ------------------------------------------------------
set "UCRT=C:\msys64\ucrt64\bin"
set "CMAKE=cmake"
if exist "%UCRT%\cmake.exe" (
    set "CMAKE=%UCRT%\cmake.exe"
    set "PATH=%UCRT%;%PATH%"
) else (
    where cmake >nul 2>nul
    if errorlevel 1 (
        echo [ship] ERROR: cmake not found. Install MSYS2 ucrt64 cmake or add cmake to PATH.
        exit /b 1
    )
)
where tar >nul 2>nul
if errorlevel 1 (
    echo [ship] ERROR: tar.exe not found ^(needed to create the zip files^).
    exit /b 1
)

set "FAILED="

call :build_win64
if errorlevel 1 set "FAILED=1"
call :build_winarm64
if errorlevel 1 set "FAILED=1"

echo.
echo [ship] ---------------------------------------------------------------
if exist "%SHIPDIR%\OpenDoc-%VERSION%-windows-x86_64.zip" echo [ship] OK   ship\OpenDoc-%VERSION%-windows-x86_64.zip
if not exist "%SHIPDIR%\OpenDoc-%VERSION%-windows-x86_64.zip" echo [ship] MISS ship\OpenDoc-%VERSION%-windows-x86_64.zip
if exist "%SHIPDIR%\OpenDoc-%VERSION%-windows-arm64.zip"   echo [ship] OK   ship\OpenDoc-%VERSION%-windows-arm64.zip
if not exist "%SHIPDIR%\OpenDoc-%VERSION%-windows-arm64.zip"   echo [ship] MISS ship\OpenDoc-%VERSION%-windows-arm64.zip
if exist "%SHIPDIR%\OpenDoc-%VERSION%-linux-deb.zip"       echo [ship] OK   ship\OpenDoc-%VERSION%-linux-deb.zip
if not exist "%SHIPDIR%\OpenDoc-%VERSION%-linux-deb.zip"       echo [ship] MISS ship\OpenDoc-%VERSION%-linux-deb.zip  ^(run ship.sh in Linux^)
if defined FAILED (
    echo [ship] RESULT: FAILED
    exit /b 1
)
echo [ship] RESULT: OK
exit /b 0

rem =========================================================================
rem  Windows x86_64
rem =========================================================================
:build_win64
echo.
echo [ship] === Windows x86_64 ===
set "BUILDDIR=%ROOT%\build-ship-win64"
set "STAGE=%BUILDDIR%\stage"
set "ZIP=%SHIPDIR%\OpenDoc-%VERSION%-windows-x86_64.zip"

"%CMAKE%" -S "%ROOT%" -B "%BUILDDIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (echo [ship] ERROR: configure failed for x86_64 & exit /b 1)
"%CMAKE%" --build "%BUILDDIR%"
if errorlevel 1 (echo [ship] ERROR: build failed for x86_64 & exit /b 1)

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
copy /y "%BUILDDIR%\opendoc.exe" "%STAGE%\" >nul || exit /b 1
copy /y "%BUILDDIR%\libopendoc_sdk.dll" "%STAGE%\" >nul || exit /b 1
rem MinGW runtime dependencies
for %%D in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libyaml-cpp.dll) do (
    if exist "%UCRT%\%%D" (
        copy /y "%UCRT%\%%D" "%STAGE%\" >nul
    ) else (
        echo [ship] WARNING: %UCRT%\%%D not found
    )
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\verify-pe.ps1" -Path "%STAGE%\opendoc.exe" -ExpectMachine x64 -RequireDir "%STAGE%"
if errorlevel 1 (echo [ship] ERROR: x86_64 opendoc.exe failed verification & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\verify-pe.ps1" -Path "%STAGE%\libopendoc_sdk.dll" -ExpectMachine x64 -RequireDir "%STAGE%"
if errorlevel 1 (echo [ship] ERROR: x86_64 libopendoc_sdk.dll failed verification & exit /b 1)

rem smoke test: must run from the stage dir so bundled DLLs are picked up
pushd "%STAGE%"
opendoc.exe --version
set "SMOKE=!errorlevel!"
popd
if not "!SMOKE!"=="0" (echo [ship] ERROR: x86_64 smoke test failed & exit /b 1)

if exist "%ZIP%" del "%ZIP%"
tar -a -cf "%ZIP%" -C "%STAGE%" .
if errorlevel 1 (echo [ship] ERROR: failed to create %ZIP% & exit /b 1)
echo [ship] created %ZIP%
exit /b 0

rem =========================================================================
rem  Windows ARM64 (cross-compiled with clang + clangarm64 sysroot)
rem =========================================================================
:build_winarm64
echo.
echo [ship] === Windows ARM64 ===
set "BUILDDIR=%ROOT%\build-ship-winarm64"
set "STAGE=%BUILDDIR%\stage"
set "SYSROOT=%ROOT%\tools\sysroot-aarch64\clangarm64"
set "ZIP=%SHIPDIR%\OpenDoc-%VERSION%-windows-arm64.zip"

if not exist "%ROOT%\tools\sysroot-aarch64\.ready" (
    echo [ship] fetching ARM64 sysroot...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\fetch-sysroot.ps1"
    if errorlevel 1 (echo [ship] ERROR: sysroot fetch failed & exit /b 1)
)

"%CMAKE%" -S "%ROOT%" -B "%BUILDDIR%" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-aarch64.cmake" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (echo [ship] ERROR: configure failed for ARM64 & exit /b 1)
"%CMAKE%" --build "%BUILDDIR%"
if errorlevel 1 (echo [ship] ERROR: build failed for ARM64 & exit /b 1)

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
copy /y "%BUILDDIR%\opendoc.exe" "%STAGE%\" >nul || exit /b 1
copy /y "%BUILDDIR%\libopendoc_sdk.dll" "%STAGE%\" >nul || exit /b 1
rem clangarm64 runtime dependencies (libc++, libwinpthread, libyaml-cpp)
copy /y "%SYSROOT%\bin\*.dll" "%STAGE%\" >nul || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\verify-pe.ps1" -Path "%STAGE%\opendoc.exe" -ExpectMachine ARM64 -RequireDir "%STAGE%"
if errorlevel 1 (echo [ship] ERROR: ARM64 opendoc.exe failed verification & exit /b 1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\verify-pe.ps1" -Path "%STAGE%\libopendoc_sdk.dll" -ExpectMachine ARM64 -RequireDir "%STAGE%"
if errorlevel 1 (echo [ship] ERROR: ARM64 libopendoc_sdk.dll failed verification & exit /b 1)

if exist "%ZIP%" del "%ZIP%"
tar -a -cf "%ZIP%" -C "%STAGE%" .
if errorlevel 1 (echo [ship] ERROR: failed to create %ZIP% & exit /b 1)
echo [ship] created %ZIP%
exit /b 0

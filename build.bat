@echo off
REM Build String Theory. Default: Standalone EXE only.
REM Pass "vst" (or "all") as first argument to also build the VST3.
setlocal

set CMAKE=C:/Users/admin/.workbuddy/binaries/cmake/cmake-3.30.5-windows-x86_64/bin/cmake.exe
set SRC=%~dp0

set BUILD_VST=0
if /i "%~1"=="vst" set BUILD_VST=1
if /i "%~1"=="all" set BUILD_VST=1

cd /d "%SRC%"

set https_proxy=
set HTTPS_PROXY=
set http_proxy=
set HTTP_PROXY=

if not exist build (
    echo [1/3] Configure...
    "%CMAKE%" -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 goto fail
)

echo [2/3] Build Standalone ...
"%CMAKE%" --build build --config Release --target StringTheory_Standalone --parallel
if errorlevel 1 goto fail

if "%BUILD_VST%"=="1" goto buildvst
echo [3/3] Skipping VST3 (pass "vst" to build it)
goto done

:buildvst
REM Windows cannot overwrite an installed .vst3 (Permission denied) but allows rename.
set VST3SYS=C:\Program Files\Common Files\VST3\String Theory.vst3
if exist "%VST3SYS%" (
    rd /s /q "%VST3SYS%.old" 2>nul
    move /y "%VST3SYS%" "%VST3SYS%.old" >nul
)

echo [3/3] Build VST3 ...
"%CMAKE%" --build build --config Release --target StringTheory_VST3 --parallel
if errorlevel 1 goto fail

if exist "%VST3SYS%.old" rd /s /q "%VST3SYS%.old" 2>nul

:done
echo.
echo === DONE ===
echo Standalone: %SRC%build\StringTheory_artefacts\Release\Standalone\String Theory.exe
if "%BUILD_VST%"=="1" echo VST3:       %SRC%build\StringTheory_artefacts\Release\VST3
exit /b 0

:fail
echo BUILD FAILED
exit /b 1

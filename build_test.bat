@echo off
setlocal
set CMAKE=C:/Users/admin/.workbuddy/binaries/cmake/cmake-3.30.5-windows-x86_64/bin/cmake.exe
cd /d "%~dp0"
set https_proxy=
set HTTPS_PROXY=
set http_proxy=
set HTTP_PROXY=
set ALL_PROXY=
set all_proxy=
"%CMAKE%" --build build --config Release --target StringAudioCheck --parallel

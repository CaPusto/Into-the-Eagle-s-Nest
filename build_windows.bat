@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build

echo Building for Windows...
cmake -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -A x64 "%SCRIPT_DIR%"
cmake --build "%BUILD_DIR%" --config Release

echo Build complete: %BUILD_DIR%\dist\eaglenest.exe
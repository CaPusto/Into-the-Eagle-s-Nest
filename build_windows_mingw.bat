@echo off
setlocal enabledelayedexpansion

set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
set "PKG_CONFIG_PATH=C:\msys64\mingw64\lib\pkgconfig"

set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build

echo Cleaning old build...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

echo Building for Windows via MSYS2 (MinGW)...

cmake -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles" %SCRIPT_DIR%
cmake --build "%BUILD_DIR%" --config Release

echo Build complete: %BUILD_DIR%\dist\eaglenest.exe
pause

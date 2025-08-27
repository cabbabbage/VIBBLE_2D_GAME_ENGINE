@echo off
setlocal enabledelayedexpansion

set "EXTRA_ARGS="
set "TOOL_MODE=0"

echo Cleaning previous CMake cache...
rmdir /s /q build >nul 2>&1
mkdir build

echo Configuring project...
cmake -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%cd%\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake" ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%cd%\\ENGINE" ^
  -B build -S .
if errorlevel 1 (
    echo ❌ CMake configuration failed.
    pause & exit /b 1
)

echo Building project...
cmake --build build --config Release
if errorlevel 1 (
    echo ❌ Build failed.
    pause & exit /b 1
)

set EXE1=%~dp0ENGINE\\Release\\engine.exe
set EXE2=%~dp0build\\Release\\engine.exe

echo Launching built game...
if exist "%EXE1%" (
    "%EXE1%" %EXTRA_ARGS%
) else if exist "%EXE2%" (
    "%EXE2%" %EXTRA_ARGS%
) else (
    echo Executable not found at "%EXE1%" or "%EXE2%"
)

pause
endlocal

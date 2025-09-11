@echo off
setlocal enabledelayedexpansion

rem Ensure the working directory is the repo root (folder of this script)
pushd "%~dp0" >nul

set "EXTRA_ARGS="

echo [run.bat] Preparing build directory...
if not exist build mkdir build

echo [run.bat] Configuring (Visual Studio 2022, x64)...
set "TOOLCHAIN_ARG="
if exist "%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake -G "Visual Studio 17 2022" -A x64 ^
  %TOOLCHAIN_ARG% ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%cd%\ENGINE" ^
  -S . -B build
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    popd & pause & exit /b 1
)

echo [run.bat] Building (RelWithDebInfo)...
cmake --build build --config RelWithDebInfo -- /m
if errorlevel 1 (
    echo [ERROR] Build failed.
    popd & pause & exit /b 1
)

set "EXE=%cd%\ENGINE\engine.exe"
if not exist "%EXE%" (
    echo [WARN] Expected exe not found at "%EXE%". Trying alt locations...
    if exist "%cd%\build\RelWithDebInfo\engine.exe" set "EXE=%cd%\build\RelWithDebInfo\engine.exe"
    if exist "%cd%\build\Release\engine.exe" set "EXE=%cd%\build\Release\engine.exe"
)

if not exist "%EXE%" (
    echo [ERROR] Executable not found. Build may have failed.
    popd & pause & exit /b 1
)

echo [run.bat] Launching: "%EXE%"
rem Run with repo root as working directory so relative paths (MAPS/, loading/, MISC_CONTENT/) resolve
"%EXE%" %EXTRA_ARGS%

popd >nul
endlocal

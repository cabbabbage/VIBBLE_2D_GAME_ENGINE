@echo off
setlocal enabledelayedexpansion

rem Ensure the working directory is the repo root (folder of this script)
pushd "%~dp0" >nul

set "EXTRA_ARGS="

echo [run.bat] Preparing build directory...
if not exist build mkdir build

rem Allow overriding the generator (e.g., set GENERATOR=Ninja)
set "GENERATOR=%GENERATOR%"
if "%GENERATOR%"=="" set "GENERATOR=Visual Studio 17 2022"

rem If there is an existing CMakeCache, check for generator mismatch
set "CACHE_GEN="
if exist build\CMakeCache.txt (
  for /f "tokens=2 delims==" %%G in ('findstr /C:"CMAKE_GENERATOR:" "build\CMakeCache.txt"') do set "CACHE_GEN=%%G"
)

if not "%CACHE_GEN%"=="" (
  if /I not "%CACHE_GEN%"=="%GENERATOR%" (
    echo [run.bat] Detected generator mismatch: cache="%CACHE_GEN%" vs desired="%GENERATOR%". Cleaning build/ ...
    rmdir /s /q build 2>nul
    mkdir build
  )
)

rem Only pass -A x64 for Visual Studio generators
set "ARCH_ARG=-A x64"
echo %GENERATOR% | findstr /I "Visual Studio" >nul
if errorlevel 1 set "ARCH_ARG="

echo [run.bat] Configuring (%GENERATOR% %ARCH_ARG%)...
set "TOOLCHAIN_ARG="
if exist "%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake -G "%GENERATOR%" %ARCH_ARG% ^
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

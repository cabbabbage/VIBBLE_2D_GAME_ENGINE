@echo off
setlocal enabledelayedexpansion

rem =========================
rem VIBBLE Engine - run.bat
rem Ninja-only build script
rem =========================

rem Always start in repo root
pushd "%~dp0" >nul

set "EXTRA_ARGS="

echo [run.bat] Preparing build directory...
if exist build\NUL (
  rem If you ever switch compilers/generators, a stale cache hurts; keep it simple:
  echo [run.bat] Cleaning stale build/ ...
  rmdir /s /q build 2>nul
)
mkdir build 2>nul

rem ----------------------------------------------------
rem FORCE Ninja (no Visual Studio generator anywhere)
rem ----------------------------------------------------
set "GENERATOR=Ninja"
set "CMAKE_GENERATOR=%GENERATOR%"
set "ARCH_ARG="
echo [run.bat] Forcing generator: %GENERATOR%

rem ----------------------------------------------------
rem Ensure MSVC toolchain env is loaded if installed
rem (vcpkg requires a Windows toolchain; Ninja is just the build driver)
rem ----------------------------------------------------
set "VS_DEV_CMD="
for %%E in (BuildTools Community Professional Enterprise) do (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat"
    goto :found_vs
  )
)
:found_vs
if not "%VS_DEV_CMD%"=="" (
  echo [run.bat] Loading MSVC environment: "%VS_DEV_CMD%" (x64)
  call "%VS_DEV_CMD%" -arch=x64 >nul
) else (
  echo [run.bat] NOTE: MSVC Build Tools not detected. vcpkg/cmake will fail until they are installed.
)

rem ----------------------------------------------------
rem Toolchain (vcpkg manifest mode)
rem ----------------------------------------------------
set "TOOLCHAIN_ARG="
if exist "%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake" (
  set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake"
  set "VCPKG_FEATURE_FLAGS=manifests"
)

rem ----------------------------------------------------
rem Configure (single-config generator → set CMAKE_BUILD_TYPE)
rem ----------------------------------------------------
echo [run.bat] Configuring (%GENERATOR%)...
cmake -G "%GENERATOR%" ^
  %TOOLCHAIN_ARG% ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%cd%\ENGINE" ^
  -S . -B build
if errorlevel 1 (
  echo [ERROR] CMake configuration failed.
  popd & pause & exit /b 1
)

rem ----------------------------------------------------
rem Build (Ninja ignores --config; kept harmless)
rem ----------------------------------------------------
echo [run.bat] Building (RelWithDebInfo)...
cmake --build build --config RelWithDebInfo
if errorlevel 1 (
  echo [ERROR] Build failed.
  popd & pause & exit /b 1
)

rem ----------------------------------------------------
rem Locate exe
rem ----------------------------------------------------
set "EXE=%cd%\ENGINE\engine.exe"
if not exist "%EXE%" (
  if exist "%cd%\build\engine.exe" set "EXE=%cd%\build\engine.exe"
)
if not exist "%EXE%" (
  echo [ERROR] Executable not found. Build may have failed.
  popd & pause & exit /b 1
)

rem ----------------------------------------------------
rem Create Desktop Shortcut to engine.exe
rem ----------------------------------------------------
set "DESKTOP=%USERPROFILE%\Desktop"
set "SHORTCUT=%DESKTOP%\VI.lnk"
set "ICONFILE=%cd%\MISC_CONTENT\vibble.ico"

rem Extract just the folder path of the exe
for %%I in ("%EXE%") do set "EXE_DIR=%%~dpI"

echo [run.bat] Creating Desktop shortcut...

powershell -Command ^
  "$s=(New-Object -COM WScript.Shell).CreateShortcut('%SHORTCUT%');" ^
  "$s.TargetPath='%EXE%';" ^
  "$s.WorkingDirectory='%EXE_DIR%';" ^
  "$s.IconLocation='%ICONFILE%';" ^
  "$s.Save()"


echo [run.bat] Launching: "%EXE%"
"%EXE%" %EXTRA_ARGS%

popd >nul
endlocal

@echo off
setlocal enabledelayedexpansion

rem =========================
rem VIBBLE Engine - run.bat
rem Local build + run using CMakePresets + auto-vcpkg
rem =========================

pushd "%~dp0" >nul

set "BUILD_CONFIG=RelWithDebInfo"
set "EXTRA_ARGS="

rem ----------------------------------------------------
rem Ensure vcpkg exists (clone if missing)
rem ----------------------------------------------------
set "LOCAL_VCPKG=%cd%\vcpkg"
if not exist "%LOCAL_VCPKG%\scripts\buildsystems\vcpkg.cmake" (
    echo [run.bat] vcpkg not found, cloning...
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "%LOCAL_VCPKG%"
    if errorlevel 1 (
        echo [ERROR] Failed to clone vcpkg repository.
        goto :fail
    )
    pushd "%LOCAL_VCPKG%" >nul
    call bootstrap-vcpkg.bat -disableMetrics
    if errorlevel 1 (
        popd >nul
        echo [ERROR] vcpkg bootstrap failed.
        goto :fail
    )
    popd >nul
)

rem ----------------------------------------------------
rem Install manifest dependencies (SDL2, etc.) from vcpkg.json
rem ----------------------------------------------------
if exist "%LOCAL_VCPKG%\vcpkg.exe" (
    echo [run.bat] Resolving manifest dependencies with vcpkg...
    "%LOCAL_VCPKG%\vcpkg.exe" install --triplet x64-windows --feature-flags=manifests,binarycaching
    if errorlevel 1 (
        echo [ERROR] vcpkg install failed.
        goto :fail
    )
) else (
    echo [ERROR] vcpkg.exe not found after bootstrap.
    goto :fail
)

rem ----------------------------------------------------
rem Configure + Build via CMakePresets.json
rem Requires a preset named "windows-vcpkg" and a build preset "windows-vcpkg-release"
rem ----------------------------------------------------
if not exist "%cd%\CMakePresets.json" (
    echo [ERROR] CMakePresets.json not found in repo root.
    goto :fail
)

echo [run.bat] Configuring with preset: windows-vcpkg
cmake --preset windows-vcpkg
if errorlevel 1 goto :fail

echo [run.bat] Building with preset: windows-vcpkg-release (%BUILD_CONFIG%)
cmake --build --preset windows-vcpkg-release --config %BUILD_CONFIG%
if errorlevel 1 goto :fail

rem ----------------------------------------------------
rem Locate exe (handle both Ninja and VS generators, and optional RUNTIME_OUTPUT dir)
rem ----------------------------------------------------
set "EXE="
if exist "%cd%\ENGINE\engine.exe" set "EXE=%cd%\ENGINE\engine.exe"
if not defined EXE if exist "%cd%\build\%BUILD_CONFIG%\engine.exe" set "EXE=%cd%\build\%BUILD_CONFIG%\engine.exe"
if not defined EXE if exist "%cd%\build\engine.exe" set "EXE=%cd%\build\engine.exe"

if not defined EXE (
    echo [ERROR] Executable not found in ENGINE\, build\%BUILD_CONFIG%\ or build\ .
    goto :fail
)

rem ----------------------------------------------------
rem Create Desktop Shortcut
rem ----------------------------------------------------
set "DESKTOP=%USERPROFILE%\Desktop"
set "SHORTCUT=%DESKTOP%\VI.lnk"
set "ICONFILE=%cd%\MISC_CONTENT\vibble.ico"
set "ROOT_DIR=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s=(New-Object -COM WScript.Shell).CreateShortcut('%SHORTCUT%');" ^
  "$s.TargetPath='%EXE%';" ^
  "$s.WorkingDirectory='%ROOT_DIR%';" ^
  "$s.IconLocation='%ICONFILE%';" ^
  "$s.Save()"

echo [run.bat] Launching: "%EXE%"
"%EXE%" %EXTRA_ARGS%

popd >nul
exit /b 0

:fail
echo [run.bat] Build failed.
popd >nul
pause
exit /b 1

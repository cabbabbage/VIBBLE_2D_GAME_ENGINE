@echo off
setlocal enabledelayedexpansion

if "%~1"=="__RUN__" (
    shift
    goto :main
)

set "RUN_LOG=%~dp0log.txt"
type nul > "%RUN_LOG%"
set "SCRIPT_PATH=%~f0"

cmd /v:on /c call "%SCRIPT_PATH%" __RUN__ %* 2>&1 | powershell -NoProfile -Command ^
  "$input | Tee-Object -FilePath '%RUN_LOG%'; exit $LASTEXITCODE"
exit /b %ERRORLEVEL%

:main
rem =========================
rem VIBBLE Engine - run.bat
rem Build and launch only
rem =========================

pushd "%~dp0" >nul
set "REPO_ROOT=%CD%"

set "BUILD_CONFIG=RelWithDebInfo"
set "EXTRA_ARGS="

if not defined VIBBLE_SAFE_LOADING (
    set "VIBBLE_SAFE_LOADING=1"
    echo [run.bat] SAFE loading enabled (VIBBLE_SAFE_LOADING=1)
)

cd /d "%REPO_ROOT%"

rem Check prerequisites
if not exist "%REPO_ROOT%\CMakePresets.json" (
    echo [ERROR] CMakePresets.json not found. Run setup.bat first.
    goto :fail
)
if not exist "%REPO_ROOT%\CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found in repo root.
    goto :fail
)
set "LOCAL_VCPKG=%REPO_ROOT%\vcpkg"
if not exist "%LOCAL_VCPKG%\vcpkg.exe" (
    echo [ERROR] vcpkg not found. Run setup.bat first.
    goto :fail
)

rem Load MSVC dev environment for this shell
call :EnsureDevShell || ( echo [ERROR] MSVC not available. Run setup.bat. & goto :fail )

rem Verify cmake and ninja are present
where cmake >nul 2>&1 || ( echo [ERROR] CMake not found. Run setup.bat. & goto :fail )
where ninja >nul 2>&1 || ( echo [ERROR] Ninja not found. Run setup.bat. & goto :fail )

rem Configure and build
cd /d "%REPO_ROOT%"
echo [run.bat] Configuring with preset: windows-vcpkg
cmake --preset windows-vcpkg || goto :fail

echo [run.bat] Building with preset: windows-vcpkg-release (%BUILD_CONFIG%)
cmake --build --preset windows-vcpkg-release --config %BUILD_CONFIG% || goto :fail

rem Collect artifacts
set "RELEASE_DIR=%REPO_ROOT%\release"
if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%" || goto :fail
for %%P in ("%RELEASE_DIR%\*.exe" "%RELEASE_DIR%\*.dll" "%RELEASE_DIR%\*.pdb") do ( if exist %%~P del /q %%~P >nul 2>&1 )

call :CollectArtifacts "%REPO_ROOT%"
call :CollectArtifacts "%REPO_ROOT%\ENGINE"
call :CollectArtifacts "%REPO_ROOT%\build\%BUILD_CONFIG%"
call :CollectArtifacts "%REPO_ROOT%\build"

set "EXE=%RELEASE_DIR%\engine.exe"
if not exist "%EXE%" (
    echo [ERROR] Executable not found in release directory.
    goto :fail
)

rem Clean up .txt files except log.txt and CMakeLists.txt
echo [run.bat] Deleting *.txt under repo except log.txt and CMakeLists.txt...
for /r "%REPO_ROOT%" %%F in (*.txt) do (
    if /I not "%%~nxF"=="log.txt" if /I not "%%~nxF"=="CMakeLists.txt" del /q "%%~fF" >nul 2>&1
)

rem Desktop shortcut
set "DESKTOP=%USERPROFILE%\Desktop"
set "SHORTCUT=%DESKTOP%\VI.lnk"
set "ICONFILE=%REPO_ROOT%\SRC\MISC_CONTENT\vibble.ico"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s=(New-Object -COM WScript.Shell).CreateShortcut('%SHORTCUT%');$s.TargetPath='%EXE%';$s.WorkingDirectory='%REPO_ROOT%';$s.IconLocation='%ICONFILE%';$s.Save()"

echo [run.bat] Launching: "%EXE%"
"%EXE%" %EXTRA_ARGS%

popd >nul
exit /b 0

:CollectArtifacts
set "SRC_DIR=%~1"
if not exist "%SRC_DIR%" goto :eof
for %%E in (exe dll pdb) do (
    for /f "delims=" %%F in ('dir /b "%SRC_DIR%\*.%%E" 2^>nul') do move /y "%SRC_DIR%\%%F" "%RELEASE_DIR%" >nul
)
goto :eof

:EnsureDevShell
where cl >nul 2>&1 && (echo [run.bat] MSVC already on PATH. & exit /b 0)
set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT if exist "C:\VS2022\BuildTools" set "VSROOT=C:\VS2022\BuildTools"
if defined VSROOT (
    echo [run.bat] Loading dev environment from "%VSROOT%"...
    if exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" (
        call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64
    ) else if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
        call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
    )
)
where cl >nul 2>&1 && (echo [run.bat] MSVC toolchain loaded. & exit /b 0)
exit /b 1

:fail
echo [run.bat] Build failed.
popd >nul
pause
exit /b 1

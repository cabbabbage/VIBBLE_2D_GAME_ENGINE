@echo off
setlocal enabledelayedexpansion

set "LOG_FILE=%~dp0log.txt"
type nul > "%LOG_FILE%"
set "SCRIPT_PATH=%~f0"

if not defined VIBBLE_SUPPRESS_PAUSE set "VIBBLE_SUPPRESS_PAUSE=1"

if "%~1"=="__RUN__" (
    shift
    goto :main
)

cmd /v:on /c call "%SCRIPT_PATH%" __RUN__ 2>&1 | powershell -NoProfile -Command ^
  "$input | Tee-Object -FilePath '%LOG_FILE%'; exit $LASTEXITCODE"

exit /b %ERRORLEVEL%

:main
pushd "%~dp0" >nul

set "BUILD_CONFIG=RelWithDebInfo"
set "EXTRA_ARGS="
set "REPO_ROOT=%CD%"

if not defined VIBBLE_SAFE_LOADING (
    set "VIBBLE_SAFE_LOADING=1"
    echo [run.bat] SAFE loading enabled (VIBBLE_SAFE_LOADING=%VIBBLE_SAFE_LOADING%)
)

if not exist "%REPO_ROOT%\CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found at repo root: "%REPO_ROOT%\CMakeLists.txt"
    echo        Make sure you are running run.bat from the project root.
    goto :non_compile_fail
)

set "SETUP_JSON_FILE=%REPO_ROOT%\setup.json"
set "NEED_SETUP=0"

if exist "%SETUP_JSON_FILE%" (
    powershell -NoProfile -Command ^
      "try{$j=Get-Content '%SETUP_JSON_FILE%' -Raw|ConvertFrom-Json;if($j.status -eq 'SUCCESS'){exit 0}else{exit 1}}catch{exit 1}"
    if errorlevel 1 set "NEED_SETUP=1"
) else (
    set "NEED_SETUP=1"
)

if "%NEED_SETUP%"=="1" (
    echo [run.bat] Running setup.bat...
    call "%REPO_ROOT%\setup.bat"
    if errorlevel 1 goto :fail
)

call :EnsureDevShell
if errorlevel 1 goto :non_compile_fail

set "LOCAL_VCPKG=%REPO_ROOT%\vcpkg"
if exist "%LOCAL_VCPKG%\scripts\buildsystems\vcpkg.cmake" (
    set "VCPKG_ROOT=%LOCAL_VCPKG%"
)

if not exist "%REPO_ROOT%\CMakePresets.json" (
    echo [ERROR] CMakePresets.json not found in repo root.
    goto :non_compile_fail
)

set "CMAKE_CMD="
call :LocateCMake
if not defined CMAKE_CMD (
    echo [ERROR] CMake executable not found.
    goto :non_compile_fail
)

for %%P in ("%CMAKE_CMD%") do set "CMAKE_DIR=%%~dpP"
if defined CMAKE_DIR set "PATH=%CMAKE_DIR%;%PATH%"

echo [run.bat] Using CMake from: %CMAKE_CMD%

echo [run.bat] Configuring with preset: windows-vcpkg
"%CMAKE_CMD%" --preset windows-vcpkg
if errorlevel 1 goto :non_compile_fail

echo [run.bat] Building with preset: windows-vcpkg-release (%BUILD_CONFIG%)
"%CMAKE_CMD%" --build --preset windows-vcpkg-release --config %BUILD_CONFIG%
if errorlevel 1 goto :fail

set "RELEASE_DIR=%REPO_ROOT%\release"
if not exist "%RELEASE_DIR%" (
    mkdir "%RELEASE_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to create release directory.
        goto :non_compile_fail
    )
)

for %%P in ("%RELEASE_DIR%\*.exe" "%RELEASE_DIR%\*.dll" "%RELEASE_DIR%\*.pdb") do (
    if exist %%~P del /q %%~P >nul 2>&1
)

call :CollectArtifacts "%REPO_ROOT%"
call :CollectArtifacts "%REPO_ROOT%\ENGINE"
call :CollectArtifacts "%REPO_ROOT%\build\%BUILD_CONFIG%"
call :CollectArtifacts "%REPO_ROOT%\build"

set "EXE=%RELEASE_DIR%\engine.exe"

if not exist "%EXE%" (
    echo [ERROR] Executable not found in release directory.
    goto :non_compile_fail
)

echo [run.bat] Deleting all *.txt files (recursively) except log.txt and CMakeLists.txt...
for /r "%REPO_ROOT%" %%F in (*.txt) do (
    if /I not "%%~nxF"=="log.txt" if /I not "%%~nxF"=="CMakeLists.txt" (
        del /q "%%~fF" >nul 2>&1
    )
)

echo [run.bat] Deleting all *.ilk files (recursively)...
for /r "%REPO_ROOT%" %%F in (*.ilk) do (
    del /q "%%~fF" >nul 2>&1
)

set "DESKTOP=%USERPROFILE%\Desktop"
set "SHORTCUT=%DESKTOP%\VI.lnk"
set "ICONFILE=%REPO_ROOT%\SRC\MISC_CONTENT\vibble.ico"
set "ROOT_DIR=%REPO_ROOT%"

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

:CollectArtifacts
set "SRC_DIR=%~1"
if not exist "%SRC_DIR%" goto :eof
for %%E in (exe dll pdb) do (
    for /f "delims=" %%F in ('dir /b "%SRC_DIR%\*.%%E" 2^>nul') do (
        move /y "%SRC_DIR%\%%F" "%RELEASE_DIR%" >nul
    )
)
goto :eof

:EnsureDevShell
where cl >nul 2>&1 && (echo [run.bat] MSVC already on PATH. & exit /b 0)

set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=* delims=" %%I in (`
      "%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -property installationPath
    `) do set "VSROOT=%%I"
)
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
echo [ERROR] Could not load MSVC dev environment.
exit /b 1

:LocateCMake
set "CMAKE_CMD="

for /f "delims=" %%I in ('where cmake 2^>nul') do (
    if not defined CMAKE_CMD set "CMAKE_CMD=%%~fI"
)
if defined CMAKE_CMD goto :locate_done

for %%P in (
    "%ProgramFiles%\CMake\bin\cmake.exe"
    "C:\Program Files\CMake\bin\cmake.exe"
    "C:\Program Files (x86)\CMake\bin\cmake.exe"
    "%ProgramFiles(x86)%\CMake\bin\cmake.exe"
) do (
    if not defined CMAKE_CMD if exist %%~P set "CMAKE_CMD=%%~fP"
)

:locate_done
if defined CMAKE_CMD (
    for %%Q in ("%CMAKE_CMD%") do if exist %%~fQ set "CMAKE_CMD=%%~fQ"
)
exit /b 0

:non_compile_fail
echo [run.bat] Non compilation error detected. Running setup.bat...
call "%REPO_ROOT%\setup.bat"
goto :fail

:fail
echo [run.bat] Build failed.
popd >nul
if not defined VIBBLE_SUPPRESS_PAUSE pause
exit /b 1

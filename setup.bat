@echo off
setlocal enabledelayedexpansion

if "%~1"=="__RUN__" (
    shift
    goto :main
)

set "SETUP_LOG=%~dp0setup_log.txt"
type nul > "%SETUP_LOG%"
set "SCRIPT_PATH=%~f0"

if not defined VIBBLE_SUPPRESS_PAUSE set "VIBBLE_SUPPRESS_PAUSE=1"
cmd /v:on /c call "%SCRIPT_PATH%" __RUN__ %* 2>&1 | powershell -NoProfile -Command ^
  "$input | Tee-Object -FilePath '%SETUP_LOG%'; exit $LASTEXITCODE"
exit /b %ERRORLEVEL%

:main
rem =========================
rem VIBBLE Engine - setup.bat
rem One time install of toolchain and deps
rem =========================

rem Pin repo root and keep it fixed
pushd "%~dp0" >nul
set "REPO_ROOT=%CD%"

set "VSBT_INSTALL_DIR=C:\VS2022\BuildTools"
set "INSTALL_TIMEOUT_SECS=5400"

rem Always work from repo root
cd /d "%REPO_ROOT%"

rem Ensure winget available
call :EnsureWinget || goto :fail

rem Ensure Git
call :EnsureGit  || goto :fail

rem Install Visual Studio Build Tools with C++ workload and SDKs
call :EnsureVSBuildTools || goto :fail

rem Load MSVC dev environment for vcpkg bootstrap
call :EnsureDevShell || goto :fail
cd /d "%REPO_ROOT%"

rem Ensure CMake and Ninja
call :EnsureCMake || goto :fail
call :EnsureNinja || goto :fail

rem Ensure vcpkg and bootstrap
set "LOCAL_VCPKG=%REPO_ROOT%\vcpkg"
if not exist "%LOCAL_VCPKG%\scripts\buildsystems\vcpkg.cmake" (
    echo [setup.bat] Cloning vcpkg...
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "%LOCAL_VCPKG%" || goto :fail
)
if not exist "%LOCAL_VCPKG%\vcpkg.exe" (
    echo [setup.bat] Bootstrapping vcpkg...
    pushd "%LOCAL_VCPKG%" >nul
    call bootstrap-vcpkg.bat -disableMetrics || (popd >nul & goto :fail)
    popd >nul
)
set "VCPKG_ROOT=%LOCAL_VCPKG%"

rem Update builtin-baseline in vcpkg.json if present
if exist "%REPO_ROOT%\vcpkg.json" (
    echo [setup.bat] Updating vcpkg baseline...
    "%LOCAL_VCPKG%\vcpkg.exe" x-update-baseline
    if errorlevel 1 (
        pushd "%LOCAL_VCPKG%" >nul
        for /f "delims=" %%H in ('git rev-parse HEAD') do set "NEW_BASELINE=%%H"
        popd >nul
        if not defined NEW_BASELINE ( echo [ERROR] Could not resolve vcpkg HEAD. & goto :fail )
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
          "$p = '%REPO_ROOT%\vcpkg.json'; $b = $env:NEW_BASELINE; $j = Get-Content $p -Raw | ConvertFrom-Json; if(-not $j){$j=@{}}; $j.'builtin-baseline'=$b; ($j|ConvertTo-Json -Depth 100)|Set-Content $p -NoNewline" || goto :fail
    )
    powershell -NoProfile -Command ^
      "$b=(Get-Content '%REPO_ROOT%\vcpkg.json' -Raw|ConvertFrom-Json).'builtin-baseline'; if($b -and $b -match '^[0-9a-fA-F]{40}$'){exit 0}else{exit 1}" || (echo [ERROR] builtin-baseline invalid. & goto :fail)
) else (
    echo [setup.bat] vcpkg.json not found, skipping baseline update.
)

rem Install manifest dependencies
if exist "%REPO_ROOT%\vcpkg.json" (
    echo [setup.bat] Installing vcpkg manifest dependencies...
    "%LOCAL_VCPKG%\vcpkg.exe" install --triplet x64-windows --feature-flags=manifests,binarycaching || goto :fail
) else (
    echo [setup.bat] No vcpkg.json found. You can still build if your CMake presets do not use manifests.
)

echo [setup.bat] Setup complete.
popd >nul
exit /b 0

:EnsureWinget
where winget >nul 2>&1 && exit /b 0
echo [ERROR] winget is not available. Install App Installer from Microsoft Store.
exit /b 1

:EnsureGit
git --version >nul 2>&1 && ( echo [setup.bat] Git is installed. & exit /b 0 )
echo [setup.bat] Installing Git via winget...
winget install --id Git.Git -e --source winget --silent || (echo [ERROR] Git install failed. & exit /b 1)
git --version >nul 2>&1 && echo [setup.bat] Git installed and on PATH.
exit /b 0

:EnsureVSBuildTools
where cl >nul 2>&1 && ( echo [setup.bat] MSVC toolchain already available. & exit /b 0 )

call :DetectVSInstallPath
if not errorlevel 1 (
    echo [setup.bat] Visual Studio Build Tools with required workloads detected.
    exit /b 0
)

echo [setup.bat] Installing Visual Studio 2022 Build Tools via winget...
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements --silent ^
  --override "--installPath \"%VSBT_INSTALL_DIR%\" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621" || (
    echo [setup.bat] Retrying Visual Studio Build Tools install with Windows 10 SDK...
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements --silent ^
      --override "--installPath \"%VSBT_INSTALL_DIR%\" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows10SDK.19041" || (
        echo [ERROR] VS Build Tools install failed.
        exit /b 1
      )
  )

call :DetectVSInstallPath
if errorlevel 1 (
    echo [ERROR] Visual Studio Build Tools not found after winget install.
    exit /b 1
)

echo [setup.bat] Visual Studio Build Tools installed.
exit /b 0

:DetectVSInstallPath
set "VS_INSTALL_PATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -requires Microsoft.VisualStudio.Workload.VCTools -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath`) do set "VS_INSTALL_PATH=%%I"
)
if not defined VS_INSTALL_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools" set "VS_INSTALL_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if not defined VS_INSTALL_PATH if exist "%VSBT_INSTALL_DIR%" set "VS_INSTALL_PATH=%VSBT_INSTALL_DIR%"
if not defined VS_INSTALL_PATH if exist "C:\VS2022\BuildTools" set "VS_INSTALL_PATH=C:\VS2022\BuildTools"
if defined VS_INSTALL_PATH (
    exit /b 0
)
exit /b 1

:EnsureDevShell
where cl >nul 2>&1 && (echo [setup.bat] MSVC already on PATH. & exit /b 0)
set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT if exist "%VSBT_INSTALL_DIR%" set "VSROOT=%VSBT_INSTALL_DIR%"
if defined VSROOT (
    echo [setup.bat] Loading dev environment from "%VSROOT%"...
    if exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" (
        call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64
    ) else if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
        call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
    )
)
where cl >nul 2>&1 && (echo [setup.bat] MSVC toolchain loaded. & exit /b 0)
echo [ERROR] Could not load MSVC dev environment.
exit /b 1

:EnsureCMake
where cmake >nul 2>&1 && (
    echo [setup.bat] CMake is installed.
    call :CMakePostCheck
    exit /b 0
)

echo [setup.bat] Installing CMake...

rem Try winget first
winget -v >nul 2>&1
if "%errorlevel%"=="0" (
    echo [setup.bat] Installing CMake via winget...
    winget install --id Kitware.CMake -e --silent --accept-package-agreements --accept-source-agreements
    if "%errorlevel%"=="0" (
        echo [setup.bat] CMake installed via winget.
        goto :CMakePostCheck
    ) else (
        echo [setup.bat] winget install failed, trying MSI fallback.
        goto :CMakeMSI
    )
) else (
    echo [setup.bat] winget not found, trying MSI fallback.
    goto :CMakeMSI
)

:CMakeMSI
set "CMAKE_MSI=%TEMP%\cmake_latest_x64.msi"
echo [setup.bat] Downloading latest CMake MSI from GitHub...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$r=Invoke-RestMethod https://api.github.com/repos/Kitware/CMake/releases/latest; " ^
  "$asset=$r.assets | Where-Object { $_.name -match 'windows-x86_64\.msi$' } | Select-Object -First 1; " ^
  "if(-not $asset){Write-Error 'CMake MSI not found'; exit 1}; " ^
  "Invoke-WebRequest $asset.browser_download_url -OutFile '%CMAKE_MSI%'" || (
    echo [ERROR] Failed to download CMake MSI.
    exit /b 1
  )

echo [setup.bat] Installing CMake from MSI...
msiexec /i "%CMAKE_MSI%" /qn ADD_CMAKE_TO_PATH=System
set "msi_rc=%errorlevel%"
del "%CMAKE_MSI%" 2>nul
if not "%msi_rc%"=="0" (
  echo [ERROR] CMake MSI install failed with code %msi_rc%.
  exit /b %msi_rc%
)

:CMakePostCheck
rem Try to find and add CMake to PATH for this session and persist for the user
if exist "C:\Program Files\CMake\bin\cmake.exe" (
  call :AddToUserPath "C:\Program Files\CMake\bin"
) else if exist "C:\Program Files (x86)\CMake\bin\cmake.exe" (
  call :AddToUserPath "C:\Program Files (x86)\CMake\bin"
)

where cmake >nul 2>&1
if not "%errorlevel%"=="0" (
  echo [ERROR] CMake not found on PATH after installation.
  exit /b 1
)

echo [setup.bat] CMake installed successfully:
cmake --version
call :RecordCMakeHint
exit /b 0

:EnsureNinja
where ninja >nul 2>&1 && ( echo [setup.bat] Ninja is installed. & exit /b 0 )
echo [setup.bat] Installing Ninja via winget...
winget install -e --id Ninja-build.Ninja --source winget --silent || (echo [ERROR] Ninja install failed. & exit /b 1)
echo [setup.bat] Ninja installed.
exit /b 0

:AddToUserPath
rem %1 = directory to add
set "ADD_DIR=%~1"
if "%ADD_DIR%"=="" exit /b 0
if not exist "%ADD_DIR%\." exit /b 0

set "NEED_ADD=1"
for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','User')"`) do set "CUR_USER_PATH=%%P"
if defined CUR_USER_PATH (
    set "TMP=!CUR_USER_PATH:%ADD_DIR%=!"
    if /i "!TMP!" NEQ "!CUR_USER_PATH!" set "NEED_ADD=0"
)

if "!NEED_ADD!"=="1" (
    echo [setup.bat] Adding "%ADD_DIR%" to user PATH...
    powershell -NoProfile -Command ^
      "$p=[Environment]::GetEnvironmentVariable('Path','User');" ^
      "if([string]::IsNullOrEmpty($p)){$p='%ADD_DIR%'}" ^
      "elseif(-not ($p.Split(';') -contains '%ADD_DIR%')){$p=$p+';%ADD_DIR%'};" ^
      "[Environment]::SetEnvironmentVariable('Path',$p,'User')" >nul 2>&1
)
set "PATH=%ADD_DIR%;%PATH%"
exit /b 0

:fail
echo [setup.bat] Setup failed.
popd >nul
if not defined VIBBLE_SUPPRESS_PAUSE pause
exit /b 1

:RecordCMakeHint
set "CMAKE_FOUND="
for /f "delims=" %%I in ('where cmake 2^>nul') do (
    if not defined CMAKE_FOUND set "CMAKE_FOUND=%%~fI"
)
if not defined CMAKE_FOUND exit /b 0

set "CMAKE_HINT_DIR=%REPO_ROOT%\TEMP"
if not exist "%CMAKE_HINT_DIR%" mkdir "%CMAKE_HINT_DIR%" >nul 2>&1
set "CMAKE_HINT_FILE=%CMAKE_HINT_DIR%\cmake-path.txt"
(
    echo %CMAKE_FOUND%
) > "%CMAKE_HINT_FILE%"
exit /b 0

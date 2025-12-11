@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "SETUP_STATUS=%SCRIPT_DIR%setup.json"
set "VSBT_INSTALL_DIR=C:\VS2022\BuildTools"
set "VS_BOOT_URL=https://aka.ms/vs/17/release/vs_BuildTools.exe"
set "VS_BOOT_EXE=%TEMP%\vs_BuildTools.exe"
set "INSTALL_TIMEOUT_SECS=5400"

if "%~1"=="__RUN__" (
    shift
    goto :main
)

set "SCRIPT_PATH=%~f0"

cmd /v:on /c call "%SCRIPT_PATH%" __RUN__ %* 2>&1
set "RC=%ERRORLEVEL%"
call :WriteSetupStatus %RC%
exit /b %RC%

:main
pushd "%~dp0" >nul
set "REPO_ROOT=%CD%"
cd /d "%REPO_ROOT%"

call :EnsureWinget       || goto :fail
call :EnsureGit          || goto :fail
call :EnsureVSBuildTools || goto :fail
call :EnsureDevShell     || goto :fail
call :EnsureCMake        || goto :fail
call :EnsureNinja        || goto :fail

set "LOCAL_VCPKG=%REPO_ROOT%\vcpkg"

if exist "%LOCAL_VCPKG%" (
    echo [setup.bat] Nuking local vcpkg folder at "%LOCAL_VCPKG%"...

    tasklist /fi "IMAGENAME eq vcpkg.exe" | find /I "vcpkg.exe" >nul 2>&1
    if not errorlevel 1 (
        echo [setup.bat] Detected running vcpkg.exe. Attempting to terminate...
        taskkill /F /IM vcpkg.exe >nul 2>&1
    )

    rmdir /s /q "%LOCAL_VCPKG%"
)

echo [setup.bat] Cloning fresh vcpkg...
git clone --depth 1 https://github.com/microsoft/vcpkg.git "%LOCAL_VCPKG%"
if errorlevel 1 (
    echo [ERROR] Failed to clone vcpkg repository.
    goto :fail
)

echo [setup.bat] Bootstrapping vcpkg...
pushd "%LOCAL_VCPKG%" >nul
call bootstrap-vcpkg.bat -disableMetrics
if errorlevel 1 (
    popd >nul
    echo [ERROR] vcpkg bootstrap failed.
    goto :fail
)
popd >nul

set "VCPKG_ROOT=%LOCAL_VCPKG%"

if not exist "%LOCAL_VCPKG%\LICENSE.txt" (
    echo [setup.bat] vcpkg\LICENSE.txt missing, creating it for vcpkg-cmake...
    if exist "%LOCAL_VCPKG%\LICENSE" (
        copy /y "%LOCAL_VCPKG%\LICENSE" "%LOCAL_VCPKG%\LICENSE.txt" >nul
    ) else if exist "%LOCAL_VCPKG%\LICENSE.md" (
        copy /y "%LOCAL_VCPKG%\LICENSE.md" "%LOCAL_VCPKG%\LICENSE.txt" >nul
    ) else (
        echo Local vcpkg license placeholder> "%LOCAL_VCPKG%\LICENSE.txt"
    )
)

if exist "%REPO_ROOT%\vcpkg.json" (
    echo [setup.bat] Updating vcpkg baseline...

    "%LOCAL_VCPKG%\vcpkg.exe" x-update-baseline
    if errorlevel 1 (
        echo [setup.bat] x-update-baseline failed, falling back to manual baseline update...

        pushd "%LOCAL_VCPKG%" >nul
        for /f "delims=" %%H in ('git rev-parse HEAD') do set "NEW_BASELINE=%%H"
        popd >nul

        if not defined NEW_BASELINE (
            echo [ERROR] Could not resolve vcpkg HEAD commit. Baseline not updated.
            goto :fail
        )

        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
          "$p = 'vcpkg.json';" ^
          "$baseline = $env:NEW_BASELINE;" ^
          "if (-not $baseline -or $baseline.Length -ne 40 -or ($baseline -notmatch '^[0-9a-fA-F]{40}$')) { throw 'Invalid baseline in env:NEW_BASELINE' }" ^
          "$json = $null; try { $json = Get-Content $p -Raw | ConvertFrom-Json } catch {}" ^
          "if ($null -eq $json) { $json = [ordered]@{} }" ^
          "$json.'builtin-baseline' = $baseline;" ^
          "$out = $json | ConvertTo-Json -Depth 100;" ^
          "Set-Content -Path $p -Value $out -NoNewline;"

        if errorlevel 1 (
            echo [ERROR] Failed to write builtin-baseline into vcpkg.json
            goto :fail
        )

        for /f "usebackq delims=" %%S in (`
          powershell -NoProfile -Command "(Get-Content 'vcpkg.json' -Raw | ConvertFrom-Json).'builtin-baseline'"
        ) do set "CHECK_BASELINE=%%S"

        if not defined CHECK_BASELINE (
            echo [ERROR] builtin-baseline missing after write.
            goto :fail
        )

        echo [setup.bat] builtin-baseline set to !CHECK_BASELINE!
    ) else (
        echo [setup.bat] x-update-baseline succeeded.
    )

    powershell -NoProfile -Command ^
      "$b=(Get-Content 'vcpkg.json' -Raw | ConvertFrom-Json).'builtin-baseline';" ^
      "if($b -and $b -match '^[0-9a-fA-F]{40}$'){exit 0}else{Write-Host '[ERROR] builtin-baseline invalid:' $b; exit 1}"
    if errorlevel 1 (
        echo [ERROR] builtin-baseline is not a 40-hex SHA. Aborting.
        goto :fail
    )
) else (
    echo [setup.bat] vcpkg.json not found, skipping baseline update.
)

if exist "%REPO_ROOT%\vcpkg.json" (
    echo [setup.bat] Installing vcpkg manifest dependencies...
    "%LOCAL_VCPKG%\vcpkg.exe" install --triplet x64-windows --feature-flags=manifests,binarycaching
    if errorlevel 1 (
        echo [ERROR] vcpkg install failed.
        goto :fail
    )
) else (
    echo [setup.bat] No vcpkg.json found. You can still build if your CMake presets do not use manifests.
)

echo [setup.bat] Setup complete.
call compile_and_run.bat
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
rem Short circuit if cl is already usable
where cl >nul 2>&1 && (
    echo [setup.bat] MSVC toolchain already available.
    exit /b 0
)

set "VS_INSTALL_PATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Try to find any VS instance that already has the VC tools installed
if exist "%VSWHERE%" (
    for /f "usebackq tokens=* delims=" %%I in (`
        "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    `) do set "VS_INSTALL_PATH=%%I"
)

rem Fallback to common BuildTools folder or your custom install dir if vswhere did not return anything
if not defined VS_INSTALL_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools" set "VS_INSTALL_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if not defined VS_INSTALL_PATH if exist "%VSBT_INSTALL_DIR%" set "VS_INSTALL_PATH=%VSBT_INSTALL_DIR%"

if defined VS_INSTALL_PATH (
    echo [setup.bat] Found existing MSVC install at "%VS_INSTALL_PATH%".
    exit /b 0
)

rem If we get here, there is no usable MSVC. Install Build Tools silently via winget.
echo [setup.bat] MSVC build tools not found. Installing Visual Studio 2022 Build Tools with winget...
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --silent ^
  --override "--installPath \"%VSBT_INSTALL_DIR%\" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
if errorlevel 1 (
    echo [ERROR] VS Build Tools install failed.
    exit /b 1
)

rem Re-run detection so later steps (like EnsureDevShell) know where it ended up
set "VS_INSTALL_PATH="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=* delims=" %%I in (`
        "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    `) do set "VS_INSTALL_PATH=%%I"
)
if not defined VS_INSTALL_PATH if exist "%VSBT_INSTALL_DIR%" set "VS_INSTALL_PATH=%VSBT_INSTALL_DIR%"

if not defined VS_INSTALL_PATH (
    echo [ERROR] VS Build Tools installation did not register correctly.
    exit /b 1
)

echo [setup.bat] VS Build Tools installed at "%VS_INSTALL_PATH%".
exit /b 0



:EnsureDevShell
where cl >nul 2>&1 && (echo [setup.bat] MSVC already on PATH. & exit /b 0)
set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=* delims=" %%I in (`
      "%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -property installationPath
    `) do set "VSROOT=%%I"
)
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
where cmake >nul 2>&1 && ( echo [setup.bat] CMake is installed. & exit /b 0 )
echo [setup.bat] Installing CMake via winget...
winget install -e --id Kitware.CMake --source winget --silent || (echo [ERROR] CMake install failed. & exit /b 1)
echo [setup.bat] CMake installed.
exit /b 0

:EnsureNinja
where ninja >nul 2>&1 && ( echo [setup.bat] Ninja is installed. & exit /b 0 )
echo [setup.bat] Installing Ninja via winget...
winget install -e --id Ninja-build.Ninja --source winget --silent || (echo [ERROR] Ninja install failed. & exit /b 1)
echo [setup.bat] Ninja installed.
exit /b 0

:DownloadVSBootstrapper
if exist "%VS_BOOT_EXE%" del /q "%VS_BOOT_EXE%" >nul 2>&1
powershell -NoProfile -Command ^
  "$ErrorActionPreference='Stop';[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;Invoke-WebRequest -Uri '%VS_BOOT_URL%' -OutFile '%VS_BOOT_EXE%'"
if errorlevel 1 exit /b 1
exit /b 0

:RunWithTimeoutPS
set "_R_EXE=%~1"
set "_R_ARGS=%~2"
set "_R_TO=%~3"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$exe='%_R_EXE%';$args='%_R_ARGS%';$t=%_R_TO%;" ^
  "$p=Start-Process -FilePath $exe -ArgumentList $args -PassThru -WindowStyle Hidden;" ^
  "$sw=[Diagnostics.Stopwatch]::StartNew();" ^
  "while(-not $p.HasExited){" ^
  "  Start-Sleep -Seconds 2;" ^
  "  if($sw.Elapsed.TotalSeconds -gt $t){" ^
  "    try{ $p.Kill() } catch{}; exit 901" ^
  "  }" ^
  "};" ^
  "exit $p.ExitCode"
exit /b %ERRORLEVEL%

:WriteSetupStatus
set "RC=%~1"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$code = %RC%;" ^
  "if ($code -eq 0) { $status = 'SUCCESS' } else { $status = 'FAIL' }" ^
  "$ts = Get-Date -Format o;" ^
  "$obj = @{ status = $status; timestamp = $ts; exitCode = $code };" ^
  "$obj | ConvertTo-Json -Depth 5 | Set-Content -Path '%SETUP_STATUS%' -Encoding UTF8"
goto :eof

:fail
echo [setup.bat] Setup failed.
popd >nul
exit /b 1

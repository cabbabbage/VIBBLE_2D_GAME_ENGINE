@echo off
setlocal enabledelayedexpansion

if "%~1"=="__RUN__" (
    shift
    goto :main
)



echo Installing NVIDIA CUDA Toolkit with winget...
winget install --id=Nvidia.CUDA -e --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
    echo Failed to install NVIDIA CUDA Toolkit with winget.
    goto :EOF
)

REM Edit this if your CUDA version or path is different
set "CUDA_VER=v12.6"
set "CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\%CUDA_VER%"

echo Setting CUDA_PATH to %CUDA_PATH% ...
REM Set for future sessions (system wide)
setx CUDA_PATH "%CUDA_PATH%" /M

REM Update PATH for current session
set "PATH=%CUDA_PATH%\bin;%CUDA_PATH%\libnvvp;%PATH%"

echo Using Python from PATH. Change this if you want a specific python.exe.
set "PYTHON=python"

echo Upgrading pip...
%PYTHON% -m pip install --upgrade pip

echo Removing any existing CuPy installs...
%PYTHON% -m pip uninstall -y cupy cupy-cuda11x cupy-cuda12x

echo Installing CuPy with CUDA 12.x wheels...
%PYTHON% -m pip install cupy-cuda12x --extra-index-url https://download.cupy.dev/wheels/cu12x
if errorlevel 1 (
    echo Failed to install cupy-cuda12x. Check Python version and pip output.
    goto :EOF
)





set "SETUP_LOG=%~dp0setup_log.txt"
type nul > "%SETUP_LOG%"
set "SCRIPT_PATH=%~f0"

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
set "VS_BOOT_URL=https://aka.ms/vs/17/release/vs_BuildTools.exe"
set "VS_BOOT_EXE=%TEMP%\vs_BuildTools.exe"
set "INSTALL_TIMEOUT_SECS=5400"

rem Elevate if needed, keep working directory at repo root
call :EnsureAdmin
if %ERRORLEVEL%==1 ( popd >nul & exit /b 0 ) else if %ERRORLEVEL% GEQ 2 ( echo [ERROR] Could not elevate. & goto :fail )

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

:EnsureAdmin
powershell -NoProfile -Command ^
  "$wd='%REPO_ROOT%'; if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){ Start-Process -FilePath '%SCRIPT_PATH%' -ArgumentList '__RUN__ %*' -Verb RunAs -WorkingDirectory $wd; exit 1 } else { exit 0 }"
set "rc=%ERRORLEVEL%"
if "%rc%"=="1" exit /b 1
if "%rc%"=="0" exit /b 0
exit /b 2

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
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL_PATH="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -property installationPath`) do set "VS_INSTALL_PATH=%%I"
)
if not defined VS_INSTALL_PATH if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools" set "VS_INSTALL_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if not defined VS_INSTALL_PATH if exist "%VSBT_INSTALL_DIR%" set "VS_INSTALL_PATH=%VSBT_INSTALL_DIR%"

if not defined VS_INSTALL_PATH (
    echo [setup.bat] Installing Visual Studio 2022 Build Tools...
    call :DownloadVSBootstrapper || exit /b 1
    call :RunWithTimeoutPS "%VS_BOOT_EXE%" "--installPath ""%VSBT_INSTALL_DIR%"" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621" %INSTALL_TIMEOUT_SECS%
    if errorlevel 1 (
        call :RunWithTimeoutPS "%VS_BOOT_EXE%" "--installPath ""%VSBT_INSTALL_DIR%"" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows10SDK.19041" %INSTALL_TIMEOUT_SECS% || (echo [ERROR] VS Build Tools install failed. & exit /b 1)
    )
) else (
    echo [setup.bat] Ensuring required C++ components...
    set "VS_SETUP=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe"
    if exist "%VS_SETUP%" (
        call :RunWithTimeoutPS "%VS_SETUP%" "modify --installPath ""%VS_INSTALL_PATH%"" --quiet --norestart --wait --locale en-US --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621" %INSTALL_TIMEOUT_SECS% ^
        || call :RunWithTimeoutPS "%VS_SETUP%" "modify --installPath ""%VS_INSTALL_PATH%"" --quiet --norestart --wait --locale en-US --add Microsoft.VisualStudio.Component.Windows10SDK.19041" %INSTALL_TIMEOUT_SECS%
    ) else (
        echo [setup.bat] Visual Studio Installer not found. Using bootstrapper to modify...
        call :DownloadVSBootstrapper || exit /b 1
        call :RunWithTimeoutPS "%VS_BOOT_EXE%" "modify --installPath ""%VS_INSTALL_PATH%"" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621" %INSTALL_TIMEOUT_SECS% ^
        || call :RunWithTimeoutPS "%VS_BOOT_EXE%" "modify --installPath ""%VS_INSTALL_PATH%"" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Component.Windows10SDK.19041" %INSTALL_TIMEOUT_SECS% || (echo [ERROR] VS component modify failed. & exit /b 1)
    )
)
exit /b 0

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
  "$ErrorActionPreference='Stop';[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12;Invoke-WebRequest -Uri '%VS_BOOT_URL%' -OutFile '%VS_BOOT_EXE%'" || exit /b 1
exit /b 0

:RunWithTimeoutPS
rem Args: 1=exe 2=args 3=timeoutSeconds
set "_R_EXE=%~1"
set "_R_ARGS=%~2"
set "_R_TO=%~3"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$exe='%_R_EXE%';$args='%_R_ARGS%';$t=%_R_TO%;$p=Start-Process -FilePath $exe -ArgumentList $args -PassThru -WindowStyle Hidden;" ^
  "$sw=[Diagnostics.Stopwatch]::StartNew(); while(-not $p.HasExited){ Start-Sleep -Seconds 2; if($sw.Elapsed.TotalSeconds -gt $t){ try{ $p.Kill() } catch{}; exit 901 } } ; exit $p.ExitCode"
exit /b %ERRORLEVEL%

:fail
echo [setup.bat] Setup failed.
popd >nul
pause
exit /b 1

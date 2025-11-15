@echo off
setlocal enabledelayedexpansion

if "%~1"=="__RUN__" (
    shift
    goto :main
)

set "LOG_FILE=%~dp0log.txt"
type nul > "%LOG_FILE%"
set "SCRIPT_PATH=%~f0"

rem Run the script recursively and pipe to powershell logger
if not defined VIBBLE_SUPPRESS_PAUSE set "VIBBLE_SUPPRESS_PAUSE=1"
cmd /v:on /c call "%SCRIPT_PATH%" __RUN__ %* 2>&1 | powershell -NoProfile -Command ^
  "$input | Tee-Object -FilePath '%LOG_FILE%'; exit $LASTEXITCODE"

exit /b %ERRORLEVEL%

:main
rem =========================
rem VIBBLE Engine - run.bat
rem Local build + run using CMakePresets + auto-vcpkg
rem =========================

pushd "%~dp0" >nul

set "BUILD_CONFIG=RelWithDebInfo"
set "EXTRA_ARGS="

rem Define repo root and setup defaults
set "REPO_ROOT=%CD%"
set "VSBT_INSTALL_DIR=C:\VS2022\BuildTools"
set "INSTALL_TIMEOUT_SECS=5400"

rem Enable robust SAFE loading mode by default unless explicitly overridden
if not defined VIBBLE_SAFE_LOADING (
    set "VIBBLE_SAFE_LOADING=1"
    echo [run.bat] SAFE loading enabled (VIBBLE_SAFE_LOADING=1)
)

rem ----------------------------------------------------
rem Sanity check: ensure CMakeLists.txt exists at repo root
rem ----------------------------------------------------
if not exist "%cd%\CMakeLists.txt" (
    echo [ERROR] CMakeLists.txt not found at repo root: "%cd%\CMakeLists.txt"
    echo        Make sure you are running run.bat from the project root.
    goto :fail
)

rem ----------------------------------------------------
rem Ensure toolchain and prerequisites (merged setup stage)
rem ----------------------------------------------------
call :EnsureWinget || goto :fail
call :EnsureGit || goto :fail
call :EnsureVSBuildTools || goto :fail
call :EnsureDevShell || goto :fail
call :EnsureCMake || goto :fail
call :EnsureNinja || goto :fail

rem ----------------------------------------------------
rem Ensure vcpkg exists (clone if missing)
rem ----------------------------------------------------
set "LOCAL_VCPKG=%cd%\vcpkg"
rem Ensure local vcpkg root is used to suppress VCPKG_ROOT mismatch warning
set "VCPKG_ROOT=%LOCAL_VCPKG%"
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
rem Always refresh builtin-baseline before install (robust JSON edit)
rem Sets builtin-baseline to current vcpkg repo HEAD (40-hex)
rem ----------------------------------------------------
echo [run.bat] Updating vcpkg baseline...

if not exist "%cd%\vcpkg.json" (
    echo [WARN] vcpkg.json not found in repo root. Skipping baseline update.
) else (
    rem Try x-update-baseline first (works when manifest already valid)
    "%LOCAL_VCPKG%\vcpkg.exe" x-update-baseline
    if errorlevel 1 (
        echo [run.bat] x-update-baseline failed, falling back to manual baseline update...

        rem Get the vcpkg commit SHA we want to pin
        pushd "%LOCAL_VCPKG%" >nul
        for /f "delims=" %%H in ('git rev-parse HEAD') do set "NEW_BASELINE=%%H"
        popd >nul

        if not defined NEW_BASELINE (
            echo [ERROR] Could not resolve vcpkg HEAD commit. Baseline not updated.
            goto :fail
        )

        rem PowerShell: parse JSON, set .builtin-baseline, write back cleanly (using env var)
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

        rem Read back for logging
        for /f "usebackq delims=" %%S in (`powershell -NoProfile -Command ^
          "(Get-Content 'vcpkg.json' -Raw | ConvertFrom-Json).'builtin-baseline'"`) do set "CHECK_BASELINE=%%S"

        if not defined CHECK_BASELINE (
            echo [ERROR] builtin-baseline missing after write.
            goto :fail
        )

        echo [run.bat] builtin-baseline set to !CHECK_BASELINE!
    ) else (
        echo [run.bat] x-update-baseline succeeded.
    )
)

rem ----------------------------------------------------
rem Final sanity-check in PowerShell (avoid FINDSTR issues)
rem ----------------------------------------------------
if exist "%cd%\vcpkg.json" (
    powershell -NoProfile -Command ^
      "$b=(Get-Content 'vcpkg.json' -Raw | ConvertFrom-Json).'builtin-baseline';" ^
      "if($b -and $b -match '^[0-9a-fA-F]{40}$'){exit 0}else{Write-Host '[ERROR] builtin-baseline invalid:' $b; exit 1}"
    if errorlevel 1 (
        echo [ERROR] builtin-baseline is not a 40-hex SHA. Aborting.
        goto :fail
    )
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

set "CMAKE_CMD="
call :LocateCMake
if not defined CMAKE_CMD (
    echo [ERROR] CMake executable not found after setup stage.
    goto :fail
)
for %%P in ("%CMAKE_CMD%") do set "CMAKE_DIR=%%~dpP"
if defined CMAKE_DIR set "PATH=%CMAKE_DIR%;%PATH%"
echo [run.bat] Using CMake from: %CMAKE_CMD%

echo [run.bat] Configuring with preset: windows-vcpkg
"%CMAKE_CMD%" --preset windows-vcpkg
if errorlevel 1 goto :fail

echo [run.bat] Building with preset: windows-vcpkg-release (%BUILD_CONFIG%)
"%CMAKE_CMD%" --build --preset windows-vcpkg-release --config %BUILD_CONFIG%
if errorlevel 1 goto :fail

rem ----------------------------------------------------
rem Locate exe (handle both Ninja and VS generators, and optional RUNTIME_OUTPUT dir)
rem ----------------------------------------------------
set "RELEASE_DIR=%cd%\release"
if not exist "%RELEASE_DIR%" (
    mkdir "%RELEASE_DIR%"
    if errorlevel 1 (
        echo [ERROR] Failed to create release directory.
        goto :fail
    )
)

for %%P in ("%RELEASE_DIR%\*.exe" "%RELEASE_DIR%\*.dll" "%RELEASE_DIR%\*.pdb") do (
    if exist %%~P del /q %%~P >nul 2>&1
)

call :CollectArtifacts "%cd%"
call :CollectArtifacts "%cd%\ENGINE"
call :CollectArtifacts "%cd%\build\%BUILD_CONFIG%"
call :CollectArtifacts "%cd%\build"

set "EXE=%RELEASE_DIR%\engine.exe"

if not exist "%EXE%" (
    echo [ERROR] Executable not found in release directory.
    goto :fail
)

rem ----------------------------------------------------
rem Clean up .txt files repo-wide (except log.txt and CMakeLists.txt) before launching
rem ----------------------------------------------------
echo [run.bat] Deleting all *.txt files (recursively) except log.txt and CMakeLists.txt...
for /r "%cd%" %%F in (*.txt) do (
    rem Skip any file named exactly "log.txt" or "CMakeLists.txt" (defensive)
    if /I not "%%~nxF"=="log.txt" if /I not "%%~nxF"=="CMakeLists.txt" (
        del /q "%%~fF" >nul 2>&1
    )
)

rem ----------------------------------------------------
rem Create Desktop Shortcut
rem ----------------------------------------------------
set "DESKTOP=%USERPROFILE%\Desktop"
set "SHORTCUT=%DESKTOP%\VI.lnk"
set "ICONFILE=%cd%\SRC\MISC_CONTENT\vibble.ico"
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

:CollectArtifacts
set "SRC_DIR=%~1"
if not exist "%SRC_DIR%" goto :eof
for %%E in (exe dll pdb) do (
    for /f "delims=" %%F in ('dir /b "%SRC_DIR%\*.%%E" 2^>nul') do (
        move /y "%SRC_DIR%\%%F" "%RELEASE_DIR%" >nul
    )
)
goto :eof

:fail
echo [run.bat] Build failed.
popd >nul
if not defined VIBBLE_SUPPRESS_PAUSE pause
exit /b 1

rem ==============================================
rem Setup helpers (merged from setup.bat)
rem ==============================================

:EnsureWinget
where winget >nul 2>&1 && exit /b 0
echo [ERROR] winget is not available. Install App Installer from Microsoft Store.
exit /b 1

:EnsureGit
git --version >nul 2>&1 && ( echo [run.bat] Git is installed. & exit /b 0 )
echo [run.bat] Installing Git via winget...
winget install --id Git.Git -e --source winget --silent || (echo [ERROR] Git install failed. & exit /b 1)
git --version >nul 2>&1 && echo [run.bat] Git installed and on PATH.
exit /b 0

:EnsureVSBuildTools
where cl >nul 2>&1 && ( echo [run.bat] MSVC toolchain already available. & exit /b 0 )

call :DetectVSInstallPath
if not errorlevel 1 (
    echo [run.bat] Visual Studio Build Tools with required workloads detected.
    exit /b 0
)

echo [run.bat] Installing Visual Studio 2022 Build Tools via winget...
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements --silent ^
  --override "--installPath \"%VSBT_INSTALL_DIR%\" --quiet --wait --norestart --nocache --includeRecommended --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621" || (
    echo [run.bat] Retrying Visual Studio Build Tools install with Windows 10 SDK...
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

echo [run.bat] Visual Studio Build Tools installed.
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
where cl >nul 2>&1 && (echo [run.bat] MSVC already on PATH. & exit /b 0)
set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.BuildTools -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT if exist "%VSBT_INSTALL_DIR%" set "VSROOT=%VSBT_INSTALL_DIR%"
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

:EnsureCMake
where cmake >nul 2>&1 && (
    echo [run.bat] CMake is installed.
    call :CMakePostCheck
    exit /b 0
)

echo [run.bat] Installing CMake...

rem Try winget first
winget -v >nul 2>&1
if "%errorlevel%"=="0" (
    echo [run.bat] Installing CMake via winget...
    winget install --id Kitware.CMake -e --silent --accept-package-agreements --accept-source-agreements
    if "%errorlevel%"=="0" (
        echo [run.bat] CMake installed via winget.
        goto :CMakePostCheck
    ) else (
        echo [run.bat] winget install failed, trying MSI fallback.
        goto :CMakeMSI
    )
) else (
    echo [run.bat] winget not found, trying MSI fallback.
    goto :CMakeMSI
)

:CMakeMSI
set "CMAKE_MSI=%TEMP%\cmake_latest_x64.msi"
echo [run.bat] Downloading latest CMake MSI from GitHub...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$r=Invoke-RestMethod https://api.github.com/repos/Kitware/CMake/releases/latest; " ^
  "$asset=$r.assets | Where-Object { $_.name -match 'windows-x86_64\.msi$' } | Select-Object -First 1; " ^
  "if(-not $asset){Write-Error 'CMake MSI not found'; exit 1}; " ^
  "Invoke-WebRequest $asset.browser_download_url -OutFile '%CMAKE_MSI%'" || (
    echo [ERROR] Failed to download CMake MSI.
    exit /b 1
  )

echo [run.bat] Installing CMake from MSI...
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

echo [run.bat] CMake installed successfully:
cmake --version
call :RecordCMakeHint
exit /b 0

:EnsureNinja
where ninja >nul 2>&1 && ( echo [run.bat] Ninja is installed. & exit /b 0 )
echo [run.bat] Installing Ninja via winget...
winget install -e --id Ninja-build.Ninja --source winget --silent || (echo [ERROR] Ninja install failed. & exit /b 1)
echo [run.bat] Ninja installed.
exit /b 0

:AddToUserPath
rem %1 = directory to add
set "ADD_DIR=%~1"
if "%ADD_DIR%"=="" exit /b 0
if not exist "%ADD_DIR%\." exit /b 0

rem Decide whether we can write to System (Machine) PATH; otherwise fall back to User PATH
set "TARGET_SCOPE=User"
powershell -NoProfile -Command ^
  "$a=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator); if($a){exit 0}else{exit 1}" >nul 2>&1
if "%errorlevel%"=="0" set "TARGET_SCOPE=Machine"

rem Persist the path addition in the chosen scope if not already present
echo [run.bat] Adding "%ADD_DIR%" to %TARGET_SCOPE% PATH...
powershell -NoProfile -Command ^
  "$scope='%TARGET_SCOPE%'; $dir='%ADD_DIR%';" ^
  "$p=[Environment]::GetEnvironmentVariable('Path',$scope);" ^
  "if([string]::IsNullOrEmpty($p)){$p=$dir}" ^
  "elseif(-not ($p.Split(';') -contains $dir)){$p=$p+';'+$dir};" ^
  "[Environment]::SetEnvironmentVariable('Path',$p,$scope)" >nul 2>&1

rem Update PATH for the current session immediately
set "PATH=%ADD_DIR%;%PATH%"

rem Broadcast environment change to notify running shells/Explorer (best-effort)
powershell -NoProfile -Command ^
  "$sig='[DllImport("user32.dll",SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr hWnd, int Msg, IntPtr wParam, string lParam, int fuFlags, int uTimeout, out IntPtr lpdwResult)';" ^
  "Add-Type -Namespace Win32 -Name Native -MemberDefinition $sig;" ^
  "[void][Win32.Native]::SendMessageTimeout([IntPtr]0xffff,0x1A,[IntPtr]0,'Environment',0,5000,[ref]([IntPtr]::Zero))" >nul 2>&1

exit /b 0

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

:LocateCMake
set "CMAKE_CMD="

for /f "delims=" %%I in ('where cmake 2^>nul') do (
    if not defined CMAKE_CMD set "CMAKE_CMD=%%~fI"
)
if defined CMAKE_CMD goto :locate_done

for %%P in ("%ProgramFiles%\CMake\bin\cmake.exe" "C:\Program Files\CMake\bin\cmake.exe" "C:\Program Files (x86)\CMake\bin\cmake.exe" "%ProgramFiles(x86)%\CMake\bin\cmake.exe") do (
    if not defined CMAKE_CMD if exist %%~P set "CMAKE_CMD=%%~fP"
)
if defined CMAKE_CMD goto :locate_done

set "CMAKE_HINT_FILE=%cd%\TEMP\cmake-path.txt"
if exist "%CMAKE_HINT_FILE%" (
    set /p CMAKE_CMD=<"%CMAKE_HINT_FILE%"
    if defined CMAKE_CMD (
        if exist "!CMAKE_CMD!" goto :locate_done
        if exist "!CMAKE_CMD!\cmake.exe" (
            set "CMAKE_CMD=!CMAKE_CMD!\cmake.exe"
            goto :locate_done
        )
    )
    set "CMAKE_CMD="
)

if exist "%LOCAL_VCPKG%\downloads\tools\cmake" (
    for /d %%D in ("%LOCAL_VCPKG%\downloads\tools\cmake\cmake-*") do (
        if not defined CMAKE_CMD if exist "%%D\bin\cmake.exe" (
            set "CMAKE_CMD=%%D\bin\cmake.exe"
            goto :locate_done
        )
    )
)

:locate_done
if defined CMAKE_CMD (
    for %%Q in ("%CMAKE_CMD%") do if exist %%~fQ set "CMAKE_CMD=%%~fQ"
)
exit /b 0

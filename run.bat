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

rem Enable robust SAFE loading mode by default unless explicitly overridden
if not defined VIBBLE_SAFE_LOADING (
    set "VIBBLE_SAFE_LOADING=1"
    echo [run.bat] SAFE loading enabled (VIBBLE_SAFE_LOADING=1)
)

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

echo [run.bat] Configuring with preset: windows-vcpkg
cmake --preset windows-vcpkg
if errorlevel 1 goto :fail

echo [run.bat] Building with preset: windows-vcpkg-release (%BUILD_CONFIG%)
cmake --build --preset windows-vcpkg-release --config %BUILD_CONFIG%
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
pause
exit /b 1

@echo off
setlocal EnableDelayedExpansion

rem ==================================================
rem VIBBLE Engine - Automated setup and build script
rem ==================================================

pushd "%~dp0" >nul

set "RUN_ENGINE=1"
set "CREATE_SHORTCUT=1"
set "CI_MODE="
for %%A in (%*) do (
  if /I "%%~A"=="--skip-run" set "RUN_ENGINE=0"
  if /I "%%~A"=="--no-shortcut" set "CREATE_SHORTCUT=0"
  if /I "%%~A"=="--ci" set "CI_MODE=1"
)
if defined CI set "CI_MODE=1"
if defined CI_MODE (
  set "RUN_ENGINE=0"
  set "CREATE_SHORTCUT=0"
)

set "EXIT_CODE=0"

call :log "Starting VIBBLE environment bootstrap..."
call :detect_admin
if errorlevel 1 goto :fail

call :ensure_prereqs
if errorlevel 1 goto :fail

call :setup_python
if errorlevel 1 goto :fail

call :setup_node
if errorlevel 1 goto :fail

call :setup_vcpkg
if errorlevel 1 goto :fail

call :configure_cmake
if errorlevel 1 goto :fail

call :build_cmake
if errorlevel 1 goto :fail

if "%CREATE_SHORTCUT%"=="1" (
  call :create_shortcut
)

if "%RUN_ENGINE%"=="1" (
  call :launch_engine
)

goto :cleanup

:fail
set "EXIT_CODE=1"

:cleanup
if %EXIT_CODE% NEQ 0 (
  call :error "Setup failed. See messages above for details."
)
popd >nul
endlocal & exit /b %EXIT_CODE%

rem --------------------------------------------------
rem Helper routines
rem --------------------------------------------------

:log
if "%~1"=="" goto :eof
echo [run.bat] %~1
exit /b 0

:warn
if "%~1"=="" goto :eof
>&2 echo [run.bat][WARN] %~1
exit /b 0

:error
if "%~1"=="" goto :eof
>&2 echo [run.bat][ERROR] %~1
exit /b 0

:detect_admin
net session >nul 2>&1
if errorlevel 1 (
  set "ADMIN_PRIV="
  call :log "Running without administrative privileges. Installs will use per-user scope when possible."
) else (
  set "ADMIN_PRIV=1"
  call :log "Administrator privileges detected."
)
exit /b 0

:ensure_prereqs
call :log "Checking build prerequisites..."
call :ensure_tool git Git.Git "%ProgramFiles%\Git\bin" "%ProgramFiles%\Git\cmd" "%ProgramFiles(x86)%\Git\bin" "%ProgramFiles(x86)%\Git\cmd"
if errorlevel 1 exit /b 1
call :ensure_tool cmake Kitware.CMake "%ProgramFiles%\CMake\bin" "%ProgramFiles(x86)%\CMake\bin"
if errorlevel 1 exit /b 1
call :ensure_tool ninja Ninja-build.Ninja "%ProgramFiles%\Ninja" "%ProgramFiles(x86)%\Ninja" "%ProgramFiles%\CMake\bin"
if errorlevel 1 exit /b 1
call :ensure_tool python Python.Python.3.12 "%LocalAppData%\Programs\Python\Python312" "%LocalAppData%\Programs\Python\Python311" "%ProgramFiles%\Python312" "%ProgramFiles%\Python311"
if errorlevel 1 exit /b 1
call :ensure_tool node OpenJS.NodeJS.LTS "%ProgramFiles%\nodejs" "%LocalAppData%\Programs\nodejs" "%ProgramFiles(x86)%\nodejs"
if errorlevel 1 exit /b 1
call :ensure_vs
if errorlevel 1 exit /b 1
exit /b 0

:ensure_tool
set "TOOL=%~1"
set "WINGET_ID=%~2"
set "arg3=%~3"
set "arg4=%~4"
set "arg5=%~5"
set "arg6=%~6"
set "arg7=%~7"
set "arg8=%~8"
set "arg9=%~9"
call :log "Checking for %TOOL%..."
where %TOOL% >nul 2>&1
if not errorlevel 1 (
  call :log "%TOOL% is available."
  exit /b 0
)
for %%I in (3 4 5 6 7 8 9) do (
  set "HINT=!arg%%I!"
  if defined HINT call :append_if_exists "!HINT!"
)
where %TOOL% >nul 2>&1
if not errorlevel 1 (
  call :log "%TOOL% found after updating PATH."
  exit /b 0
)
if defined CI_MODE (
  call :error "%TOOL% is required but unavailable in CI mode."
  exit /b 1
)
if "%WINGET_ID%"=="" (
  call :error "No automatic installer mapped for %TOOL%. Install manually and re-run."
  exit /b 1
)
call :install_via_winget "%WINGET_ID%"
if errorlevel 1 exit /b 1
where %TOOL% >nul 2>&1
if not errorlevel 1 (
  call :log "%TOOL% installed successfully."
  exit /b 0
)
for %%I in (3 4 5 6 7 8 9) do (
  set "HINT=!arg%%I!"
  if defined HINT call :append_if_exists "!HINT!"
)
where %TOOL% >nul 2>&1
if not errorlevel 1 (
  call :log "%TOOL% installed and added to PATH."
  exit /b 0
)
call :warn "%TOOL% may require a terminal restart for PATH updates to apply."
exit /b 1

:append_if_exists
set "CAND=%~1"
if "%CAND%"=="" exit /b 0
if not exist "%CAND%" exit /b 0
echo %PATH% | find /I "%CAND%" >nul
if not errorlevel 1 exit /b 0
call :log "Adding to PATH for this session: %CAND%"
set "PATH=%CAND%;%PATH%"
exit /b 0

:ensure_winget
where winget >nul 2>&1
if errorlevel 1 (
  call :warn "winget not available. Install 'App Installer' from the Microsoft Store or install tools manually."
  exit /b 1
)
exit /b 0

:install_via_winget
if defined CI_MODE (
  call :error "winget installs are disabled while running in CI mode."
  exit /b 1
)
call :ensure_winget
if errorlevel 1 exit /b 1
set "PACKAGE_ID=%~1"
set "FLAGS=--silent --accept-package-agreements --accept-source-agreements --exact"
if not defined ADMIN_PRIV set "FLAGS=%FLAGS% --scope user"
call :log "Installing %PACKAGE_ID% via winget..."
winget install --id "%PACKAGE_ID%" %FLAGS%
if errorlevel 1 (
  call :error "winget failed to install %PACKAGE_ID%."
  exit /b 1
)
exit /b 0

:ensure_vs
call :log "Ensuring Microsoft C++ Build Tools are available..."
call :locate_vs_dev_cmd
if defined VS_DEV_CMD goto :load_vs_env
if defined CI_MODE (
  call :error "MSVC Build Tools not detected in CI environment."
  exit /b 1
)
if not defined ADMIN_PRIV (
  call :error "MSVC Build Tools not detected. Run this script as administrator or install Visual Studio Build Tools manually."
  exit /b 1
)
call :log "Installing Visual Studio 2022 Build Tools (this may take a while)..."
call :install_visual_studio
if errorlevel 1 exit /b 1
call :locate_vs_dev_cmd
if not defined VS_DEV_CMD (
  call :error "VsDevCmd.bat still not found after installation."
  exit /b 1
)

:load_vs_env
call :log "Loading MSVC environment..."
call "%VS_DEV_CMD%" -arch=x64 >nul
if errorlevel 1 (
  call :error "Failed to load MSVC environment."
  exit /b 1
)
where cl >nul 2>&1
if errorlevel 1 (
  call :error "MSVC compiler (cl.exe) not available even after environment setup."
  exit /b 1
)
call :log "MSVC compiler ready."
exit /b 0

:install_visual_studio
call :ensure_winget
if errorlevel 1 exit /b 1
if not defined ADMIN_PRIV (
  call :error "Administrator privileges are required to install Visual Studio Build Tools."
  exit /b 1
)
set "FLAGS=--silent --accept-package-agreements --accept-source-agreements --exact"
call :log "Downloading and installing Microsoft.VisualStudio.2022.BuildTools via winget..."
winget install --id "Microsoft.VisualStudio.2022.BuildTools" %FLAGS% --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive --norestart"
if errorlevel 1 (
  call :error "winget was unable to install Microsoft.VisualStudio.2022.BuildTools."
  exit /b 1
)
exit /b 0

:locate_vs_dev_cmd
set "VS_DEV_CMD="
for %%E in (BuildTools Community Professional Enterprise) do (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat"
)
if not defined VS_DEV_CMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_DEV_CMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not defined VS_DEV_CMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq tokens=* delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find Common7\Tools\VsDevCmd.bat 2^>nul`) do (
    if exist "%%~V" set "VS_DEV_CMD=%%~V"
  )
)
exit /b 0

:setup_python
if not exist requirements.txt (
  call :log "No requirements.txt found. Skipping Python dependency installation."
  exit /b 0
)
call :log "Installing Python dependencies..."
python -m pip install --upgrade pip
if errorlevel 1 (
  call :warn "Failed to upgrade pip system-wide. Retrying with --user scope..."
  python -m pip install --user --upgrade pip
  if errorlevel 1 (
    call :error "Failed to upgrade pip."
    exit /b 1
  )
)
python -m pip install -r requirements.txt
if errorlevel 1 (
  call :warn "Python dependency installation failed. Retrying with --user scope..."
  python -m pip install --user -r requirements.txt
  if errorlevel 1 (
    call :error "Python dependency installation failed."
    exit /b 1
  )
)
exit /b 0

:setup_node
where npm >nul 2>&1
if errorlevel 1 (
  call :append_if_exists "%AppData%\npm"
  where npm >nul 2>&1
)
if errorlevel 1 (
  call :error "npm command not found even after installing Node.js. Restart your terminal and try again."
  exit /b 1
)
if not exist package.json (
  call :log "No package.json found. Skipping npm install."
  exit /b 0
)
call :log "Installing Node.js dependencies (npm install)..."
call npm install
if errorlevel 1 (
  call :error "npm install failed."
  exit /b 1
)
exit /b 0

:setup_vcpkg
set "VCPKG_ROOT=%cd%\vcpkg"
if not exist "%VCPKG_ROOT%\" (
  call :log "Cloning vcpkg into %VCPKG_ROOT%..."
  git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
  if errorlevel 1 (
    call :error "Failed to clone vcpkg repository."
    exit /b 1
  )
) else (
  if exist "%VCPKG_ROOT%\.git" (
    call :log "Updating existing vcpkg checkout..."
    git -C "%VCPKG_ROOT%" pull --ff-only
    if errorlevel 1 call :warn "Unable to update vcpkg (continuing with local copy)."
  )
)
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  call :log "Bootstrapping vcpkg (this may take a moment)..."
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
  if errorlevel 1 (
    call :error "vcpkg bootstrap failed."
    exit /b 1
  )
)
call :append_if_exists "%VCPKG_ROOT%"
set "VCPKG_ROOT=%VCPKG_ROOT%"
set "VCPKG_DEFAULT_TRIPLET=x64-windows"
set "VCPKG_FEATURE_FLAGS=manifests,registries,binarycaching"
if exist vcpkg.json (
  call :log "Installing C/C++ dependencies via vcpkg manifest..."
  "%VCPKG_ROOT%\vcpkg.exe" install --clean-after-build
  if errorlevel 1 (
    call :error "vcpkg install failed."
    exit /b 1
  )
)
exit /b 0

:configure_cmake
call :log "Preparing build directory..."
if exist build (
  call :log "Removing existing build directory to avoid stale cache..."
  rmdir /s /q build 2>nul
)
mkdir build 2>nul
if not exist build (
  call :error "Unable to create build directory."
  exit /b 1
)
set "RUNTIME_DIR=%cd%\ENGINE"
set "TOOLCHAIN_ARG="
if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
call :log "Configuring CMake project (Ninja generator)..."
cmake -G "Ninja" -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%RUNTIME_DIR%" %TOOLCHAIN_ARG%
if errorlevel 1 (
  call :error "CMake configuration failed."
  exit /b 1
)
exit /b 0

:build_cmake
call :log "Building project (RelWithDebInfo)..."
cmake --build build --config RelWithDebInfo
if errorlevel 1 (
  call :error "Build failed."
  exit /b 1
)
exit /b 0

:resolve_engine_exe
set "ENGINE_EXE="
if exist "%cd%\ENGINE\engine.exe" set "ENGINE_EXE=%cd%\ENGINE\engine.exe"
if "%ENGINE_EXE%"=="" if exist "%cd%\build\engine.exe" set "ENGINE_EXE=%cd%\build\engine.exe"
if "%ENGINE_EXE%"=="" exit /b 1
exit /b 0

:create_shortcut
call :resolve_engine_exe
if errorlevel 1 (
  call :warn "Executable not found; skipping desktop shortcut creation."
  exit /b 0
)
set "DESKTOP=%USERPROFILE%\Desktop"
if not exist "%DESKTOP%" (
  call :warn "Desktop path not found (%DESKTOP%). Skipping shortcut."
  exit /b 0
)
set "SHORTCUT=%DESKTOP%\VI.lnk"
set "ICONFILE=%cd%\MISC_CONTENT\vibble.ico"
set "ROOT_DIR=%cd%"
call :log "Creating desktop shortcut at %SHORTCUT%..."
powershell -NoProfile -Command ^
  "$s=(New-Object -COM WScript.Shell).CreateShortcut('%SHORTCUT%');" ^
  "$s.TargetPath='%ENGINE_EXE%';" ^
  "$s.WorkingDirectory='%ROOT_DIR%';" ^
  "$s.IconLocation='%ICONFILE%';" ^
  "$s.Save()"
if errorlevel 1 call :warn "Failed to create desktop shortcut."
exit /b 0

:launch_engine
call :resolve_engine_exe
if errorlevel 1 (
  call :error "Executable not found after build."
  exit /b 1
)
call :log "Launching: %ENGINE_EXE%"
"%ENGINE_EXE%"
set "GAME_EXIT=%ERRORLEVEL%"
if not "%GAME_EXIT%"=="0" (
  call :warn "Engine exited with code %GAME_EXIT%."
)
exit /b 0


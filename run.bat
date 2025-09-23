@echo off
setlocal EnableDelayedExpansion

rem ==================================================
rem VIBBLE Engine - Automated setup and build script
rem ==================================================

pushd "%~dp0" >nul

set "EXIT_CODE=0"
set "RUN_ENGINE=1"
set "CREATE_SHORTCUT=1"
set "CI_MODE="
set "REUSE_BUILD="
set "SKIP_CONFIGURE="
set "SKIP_BUILD="
set "FORCE_GENERATOR="

call :print_banner

:parse_arguments
if "%~1"=="" goto :post_parse
set "ARG=%~1"
if /I "%ARG%"=="--help" (
  call :show_help
  goto :cleanup
)
if /I "%ARG%"=="--skip-run" (
  set "RUN_ENGINE=0"
  shift
  goto :parse_arguments
)
if /I "%ARG%"=="--no-shortcut" (
  set "CREATE_SHORTCUT=0"
  shift
  goto :parse_arguments
)
if /I "%ARG%"=="--ci" (
  set "CI_MODE=1"
  set "RUN_ENGINE=0"
  set "CREATE_SHORTCUT=0"
  set "REUSE_BUILD=1"
  shift
  goto :parse_arguments
)
if /I "%ARG%"=="--reuse-build" (
  set "REUSE_BUILD=1"
  shift
  goto :parse_arguments
)
if /I "%ARG%"=="--configure-only" (
  set "SKIP_BUILD=1"
  shift
  goto :parse_arguments
)
if /I "%ARG%"=="--build-only" (
  set "SKIP_CONFIGURE=1"
  set "REUSE_BUILD=1"
  shift
  goto :parse_arguments
)
if /I "%ARG%"=="--generator" (
  if "%~2"=="" (
    call :error "Missing generator value for --generator."
    set "EXIT_CODE=1"
    goto :cleanup
  )
  set "FORCE_GENERATOR=%~2"
  shift
  shift
  goto :parse_arguments
)
call :warn "Unrecognized option: %ARG%"
shift
goto :parse_arguments

:post_parse
if defined CI set "CI_MODE=1"
if defined CI_MODE (
  set "RUN_ENGINE=0"
  set "CREATE_SHORTCUT=0"
)
if defined SKIP_CONFIGURE set "REUSE_BUILD=1"

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

if not defined SKIP_CONFIGURE (
  call :configure_cmake
  if errorlevel 1 goto :fail
) else (
  call :log "Skipping configure step per --build-only."
)

if not defined SKIP_BUILD (
  call :build_cmake
  if errorlevel 1 goto :fail
) else (
  call :log "Skipping build step per --configure-only."
)

if "%CREATE_SHORTCUT%"=="1" (
  call :create_shortcut
)

if "%RUN_ENGINE%"=="1" (
  call :launch_engine
  if errorlevel 1 goto :fail
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

:print_banner
echo ==================================================
echo  VIBBLE 2D ENGINE BOOTSTRAP
echo ==================================================
exit /b 0

:show_help
echo Usage: run.bat [options]
echo.
echo   --ci               Run in CI-friendly mode (no shortcut, no launch, reuse build)
echo   --skip-run         Do not launch the engine after building
echo   --no-shortcut      Skip creating the desktop shortcut
echo   --reuse-build      Reuse the existing build directory instead of recreating it
echo   --configure-only   Configure CMake but do not build
echo   --build-only       Build using an existing configuration
echo   --generator NAME   Force a specific CMake generator
echo   --help             Show this help message
exit /b 0

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
  set "INSTALL_SCOPE=user"
  call :log "Running without administrative privileges. Installs will use per-user scope when possible."
) else (
  set "ADMIN_PRIV=1"
  set "INSTALL_SCOPE=machine"
  call :log "Administrator privileges detected."
)
exit /b 0

:require_winget
where winget >nul 2>&1
if errorlevel 1 (
  call :error "winget is required but was not found. Install the Microsoft App Installer from the Microsoft Store and retry."
  exit /b 1
)
set "WINGET_AVAILABLE=1"
exit /b 0

:ensure_prereqs
call :log "Checking build prerequisites..."
call :require_winget
if errorlevel 1 exit /b 1
call :ensure_tool git Git.Git git.exe "%ProgramFiles%\Git\bin" "%ProgramFiles%\Git\cmd" "%ProgramFiles(x86)%\Git\bin" "%ProgramFiles(x86)%\Git\cmd"
if errorlevel 1 exit /b 1
call :ensure_tool cmake Kitware.CMake cmake.exe "%ProgramFiles%\CMake\bin" "%ProgramFiles(x86)%\CMake\bin"
if errorlevel 1 exit /b 1
call :ensure_tool ninja Ninja-build.Ninja ninja.exe "%ProgramFiles%\Ninja" "%ProgramFiles(x86)%\Ninja" "%ProgramFiles%\CMake\bin"
if errorlevel 1 exit /b 1
call :ensure_python
if errorlevel 1 exit /b 1
call :ensure_node
if errorlevel 1 exit /b 1
exit /b 0

:ensure_tool
set "TOOL_NAME=%~1"
set "WINGET_ID=%~2"
set "COMMAND_HINT=%~3"
set "PATH_HINT1=%~4"
set "PATH_HINT2=%~5"
set "PATH_HINT3=%~6"
set "PATH_HINT4=%~7"
set "PATH_HINT5=%~8"
if "%COMMAND_HINT%"=="" set "COMMAND_HINT=%TOOL_NAME%"
where %COMMAND_HINT% >nul 2>&1
if not errorlevel 1 goto :tool_append_paths
if not defined WINGET_AVAILABLE (
  call :error "%TOOL_NAME% not found and winget is unavailable to install it."
  exit /b 1
)
call :log "%TOOL_NAME% not found. Installing via winget (%WINGET_ID%)..."
winget install --id %WINGET_ID% --silent --accept-package-agreements --accept-source-agreements --scope %INSTALL_SCOPE% --exact
if errorlevel 1 (
  call :error "Failed to install %TOOL_NAME% via winget."
  exit /b 1
)
for %%D in ("%PATH_HINT1%" "%PATH_HINT2%" "%PATH_HINT3%" "%PATH_HINT4%" "%PATH_HINT5%") do (
  if not "%%~D"=="" call :append_if_exists "%%~D"
)
where %COMMAND_HINT% >nul 2>&1
if errorlevel 1 (
  call :error "%TOOL_NAME% was installed but is not on PATH. Restart your terminal or add it manually."
  exit /b 1
)
goto :tool_append_paths

:tool_append_paths
for %%D in ("%PATH_HINT1%" "%PATH_HINT2%" "%PATH_HINT3%" "%PATH_HINT4%" "%PATH_HINT5%") do (
  if not "%%~D"=="" call :append_if_exists "%%~D"
)
exit /b 0

:ensure_python
call :find_python
if not defined PYTHON_EXE (
  if defined WINGET_AVAILABLE (
    call :log "Python 3 not detected. Installing via winget..."
    winget install --id Python.Python.3.12 --silent --accept-package-agreements --accept-source-agreements --scope %INSTALL_SCOPE% --exact
    if errorlevel 1 (
      call :error "Failed to install Python using winget."
      exit /b 1
    )
    call :append_if_exists "%LocalAppData%\Programs\Python\Python312"
    call :append_if_exists "%LocalAppData%\Programs\Python\Python312\Scripts"
  )
  call :find_python
)
if not defined PYTHON_EXE (
  call :error "Python executable not found on PATH after installation."
  exit /b 1
)
call %PYTHON_EXE% -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
if errorlevel 1 (
  call :warn "Python version is older than 3.10. Attempting to install Python 3.12 via winget..."
  winget install --id Python.Python.3.12 --silent --accept-package-agreements --accept-source-agreements --scope %INSTALL_SCOPE% --exact
  if errorlevel 1 (
    call :error "Unable to upgrade Python."
    exit /b 1
  )
  call :append_if_exists "%LocalAppData%\Programs\Python\Python312"
  call :append_if_exists "%LocalAppData%\Programs\Python\Python312\Scripts"
  call :find_python
  if not defined PYTHON_EXE (
    call :error "Python executable not found after upgrade."
    exit /b 1
  )
  call %PYTHON_EXE% -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
  if errorlevel 1 (
    call :error "Python upgrade did not succeed."
    exit /b 1
  )
)
exit /b 0

:find_python
set "PYTHON_EXE="
for %%C in (python python3) do (
  if not defined PYTHON_EXE (
    where %%C >nul 2>&1
    if not errorlevel 1 set "PYTHON_EXE=%%C"
  )
)
if not defined PYTHON_EXE (
  where py >nul 2>&1
  if not errorlevel 1 set "PYTHON_EXE=py -3"
)
exit /b 0

:ensure_node
where node >nul 2>&1
if errorlevel 1 (
  if defined WINGET_AVAILABLE (
    call :log "Node.js not detected. Installing via winget..."
    winget install --id OpenJS.NodeJS.LTS --silent --accept-package-agreements --accept-source-agreements --scope %INSTALL_SCOPE% --exact
    if errorlevel 1 (
      call :error "Failed to install Node.js using winget."
      exit /b 1
    )
  )
  call :append_if_exists "%LocalAppData%\Programs\nodejs"
  call :append_if_exists "%AppData%\npm"
  where node >nul 2>&1
)
if errorlevel 1 (
  call :error "Node.js executable not found on PATH after installation."
  exit /b 1
)
set "NODE_VER="
set "NODE_MAJOR="
for /f "usebackq tokens=1" %%V in (`node --version 2^>nul`) do set "NODE_VER=%%V"
if defined NODE_VER (
  for /f "tokens=2 delims=v." %%A in ("!NODE_VER!") do set "NODE_MAJOR=%%A"
  if defined NODE_MAJOR (
    if !NODE_MAJOR! LSS 18 (
      call :warn "Detected Node.js !NODE_VER!. Installing latest LTS version..."
      winget install --id OpenJS.NodeJS.LTS --silent --accept-package-agreements --accept-source-agreements --scope %INSTALL_SCOPE% --exact
      if errorlevel 1 (
        call :error "Failed to upgrade Node.js."
        exit /b 1
      )
      call :append_if_exists "%LocalAppData%\Programs\nodejs"
      call :append_if_exists "%AppData%\npm"
    )
  )
)
call :append_if_exists "%LocalAppData%\Programs\nodejs"
call :append_if_exists "%AppData%\npm"
exit /b 0

:setup_python
if not exist requirements.txt (
  call :log "No requirements.txt found. Skipping Python dependency installation."
  exit /b 0
)
call :log "Installing Python dependencies..."
call %PYTHON_EXE% -m pip install --upgrade pip >nul 2>&1
if errorlevel 1 (
  call :warn "Failed to upgrade pip globally. Retrying with --user scope..."
  call %PYTHON_EXE% -m pip install --user --upgrade pip
  if errorlevel 1 (
    call :error "Failed to upgrade pip."
    exit /b 1
  )
)
call %PYTHON_EXE% -m pip install -r requirements.txt
if errorlevel 1 (
  call :warn "Python dependency installation failed. Retrying with --user scope..."
  call %PYTHON_EXE% -m pip install --user -r requirements.txt
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
if defined CI_MODE (
  call :log "Installing Node.js dependencies (npm ci)..."
  call npm ci --no-audit --no-fund
) else (
  call :log "Installing Node.js dependencies (npm install)..."
  call npm install --no-audit --no-fund
)
if errorlevel 1 (
  call :error "npm dependency installation failed."
  exit /b 1
)
exit /b 0

:setup_vcpkg
set "PREFERRED_VCPKG=%cd%\external\vcpkg"
if defined VCPKG_ROOT (
  set "VCPKG_ROOT=%VCPKG_ROOT%"
  call :log "Using VCPKG_ROOT from environment: %VCPKG_ROOT%"
) else (
  if exist "%PREFERRED_VCPKG%\vcpkg.exe" (
    set "VCPKG_ROOT=%PREFERRED_VCPKG%"
  ) else if exist "%cd%\vcpkg\vcpkg.exe" (
    set "VCPKG_ROOT=%cd%\vcpkg"
  ) else (
    set "VCPKG_ROOT=%PREFERRED_VCPKG%"
  )
)
call :log "vcpkg directory: %VCPKG_ROOT%"
if not exist "%VCPKG_ROOT%" (
  mkdir "%VCPKG_ROOT%" >nul 2>&1
)
if not exist "%VCPKG_ROOT%\.git" (
  if exist "%VCPKG_ROOT%\vcpkg.exe" goto :vcpkg_bootstrap
  call :log "Cloning vcpkg into %VCPKG_ROOT%..."
  git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
  if errorlevel 1 (
    call :error "Failed to clone vcpkg repository."
    exit /b 1
  )
) else (
  call :log "Updating existing vcpkg checkout..."
  git -C "%VCPKG_ROOT%" pull --ff-only
  if errorlevel 1 call :warn "Unable to update vcpkg (continuing with local copy)."
)

:vcpkg_bootstrap
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  call :log "Bootstrapping vcpkg (this may take a moment)..."
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
  if errorlevel 1 (
    call :error "vcpkg bootstrap failed."
    exit /b 1
  )
)
set "VCPKG_ROOT=%VCPKG_ROOT%"
set "VCPKG_DEFAULT_TRIPLET=x64-windows"
if not defined VCPKG_FEATURE_FLAGS set "VCPKG_FEATURE_FLAGS=manifests,registries,binarycaching"
if exist vcpkg.json (
  call :log "Installing C/C++ dependencies via vcpkg manifest..."
  "%VCPKG_ROOT%\vcpkg.exe" install --x-wait-for-lock --clean-after-build
  if errorlevel 1 (
    call :error "vcpkg install failed."
    exit /b 1
  )
)
exit /b 0

:detect_generator
if defined FORCE_GENERATOR (
  set "CMAKE_GENERATOR=%FORCE_GENERATOR%"
  exit /b 0
)
where ninja >nul 2>&1
if not errorlevel 1 (
  set "CMAKE_GENERATOR=Ninja"
  exit /b 0
)
set "CMAKE_GENERATOR=Visual Studio 17 2022"
exit /b 0

:configure_cmake
call :detect_generator
set "BUILD_DIR=%cd%\build"
set "RUNTIME_DIR=%cd%\ENGINE"
if not defined REUSE_BUILD (
  if exist "%BUILD_DIR%" (
    call :log "Removing existing build directory..."
    rmdir /s /q "%BUILD_DIR%" 2>nul
  )
)
if not exist "%BUILD_DIR%" (
  mkdir "%BUILD_DIR%" >nul 2>&1
  if errorlevel 1 (
    call :error "Unable to create build directory."
    exit /b 1
  )
)
set "TOOLCHAIN_ARG="
if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
call :log "Configuring CMake project (%CMAKE_GENERATOR%)..."
cmake -S . -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%RUNTIME_DIR%" %TOOLCHAIN_ARG%
if errorlevel 1 (
  call :error "CMake configuration failed."
  exit /b 1
)
exit /b 0

:build_cmake
set "BUILD_DIR=%cd%\build"
if not exist "%BUILD_DIR%" (
  call :error "Build directory not found. Run configuration first or use --reuse-build with an existing build."
  exit /b 1
)
call :log "Building project (RelWithDebInfo)..."
cmake --build "%BUILD_DIR%" --config RelWithDebInfo
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

:append_if_exists
if "%~1"=="" exit /b 0
if exist "%~1" (
  set "PATH=%~1;%PATH%"
)
exit /b 0

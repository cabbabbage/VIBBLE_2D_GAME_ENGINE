@echo off
setlocal enabledelayedexpansion

set "EXTRA_ARGS="
set "TOOL_MODE=0"

if "%~1"=="-r" (
    echo [INFO] RebuildAssets mode requested.
    set "EXTRA_ARGS=-r"
)
if "%~1"=="-t" (
    echo [INFO] ToolBuild mode requested.
    set "TOOL_MODE=1"
)

:: === Tool mode steps (-t) ===
if %TOOL_MODE%==1 (
    echo [TOOL] Step 1: Checking Python installation...
    where python >nul 2>nul
    if errorlevel 1 (
        echo [TOOL] Python not found. Installing via winget...
        winget install -e --id Python.Python.3
        if errorlevel 1 (
            echo [ERROR] Failed to install Python.
            pause & exit /b 1
        )
    ) else (
        echo [TOOL] Python found: 
        python --version
    )

    echo [TOOL] Step 2: Installing Python requirements...
    if exist requirements.txt (
        python -m pip install --upgrade pip
        python -m pip install -r requirements.txt
        if errorlevel 1 (
            echo [ERROR] Failed to install requirements.txt
            pause & exit /b 1
        )
    ) else (
        echo [WARN] requirements.txt not found.
    )

    echo [TOOL] Step 3: Checking executable build...
    if not exist "PYTHON ASSET MANAGER.exe" (
        echo [TOOL] Building PYTHON ASSET MANAGER executable with PyInstaller...
        python -m PyInstaller --onefile --windowed PYTHON ASSET MANAGER/main.py
        if errorlevel 1 (
            echo [ERROR] PyInstaller build failed.
            pause & exit /b 1
        )
        if exist "dist\main.exe" (
            move /Y "dist\main.exe" "PYTHON ASSET MANAGER.exe" >nul
            echo [TOOL] Executable moved to root as PYTHON ASSET MANAGER.exe
        ) else (
            echo [ERROR] PyInstaller did not produce dist\main.exe
            pause & exit /b 1
        )
    ) else (
        echo [TOOL] Executable already exists.
    )
)

:: === Step 1: Ensure vcpkg exists and is bootstrapped ===
echo Checking vcpkg setup...
if not exist "vcpkg" (
    echo [INFO] Cloning vcpkg...
    git clone https://github.com/microsoft/vcpkg.git
    if errorlevel 1 (
        echo [ERROR] Failed to clone vcpkg.
        pause
        exit /b 1
    )
)
if not exist "vcpkg\vcpkg.exe" (
    echo [INFO] Bootstrapping vcpkg...
    pushd vcpkg
    call bootstrap-vcpkg.bat
    popd
    if not exist "vcpkg\vcpkg.exe" (
        echo [ERROR] vcpkg bootstrap failed.
        pause
        exit /b 1
    )
)

:: === Step 2: Confirm critical files exist ===
echo Checking required files and directories...
if not exist "CMakeLists.txt" (
    echo [ERROR] Missing CMakeLists.txt in root directory.
    pause & exit /b 1
)
if not exist "ENGINE\\main.cpp" (
    echo [ERROR] Missing ENGINE\\main.cpp
    pause & exit /b 1
)
if not exist "ENGINE\\engine.cpp" (
    echo [ERROR] Missing ENGINE\\engine.cpp
    pause & exit /b 1
)
if not exist "vcpkg\\scripts\\buildsystems\\vcpkg.cmake" (
    echo [ERROR] Missing toolchain file at vcpkg\\scripts\\buildsystems\\vcpkg.cmake
    pause & exit /b 1
)

:: === Step 2.5: Ensure stb_image_resize2.h is present ===
set "STB_DEST=vcpkg\installed\x64-windows\include\custom"
if not exist "%STB_DEST%\stb_image_resize2.h" (
    echo [INFO] Downloading stb_image_resize2.h...
    mkdir "%STB_DEST%" >nul 2>&1
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12; " ^
        "Invoke-WebRequest https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h -OutFile '%STB_DEST%\stb_image_resize2.h'"
    if errorlevel 1 (
        echo [ERROR] Failed to download stb_image_resize2.h
        pause & exit /b 1
    )
)


:: === Step 4: Clean build ===
echo Cleaning previous CMake cache...
rmdir /s /q build >nul 2>&1
mkdir build

:: === Step 5: Configure with CMake ===
echo Configuring project...
cmake -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%cd%\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake" ^
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="%cd%\\ENGINE" ^
  -B build -S .
if errorlevel 1 (
    echo ❌ CMake configuration failed.
    pause & exit /b 1
)

:: === Step 6: Build ===
echo Building project...
cmake --build build --config Release
if errorlevel 1 (
    echo ❌ Build failed.
    pause & exit /b 1
)

:: === Step 7: Launch executable ===
set EXE1=%~dp0ENGINE\\Release\\engine.exe
set EXE2=%~dp0build\\Release\\engine.exe

echo Launching built game...
if exist "%EXE1%" (
    "%EXE1%" %EXTRA_ARGS%
) else if exist "%EXE2%" (
    "%EXE2%" %EXTRA_ARGS%
) else (
    echo Executable not found at "%EXE1%" or "%EXE2%"
)

pause
endlocal

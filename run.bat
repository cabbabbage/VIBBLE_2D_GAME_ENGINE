@echo off
setlocal

set "REPO_ROOT=%~dp0"
set "EXE_PATH=%REPO_ROOT%release\engine.exe"

if not exist "%EXE_PATH%" (
    echo [run] engine.exe not found at "%EXE_PATH%". Please build the project or place the executable there first.
    exit /b 1
)

echo [run] Launching "%EXE_PATH%"...
start "" "%EXE_PATH%"

exit /b 0

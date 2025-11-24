@echo off
setlocal ENABLEDELAYEDEXPANSION

rem Determine repo root (directory where this script lives)
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%"

rem Recreate context folder
rd /s /q "context" 2>nul
mkdir "context"

rem Decide which list file to use
set "LIST_FILE=context_files.txt"
if not exist "%LIST_FILE%" (
    if exist "context_list.txt" (
        set "LIST_FILE=context_list.txt"
    )
)

rem Copy each file listed in the list file into context\
if exist "%LIST_FILE%" (
    for /f "usebackq delims=" %%F in ("%LIST_FILE%") do (
        if not "%%F"=="" (
            copy "%%F" "context\" >nul
        )
    )
) else (
    echo WARNING: No context_files.txt or context_list.txt found. Skipping ENGINE file copies.
)

rem Always copy log.txt explicitly
if exist "log.txt" (
    copy "log.txt" "context\" >nul
) else (
    echo WARNING: log.txt not found in "%CD%".
)

rem Ensure ENGINE folder exists
if not exist "ENGINE" (
    echo ERROR: ENGINE folder not found in "%CD%".
    popd
    endlocal
    exit /b 1
)

rem Ensure context folder exists (should already, but just in case)
if not exist "context" (
    mkdir "context"
)

rem Generate recursive file listing starting at ENGINE into context folder
tree "ENGINE" /F > "context\engine_struct.txt"

popd
endlocal

@echo off
REM Outline the file structure starting at ENGINE folder

set ROOT=ENGINE

echo File structure for %ROOT%:
echo ----------------------------

REM /F to show file names, /A:D for directories, /A:-D for files
REM /S to recurse, /B for bare format

tree %ROOT% /F


echo ----------------------------
echo Done.
pause

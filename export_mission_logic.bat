@echo off
setlocal
title Brute Force - export mission logic
rem Writes every level's trigger network (the mission logic) as text, JSON and Graphviz files.
rem Run convert_all.bat first: this reads its output folder.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force mission logic exporter
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Each levels-NAME.xml gets levels-NAME_logic.txt, _logic.json and _logic.dot next to it.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\level2logic.py" "%OUT%"
echo.
echo  Done.
echo.
pause
exit /b 0

:nopython
echo  Python was not found. Install it from python.org with "Add python.exe to PATH" ticked.
echo.
pause
exit /b 1

:nodata
echo  Could not find %OUT%
echo  Run convert_all.bat first.
echo.
pause
exit /b 1

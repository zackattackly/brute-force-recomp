@echo off
setlocal
title Brute Force - export animations to JSON
rem Writes every animation clip of the game as JSON and as BVH (opens in Blender).
rem Run convert_all.bat first: this reads its output folder.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force animation exporter
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Each animations-NAME.xml gets folders animations-NAME_json and _bvh next to it.
echo  About 6,100 clips; this takes a few minutes.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\anim2json.py" "%OUT%"
python "%TOOLS%\anim2bvh.py" "%OUT%"
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

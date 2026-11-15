@echo off
setlocal
title Brute Force - export effects to JSON
rem Writes every particle effect (effects-common.ale) as a JSON file.
rem Run convert_all.bat first; run export_textures.bat first to link the effect textures.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force effects exporter
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Writes common\effects-common_json\ (603 effects, one JSON file each, plus _index.json).
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\ale2json.py" "%OUT%"
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

@echo off
setlocal
title Brute Force - export the effect viewer
rem Writes one HTML page that plays the particle effects in 3D (works offline, effects and textures embedded).
rem Run convert_all.bat, export_textures.bat and export_effects.bat first.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force effect viewer
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Writes common\effects_viewer.html (603 effects, about 4 MB). Open it in Chrome, Edge or Firefox.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\fx2html.py" "%OUT%"
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

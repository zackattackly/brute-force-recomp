@echo off
setlocal
title Brute Force - export textures to PNG
rem Writes every texture of the game as a PNG image.
rem Run convert_all.bat first: this reads its output folder.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force texture exporter
echo  ----------------------------
echo  Reads and writes in: %OUT%
echo  Each textures-NAME.xml gets a folder textures-NAME_png next to it.
echo  About 12,600 textures; this takes several minutes.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\tex2png.py" "%OUT%"
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

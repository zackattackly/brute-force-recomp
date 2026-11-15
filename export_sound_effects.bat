@echo off
setlocal
title Brute Force - export the sound effect browser
rem Writes every sound effect, listed by where the game uses it, as one HTML page with players.
rem Run convert_all.bat and export_sounds.bat (answer y to PCM, so the browser can play them) first.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force sound effect browser
echo  --------------------------------
echo  Reads and writes in: %OUT%
echo  Writes common\sound_effects.html (weapons, materials, objects and the placed sounds of every level).
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\sfx2html.py" "%OUT%"
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

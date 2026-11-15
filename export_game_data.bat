@echo off
setlocal
title Brute Force - export the game data sheet
rem Writes the weapons, characters and items as one HTML page plus CSV spreadsheets.
rem Run convert_all.bat first.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force game data sheet
echo  ---------------------------
echo  Reads and writes in: %OUT%
echo  Writes common\game_data.html and common\game_data_*.csv (weapons, characters, items).
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\data2sheet.py" "%OUT%"
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

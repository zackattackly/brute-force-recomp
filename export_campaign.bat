@echo off
setlocal
title Brute Force - export the mission guide
rem Writes every mission with its levels, movies, on-screen texts, objectives and credits as one HTML page.
rem Run convert_all.bat first. Run it again after the other exporters to get links to their files.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force mission guide
echo  ---------------------------
echo  Reads and writes in: %OUT%
echo  Writes common\campaign.html (18 campaign missions, 7 deathmatch and 7 squad deathmatch maps).
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\campaign2html.py" "%OUT%"
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

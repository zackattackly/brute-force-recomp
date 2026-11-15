@echo off
setlocal
title Brute Force - rebuild the game menus
rem Rebuilds every front-end and in-game menu as one web page you open in a browser.
rem Run convert_all.bat and export_textures.bat first.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force menu viewer
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Writes common\game-options-en_menus.html (64 menus). Open it in a browser.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\menu2html.py" "%OUT%" en
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

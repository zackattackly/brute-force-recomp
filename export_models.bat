@echo off
setlocal
title Brute Force - export 3D models to OBJ
rem Writes every 3D model library of the game as an OBJ file (with MTL).
rem Run convert_all.bat first (and export_textures.bat for textures).

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force model exporter
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Each objects-NAME.xml gets objects-NAME.obj and .mtl next to it.
echo  This takes a minute or two.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\mesh2obj.py" "%OUT%"
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

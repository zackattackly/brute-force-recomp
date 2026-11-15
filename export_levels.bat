@echo off
setlocal
title Brute Force - export whole levels to glTF
rem Writes every level as one 3D scene: terrain plus every placed building, prop and pickup,
rem with textures. Open the .glb files in Blender (File > Import > glTF 2.0) or 3D Viewer.
rem Run convert_all.bat and export_textures.bat first.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force level exporter
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Writes ^<level^>\^<level^>_scene.glb for each of the 53 levels, with sky and lights (about 370 MB).
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\level2glb.py" "%OUT%"
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

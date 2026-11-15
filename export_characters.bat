@echo off
setlocal
title Brute Force - export rigged characters to glTF
rem Writes every character as a rigged, skinned, animated glTF (.glb) model.
rem Run convert_all.bat first: this reads its output folder.
rem Run export_textures.bat first too, so the characters have their textures.

set "TOOLS=%~dp0tools"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force character exporter
echo  --------------------------
echo  Reads and writes in: %OUT%
echo  Writes one .glb per character into %OUT%\characters_glb
echo  (31 characters, about 60 MB). In Blender: File - Import - glTF 2.0.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata

pause
python "%TOOLS%\char2gltf.py" "%OUT%"
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

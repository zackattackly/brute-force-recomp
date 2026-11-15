@echo off
setlocal
title Brute Force - export everything
rem Runs every exporter in the right order and finishes with index.html, a start page for it all.
rem Run convert_all.bat first (or answer y below). It can take up to an hour.

set "TOOLS=%~dp0tools"
set "DATA=%~dp0..\Brute Force (USA) (En,Fr,De,Es,It).xiso\data"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force: export everything
echo  ------------------------------
echo  Game data : %DATA%
echo  Output    : %OUT%
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython

set "CONV=n"
set /p "CONV=Unpack and convert the game data first (needed once, or after a tools update)? (y/N): "
set "PCM=--pcm"
set "ASKPCM=y"
set /p "ASKPCM=Sounds as plain PCM, so the dialogue and sound pages can play them (bigger files)? (Y/n): "
if /i "%ASKPCM%"=="n" set "PCM="

if /i "%CONV%"=="y" python "%TOOLS%\extract_and_convert.py" "%DATA%" "%OUT%"
if not exist "%OUT%" goto nodata

echo [1/15] textures & python "%TOOLS%\tex2png.py" "%OUT%"
echo [2/15] models & python "%TOOLS%\mesh2obj.py" "%OUT%"
echo [3/15] animations & python "%TOOLS%\anim2json.py" "%OUT%" & python "%TOOLS%\anim2bvh.py" "%OUT%"
echo [4/15] characters & python "%TOOLS%\char2gltf.py" "%OUT%"
echo [5/15] effects & python "%TOOLS%\ale2json.py" "%OUT%"
echo [6/15] sounds & python "%TOOLS%\snd2wav.py" "%DATA%" "%OUT%" --lang en %PCM%
echo [7/15] level scenes & python "%TOOLS%\level2glb.py" "%OUT%"
echo [8/15] mission logic & python "%TOOLS%\level2logic.py" "%OUT%"
echo [9/15] menus & python "%TOOLS%\menu2html.py" "%OUT%" en
echo [10/15] game data & python "%TOOLS%\data2sheet.py" "%OUT%"
echo [11/15] mission guide & python "%TOOLS%\campaign2html.py" "%OUT%"
echo [12/15] dialogue & python "%TOOLS%\chatter2html.py" "%OUT%"
echo [13/15] sound effects & python "%TOOLS%\sfx2html.py" "%OUT%"
echo [14/15] effect viewer & python "%TOOLS%\fx2html.py" "%OUT%"
echo [15/15] start page & python "%TOOLS%\make_index.py" "%OUT%"
echo.
echo  Done. Open %OUT%\index.html
start "" "%OUT%\index.html"
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
echo  Run convert_all.bat first (or answer y to the first question).
echo.
pause
exit /b 1

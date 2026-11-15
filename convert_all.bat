@echo off
setlocal
title Brute Force - convert all game data
rem Unpacks every .tgz archive of the game and converts all .xmb files to readable .xml.
rem Double-click to run. Needs Python 3 (python.org) and about 10 GB of free disk space.

set "TOOLS=%~dp0tools"
set "DATA=%~dp0..\Brute Force (USA) (En,Fr,De,Es,It).xiso\data"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force data converter
echo  --------------------------
echo  Game data : %DATA%
echo  Output    : %OUT%
echo.
echo  This unpacks about 2.2 GB of archives (roughly 5-8 GB on disk once unpacked
echo  and converted). It can take a while. Close this window to cancel.
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%DATA%\common.tgz" goto nodata

pause
python "%TOOLS%\extract_and_convert.py" "%DATA%" "%OUT%"
echo.
echo  Done. Your converted files are in: %OUT%
echo.
pause
exit /b 0

:nopython
echo  Python was not found. Install it from python.org with "Add python.exe to PATH" ticked,
echo  then open this file again.
echo.
pause
exit /b 1

:nodata
echo  Could not find the game's data folder at:
echo    %DATA%
echo  It should contain common.tgz and the other .tgz archives.
echo.
pause
exit /b 1

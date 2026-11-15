@echo off
setlocal
title Brute Force - export sounds to WAV
rem Writes every sound of the game as a .wav file: music, dialogue, and the effects
rem kept in memory (weapons, hits, menus ...), plus a list of every sound cue.
rem Run convert_all.bat first.

set "TOOLS=%~dp0tools"
set "DATA=%~dp0..\Brute Force (USA) (En,Fr,De,Es,It).xiso\data"
set "OUT=%USERPROFILE%\Brute Force data"

echo.
echo  Brute Force sound exporter
echo  --------------------------
echo  Game data : %DATA%
echo  Reads and writes in: %OUT%
echo  Writes sounds_wav\ (about 24,000 .wav files, 0.5 GB for one language) and sounds_wav\_sounds.csv
echo.

where python >nul 2>nul
if errorlevel 1 goto nopython
if not exist "%OUT%" goto nodata
if not exist "%DATA%\sounds" goto nogame

set "LANG=en"
set /p "LANG=Dialogue language: en, de, es, fr, it or all (press Enter for en): "
set "PCM="
set /p "ASKPCM=Plain 16-bit PCM instead of ADPCM? Slower and 4x bigger, only needed for some tools (y/N): "
if /i "%ASKPCM%"=="y" set "PCM=--pcm"

python "%TOOLS%\snd2wav.py" "%DATA%" "%OUT%" --lang %LANG% %PCM%
echo.
echo  Done. The sounds are in: %OUT%\sounds_wav
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

:nogame
echo  Could not find the game's sounds folder: %DATA%\sounds
echo.
pause
exit /b 1

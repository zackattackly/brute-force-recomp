@echo off
rem Brute Force PC recomp: starts the game (the desktop shortcut runs this).
cd /d "%~dp0"
if not exist gamepath.txt (
    echo The game folder is not set. Run Install.bat again.
    pause
    exit /b 1
)
set /p GP=<gamepath.txt
if not exist "%GP%\default.xbe" (
    echo default.xbe was not found in "%GP%". Run Install.bat again.
    pause
    exit /b 1
)
tasklist /fi "imagename eq bflaunch.exe" | find /i "bflaunch.exe" >nul && exit /b 0
del restart_loader.txt 2>nul
rem online: with "upnp" in netplay.txt the router forwards the game's port while it runs (and tells the game its internet address)
set UPNP=0
if exist netplay.txt findstr /b /i "upnp" netplay.txt >nul && set UPNP=1
del external_ip.txt 2>nul
if "%UPNP%"=="1" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0online_port.ps1" open
:again
bflaunch.exe "%GP%\default.xbe"
if exist restart_loader.txt (
    del restart_loader.txt
    timeout /t 2 /nobreak >nul
    goto again
)
if "%UPNP%"=="1" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0online_port.ps1" close

@echo off
rem Runs the Brute Force PC loader on the game in your iso folder; results go to bfloader.log here.
rem Only one copy can run at a time; close the game window to stop it (it also stops by itself after 10 minutes).
rem When the game asks the Xbox to reboot (for example its reset button combination), the loader leaves
rem restart_loader.txt and the game is started again, like a console reboot.
cd /d "%~dp0"
del restart_loader.txt 2>nul
:again
bflaunch.exe "%USERPROFILE%\OneDrive\Desktop\iso\Brute Force (USA) (En,Fr,De,Es,It).xiso\default.xbe"
if exist restart_loader.txt (
    del restart_loader.txt
    timeout /t 2 /nobreak >nul
    goto again
)

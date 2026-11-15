@echo off
rem Runs the Brute Force online server (only one PC needs this: the one whose address is in server.txt).
rem Forward UDP port 34567 on the router to this PC, and allow bfserver.exe through the Windows firewall when asked.
cd /d "%~dp0"
title Brute Force online server
bfserver.exe 34567
pause

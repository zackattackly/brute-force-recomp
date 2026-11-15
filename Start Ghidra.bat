@echo off
rem Starts Ghidra 12.1.4 and writes its startup messages to ghidra_start.log next to this file.
set "GH=C:\Users\zackh\Downloads\ghidra_12.1.4_PUBLIC_20260921\ghidra_12.1.4_PUBLIC"
rem The Xbox loader is already installed in your Ghidra settings; a second copy in the install folder stops Ghidra starting, so move it aside.
if exist "%GH%\Ghidra\Extensions\ghidra-xbe" (
  if not exist "C:\Users\zackh\Downloads\_to_delete" mkdir "C:\Users\zackh\Downloads\_to_delete"
  move "%GH%\Ghidra\Extensions\ghidra-xbe" "C:\Users\zackh\Downloads\_to_delete\ghidra-xbe-duplicate"
)
echo Starting Ghidra... (keep this window open while Ghidra runs; messages go to ghidra_start.log)
call "%GH%\support\launch.bat" fg jdk Ghidra "" "" ghidra.GhidraRun > "%~dp0ghidra_start.log" 2>&1

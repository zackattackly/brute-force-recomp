@echo off
rem Builds rbench.exe (renderer speed test, needs bench.txt next to it) from render.c.
cd /d "%~dp0"
set PATH=C:\Program Files\LLVM\bin;%PATH%
llvm-dlltool -m i386 -d kernel32.def -l kernel32.lib -k || exit /b 1
llvm-dlltool -m i386 -d ntdll.def -l ntdll.lib -k || exit /b 1
set F=--target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -msse2 -mfpmath=sse -mstackrealign -Wno-incompatible-pointer-types -Wno-pointer-sign
clang %F% -c render.c -o render_b.o || exit /b 1
clang %F% -c rbench.c util.c || exit /b 1
lld-link /subsystem:console /entry:start /largeaddressaware /nodefaultlib /out:rbench.exe rbench.o util.o render_b.o kernel32.lib ntdll.lib || exit /b 1
rbench.exe

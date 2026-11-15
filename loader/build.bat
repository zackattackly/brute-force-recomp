@echo off
rem Builds bfcore.dll and bflaunch.exe with LLVM (no Windows SDK needed).
cd /d "%~dp0"
set PATH=C:\Program Files\LLVM\bin;%PATH%
for %%L in (kernel32 ntdll user32 gdi32 k32_launch opengl32 winmm) do llvm-dlltool -m i386 -d %%L.def -l %%L.lib -k || exit /b 1
set F=--target=i686-pc-windows-msvc -O1 -ffreestanding -fno-builtin -fno-stack-protector -mno-sse -Wno-incompatible-pointer-types -Wno-pointer-sign
clang %F% -c bfcore.c util.c kernel.c kernel2.c gpu.c input.c net.c patch.c || exit /b 1
clang %F:-mno-sse=-msse2 -mfpmath=sse -mstackrealign% -O2 -c render.c || exit /b 1
clang %F:-mno-sse=-msse2 -mfpmath=sse -mstackrealign% -O2 -c dsound.c || exit /b 1
lld-link /dll /entry:DllMain /base:0x10000000 /nodefaultlib /map:bfcore.map /out:bfcore.dll bfcore.o util.o kernel.o kernel2.o gpu.o render.o input.o dsound.o net.o patch.o kernel32.lib ntdll.lib user32.lib gdi32.lib || exit /b 1
clang %F% -c bflaunch.c || exit /b 1
lld-link /subsystem:console /entry:start /base:0x10000 /fixed /dynamicbase:no /largeaddressaware /opt:noref /nodefaultlib /out:bflaunch.exe bflaunch.o k32_launch.lib user32.lib || exit /b 1
clang %F% -c bfview.c || exit /b 1
clang %F:-mno-sse=-msse2 -mfpmath=sse -mstackrealign% -c bfgl.c || exit /b 1
lld-link /subsystem:windows /entry:start /nodefaultlib /out:bfview.exe bfview.o bfgl.o kernel32.lib user32.lib gdi32.lib opengl32.lib ntdll.lib winmm.lib || exit /b 1
clang %F% -c bfserver.c || exit /b 1
lld-link /subsystem:console /entry:start /nodefaultlib /out:bfserver.exe bfserver.o kernel32.lib || exit /b 1
clang %F% -c bflist.c || exit /b 1
lld-link /subsystem:windows /entry:start /nodefaultlib /out:bflist.exe bflist.o kernel32.lib || exit /b 1
if exist bfcore_new.dll del bfcore_new.dll
echo Build OK

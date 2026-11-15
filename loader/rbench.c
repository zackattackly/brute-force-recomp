/* rbench.exe: runs render.c's benchmark outside the game (development aid, Milestone 77) */
#include "util.h"
IMP HANDLE WINAPI GetStdHandle(DWORD);
IMP HANDLE WINAPI CreateFileMappingA(HANDLE, void *, DWORD, DWORD, DWORD, const char *);
IMP void *WINAPI MapViewOfFileEx(HANDLE, DWORD, DWORD, DWORD, u32, void *);
u32 kel[0x800];
void render_bench(void);
void start(void) {
    g_log = GetStdHandle((DWORD)-11);
    char *cl = GetCommandLineA(); int mode = 0; for (; *cl; cl++) if (*cl >= '1' && *cl <= '9') mode = *cl - '0';
    if (mode == 0) { if (!VirtualAlloc((void *)0x80000000, 0x4000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) { out("no memory\r\n"); ExitProcess(1); } out("VirtualAlloc RW: "); }
    else { HANDLE s = CreateFileMappingA((HANDLE)-1, 0, mode == 1 ? 0x40 : 0x04, 0, 0x4000000, 0);
        if (!MapViewOfFileEx(s, mode == 1 ? 0x26 : 0x06, 0, 0, 0x4000000, (void *)0x80000000)) { out("map failed\r\n"); ExitProcess(1); }
        if (mode != 3 && !MapViewOfFileEx(s, mode == 1 ? 0x26 : 0x06, 0, 0, 0x4000000, (void *)0xF0000000)) out("(no second view) ");
        out(mode == 1 ? "section RWX, two views: " : mode == 2 ? "section RW, two views: " : "section RW, one view: "); }
    for (int r = 0; r < 2; r++) render_bench(); ExitProcess(0);
}

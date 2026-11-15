/* Brute Force PC loader - stage 3, version 0.
 * Maps default.xbe at its Xbox addresses (0x10000..), points the kernel import table at
 * logging stubs, and reports. It does not start the game yet.
 * Built freestanding with clang + lld-link (no C runtime). */
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
typedef void *HANDLE; typedef int BOOL; typedef u32 DWORD;
#define WINAPI __stdcall
__declspec(dllimport) void *WINAPI VirtualAlloc(void *, u32, DWORD, DWORD);
__declspec(dllimport) BOOL WINAPI VirtualFree(void *, u32, DWORD);
__declspec(dllimport) u32 WINAPI VirtualQuery(const void *, void *, u32);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) DWORD WINAPI GetFileSize(HANDLE, DWORD *);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) void WINAPI ExitProcess(u32);
__declspec(dllimport) DWORD WINAPI GetLastError(void);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameA(void *, char *, DWORD);
__declspec(dllimport) char *WINAPI GetCommandLineA(void);
__declspec(dllimport) HANDLE WINAPI GetProcessHeap(void);
__declspec(dllimport) void *WINAPI HeapAlloc(HANDLE, DWORD, u32);
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_EXECUTE_READWRITE 0x40
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define INVALID_HANDLE ((HANDLE)-1)

#include "kexports.h"

static HANDLE g_log;
static void out(const char *s) { DWORD n, l = 0; while (s[l]) l++; WriteFile(g_log, s, l, &n, 0); }
static void hex(u32 v) { char b[11]; int i; b[0]='0'; b[1]='x'; for (i = 0; i < 8; i++) b[2+i] = "0123456789ABCDEF"[(v >> (28 - 4*i)) & 15]; b[10]=0; out(b); }
static void dec(u32 v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); out(b + i); }
static void *memcpy_(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
static void memset_(void *d, int c, u32 n) { u8 *a = d; while (n--) *a++ = (u8)c; }
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

/* Called by every kernel stub: logs the call and stops (nothing is implemented yet). */
static void __stdcall unimplemented(u32 ordinal) {
    out("UNIMPLEMENTED kernel call: "); dec(ordinal); out(" ");
    out(ordinal < sizeof(g_kname)/sizeof(g_kname[0]) ? g_kname[ordinal] : "?"); out("\r\n");
    ExitProcess(2);
}

typedef struct { u32 base, size, prot, state, type; } MBI; /* subset of MEMORY_BASIC_INFORMATION layout below */

void __stdcall start(void) {
    char path[600], logp[600]; DWORD n; u32 i, len;
    len = GetModuleFileNameA(0, path, 500);
    for (i = len; i > 0 && path[i-1] != '\\'; i--) ;
    memcpy_(logp, path, i); memcpy_(logp + i, "bfloader.log", 13);
    g_log = CreateFileA(logp, GENERIC_WRITE, 1, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
    out("Brute Force PC loader v0\r\n");

    /* 1. claim the Xbox address range */
    const u32 BASE = 0x10000, END = 0x4C5000;
    void *mem = VirtualAlloc((void *)BASE, END - BASE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    out("Reserve 0x00010000-0x004C5000: "); out(mem ? "OK\r\n" : "FAILED, error "); if (!mem) { dec(GetLastError()); out("\r\n"); }
    if (!mem) {
        /* report what is already mapped in that range */
        u32 a = BASE; u32 info[7];
        while (a < END) {
            if (!VirtualQuery((void *)a, info, sizeof info)) break;
            out("  region "); hex(info[0]); out(" size "); hex(info[3]); out(" state "); hex(info[4]); out(" type "); hex(info[6]); out("\r\n");
            a = info[0] + info[3];
        }
        out("Stopping: the game's address range is not free.\r\n"); CloseHandle(g_log); ExitProcess(1);
    }

    /* 2. read default.xbe (path: first argument, else next to the loader) */
    char xbe[600]; const char *cl = GetCommandLineA(); u32 q = 0;
    if (*cl == '"') { cl++; while (*cl && *cl != '"') cl++; if (*cl) cl++; } else { while (*cl && *cl != ' ') cl++; }
    while (*cl == ' ') cl++;
    if (*cl) { if (*cl == '"') cl++; while (*cl && *cl != '"' && q < 590) xbe[q++] = *cl++; xbe[q] = 0; }
    else { memcpy_(xbe, path, i); memcpy_(xbe + i, "default.xbe", 12); }
    out("XBE: "); out(xbe); out("\r\n");
    HANDLE f = CreateFileA(xbe, GENERIC_READ, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
    if (f == INVALID_HANDLE) { out("Cannot open the XBE, error "); dec(GetLastError()); out("\r\n"); ExitProcess(1); }
    u32 fsz = GetFileSize(f, 0);
    u8 *x = HeapAlloc(GetProcessHeap(), 0, fsz);
    ReadFile(f, x, fsz, &n, 0); CloseHandle(f);
    out("XBE size "); dec(n); out(" bytes\r\n");
    if (rd32(x) != 0x48454258) { out("Not an XBE (bad magic)\r\n"); ExitProcess(1); }

    /* 3. map headers and sections */
    u32 base = rd32(x + 0x104), hsz = rd32(x + 0x108), isz = rd32(x + 0x10C);
    u32 nsec = rd32(x + 0x11C), sech = rd32(x + 0x120);
    u32 entry = rd32(x + 0x128) ^ 0xA8FC57AB, thunk = rd32(x + 0x158) ^ 0x5B6D40B6;
    out("base "); hex(base); out(" image size "); hex(isz); out(" entry "); hex(entry); out(" kernel thunks "); hex(thunk); out("\r\n");
    if (base != BASE || base + isz > END) { out("Unexpected layout\r\n"); ExitProcess(1); }
    memset_((void *)BASE, 0, END - BASE);
    memcpy_((void *)base, x, hsz);
    for (u32 s = 0; s < nsec; s++) {
        const u8 *h = x + (sech - base) + s * 0x38;
        u32 va = rd32(h + 4), vs = rd32(h + 8), ra = rd32(h + 12), rs = rd32(h + 16), na = rd32(h + 20);
        memcpy_((void *)va, x + ra, rs < vs ? rs : vs);
        out("  section "); out((const char *)(x + (na - base))); out(" at "); hex(va); out(" size "); hex(vs); out("\r\n");
    }

    /* 4. kernel imports -> logging stubs (push ordinal; call unimplemented) */
    u8 *stubs = VirtualAlloc(0, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    u32 *t = (u32 *)thunk, k = 0;
    while (t[k]) {
        u32 ord = t[k] & 0x7FFFFFFF; u8 *s = stubs + k * 16;
        s[0] = 0x68; *(u32 *)(s + 1) = ord;                                   /* push ord */
        s[5] = 0xE8; *(u32 *)(s + 6) = (u32)unimplemented - (u32)(s + 10);    /* call unimplemented */
        s[10] = 0xCC;
        t[k] = (u32)s; k++;
    }
    out("Kernel imports patched: "); dec(k); out("\r\n");
    out("First bytes at entry: ");
    for (u32 b = 0; b < 8; b++) { hex(((u8 *)entry)[b]); out(" "); }
    out("\r\nLoader v0 finished: the game image is in place. It is not started yet.\r\n");
    CloseHandle(g_log);
    ExitProcess(0);
}

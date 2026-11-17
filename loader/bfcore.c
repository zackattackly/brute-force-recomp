/* Brute Force PC loader - bfcore.dll (Milestone 73).
 * bflaunch.exe (linked at 0x10000, as large as the game image) reserves the Xbox address range.
 * This DLL copies default.xbe over it, replaces the Xbox kernel imports (kernel.c), patches the
 * game's few Xbox-specific fs: reads to per-thread fake KPCR blocks, and jumps to the game's entry. */
#include "util.h"

u32 kernel_patch_imports(u32 *t, u8 *stubs, u32 *nimpl, u32 *ndata);
void kernel_start_ticker(void);
IMP DWORD WINAPI GetFileAttributesA(const char *);
DWORD g_tls;

/* ---- fake Xbox KPCR / KTHREAD per thread ----
 * KPCR: +0x04 "StackBase" = end of this thread's TLS block (XAPI indexes TLS with a negative index from it),
 *       +0x1C self, +0x20 Prcb (-> +0x28), +0x24 Irql, +0x28 Prcb.CurrentThread, +0x58 (DSOUND reads it; 0)
 * KTHREAD: +0x28 TlsData (start of the TLS block; XAPI stores TlsData+4 in its first dword) */
void *new_kpcr(u32 tlsSize) {
    u8 *b = VirtualAlloc(0, 0x1000 + tlsSize + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    u8 *kpcr = b, *kthread = b + 0x200, *tls = b + 0x1000;
    *(u32 *)(kpcr + 0x04) = (u32)(tls + tlsSize);
    *(u32 *)(kpcr + 0x1C) = (u32)kpcr;
    *(u32 *)(kpcr + 0x20) = (u32)(kpcr + 0x28);
    *(u32 *)(kpcr + 0x28) = (u32)kthread;
    *(u32 *)(kthread + 0x28) = (u32)tls;
    *(u32 *)tls = (u32)(tls + 4);          /* what XAPI writes for threads it creates */
    TlsSetValue(g_tls, kpcr);
    return kpcr;
}

/* ---- fs: patches ---- each site becomes "call helper" + NOPs; the helper reads this thread's
 * fake KPCR through the Windows TLS slot (fs:[0xE10 + 4*slot]) and returns the value in the same
 * register the original instruction wrote. */
typedef struct { u32 addr; u8 len; u8 kind; u8 off; } FSPATCH;   /* kind: 0 eax dword, 1 eax byte (movzx), 2 ecx, 3 edi */
static const FSPATCH g_fs[] = {
    {0x2f25de,6,0,0x20},{0x2f2776,6,0,0x20},{0x2f4cc4,6,0,0x20},{0x2f5cb5,6,0,0x20},{0x2f75a6,6,0,0x20},
    {0x2f761d,6,0,0x20},{0x376d3d,6,0,0x20},{0x34d751,6,0,0x20},{0x3309a4,6,0,0x20},
    {0x2f4d10,6,0,0x28},{0x2f4de9,6,0,0x28},{0x2f5023,6,0,0x28},{0x2f504b,6,0,0x28},{0x2f5cd1,6,0,0x28},
    {0x31193a,6,0,0x58},{0x31195f,6,0,0x58},
    {0x2f4ddd,8,1,0x24},{0x2f5017,8,1,0x24},{0x2f503f,8,1,0x24},{0x3118f7,8,1,0x24},{0x3119cd,8,1,0x24},{0x316ef3,8,1,0x24},
    {0x2f502e,7,2,0x04},{0x2f5051,7,2,0x04},{0x2f5cd7,7,3,0x04},
};
static u32 patch_fs(u8 *code) {
    u32 slot = 0xE10 + 4 * g_tls, n = 0;
    for (u32 i = 0; i < sizeof g_fs / sizeof *g_fs; i++) {
        const FSPATCH *p = &g_fs[i]; u8 *a = (u8 *)p->addr;
        /* check it is the expected fs: instruction */
        if (a[0] != 0x64 || *(u32 *)(a + p->len - 4) != p->off) { out("  fs patch skipped (bytes differ) at "); hex(p->addr); out("\r\n"); continue; }
        u8 *h = code + i * 16;
        if (p->kind == 2)      { h[0]=0x64; h[1]=0x8B; h[2]=0x0D; *(u32*)(h+3)=slot; h[7]=0x8B; h[8]=0x49; h[9]=p->off; h[10]=0xC3; }   /* mov ecx,fs:[slot]; mov ecx,[ecx+off] */
        else if (p->kind == 3) { h[0]=0x64; h[1]=0x8B; h[2]=0x3D; *(u32*)(h+3)=slot; h[7]=0x8B; h[8]=0x7F; h[9]=p->off; h[10]=0xC3; }   /* edi */
        else if (p->kind == 1) { h[0]=0x64; h[1]=0xA1; *(u32*)(h+2)=slot; h[6]=0x0F; h[7]=0xB6; h[8]=0x40; h[9]=p->off; h[10]=0xC3; }   /* movzx eax,byte [eax+off] */
        else                   { h[0]=0x64; h[1]=0xA1; *(u32*)(h+2)=slot; h[6]=0x8B; h[7]=0x40; h[8]=p->off; h[9]=0xC3; }             /* mov eax,[eax+off] */
        a[0] = 0xE8; *(u32 *)(a + 1) = (u32)h - (u32)(a + 5);
        for (u32 j = 5; j < p->len; j++) a[j] = 0x90;
        n++;
    }
    return n;
}

/* ---- crash report ---- */
static long __stdcall crash(u32 **ep) {
    u32 *rec = ep[0], *ctx = ep[1];
    out("\r\nCRASH: exception "); hex(rec[0]); out(" at "); hex(rec[3]);
    if (rec[0] == 0xC0000005) { out(rec[5] ? " (writing " : " (reading "); hex(rec[6]); out(")"); }
    out("\r\n  eax "); hex(ctx[0xB0/4]); out(" ebx "); hex(ctx[0xA4/4]); out(" ecx "); hex(ctx[0xAC/4]); out(" edx "); hex(ctx[0xA8/4]);
    out("\r\n  esi "); hex(ctx[0xA0/4]); out(" edi "); hex(ctx[0x9C/4]); out(" ebp "); hex(ctx[0xB4/4]); out(" esp "); hex(ctx[0xC4/4]);
    out("\r\n  stack:"); u32 *sp = (u32 *)ctx[0xC4/4]; for (int i = 0; i < 12; i++) { out(" "); hex(sp[i]); }
    out("\r\n"); log_flush(); STOP(3); return 0;
}

/* Instructions only the Xbox kernel may run (port I/O, cli/sti, cache and control registers).
   The game's driver code uses them; here they are skipped, and port reads return 0 (Milestone 74). */
u32 port_in(u32 port, u32 size);
void port_out(u32 port, u32 size, u32 val);
static long emulate_privileged(u32 *c) {
    u8 *ip = (u8 *)c[0xB8/4]; u32 *eax = &c[0xB0/4], dx = c[0xA8/4] & 0xFFFF, len = 0, sz = 4; int op16 = 0;
    if (ip[0] == 0x66) { op16 = 1; ip++; len = 1; }
    switch (ip[0]) {
    case 0xEE: port_out(dx, 1, *eax & 0xFF); len += 1; break;
    case 0xEF: sz = op16 ? 2 : 4; port_out(dx, sz, sz == 2 ? *eax & 0xFFFF : *eax); len += 1; break;
    case 0xEC: *eax = (*eax & ~0xFFu) | (port_in(dx, 1) & 0xFF); len += 1; break;
    case 0xED: sz = op16 ? 2 : 4; { u32 v = port_in(dx, sz); *eax = sz == 2 ? (*eax & ~0xFFFFu) | (v & 0xFFFF) : v; } len += 1; break;
    case 0xE6: port_out(ip[1], 1, *eax & 0xFF); len += 2; break;
    case 0xE7: sz = op16 ? 2 : 4; port_out(ip[1], sz, *eax); len += 2; break;
    case 0xE4: *eax = (*eax & ~0xFFu) | (port_in(ip[1], 1) & 0xFF); len += 2; break;
    case 0xE5: sz = op16 ? 2 : 4; *eax = port_in(ip[1], sz); len += 2; break;
    case 0xFA: case 0xFB: case 0xF4: len += 1; break;                 /* cli, sti, hlt */
    case 0x0F:
        if (ip[1] == 0x09 || ip[1] == 0x08 || ip[1] == 0x06) len += 2;   /* wbinvd, invd, clts */
        else if (ip[1] == 0x32) { *eax = 0; c[0xA8/4] = 0; len += 2; }   /* rdmsr */
        else if (ip[1] == 0x30) len += 2;                                /* wrmsr */
        else if (ip[1] == 0x20 || ip[1] == 0x22) {                       /* mov reg,crN / mov crN,reg */
            if (ip[1] == 0x20) { static const u8 ro[8] = {0xB0, 0xAC, 0xA8, 0xA4, 0xC4, 0xB4, 0xA0, 0x9C}; c[ro[ip[2] & 7] / 4] = 0; }
            len += 3;
        } else return 0;
        break;
    default: return 0;
    }
    c[0xB8/4] += len; return -1;   /* EXCEPTION_CONTINUE_EXECUTION */
}

/* Milestone 106 test aid: bptrace.txt + bp_list.bin (game addresses) = one-shot breakpoints; the first time each address runs,
   "[bp] address" goes to the log (which code a System Link reply reaches). */
static u32 *g_bp_a, g_bp_n; static u8 *g_bp_b;
static int bp_find(u32 a) { u32 lo = 0, hi = g_bp_n; while (lo < hi) { u32 m = (lo + hi) / 2; if (g_bp_a[m] < a) lo = m + 1; else hi = m; } return (lo < g_bp_n && g_bp_a[lo] == a) ? (int)lo : -1; }
static void bptrace_setup(void) {
    HANDLE f = CreateFileA("bptrace.txt", 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return; CloseHandle(f);
    f = CreateFileA("bp_list.bin", 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return;
    static u32 a[8192]; static u8 b[8192]; DWORD n = 0; ReadFile(f, a, sizeof a, &n, 0); CloseHandle(f);
    g_bp_a = a; g_bp_b = b; g_bp_n = n / 4;
    for (u32 i = 0; i < g_bp_n; i++) { u8 *p = (u8 *)a[i]; DWORD old; VirtualProtect(p, 1, 0x40, &old); b[i] = *p; *p = 0xCC; }
    out("  [bp] breakpoints set: "); dec(g_bp_n); out("\r\n");
}
/* sees every exception first: logs it (the game may handle it itself) and catches stack overflows,
   which never reach the normal crash report */
u32 g_exc_av, g_exc_step, g_exc_priv, g_exc_other;   /* exception counts, for the status line */
static long __stdcall first_chance(u32 **ep) {
    static u32 n; u32 *rec = ep[0];
    if (rec[0] == 0xC0000005) g_exc_av++; else if (rec[0] == 0x80000004) g_exc_step++; else if (rec[0] == 0xC0000096) g_exc_priv++; else g_exc_other++;
    if (rec[0] == 0x80000003 && g_bp_n) { int i = bp_find(rec[3]); if (i >= 0) { *(u8 *)rec[3] = g_bp_b[i]; ep[1][0xB8 / 4] = rec[3];
        out("  [bp] "); hex(rec[3]); out(" t"); dec(GetTickCount() % 1000000); out("\r\n"); return -1; } }
    if (rec[0] == 0xC00000FD) { out("\r\nSTACK OVERFLOW at "); hex(rec[3]); out("\r\n"); return crash(ep); }
    if (rec[0] == 0x406D1388 || rec[0] == 0x40010006) return 0;   /* debugger notifications */
    if (rec[0] == 0xC0000096) { long r = emulate_privileged(ep[1]); if (r) return r; }
    { int mmio_fault(u32, u32 *); int mmio_step(u32 *);
      if (rec[0] == 0xC0000005 && mmio_fault(rec[6], ep[1])) return -1;
      if (rec[0] == 0x80000004) { if (!mmio_step(ep[1])) { void mmio_protect_all(void); mmio_protect_all(); } return -1; } }   /* a stray step: ignore */
    if (rec[3] >= 0x70000000) return 0;   /* inside Windows' own DLLs: theirs to handle */
    if (rec[3] == 0 && n < 20) {   /* a jump to 0: list the return addresses into game code still on the stack */
        u32 *sp = (u32 *)ep[1][0xC4 / 4]; out("  [jump to 0] return addresses on the stack:");
        for (u32 i = 0; i < 256; i++) { u32 a = sp[i]; if (a > 0x12005 && a < 0x320000) { u8 *b = (u8 *)a; if (b[-5] == 0xE8 || b[-2] == 0xFF || b[-3] == 0xFF || b[-6] == 0xFF) { out(" ["); dec(i); out("]"); hex(a); } } }
        out("\r\n"); { void dsound_trace_dump(void); if (n < 2) dsound_trace_dump(); }
    }
    if (n++ < 20) { out("  [exception "); hex(rec[0]); out(" at "); hex(rec[3]); out(" thread "); dec(GetCurrentThreadId()); if (rec[0] == 0xC0000005) { out(rec[5] ? " writing " : " reading "); hex(rec[6]); } out("]\r\n");
        if (n <= 3) { u32 *ctx = ep[1]; out("    eax "); hex(ctx[0xB0/4]); out(" ebx "); hex(ctx[0xA4/4]); out(" ecx "); hex(ctx[0xAC/4]); out(" edx "); hex(ctx[0xA8/4]);
            out(" esi "); hex(ctx[0xA0/4]); out(" edi "); hex(ctx[0x9C/4]); out(" ebp "); hex(ctx[0xB4/4]); out(" esp "); hex(ctx[0xC4/4]);
            out("\r\n    stack:"); u32 *sp = (u32 *)ctx[0xC4/4]; for (int i = 0; i < 24; i++) { out(" "); hex(sp[i]); }
            u32 *bp = (u32 *)ctx[0xB4/4]; out("\r\n    callers:"); for (int i = 0; i < 10 && (u32)bp > 0x10000 && (u32)bp < 0x7FFF0000; i++) { out(" "); hex(bp[1]); bp = (u32 *)bp[0]; }
            out("\r\n"); } }
    return 0;
}
static void *g_self;
char g_gamedir[600], g_homedir[600];
u8 g_pehdr[0x1000], g_xbehdr[0x1000];
static volatile u32 g_swaplock;
void swap_header(int pe) {   /* pe=1: show Windows the PE header; pe=0: give the game its XBE header back */
    if (pe) { while (__sync_val_compare_and_swap(&g_swaplock, 0, 1)) Sleep(0); memcpy_(g_xbehdr, (void *)0x10000, 0x1000); memcpy_((void *)0x10000, g_pehdr, 0x1000); }
    else { memcpy_((void *)0x10000, g_xbehdr, 0x1000); g_swaplock = 0; }
}
BOOL __stdcall DllMain(void *h, DWORD r, void *p) { if (r == 1) g_self = h; return 1; }

__declspec(dllexport) void __cdecl BfMain(const char *unused) {
    char path[600], logp[600]; DWORD n; u32 i, len;
    len = GetModuleFileNameA(0, path, 500);
    for (i = len; i > 0 && path[i-1] != '\\'; i--) ;
    memcpy_(logp, path, i); memcpy_(logp + i, "bfloader.log", 13);
    g_log = CreateFileA(logp, GENERIC_WRITE, 1, 0, 2, 0x80, 0);
    out("Brute Force PC loader v44 (Milestone 77)\r\n");
    /* Opt out of Windows 11 power throttling (EcoQoS), so the loader keeps its speed and timer resolution when its
       window is not in front (a precaution: the long freezes of Milestone 77 had another cause, see gpu.c's viewer). */
    { IMP BOOL WINAPI SetProcessInformation(HANDLE, u32, void *, u32); u32 pt[3] = {1, 0x1 | 0x4, 0};   /* EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION, both off */
      /* Windows 10 does not know IGNORE_TIMER_RESOLUTION (Windows 11 only) and refused the whole request, so nothing was turned
         off there: a friend's PC logged "could not be turned off" and had frame drops and flicker (the loader is a background
         process to Windows, the game window being the viewer's). Then: execution speed alone. */
      if (SetProcessInformation((HANDLE)-1, 4 /* ProcessPowerThrottling */, pt, sizeof pt)) out("Power throttling: off\r\n");
      else { u32 p2[3] = {1, 0x1, 0}; out(SetProcessInformation((HANDLE)-1, 4, p2, sizeof p2) ? "Power throttling: off (execution speed; this Windows has no timer setting)\r\n" : "Power throttling: could not be turned off\r\n"); } }
    /* Milestone 86: in the background (the game window is in front) Windows froze most of this process's threads for
       ~1.5 s every few seconds, even a time-critical spinning one: 1.5 s gaps in the sound. High priority class. */
    { IMP BOOL WINAPI SetPriorityClass(HANDLE, DWORD); if (GetFileAttributesA("normalpriority.txt") == 0xFFFFFFFF) out(SetPriorityClass((HANDLE)-1, 0x80 /* HIGH_PRIORITY_CLASS */) ? "Priority class: high\r\n" : "Priority class: could not be raised\r\n"); }
    SetUnhandledExceptionFilter(crash); AddVectoredExceptionHandler(1, first_chance);   /* first, so no fault ever reaches Windows Error Reporting */


    const u32 BASE = 0x10000, END = 0x4C5000;
    DWORD oldp;
    if (!VirtualProtect((void *)BASE, END - BASE, PAGE_EXECUTE_READWRITE, &oldp)) { out("Cannot take over the Xbox address range, error "); dec(GetLastError()); out("\r\n"); STOP(1); }

    char xbe[600]; const char *cl = GetCommandLineA(); u32 q = 0;
    if (*cl == '"') { cl++; while (*cl && *cl != '"') cl++; if (*cl) cl++; } else { while (*cl && *cl != ' ') cl++; }
    while (*cl == ' ') cl++;
    if (*cl) { if (*cl == '"') cl++; while (*cl && *cl != '"' && q < 590) xbe[q++] = *cl++; xbe[q] = 0; }
    else { memcpy_(xbe, path, i); memcpy_(xbe + i, "default.xbe", 12); }
    memcpy_(g_homedir, path, i); g_homedir[i] = 0;
    { u32 e = 0; while (xbe[e]) e++; while (e > 0 && xbe[e-1] != '\\') e--; memcpy_(g_gamedir, xbe, e); g_gamedir[e] = 0; }
    out("Game folder: "); out(g_gamedir); out("\r\n");
    HANDLE f = CreateFileA(xbe, GENERIC_READ, 1, 0, 3, 0x80, 0);
    if (f == INVALID_HANDLE) { out("Cannot open "); out(xbe); out("\r\n"); STOP(1); }
    u32 fsz = GetFileSize(f, 0); u8 *x = HeapAlloc(GetProcessHeap(), 0, fsz);
    ReadFile(f, x, fsz, &n, 0); CloseHandle(f);
    if (rd32(x) != 0x48454258) { out("Not an XBE\r\n"); STOP(1); }

    u32 base = rd32(x + 0x104), hsz = rd32(x + 0x108), nsec = rd32(x + 0x11C), sech = rd32(x + 0x120);
    u32 entry = rd32(x + 0x128) ^ 0xA8FC57AB, thunk = rd32(x + 0x158) ^ 0x5B6D40B6, tlsdir = rd32(x + 0x12C);
    /* Windows reads the main program's PE header when it creates a thread; the XBE header covers it,
       so keep a copy and put it back for the moment a thread is created (see swap_header). */
    memcpy_(g_pehdr, (void *)BASE, 0x1000);
    memset_((void *)BASE, 0, END - BASE);
    memcpy_((void *)base, x, hsz);
    for (u32 s = 0; s < nsec; s++) {
        const u8 *h = x + (sech - base) + s * 0x38;
        u32 va = rd32(h + 4), vs = rd32(h + 8), ra = rd32(h + 12), rs = rd32(h + 16);
        memcpy_((void *)va, x + ra, rs < vs ? rs : vs);
    }
    out("Image loaded: "); dec(nsec); out(" sections, entry "); hex(entry); out("\r\n");

    { void gpu_start_window(void); gpu_start_window(); }
    { u32 gpu_init(void); out(gpu_init() ? "Contiguous memory at 0x80000000; hardware registers 0xFD000000-0xFEFFFFFF are plain memory (no real GPU yet)\r\n" : "Could not map the contiguous memory or hardware register range\r\n"); } g_tls = TlsAlloc();
    u8 *code = VirtualAlloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    u32 ni, nd, nk = kernel_patch_imports((u32 *)thunk, code, &ni, &nd);
    out("Kernel imports: "); dec(nk); out(" ("); dec(ni); out(" implemented, "); dec(nd); out(" data, "); dec(nk - ni - nd); out(" stubs)\r\n");
    out("fs: reads patched: "); dec(patch_fs(code + 0x8000)); out(" of 25\r\n");
    { void input_patch(void); input_patch(); }
    { void dsound_patch(void); dsound_patch(); }
    bptrace_setup();
    { void render_bench(void); render_bench(); }   /* with bench.txt: renderer speed before the game runs */

    /* TLS: index variable = 0; the main thread gets a block of the template's size */
    u32 *td = (u32 *)tlsdir; u32 tlssize = (td[1] - td[0]) + td[4];
    *(u32 *)td[2] = 0;
    out("TLS template "); hex(td[0]); out("-"); hex(td[1]); out(" + zero fill "); hex(td[4]); out("\r\n");
    new_kpcr(((tlssize + 15) & ~15) + 4);   /* the same size XAPI asks for when it creates threads */

    SetUnhandledExceptionFilter(crash); AddVectoredExceptionHandler(1, first_chance);
    { void kernel_add_thread(HANDLE); IMP HANDLE WINAPI OpenThread(DWORD, BOOL, DWORD); kernel_add_thread(OpenThread(0x1F03FF, 0, GetCurrentThreadId())); }
    kernel_start_ticker();
    out("Starting the game at "); hex(entry); out(" ...\r\n"); log_flush();
    ((void (__cdecl *)(void))entry)();
    out("The game's entry point returned.\r\n"); log_flush();
    for (;;) Sleep(1000);   /* the game's own threads keep running */
}

/* Xbox kernel (xboxkrnl.exe) replacements, first batch (Milestone 73).
 * Each function has the Xbox calling convention and argument count, so the game's own code can call
 * it through the kernel import table. Unimplemented imports go to a stub that logs and stops. */
#include "util.h"
#include "kexports.h"

#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL 0xC0000001
#define STATUS_TIMEOUT 0x102
#define NTAPI __stdcall
IMP DWORD WINAPI GetFileAttributesA(const char *);
IMP HANDLE WINAPI OpenThread(DWORD, BOOL, DWORD);
extern DWORD g_tls;                 /* Windows TLS slot holding this thread's fake KPCR */
void *new_kpcr(u32 tlsSize);        /* in bfcore.c */

/* ---- per-call log ---- */
static u32 g_calls;
static u32 g_ring[16];
static u32 g_kcount[400];   /* kernel calls by ordinal since the last status line (Milestone 78) */
void __stdcall log_call(u32 ord) {
    static u32 nr;
    if (ord != 119) g_ring[nr++ & 15] = ord;   /* KeInsertQueueDpc (vblank) would flood the list */
    g_calls++;
    if (g_calls <= 300) { out("  k "); out(g_kname[ord]); out("\r\n"); }
    if (g_calls == 300) { out("  (further kernel calls are counted, not listed)\r\n"); }
}
void __stdcall unimplemented(u32 ord) {
    out("UNIMPLEMENTED kernel call: "); dec(ord); out(" "); out(g_kname[ord]);
    out("  (after "); dec(g_calls); out(" kernel calls)\r\n"); log_flush();
    STOP(2);
}

/* ---- data exports ---- */
static u32 g_tick;                                  /* KeTickCount (ms) */
static u16 g_krnlver[4] = {1, 0, 5838, 1};          /* XboxKrnlVersion */
static u32 g_hwinfo[2] = {0x00000020, 0x0000D4D2};  /* XboxHardwareInfo: flags, GPU/MCP revisions */
static u32 g_launch;                                /* LaunchDataPage (pointer, NULL = cold boot) */
static u8  g_zero[0x400];                           /* generic zeroed data (keys, object types ...) */
static u32 g_one = 1, g_three = 3;
static char g_model[] = "PC LOADER DISK", g_serial[] = "0000000001";
static struct { u16 len, max; char *buf; } g_models = {sizeof g_model - 1, sizeof g_model, g_model}, g_serials = {sizeof g_serial - 1, sizeof g_serial, g_serial};
static void *g_modelp = &g_models, *g_serialp = &g_serials;
static char g_image[] = "\\Device\\CdRom0\\default.xbe";
static struct { u16 len, max; char *buf; } g_imagename = {sizeof g_image - 1, sizeof g_image, g_image};

void kernel2_tick(void); void kernel2_close(HANDLE h);
/* game threads, so a watchdog can say where they are when the game goes quiet */
IMP DWORD WINAPI SuspendThread(HANDLE);
IMP BOOL WINAPI GetThreadContext(HANDLE, void *);
static HANDLE g_thr[32]; static u32 g_nthr;
void kernel_add_thread(HANDLE t) { if (t && g_nthr < 32) g_thr[g_nthr++] = t; }
static void watchdog(void) {
    static u32 last = 0xFFFFFFFF, reports;
    int stuck = g_calls == last; last = g_calls;
    if (reports >= 40) return;
    reports++;
    out(stuck ? "  [watchdog] no kernel calls for 3 s (total " : "  [status] kernel calls so far "); dec(g_calls); out(" at tick "); dec(GetTickCount()); out(":\r\n");
    out("    last kernel calls (oldest first):"); for (u32 j = 0; j < 16; j++) if (g_ring[j]) { out(" "); out(g_kname[g_ring[j]]); } out("\r\n");
    out("    busiest kernel calls in the last 3 s:"); for (u32 q = 0; q < 8; q++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < 400; i++) if (g_kcount[i] > bv) { bv = g_kcount[i]; bi = i; } if (!bv) break; out(" "); out(g_kname[bi]); out(" "); dec(bv); g_kcount[bi] = 0; } memset_(g_kcount, 0, sizeof g_kcount); out("\r\n"); { void kewait_report(void); kewait_report(); void dsound_report(void); dsound_report(); void input_report(void); input_report(); }
    { volatile u32 *nv = (u32 *)0xFD000000; out("    gpu: user put "); hex(nv[0x800040/4]); out(" get "); hex(nv[0x800044/4]); out("\r\n");   /* (CACHE1 is a trapped page now: not read here) */
      u32 *d = *(u32 **)0x37DD88; out("    device:"); for (u32 j = 0; j < 4; j++) { out(" "); hex(d[j]); }
      u32 *g = (u32 *)d[0x30/4]; out(" [+2C] "); hex(d[0x2C/4]); if ((u32)g > 0x10000) { out(" [+30]->"); hex(*g); } out("\r\n"); }
    if (!stuck && reports % 4 != 0) return;   /* reading other threads' registers is only worth its risk when the game has stopped (and once, at 12 s) */
    for (u32 i = 0; i < g_nthr; i++) {
        u32 c[0xB4]; memset_(c, 0, sizeof c); c[0] = 0x10003;   /* CONTEXT_CONTROL | CONTEXT_INTEGER */
        SuspendThread(g_thr[i]); BOOL ok = GetThreadContext(g_thr[i], c); ResumeThread(g_thr[i]);
        out("    thread "); dec(i); if (ok) { out(" eip "); hex(c[0xB8 / 4]); out(" esp "); hex(c[0xC4 / 4]);
            u32 *sp = (u32 *)c[0xC4 / 4]; u32 rend = 0;
            {   /* only read the stack if it is readable (Milestone 88: a thread that had just exited crashed the loader here) */
                IMP u32 WINAPI VirtualQuery(const void *, void *, u32); u32 mb[7];
                if (!VirtualQuery(sp, mb, sizeof mb) || mb[4] != 0x1000 || (mb[5] & 0x101) || !(mb[5] & 0xEE) || (u32)sp + 0x40 > mb[0] + mb[3]) { out(" (stack not readable)\r\n"); continue; } rend = mb[0] + mb[3]; }
            out(" stack"); for (u32 j = 0; j < 8; j++) { out(" "); hex(sp[j]); }
            /* frame-pointer chain, only while it stays on this thread's stack (game code often has no frame pointer) */
            u32 *bp = (u32 *)c[0xB4 / 4]; out(" callers"); for (u32 j = 0; j < 8 && (u32)bp >= (u32)sp && (u32)bp + 8 <= rend && !((u32)bp & 3); j++) { u32 *nb = (u32 *)bp[0]; out(" "); hex(bp[1]); if (nb <= bp) break; bp = nb; }
            out(" ebp "); hex(c[0xB4 / 4]); out(" eax "); hex(c[0xB0 / 4]); } else out(" (no context)");
        out("\r\n");
    }
}
/* vertical blank, 60 times a second; also called from inside long push-buffer passes (Milestone 81) so it is not
   delayed while the ticker is busy drawing */
static u32 g_t0, g_vb;
/* Milestone 86: the vblank is timed with the performance counter on a steady 16,667 us schedule. GetTickCount only
   moves every 15.6 ms, so "16 ms passed" came after 15.6 or 31.2 ms: uneven vblanks made movies and menus stutter. */
static volatile u32 g_vb_next;   /* microseconds (wraps after 71 minutes; compared as a difference) */
static u32 now_us(void) { static s64 f; s64 c; if (!f) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (u32)(c / f * 1000000 + (c % f) * 1000000 / f); }
/* when the viewer counts the monitor's refreshes (Milestone 86), the vblank follows them; the timer is the fallback */
static volatile u32 g_vs_seen; static u32 g_vs_t;
static int vsync_pending(void) { u32 gpu_vsync_count(void); return gpu_vsync_count() != g_vs_seen; }
static int vblank_due(void) {   /* the ticker and the push-buffer reader both call this: only the one that moves the deadline runs the vblank */
    {   u32 gpu_vsync_count(void); u32 c = gpu_vsync_count(), s = g_vs_seen, t = now_us();
        if (c != s) { if (!__sync_bool_compare_and_swap(&g_vs_seen, s, c)) return 0; g_vs_t = t; g_vb_next = t + 16667; return 1; }
        if (t - g_vs_t < 50000) return 0; }   /* refreshes are arriving: wait for the next one */
    u32 n = g_vb_next, t = now_us();
    if ((s32)(t - n) < 0) { if ((s32)(n - t) <= 100000) return 0;   /* not yet */
        return __sync_bool_compare_and_swap(&g_vb_next, n, t + 16667); }   /* deadline nonsense (start-up: 0): restart */
    u32 nx = (s32)(t - n) > 50000 ? t + 16667 : n + 16667;   /* far behind (a stall): start the schedule again */
    return __sync_bool_compare_and_swap(&g_vb_next, n, nx);
}
void vblank_maybe(void) { void vblank_tick(void); if (vblank_due()) { u32 t = GetTickCount() - g_t0; g_vb = t; g_tick = t; vblank_tick(); kernel2_tick(); } }
static DWORD __stdcall ticker(void *p) {
    u32 t0 = GetTickCount(); new_kpcr(0x20); g_t0 = t0;
    u32 vb = 0; void vblank_tick(void); void apu_tick(void);
    void dsound_tick(void);
    for (;;) { { extern u32 *gpu_pad(void); u32 *pd = gpu_pad(); if (pd) { pd[21]++; pd[22] = g_calls; } } g_tick = GetTickCount() - t0; apu_tick(); kernel2_tick(); dsound_tick(); { void nic_service(void); nic_service(); } if (vblank_due()) { g_vb = g_tick; vblank_tick(); } (void)vb; { void gpu_process(u32); void gpu_tick(void); gpu_process(g_tick); gpu_tick(); }
        {   static u64 lt; u64 c = __builtin_ia32_rdtsc(); if (lt && c - lt > 380000000ull) { static u32 nl; if (nl++ < 20) { out("  [ticker] loop gap "); dec((u32)((c - lt) / 3800000)); out(" ms, at tick "); dec(GetTickCount()); out("\r\n"); } } Sleep(1); lt = __builtin_ia32_rdtsc(); } }   /* Milestone 86: a busy-wait here (to react to GPU work at once) ran the game loop ~410 times a second and starved the other threads */
}
/* sampling profiler (Milestone 77): the ticker thread's eip 1000 times a second, in 32-byte buckets of bfcore.dll;
   every 30 s the busiest buckets go to the log as RVAs (look them up in bfcore.map, base 0x10000000) */
IMP void *WINAPI GetModuleHandleA(const char *);
static u32 g_prof[0x48000 / 32], g_prof_other, g_prof_n, g_prof_game, g_tprof[(0x4D4000 - 0x10000) / 64];
static u32 g_gprof[(0x4D4000 - 0x10000) / 64], g_gprof_core[0x48000 / 32], g_gprof_win, g_gwait[(0x4D4000 - 0x10000) / 16], g_lwait[0x48000 / 16];   /* g_lwait: the loader code the game thread was in when inside Windows */   /* g_gwait: the game code that called into Windows / the loader (first game return address on the stack) */
static void *g_dly_who[8]; static u64 g_dly_ms[8]; static u32 g_dly_n[8];
static DWORD __stdcall profiler(void *p) {
    u32 base = (u32)GetModuleHandleA("bfcore.dll"), t = 0;
    for (;;) {
        Sleep(1);
        if (g_nthr < 2) continue;
        u32 c[0xB4]; memset_(c, 0, sizeof c); c[0] = 0x10001;
        SuspendThread(g_thr[1]); BOOL ok = GetThreadContext(g_thr[1], c); ResumeThread(g_thr[1]);
        if (!ok) continue;
        u32 e = c[0xB8 / 4] - base; if (e < 0x48000) g_prof[e / 32]++; else { g_prof_other++; u32 ip = c[0xB8 / 4]; if (ip >= 0x10000 && ip < 0x4D4000) { g_prof_game++; g_tprof[(ip - 0x10000) / 64]++; } }
        /* the game's main thread too (Milestone 78): game code in 64-byte buckets, loader code, Windows */
        if (g_nthr >= 3) { u32 c2[0xB4]; memset_(c2, 0, sizeof c2); c2[0] = 0x10001;
            SuspendThread(g_thr[2]); BOOL ok2 = GetThreadContext(g_thr[2], c2); ResumeThread(g_thr[2]);
            if (ok2) { u32 ip = c2[0xB8 / 4]; if (ip >= 0x10000 && ip < 0x4D4000) g_gprof[(ip - 0x10000) / 64]++; else { if (ip - base < 0x48000) g_gprof_core[(ip - base) / 32]++; else g_gprof_win++;
                u32 *sp = (u32 *)c2[0xC4 / 4]; IMP u32 WINAPI VirtualQuery(const void *, void *, u32); u32 mb[7];
                if (VirtualQuery(sp, mb, sizeof mb) && mb[4] == 0x1000 && !(mb[5] & 0x101)) { u32 end = mb[0] + mb[3]; int lw = 0; for (u32 j = 0; j < 256 && (u32)(sp + j) + 4 <= end; j++) { { u32 v = sp[j]; if (v >= 0x11000 && v < 0x3A0000) { const u8 *q = (const u8 *)v;   /* a return address: a call just before it */
                        if (q[-5] == 0xE8 || (q[-2] == 0xFF && ((q[-1] >> 3) & 7) == 2) || (q[-3] == 0xFF && ((q[-2] >> 3) & 7) == 2) || (q[-6] == 0xFF && ((q[-5] >> 3) & 7) == 2)) { g_gwait[(v - 0x10000) / 16]++; break; } }
                      if (v - base >= 0x1000 && v - base < 0x48000) { const u8 *q = (const u8 *)v; if (q[-5] == 0xE8 || (q[-2] == 0xFF && ((q[-1] >> 3) & 7) == 2) || (q[-6] == 0xFF && ((q[-5] >> 3) & 7) == 2)) { if (!lw) { g_lwait[(v - base) / 16]++; lw = 1; } } } } } } } } }
        if (++g_prof_n % 8000 == 0) {
            out("  [profile] ticker samples "); dec(g_prof_n); out(", outside bfcore "); dec(g_prof_other); out(", top (rva:count):");
            for (u32 k = 0; k < 15; k++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < 0x48000 / 32; i++) if (g_prof[i] > bv) { bv = g_prof[i]; bi = i; } if (!bv) break; out(" "); hex(0x10000000 + bi * 32); out(":"); dec(bv); g_prof[bi] = 0; }
            out(", of which game code "); dec(g_prof_game); out(":");
            for (u32 kq = 0; kq < 10; kq++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < (0x4D4000 - 0x10000) / 64; i++) if (g_tprof[i] > bv) { bv = g_tprof[i]; bi = i; } if (!bv) break; out(" "); hex(0x10000 + bi * 64); out(":"); dec(bv); g_tprof[bi] = 0; }
            out("\r\n"); memset_(g_prof, 0, sizeof g_prof); g_prof_other = 0; g_prof_game = 0; memset_(g_tprof, 0, sizeof g_tprof);
            out("  [profile] game main thread: in Windows "); dec(g_gprof_win); out(", top game (addr:count):");
            for (u32 kq = 0; kq < 20; kq++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < (0x4D4000 - 0x10000) / 64; i++) if (g_gprof[i] > bv) { bv = g_gprof[i]; bi = i; } if (!bv) break; out(" "); hex(0x10000 + bi * 64); out(":"); dec(bv); g_gprof[bi] = 0; }
            out(", waiting called from (game addr:count):"); for (u32 kq = 0; kq < 12; kq++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < (0x4D4000 - 0x10000) / 16; i++) if (g_gwait[i] > bv) { bv = g_gwait[i]; bi = i; } if (!bv) break; out(" "); hex(0x10000 + bi * 16); out(":"); dec(bv); g_gwait[bi] = 0; } memset_(g_gwait, 0, sizeof g_gwait);
            { void kewait_report(void); out("\r\n"); kewait_report(); out("    sleeps (caller: count, ms asked):"); for (u32 s2 = 0; s2 < 8 && g_dly_who[s2]; s2++) { out(" "); hex((u32)g_dly_who[s2]); out(": "); dec(g_dly_n[s2]); out(", "); dec((u32)g_dly_ms[s2]); g_dly_n[s2] = 0; g_dly_ms[s2] = 0; } out("\r\n"); }
            out("    game thread in Windows, called from loader (rva:count):"); for (u32 kq = 0; kq < 10; kq++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < 0x48000 / 16; i++) if (g_lwait[i] > bv) { bv = g_lwait[i]; bi = i; } if (!bv) break; out(" "); hex(0x10000000 + bi * 16); out(":"); dec(bv); g_lwait[bi] = 0; } memset_(g_lwait, 0, sizeof g_lwait); out("\r\n");
            out("  top loader (rva:count):");
            for (u32 kq = 0; kq < 8; kq++) { u32 bi = 0, bv = 0; for (u32 i = 0; i < 0x48000 / 32; i++) if (g_gprof_core[i] > bv) { bv = g_gprof_core[i]; bi = i; } if (!bv) break; out(" "); hex(0x10000000 + bi * 32); out(":"); dec(bv); g_gprof_core[bi] = 0; }
            out("\r\n"); memset_(g_gprof, 0, sizeof g_gprof); memset_(g_gprof_core, 0, sizeof g_gprof_core); g_gprof_win = 0;
        }
    }
}
static DWORD __stdcall watcher(void *p) {
    for (u32 t = 0;; t += 3) {
        Sleep(3000); watchdog();
        { extern u32 g_win_loops, g_exc_av, g_exc_step, g_exc_priv, g_exc_other; out("  [window loop "); dec(g_win_loops); out("] exceptions: access "); dec(g_exc_av); out(" step "); dec(g_exc_step); out(" privileged "); dec(g_exc_priv); out(" other "); dec(g_exc_other); out("\r\n"); }
        if (GetFileAttributesA("stop_loader.txt") != 0xFFFFFFFF) { out("stop_loader.txt found - stopping.\r\n"); STOP(0); }
        static int lim = -1; if (lim < 0) lim = GetFileAttributesA("timelimit.txt") != 0xFFFFFFFF;   /* the safety stop is now opt-in (timelimit.txt) */
        if (lim && t >= 1800) { out("Stopping after 30 minutes (safety limit while testing).\r\n"); log_flush(); STOP(0); }
    }
}   /* separate, so it still reports if the ticker hangs */
void swap_header(int pe);
IMP s32 NTAPI NtSetTimerResolution(u32 want, u8 set, u32 *cur);
void kernel_start_ticker(void) {
    { u32 cur = 0; NtSetTimerResolution(10000, 1, &cur); out("Timer resolution: "); dec(cur / 10); out(" us\r\n"); }   /* 1 ms, so Sleep(1) is 1 ms */
    DWORD id; swap_header(1); HANDLE t = CreateThread(0, 0, ticker, 0, 0, &id); CreateThread(0, 0, watcher, 0, 0, 0); if (GetFileAttributesA("profile.txt") != 0xFFFFFFFF) CreateThread(0, 0, profiler, 0, 0, 0);   /* opt-in (Milestone 86): it suspends threads 2000 times a second */ swap_header(0);
    if (!t) out("ticker thread failed\r\n"); else kernel_add_thread(OpenThread(0x1F03FF, 0, id));
}

/* ---- functions ---- */
static void *NTAPI AvGetSavedDataAddress(void) { return 0; }
static void NTAPI AvSetSavedDataAddress(void *a) { }
static void __cdecl DbgPrint(const char *fmt, ...) { out("  DbgPrint: "); out(fmt); out("\r\n"); }
static void NTAPI DbgBreakPoint(void) { out("  DbgBreakPoint\r\n"); }

/* memory */
static s32 NTAPI NtAllocateVirtualMemory(void **base, u32 zb, u32 *size, u32 type, u32 prot) {
    void *p = VirtualAlloc(*base, *size, type & 0x3000, prot ? prot : PAGE_READWRITE);
    if (!p && *base) p = VirtualAlloc(0, *size, type & 0x3000, prot ? prot : PAGE_READWRITE);
    if (!p) { static u32 nl; if (nl++ < 10) { out("  NtAllocateVirtualMemory: no room for "); hex(*size); out("\r\n"); } return 0xC0000017; } /* STATUS_NO_MEMORY */
    *base = p; *size = (*size + 0xFFF) & ~0xFFF;
    { static u32 nl2; if (*size >= 0x10000 && nl2 < 200) { nl2++; out("  [valloc] "); hex((u32)p); out(" size "); hex(*size); out("\r\n"); } }   /* large game allocations outside the pool (the GPU cannot see them) */
    return STATUS_SUCCESS;
}
static s32 NTAPI NtFreeVirtualMemory(void **base, u32 *size, u32 type) {
    return VirtualFree(*base, type & MEM_RELEASE ? 0 : *size, type) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
static s32 NTAPI NtQueryVirtualMemory(void *base, void *mbi) { return VirtualQuery(base, mbi, 28) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL; }
static s32 NTAPI NtProtectVirtualMemory(void **base, u32 *size, u32 prot, u32 *old) { DWORD o; BOOL r = VirtualProtect(*base, *size, prot & ~0x600u, &o); if (old) *old = o; return r ? 0 : STATUS_UNSUCCESSFUL; }
/* Contiguous memory: like the Xbox, a 64 MB pool at 0x80000000 where address = 0x80000000 | physical address.
   The top 1 MB is left for the GPU's instance memory. (Milestone 74) */
#define POOL 0x80000000u
#define POOL_END 0x83F00000u
typedef struct { u32 a, n; } BLK;
static BLK g_blk[4096]; static u32 g_nblk; static volatile u32 g_plock;
/* Unconstrained requests are placed from the top down, like the Xbox kernel does, so memory below
   16 MB etc. stays free for requests that need a low physical address. */
static void *pool_alloc(u32 n, u32 align, u32 lo, u32 hi) {
    if (!align) align = 0x1000; if (align < 0x1000) align = 0x1000; n = (n + 0xFFF) & ~0xFFF; if (!n) n = 0x1000;
    int constrained = hi && hi < 0x03EFFFFF;
    u32 start = POOL + (lo & 0x03FFFFFF), end = constrained ? POOL + hi + 1 : POOL_END, r = 0, i;
    while (__sync_val_compare_and_swap(&g_plock, 0, 1)) ;
    if (!constrained) {   /* top-down: highest gap that fits */
        for (i = g_nblk + 1; i-- > 0;) {
            u32 gap_lo = i ? g_blk[i - 1].a + g_blk[i - 1].n : start, gap_hi = i < g_nblk ? g_blk[i].a : end;
            if (gap_hi > end) gap_hi = end; if (gap_lo < start) gap_lo = start;
            if (gap_hi >= n && gap_hi - n >= gap_lo) { u32 c = (gap_hi - n) & ~(align - 1); if (c >= gap_lo) { r = c; break; } }
        }
    } else {
        u32 cand = (start + align - 1) & ~(align - 1);
        for (i = 0; i <= g_nblk; i++) {
            u32 lim = i < g_nblk ? g_blk[i].a : end;
            if (cand + n <= lim) break;
            if (i < g_nblk && g_blk[i].a + g_blk[i].n > cand) cand = (g_blk[i].a + g_blk[i].n + align - 1) & ~(align - 1);
        }
        if (cand + n <= end) r = cand;
    }
    if (r && g_nblk < 4096) {
        for (i = 0; i < g_nblk && g_blk[i].a < r; i++) ;
        for (u32 j = g_nblk; j > i; j--) g_blk[j] = g_blk[j - 1];
        g_blk[i].a = r; g_blk[i].n = n; g_nblk++;
    } else r = 0;
    g_plock = 0;
    if (r) memset_((void *)r, 0, n);
    else { static u32 nf; if (nf++ < 8) { u32 used = 0; for (u32 j = 0; j < g_nblk; j++) used += g_blk[j].n;
        out("  contiguous memory: no room for "); hex(n); out(" (range "); hex(lo); out("-"); hex(hi); out(", align "); hex(align); out("); in use "); hex(used); out(" in "); dec(g_nblk); out(" blocks\r\n"); } }
    return (void *)r;
}
static u32 pool_size(u32 a) { for (u32 i = 0; i < g_nblk; i++) if (g_blk[i].a == a) return g_blk[i].n; return 0; }
static void *NTAPI MmAllocateContiguousMemory(u32 n) { return pool_alloc(n, 0x1000, 0, 0); }
static void *NTAPI MmAllocateContiguousMemoryEx(u32 n, u32 lo, u32 hi, u32 align, u32 prot) { return pool_alloc(n, align, lo, hi); }
static void NTAPI MmFreeContiguousMemory(void *p) {
    while (__sync_val_compare_and_swap(&g_plock, 0, 1)) ;
    for (u32 i = 0; i < g_nblk; i++) if (g_blk[i].a == (u32)p) { for (u32 j = i; j + 1 < g_nblk; j++) g_blk[j] = g_blk[j + 1]; g_nblk--; break; }
    g_plock = 0;
}
static void NTAPI MmPersistContiguousMemory(void *p, u32 n, u8 persist) { }
/* Memory outside the pool has no physical address of its own: its own address is returned. Devices that use these numbers
   (the network chip, net.c) must then read that memory directly, not the pool at that offset: the pages handed out this way
   are remembered (Milestone 104: the game's network buffers live outside the pool; the chip copied zeros from the pool at
   the same offset, so every packet sent and received was empty). */
/* Not just the page asked about: the whole committed region around it (VirtualQuery). The game asks once for a block of
   receive buffers and adds offsets itself; only the first page was known, so the large session replies (714 bytes) that
   landed in later pages went to the pool again and the joining console never saw the host's match. */
#define NPRANGE 256
static u32 g_prlo[NPRANGE], g_prhi[NPRANGE], g_nprange; static volatile u32 g_pplock;
static u32 NTAPI MmGetPhysicalAddress(void *p) {
    u32 a = (u32)p; if (a >= POOL && a < 0x84000000) return a - POOL;
    u32 lo = a & ~0xFFFu, hi = lo + 0x1000; u32 mbi[7];   /* BaseAddress, AllocationBase, AllocationProtect, RegionSize, State, Protect, Type */
    if (VirtualQuery(p, mbi, sizeof mbi) && mbi[4] == 0x1000 /* MEM_COMMIT */) { hi = mbi[0] + mbi[3]; if (hi > lo + 0x20000) hi = lo + 0x20000; }   /* from the page asked about to the region's end, at most 128 KB (a big region must not swallow pool addresses such as the NIC rings') */
    while (__sync_lock_test_and_set(&g_pplock, 1)) ;
    u32 i = 0; for (; i < g_nprange; i++) if (a >= g_prlo[i] && a < g_prhi[i]) break;
    if (i == g_nprange && g_nprange < NPRANGE) { g_prlo[g_nprange] = lo; g_prhi[g_nprange] = hi; g_nprange++;
        static u32 nl; if (nl++ < 16) { out("  [phys] direct range "); hex(lo); out(" - "); hex(hi); out("\r\n"); } }
    __sync_lock_release(&g_pplock); return a; }
int phys_is_direct(u32 a) {   /* was this "physical" address handed out for memory outside the pool? (then it is the memory's own address) */
    for (u32 i = 0; i < g_nprange; i++) if (a >= g_prlo[i] && a < g_prhi[i]) return 1; return 0; }
static u32 NTAPI MmQueryAllocationSize(void *p) { u32 s = pool_size((u32)p); if (s) return s; u32 m[7]; return VirtualQuery(p, m, 28) ? m[3] : 0; }
static void *NTAPI MmAllocateSystemMemory(u32 n, u32 prot) { return VirtualAlloc(0, n, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE); }
static u32 NTAPI MmFreeSystemMemory(void *p, u32 n) { VirtualFree(p, 0, MEM_RELEASE); return n; }
/* PAGE_NOCACHE (0x200) and PAGE_WRITECOMBINE (0x400) are dropped: Direct3D asks for them on its frame buffers, and on
   the PC that made every read-back (blending, depth, textures drawn into) very slow for the software renderer. */
static void NTAPI MmSetAddressProtect(void *p, u32 n, u32 prot) {
    static u32 nl; if ((prot & 0x600) && nl++ < 6) { out("  MmSetAddressProtect "); hex((u32)p); out(" size "); hex(n); out(" prot "); hex(prot); out(" (cache flags dropped)\r\n"); }
    DWORD o; VirtualProtect(p, n, prot & ~0x600u, &o);
}
static u32 NTAPI MmQueryAddressProtect(void *p) { u32 m[7]; return VirtualQuery(p, m, 28) ? m[5] : 0; }
static s32 NTAPI MmQueryStatistics(u32 *st) {   /* MM_STATISTICS: Length, TotalPhysicalPages, AvailablePages, ... */
    u32 n = st[0] ? st[0] : 36; memset_(st, 0, n); st[0] = n; st[1] = 16384;
    /* Milestone 97: available pages = what is really free in the 64 MB pool, less the game image (on the Xbox it lives in the same
       memory; here it does not), never below the old fixed 12000. The game sizes its big-block heap from this answer
       (GlobalMemoryStatus at 0x68020: free - pools - 1.5 MB); with 12000 it got 29 MB and mission E01's 3.7 MB object data
       (objects-e01.ipn) no longer fitted, so the data was never loaded and the mission crashed while loading. */
    {   u32 used = 0; for (u32 j = 0; j < g_nblk; j++) used += g_blk[j].n;
        u32 fr = (POOL_END - POOL) - used, img = 0x4D4000 - 0x10000, avail = fr > img ? (fr - img) >> 12 : 0;
        st[2] = avail > 12000 ? avail : 12000;
        static u32 nl; if (nl++ < 6) { out("  MmQueryStatistics: pool in use "); hex(used); out(", available pages "); dec(st[2]); out("\r\n"); } }
    return STATUS_SUCCESS;
}
static void *NTAPI MmClaimGpuInstanceMemory(u32 n, u32 *padding) { if (padding) *padding = 0; return (void *)0x84000000; }
static u32 NTAPI MmLockUnlockBufferPages(void *p, u32 n, u8 unlock) { return 0; }
static void NTAPI MmLockUnlockPhysicalPage(u32 pa, u8 unlock) { }
static void *NTAPI ExAllocatePoolWithTag(u32 n, u32 tag) { return HeapAlloc(GetProcessHeap(), 8, n ? n : 1); }
static void NTAPI ExFreePool(void *p) { HeapFree(GetProcessHeap(), 0, p); }
static u32 NTAPI ExQueryPoolBlockSize(void *p) { return HeapSize(GetProcessHeap(), 0, p); }

/* critical sections (Xbox RTL_CRITICAL_SECTION: +0x10 LockCount, +0x14 RecursionCount, +0x18 OwningThread) */
static void NTAPI RtlInitializeCriticalSection(u32 *cs) { memset_(cs, 0, 0x1C); cs[4] = (u32)-1; }
static void NTAPI RtlEnterCriticalSection(u32 *cs) {
    u32 me = GetCurrentThreadId();
    if (cs[6] == me) { cs[5]++; return; }
    while (__sync_val_compare_and_swap(&cs[6], 0, me) != 0) Sleep(0);
    cs[5] = 1; cs[4] = 0;
}
static void NTAPI RtlLeaveCriticalSection(u32 *cs) { if (--cs[5] == 0) { cs[4] = (u32)-1; __sync_lock_release(&cs[6]); } }

/* time */
static s64 NTAPI KeQueryPerformanceCounter(void) { s64 c; QueryPerformanceCounter(&c); return c; }
static s64 NTAPI KeQueryPerformanceFrequency(void) { s64 f; QueryPerformanceFrequency(&f); return f; }
static void NTAPI KeQuerySystemTime(u64 *t) { GetSystemTimeAsFileTime(t); }
static void NTAPI KeStallExecutionProcessor(u32 us) { s64 f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a); do QueryPerformanceCounter(&b); while ((b - a) * 1000000 / f < us); }
static void *g_dly_who[8]; static u64 g_dly_ms[8]; static u32 g_dly_n[8];   /* sleeps by caller (profiler) */
static s32 NTAPI KeDelayExecutionThread(u8 mode, u8 alertable, s64 *interval) {
    { void *w = __builtin_return_address(0); u32 s2 = 0; for (; s2 < 8; s2++) if (g_dly_who[s2] == w || !g_dly_who[s2]) break; if (s2 < 8) { g_dly_who[s2] = w; g_dly_n[s2]++; s64 v0 = *interval; g_dly_ms[s2] += v0 < 0 ? (u64)(-v0) / 10000 : 0; } }
    /* A zero delay is a yield (SleepEx(0)): the game's synchronous reads spin on it while its I/O thread completes them,
       so turning it into Sleep(1) made every read cost a timer tick (Milestone 76). */
    s64 v = *interval; u32 ms;
    if (v < 0) ms = (u32)((-v + 9999) / 10000);
    else { u64 t; GetSystemTimeAsFileTime(&t); ms = (s64)t >= v ? 0 : (u32)((v - (s64)t + 9999) / 10000); }
    Sleep(ms); return STATUS_SUCCESS;
}

/* IRQL (kept in the fake KPCR at +0x24) */
static u8 *kpcr(void) { return TlsGetValue(g_tls); }
static u8 __fastcall KfRaiseIrql(u8 n) { u8 o = kpcr()[0x24]; kpcr()[0x24] = n; return o; }
static void __fastcall KfLowerIrql(u8 n) { kpcr()[0x24] = n; }
static u8 NTAPI KeRaiseIrqlToDpcLevel(void) { return KfRaiseIrql(2); }

/* strings */
typedef struct { u16 len, max; char *buf; } ANSI_STRING;
typedef struct { u16 len, max; u16 *buf; } UNICODE_STRING;
static void NTAPI RtlInitAnsiString(ANSI_STRING *d, const char *s) { u16 n = 0; if (s) while (s[n]) n++; d->len = n; d->max = s ? n + 1 : 0; d->buf = (char *)s; }
static s32 NTAPI RtlAnsiStringToUnicodeString(UNICODE_STRING *d, ANSI_STRING *s, u8 alloc) {
    if (alloc) { d->max = (s->len + 1) * 2; d->buf = HeapAlloc(GetProcessHeap(), 0, d->max); }
    for (u32 i = 0; i < s->len && i * 2 + 2 <= d->max; i++) d->buf[i] = (u8)s->buf[i];
    d->len = s->len * 2; if (d->len + 2 <= d->max) d->buf[s->len] = 0; return STATUS_SUCCESS;
}
static u32 NTAPI RtlCompareMemoryUlong(u32 *p, u32 n, u32 pat) { u32 i = 0; while (i + 4 <= n && p[i / 4] == pat) i += 4; return i; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static s32 NTAPI RtlCompareString(ANSI_STRING *a, ANSI_STRING *b, u8 ci) {
    u32 n = a->len < b->len ? a->len : b->len;
    for (u32 i = 0; i < n; i++) { int x = (u8)a->buf[i], y = (u8)b->buf[i]; if (ci) { x = lower(x); y = lower(y); } if (x != y) return x - y; }
    return a->len - b->len;
}
static u8 NTAPI RtlEqualString(ANSI_STRING *a, ANSI_STRING *b, u8 ci) { return a->len == b->len && RtlCompareString(a, b, ci) == 0; }
static u32 NTAPI RtlNtStatusToDosError(u32 st) { return st == 0 ? 0 : st == STATUS_TIMEOUT ? 1460 : (st & 0xFFFF); }
static void NTAPI RtlUnwind_(void *frame, void *ip, void *rec, void *ret) { RtlUnwind(frame, ip, rec, ret); }
static void NTAPI RtlRaiseException(u32 *rec) { out("  RtlRaiseException code "); hex(rec[0]); out("\r\n"); log_flush(); STOP(4); }

/* events, mutants, waits */
static s32 NTAPI NtCreateEvent(HANDLE *h, void *oa, u32 type, u8 initial) { *h = CreateEventA(0, type == 0, initial, 0); return *h ? 0 : STATUS_UNSUCCESSFUL; }
static s32 NTAPI NtSetEvent(HANDLE h, s32 *prev) { if (prev) *prev = 0; return SetEvent(h) ? 0 : STATUS_UNSUCCESSFUL; }
static s32 NTAPI NtClearEvent(HANDLE h) { return ResetEvent(h) ? 0 : STATUS_UNSUCCESSFUL; }
static s32 NTAPI NtCreateMutant(HANDLE *h, void *oa, u8 owner) { *h = CreateMutexA(0, owner, 0); return *h ? 0 : STATUS_UNSUCCESSFUL; }
static s32 NTAPI NtReleaseMutant(HANDLE h, s32 *prev) { if (prev) *prev = 0; return ReleaseMutex(h) ? 0 : STATUS_UNSUCCESSFUL; }
static u32 to_ms(s64 *t) { if (!t) return 0xFFFFFFFF; if (*t >= 0) return 0; return (u32)(-*t / 10000); }
void swap_header(int pe);
static s32 wait_result(DWORD r) { return r == 0x102 ? STATUS_TIMEOUT : r; }
static s32 NTAPI NtWaitForSingleObject(HANDLE h, u8 alertable, s64 *t) { return wait_result(WaitForSingleObject(h, to_ms(t))); }
static s32 NTAPI NtWaitForSingleObjectEx(HANDLE h, u8 mode, u8 alertable, s64 *t) { return wait_result(WaitForSingleObject(h, to_ms(t))); }
static s32 NTAPI NtWaitForMultipleObjectsEx(u32 n, HANDLE *hs, u32 waitType, u8 mode, u8 alertable, s64 *t) {
    /* NULL handles are left out (and the index mapped back). The game's I/O thread (0x264620) can see its handle
       count raised before the array slot is filled; on the single-core Xbox it would already be waiting by then. */
    HANDLE v[64]; u32 map[64], m = 0;
    for (u32 i = 0; i < n && m < 64; i++) if (hs[i]) { v[m] = hs[i]; map[m++] = i; }
    if (m == 0) { Sleep(to_ms(t) == 0xFFFFFFFF ? 1 : to_ms(t)); return STATUS_TIMEOUT; }
    DWORD r = WaitForMultipleObjects(m, v, waitType == 0, to_ms(t));
    if (waitType != 0 && r < m) r = map[r];
    else if (waitType != 0 && r >= 0x80 && r < 0x80 + m) r = 0x80 + map[r - 0x80];
    if (r == 0xFFFFFFFF) { static u32 nl; if (nl++ < 10) { out("  NtWaitForMultipleObjectsEx FAILED, error "); dec(GetLastError()); out(", "); dec(n); out(" handles:"); for (u32 i = 0; i < n && i < 16; i++) { out(" "); hex((u32)hs[i]); } out("\r\n"); } }
    return wait_result(r);
}
static s32 NTAPI NtClose(HANDLE h) { kernel2_close(h); CloseHandle(h); return STATUS_SUCCESS; }

/* objects and threads */
static s32 NTAPI ObReferenceObjectByHandle(HANDLE h, void *type, void **obj) { *obj = h; return STATUS_SUCCESS; }
static void __fastcall ObfDereferenceObject(void *o) { }
typedef struct { void *start, *ctx, *sysroutine; u32 tls; } THREADARGS;
static DWORD __stdcall thread_entry(void *p) {
    THREADARGS a = *(THREADARGS *)p; HeapFree(GetProcessHeap(), 0, p);
    new_kpcr(a.tls);
    if (a.sysroutine) ((void (__stdcall *)(void *, void *))a.sysroutine)(a.start, a.ctx);
    else ((void (__stdcall *)(void *))a.start)(a.ctx);
    out("  thread returned: start "); hex((u32)a.start); out("\r\n");
    return 0;
}
static s32 NTAPI PsCreateSystemThreadEx(HANDLE *h, u32 extSize, u32 stack, u32 tlsSize, u32 *tid, void *start, void *ctx, u8 suspended, u8 dbg, void *sysroutine) {
    THREADARGS *a = HeapAlloc(GetProcessHeap(), 0, sizeof *a); a->start = start; a->ctx = ctx; a->sysroutine = sysroutine; a->tls = tlsSize;
    DWORD id; swap_header(1); HANDLE t = CreateThread(0, stack < 0x40000 ? 0x40000 : stack, thread_entry, a, suspended ? 4 : 0, &id); DWORD e = GetLastError(); swap_header(0); if (t) kernel_add_thread(OpenThread(0x1F03FF, 0, id));
    out("  thread "); out(t ? "created" : "NOT created"); out(" (id "); dec(id); out("): start "); hex((u32)start); out(" tls size "); hex(tlsSize);
    out(" stack "); hex(stack); if (!t) { out(" error "); dec(e); } out("\r\n");
    if (h) *h = t; if (tid) *tid = id; return t ? 0 : STATUS_UNSUCCESSFUL;
}
static void NTAPI PsTerminateSystemThread(s32 st) { out("  PsTerminateSystemThread "); hex(st); out(" (thread "); dec(GetCurrentThreadId()); out(")\r\n"); ExitThread(st); }
static s32 NTAPI NtResumeThread(HANDLE h, u32 *prev) { DWORD r = ResumeThread(h); if (prev) *prev = r; return 0; }
static s32 NTAPI KeSetBasePriorityThread(void *t, s32 p) { return 0; }

/* sections: everything is already loaded */
static s32 NTAPI XeLoadSection(void *sec) { return STATUS_SUCCESS; }
static s32 NTAPI XeUnloadSection(void *sec) { return STATUS_SUCCESS; }

/* shutdown */
static void NTAPI HalReturnToFirmware(u32 how) { out("HalReturnToFirmware("); dec(how); out(") - the game asked to reboot or quit");
    { extern u32 g_last_pad[3]; out(" (last pad: buttons "); hex(g_last_pad[0]); out(" A/B/X/Y "); hex(g_last_pad[1]); out(" black/white/LT/RT "); hex(g_last_pad[2]); out(")"); } out("\r\n");
    if (how == 1 || how == 2) { HANDLE f = CreateFileA("restart_loader.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (f != (HANDLE)-1) CloseHandle(f); out("restart_loader.txt written: run_loader.bat starts the game again\r\n"); } log_flush(); STOP(0); }
static void NTAPI KeBugCheck(u32 code) { out("KeBugCheck "); hex(code); out("\r\n"); log_flush(); STOP(5); }
static u8 NTAPI HalIsResetOrShutdownPending(void) { return 0; }
static void NTAPI HalRegisterShutdownNotification(void *r, u8 reg) { }

/* settings */
static s32 NTAPI ExQueryNonVolatileSetting(u32 idx, u32 *type, void *val, u32 len, u32 *outLen) {
    out("  ExQueryNonVolatileSetting "); hex(idx); out("\r\n");
    if (type) *type = 4; memset_(val, 0, len);
    if (idx == 0x104 && len >= 4) *(u32 *)val = 0x00010000;   /* XC_FACTORY_GAME_REGION: North America */
    {   /* Milestone 97: the console's network address (each PC its own, net.c): factory setting 0x101, and at 0x40 of the whole EEPROM (0xFFFF) */
        void net_mac(u8 *m);
        /* the whole EEPROM (0xFFFF) also feeds the key of the game's signed option files: the address is in it only for the network
           start-up's read (before it asks for 0x101), otherwise the saved options no longer verify ("Unable to load option settings") */
        static int asked_mac; u32 caller = (u32)__builtin_return_address(0); int net_enabled(void);
        if (idx == 0xFFFF) { out("    (caller "); hex(caller); out(")\r\n"); }
        if (!net_enabled()) goto plain;   /* without netplay.txt: exactly as before */
        if (idx == 0x101 && len >= 6) { asked_mac = 1; net_mac((u8 *)val); if (outLen) *outLen = 6; return STATUS_SUCCESS; }
        if (idx == 0xFFFF && len >= 0x46 && !asked_mac) { u8 m[6]; net_mac(m); int any = 0; for (int k = 0; k < 6; k++) any |= m[k] & (k < 3 ? 0 : 0xFF);
            if (any) for (int k = 0; k < 6; k++) ((u8 *)val)[0x40 + k] = m[k]; if (outLen) *outLen = len; return STATUS_SUCCESS; } }
plain:
    if (outLen) *outLen = len < 4 ? len : 4; return STATUS_SUCCESS;
}

/* ---- import table ---- */
typedef struct { const char *name; void *fn; } KFN;
static const KFN g_impl[] = {
#define F(n) { #n, (void *)n }
    F(AvGetSavedDataAddress), F(AvSetSavedDataAddress), F(DbgPrint), F(DbgBreakPoint),
    F(NtAllocateVirtualMemory), F(NtFreeVirtualMemory), F(NtQueryVirtualMemory), F(NtProtectVirtualMemory),
    F(MmAllocateContiguousMemory), F(MmAllocateContiguousMemoryEx), F(MmFreeContiguousMemory), F(MmPersistContiguousMemory),
    F(MmGetPhysicalAddress), F(MmQueryAllocationSize), F(MmAllocateSystemMemory), F(MmFreeSystemMemory),
    F(MmSetAddressProtect), F(MmQueryAddressProtect), F(MmQueryStatistics), F(MmClaimGpuInstanceMemory),
    F(MmLockUnlockBufferPages), F(MmLockUnlockPhysicalPage),
    F(ExAllocatePoolWithTag), F(ExFreePool), F(ExQueryPoolBlockSize),
    F(RtlInitializeCriticalSection), F(RtlEnterCriticalSection), F(RtlLeaveCriticalSection),
    F(KeQueryPerformanceCounter), F(KeQueryPerformanceFrequency), F(KeQuerySystemTime), F(KeStallExecutionProcessor),
    F(KeDelayExecutionThread), F(KfRaiseIrql), F(KfLowerIrql), F(KeRaiseIrqlToDpcLevel),
    F(RtlInitAnsiString), F(RtlAnsiStringToUnicodeString), F(RtlCompareMemoryUlong), F(RtlCompareString), F(RtlEqualString),
    F(RtlNtStatusToDosError), F(RtlRaiseException),
    F(NtCreateEvent), F(NtSetEvent), F(NtClearEvent), F(NtCreateMutant), F(NtReleaseMutant),
    F(NtWaitForSingleObject), F(NtWaitForSingleObjectEx), F(NtWaitForMultipleObjectsEx), F(NtClose),
    F(ObReferenceObjectByHandle), F(ObfDereferenceObject),
    F(PsCreateSystemThreadEx), F(PsTerminateSystemThread), F(NtResumeThread), F(KeSetBasePriorityThread),
    F(XeLoadSection), F(XeUnloadSection),
    F(HalReturnToFirmware), F(KeBugCheck), F(HalIsResetOrShutdownPending), F(HalRegisterShutdownNotification),
    F(ExQueryNonVolatileSetting),
    { "RtlUnwind", (void *)RtlUnwind_ },
#undef F
};
typedef struct { const char *name; void *data; } KDATA;
static const KDATA g_data[] = {
    { "KeTickCount", &g_tick }, { "XboxKrnlVersion", g_krnlver }, { "XboxHardwareInfo", g_hwinfo },
    { "LaunchDataPage", &g_launch }, { "HalDiskCachePartitionCount", &g_three }, { "HalDiskModelNumber", &g_modelp }, { "HalDiskSerialNumber", &g_serialp }, { "XeImageFileName", &g_imagename }, { "HalBootSMCVideoMode", &g_one },
};
extern const KFN g_impl2[];
static int streq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

/* Fills the kernel import table: implemented functions go through a logging trampoline,
 * data imports get their variable's address, everything else a stop-and-log stub. */
u32 kernel_patch_imports(u32 *t, u8 *stubs, u32 *nimpl, u32 *ndata) {
    u32 k = 0; *nimpl = *ndata = 0;
    for (; t[k]; k++) {
        u32 ord = t[k] & 0x7FFFFFFF; const char *nm = g_kname[ord]; u8 *s = stubs + k * 24; void *fn = 0;
        for (u32 i = 0; i < sizeof g_data / sizeof *g_data; i++) if (streq(nm, g_data[i].name)) { t[k] = (u32)g_data[i].data; (*ndata)++; goto next; }
        if (ord == 16 || ord == 259 || ord == 71 || ord == 323 || ord == 325 || ord == 353 || ord == 354 || ord == 355 ||
            ord == 357) { t[k] = (u32)g_zero; (*ndata)++; goto next; }   /* other data imports */
        for (u32 i = 0; i < sizeof g_impl / sizeof *g_impl; i++) if (streq(nm, g_impl[i].name)) { fn = g_impl[i].fn; break; }
        for (u32 i = 0; !fn && g_impl2[i].name; i++) if (streq(nm, g_impl2[i].name)) fn = g_impl2[i].fn;
        if (fn) {   /* pushad; push ord; call log_call; popad; jmp fn */
            s[0] = 0x60; s[1] = 0x68; *(u32 *)(s + 2) = ord; s[6] = 0xE8; *(u32 *)(s + 7) = (u32)log_call - (u32)(s + 11);
            s[11] = 0x61; s[12] = 0xE9; *(u32 *)(s + 13) = (u32)fn - (u32)(s + 17); (*nimpl)++;
        } else {    /* push ord; call unimplemented */
            s[0] = 0x68; *(u32 *)(s + 1) = ord; s[5] = 0xE8; *(u32 *)(s + 6) = (u32)unimplemented - (u32)(s + 10); s[10] = 0xCC;
        }
        t[k] = (u32)s;
    next:;
    }
    return k;
}

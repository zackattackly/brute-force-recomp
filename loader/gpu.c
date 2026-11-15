/* NV2A (Xbox GPU) stand-in, part 1 (Milestone 75): reads the command stream ("push buffer") that
 * Direct3D and the game write, follows its jumps and calls, keeps the 3D object's state, carries out
 * clears and semaphore releases, and shows the Xbox's screen in a window.
 * Drawing triangles comes next; for now draw calls are only counted. */
#include "util.h"
#include "bfgl.h"
static RING *g_ringp;   /* the command ring to the viewer (Milestone 79) */

#define NV(o) (*(volatile u32 *)(0xFD000000 + (o)))
volatile u32 *regp(u32 off);
#define REG(o) (*regp(o))
#define PHYS(a) ((u8 *)(0x80000000u + ((a) & 0x03FFFFFF)))

typedef struct { u32 style; void *proc; int cls, wnd; void *inst, *icon, *cursor, *brush; const char *menu, *name; } WNDCLASSA;
typedef struct { void *hwnd; u32 msg; u32 wp, lp; u32 time; s32 x, y; } MSG;
typedef struct { u32 size; s32 w, h; u16 planes, bpp; u32 compr, img, xppm, yppm, used, important; } BMIH;
#define U32IMP __declspec(dllimport)
U32IMP u16 WINAPI RegisterClassA(const WNDCLASSA *);
U32IMP void *WINAPI CreateWindowExA(u32, const char *, const char *, u32, s32, s32, s32, s32, void *, void *, void *, void *);
U32IMP s32 WINAPI DefWindowProcA(void *, u32, u32, u32);
U32IMP BOOL WINAPI PeekMessageA(MSG *, void *, u32, u32, u32);
U32IMP BOOL WINAPI TranslateMessage(const MSG *);
U32IMP s32 WINAPI DispatchMessageA(const MSG *);
U32IMP void *WINAPI GetDC(void *);
U32IMP s32 WINAPI ReleaseDC(void *, void *);
U32IMP BOOL WINAPI GetClientRect(void *, s32 *);
U32IMP BOOL WINAPI AdjustWindowRect(s32 *, u32, BOOL);
U32IMP void *WINAPI LoadCursorA(void *, const char *);
U32IMP BOOL WINAPI SetWindowTextA(void *, const char *);
U32IMP s32 WINAPI StretchDIBits(void *, s32, s32, s32, s32, s32, s32, s32, s32, const void *, const void *, u32, u32);
U32IMP void *WINAPI GetModuleHandleA(const char *);

/* ---- what the display is showing (set by AvSetDisplayMode and the CRTC start register) ---- */
u32 g_av_pitch = 1280, g_av_format = 0x11, g_av_fb;
static u32 g_frames, g_draws, g_clears, g_methods, g_flips;
u32 g_flips_total;   /* game flips, for the GPU path's present (kernel2.c vblank_tick) */
u32 kel[0x2000 / 4];   /* shared with render.c */
void render_method(u32 m, u32 v); void render_clear(u32 flags); void render_init(void); void render_flush(void);
extern u32 g_r_tris, g_r_pix, g_r_ff, g_r_prims[16], g_r_us, g_r_verts; extern u64 g_vs_cyc; static u64 g_pb_cyc, g_tick_state[4]; extern u32 g_fl_par, g_fl_ser, g_fl_tris;
static u32 g_sem_off, g_sem_val;   /* last value written to each method of the 3D object */

static void clear_surface(u32 flags) {
    u32 fmt = kel[0x208 / 4], pitch = kel[0x20C / 4] & 0xFFFF, off = kel[0x210 / 4];
    u32 x0 = kel[0x1D98 / 4] & 0xFFFF, x1 = kel[0x1D98 / 4] >> 16, y0 = kel[0x1D9C / 4] & 0xFFFF, y1 = kel[0x1D9C / 4] >> 16;
    u32 cf = fmt & 0xF, bpp = (cf >= 4 && cf <= 8) ? 4 : (cf == 9 ? 1 : 2);
    g_clears++;
    if (!(flags & 0xF0) || !pitch || x1 >= 4096 || y1 >= 4096 || ((fmt >> 8) & 0xF) == 2) return;   /* no colour, or swizzled */
    u32 c = kel[0x1D90 / 4];
    if (bpp == 2) c = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);   /* to R5G6B5 */
    for (u32 y = y0; y <= y1; y++) {
        u8 *row = PHYS(off + y * pitch);
        if ((u32)row + (x1 + 1) * bpp > 0x84000000u) break;
        if (bpp == 4) for (u32 x = x0; x <= x1; x++) ((u32 *)row)[x] = c;
        else if (bpp == 2) for (u32 x = x0; x <= x1; x++) ((u16 *)row)[x] = (u16)c;
    }
}

/* A NO_OPERATION with a non-zero parameter is Direct3D asking for a software interrupt (Milestone 76): the GPU
   stops, puts the method and parameter in PGRAPH's trapped registers and raises PGRAPH_INTR ERROR; Direct3D's
   interrupt routine handles it (e.g. sets the event a full push buffer waits on) and clears the bit. */
static u32 g_nop_wait, g_nops, g_nop_get;
/* FLIP_STALL (Milestone 86): as on the Xbox, the GPU stops at a flip until the next vertical blank. That locks the
   game loop to 60 Hz; without it the loop ran 95-280 times a second and Bink movies showed their frames 26-46 ms apart.
   Opt-in (loader\flipstall.txt): with it the game loop ran at ~42 Hz instead of 60 (the flip handshake is not fully
   emulated), so movies ran long. */
volatile u32 g_flip_stall; u32 g_flip_stall_on;
/* what the display shows (Milestone 86): at a flip, the colour surface just drawn. Direct3D writes the CRTC start
   itself only in one mode (device field +0x1B8 clear); otherwise the GPU flips, so the CRTC register stays stale and
   showing it made menus flicker (the buffer being drawn) or stay black (0). CPU-written screens (Bink movies, no draws
   between flips) keep the CRTC address the game writes. */
volatile u32 g_flip_surf, g_flip_drawn; u32 g_draws_since_flip;
static u32 g_prev_surf, g_mem_pick;
static u32 mem_sig(u32 a) { u32 s = 0; volatile u32 *q = (volatile u32 *)PHYS(a); for (u32 i = 0; i < 640 * 480; i += 4801) s = s * 31 + q[i]; return s; }
u32 gpu_display_addr(void) {   /* called once per vblank */
    if (g_flip_drawn && g_flip_surf) return g_flip_surf;
    /* CPU-written screen: of the candidate buffers, the one whose memory changed since the last vblank (where Bink writes) */
    static u32 cand[4], sig[4];
    u32 c[4] = {REG(0x600800), g_flip_surf, g_prev_surf, g_av_fb};
    for (u32 i = 0; i < 4; i++) { u32 a = c[i] & 0x03FFFFFF; if (!a) continue; u32 s = mem_sig(a);
        u32 k = 0; for (; k < 4 && cand[k] != a; k++) ;
        if (k == 4) { for (k = 0; k < 4 && cand[k]; k++) ; if (k == 4) k = i; cand[k] = a; sig[k] = s; continue; }
        if (sig[k] != s) { sig[k] = s; g_mem_pick = a; } }
    return g_mem_pick ? g_mem_pick : (c[0] ? c[0] : c[3]);
}
void gpu_raise_irq(void);
static u32 g_hist[0x800], g_hist_sub[8], g_hist_done;
static void method(u32 sub, u32 m, u32 v) {
    g_methods++;
    g_hist_sub[sub]++;
    if (m >= 0x2000) return;
    g_hist[m / 4]++;
    kel[m / 4] = v;
    switch (m) {
    case 0x0100: if (v) { REG(0x400704) = (sub << 16) | m; REG(0x400708) = v; REG(0x400108) = 1; REG(0x400100) |= 0x00100000; g_nop_wait = 1; g_nops++; } break;
    case 0x1D94: g_clears++; render_clear(v); break;                         /* CLEAR_SURFACE */
    default: if (sub == 0) render_method(m, v); break;
    case 0x1D70:   /* BACK_END_WRITE_SEMAPHORE_RELEASE: Direct3D's fence. Every release goes to offset 0 of the semaphore context,
                      which is the word the device's +0x30 pointer reads, so the value is written there, in command order
                      (Milestone 77: reporting fences early made D3D re-patch a push buffer the GPU had not run yet) */
        render_flush();   /* the CPU may act on this fence: what came before it must be drawn */
        g_sem_off = kel[0x1D6C / 4]; g_sem_val = v;
        { extern int g_gl; void render_gl_fence(u32);
          if (g_gl) render_gl_fence(v);   /* GPU path: written when the viewer has drawn up to here (gpu_process) */
          else { u32 dev = *(volatile u32 *)0x37DD88; if (dev > 0x10000) { volatile u32 *gt = *(u32 **)(dev + 0x30); if ((u32)gt > 0x10000) *gt = v; } } }
        break;
    case 0x17FC: if (v) { g_draws++; g_draws_since_flip++; } if (sub == 0) render_method(m, v); break;                                   /* SET_BEGIN_END */
    /* pixel-count reports (Milestone 87): the game measures how much of something is visible (the sun, for its glare)
       with depth-pass pixel counts. The report DMA object (handle 0xC) starts at physical 0, so GET_REPORT's offset is a
       physical address. The viewer counts with an occlusion query and writes the 16-byte report. */
    case 0x17C8: if (v == 1) { render_flush(); void render_gl_zpass(u32, u32); render_gl_zpass(0, 0); } break;   /* CLEAR_REPORT_VALUE (zpass) */
    case 0x17CC: { render_flush(); void render_gl_zpass(u32, u32); render_gl_zpass(1, v); } break;              /* SET_ZPASS_PIXEL_COUNT_ENABLE */
    case 0x17D0: if ((v >> 24) == 1) { render_flush(); void render_gl_zpass(u32, u32); render_gl_zpass(2, v & 0xFFFFFF); } break;   /* GET_REPORT */
    case 0x0130: g_flips++; g_flips_total++; if (g_flip_stall_on) g_flip_stall = 1;
        if (kel[0x210 / 4] != g_flip_surf) g_prev_surf = g_flip_surf; g_flip_surf = kel[0x210 / 4]; g_flip_drawn = g_draws_since_flip != 0; g_draws_since_flip = 0; break;   /* FLIP_STALL: wait for the vblank (Milestone 86) */
    }
}

/* Walks the push buffer from GET to PUT. Returns the number of words read. */
static u32 g_ret, g_have_ret, g_bad;
static u32 g_jr[16][3], g_njr;   /* recent jumps (kind, from, to), for diagnosing loops */
#define JREC(k, f, t) { u32 *j_ = g_jr[g_njr++ & 15]; j_[0] = k; j_[1] = f; j_[2] = t; }
static u32 pb_run(void) {
    if (g_flip_stall) return 0;
    if (g_nop_wait) {   /* still waiting for the interrupt routine: run it again every 2 ms (it may have had PGRAPH interrupts off) */
        if (REG(0x400100) & 0x00100000) {
            static u32 tries; tries++;
            if (tries % 2 == 0) gpu_raise_irq();
            if (tries == 2000) { out("  [gpu] NOP interrupt not handled after 2 s: PGRAPH intr "); hex(REG(0x400100)); out(" enable "); hex(REG(0x400140)); out(", PMC enable "); hex(REG(0x000140)); out(", trapped "); hex(REG(0x400704)); out(" = "); hex(REG(0x400708)); out(", fifo access "); hex(REG(0x400720)); out("\r\n"); }
            return 0;
        }
        g_nop_wait = 0; { static u32 tries; tries = 0; }
        { static u32 nl; if (nl++ < 12) { out("  [gpu] NOP "); hex(REG(0x400708)); out(" handled: user get "); hex(NV(0x800044)); out(" (was "); hex(g_nop_get); out("), cache1 get "); hex(REG(0x3244)); out(" put "); hex(REG(0x3240)); out(", dma state "); hex(REG(0x3228)); out(" "); hex(REG(0x322C)); out("\r\n"); } }
    }
    u32 get = NV(0x800044), put = NV(0x800040), n = 0;
    void vblank_maybe(void); u32 nvb = 0;
    while (get != put && n < 1000000 && !g_nop_wait && !g_flip_stall) {
        if (++nvb >= 4096) { nvb = 0; vblank_maybe(); }
        if (get >= 0x4000000) { if (g_bad++ < 5) { out("  [gpu] command pointer out of range "); hex(get); out("\r\n"); } get = put; break; }
        u32 w = *(volatile u32 *)PHYS(get); n++;
        if ((w & 0xE0000003) == 0x20000000) { JREC(0, get, w & 0x1FFFFFFC); get = w & 0x1FFFFFFC; continue; }          /* old jump */
        if ((w & 3) == 1) { JREC(1, get, w & 0xFFFFFFFC); get = w & 0xFFFFFFFC; continue; }                             /* jump */
        if ((w & 3) == 2) { JREC(2, get, w & 0xFFFFFFFC); g_ret = get + 4; g_have_ret = 1; get = w & 0xFFFFFFFC; continue; }   /* call */
        if (w == 0x00020000) { JREC(3, get, g_have_ret ? g_ret : get + 4); get = g_have_ret ? g_ret : get + 4; g_have_ret = 0; continue; }  /* return */
        if ((w & 0xE0030003) == 0 || (w & 0xE0030003) == 0x40000000) {                  /* methods */
            u32 m = w & 0x1FFC, sub = (w >> 13) & 7, cnt = (w >> 18) & 0x7FF, inc = !(w & 0x40000000);
            get += 4;
            for (u32 i = 0; i < cnt && get != put; i++) { method(sub, m, *(volatile u32 *)PHYS(get)); get += 4; n++; if (inc) m += 4; }   /* a flip stall stops the walk after this packet */
            continue;
        }
        if (g_bad++ < 5) { out("  [gpu] unknown command word "); hex(w); out(" at "); hex(get); out("\r\n"); }
        get += 4;
    }
    render_flush();   /* the queued triangles of this pass (they are drawn across draw calls, Milestone 78) */
    NV(0x800044) = get; if (g_nop_wait) { g_nop_get = get; REG(0x3244) = get; }
    /* the interrupt routine runs synchronously: if it handled the NOP, carry on at once (Milestone 78: waiting
       for the next tick cost ~2.6 ms per NOP, about a fifth of the GPU thread's time in missions) */
    if (g_nop_wait && n < 1000000) { gpu_raise_irq(); { void kernel2_tick(void); kernel2_tick(); }   /* the routine only queues D3D's DPC: run it now */
        if (!(REG(0x400100) & 0x00100000)) { g_nop_wait = 0; if (NV(0x800044) != NV(0x800040)) return n + pb_run(); return n; }
        { static u32 cnt[32], tot; u32 cmd = REG(0x400708) & 31; cnt[cmd]++; if (++tot % 2000 == 0) { out("  [gpu] NOPs left pending, by command:"); for (u32 i = 0; i < 32; i++) if (cnt[i]) { out(" "); dec(i); out(":"); dec(cnt[i]); } out("\r\n"); } }
        return n; }
    if (n >= 1000000) { static u32 nl; if (nl++ < 3) { out("  [gpu] 1M words in one pass: get "); hex(get); out(" put "); hex(put); out(", recent jumps (kind from->to; 0 old jump, 1 jump, 2 call, 3 return):");
        for (u32 i = 0; i < 16; i++) { u32 *j = g_jr[(g_njr + i) & 15]; out(" "); dec(j[0]); out(":"); hex(j[1]); out("->"); hex(j[2]); } out("\r\n");
        /* the words around each jump that leaves or enters the main buffer */
        for (u32 i = 0; i < 16; i++) { u32 *j = g_jr[(g_njr + i) & 15]; if (((j[1] ^ j[2]) & 0xFF00000) == 0) continue;
            out("    around "); hex(j[1]); out(":"); for (s32 k = -12; k <= 2; k++) { out(" "); hex(*(u32 *)PHYS(j[1] + k * 4)); } out("\r\n");
            out("    around "); hex(j[2]); out(":"); for (s32 k = -12; k <= 6; k++) { out(" "); hex(*(u32 *)PHYS(j[2] + k * 4)); } out("\r\n"); }
        { u32 base = 0; for (u32 i = 0; i < 16; i++) { u32 *j = g_jr[(g_njr + i) & 15]; if (j[0] == 1 && (j[2] & 0xFF000000) == 0x03000000 && (j[1] & 0xFFF00000) == (put & 0xFFF00000)) base = j[1]; }
          if (base) { out("    main buffer from "); hex(base - 0x100); out(":\r\n"); for (u32 r = 0; r < 24; r++) { out("     "); for (u32 c = 0; c < 8; c++) { out(" "); hex(*(u32 *)PHYS(base - 0x100 + (r * 8 + c) * 4)); } out("\r\n"); } } }
        out("    semaphore context "); hex(kel[0x1A4 / 4]); out(", offset "); hex(kel[0x1D6C / 4]); out(", last values "); hex(g_sem_val); out("\r\n"); } }
    if (g_nop_wait) gpu_raise_irq();
    return n;
}

/* ---- the picture (Milestone 77) ----
 * The window lives in a separate small program, bfview.exe: with a window of its own, this process (whose main
 * image is overwritten by the XBE) was frozen by Windows for about a second at a time. Every 16 ms the frame the
 * CRTC shows is copied into shared memory "BruteForcePCLoader_View"; the viewer displays it and hands back the
 * keyboard state and a "closed" flag. */
typedef struct { u32 magic, frame, w, h, closed, viewer_pid, loader_pid, focused; u8 keys[256]; char title[128]; s32 mdx, mdy; u32 mcap; u32 pix[640 * 480]; } VIEW;
IMP DWORD WINAPI GetFileAttributesA(const char *);
IMP void *WINAPI MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, u32);
IMP HANDLE WINAPI CreateFileMappingA(HANDLE, void *, DWORD, DWORD, DWORD, const char *);
IMP BOOL WINAPI CreateProcessA(const char *, char *, void *, void *, BOOL, DWORD, void *, const char *, void *, void *);
IMP DWORD WINAPI GetCurrentProcessId(void);
static VIEW *g_view;
u32 g_win_loops;
static u32 g_pix[640 * 480];
void *gpu_window(void) { return g_view; }
/* the keyboard as the viewer sees it (only while its window is in front) */
int view_key(int vk) { return g_view && g_view->focused && g_view->keys[vk & 255]; }
/* the mouse (Milestone 87): captured by bfview.exe; total movement in pixels; returns 1 while captured */
int view_mouse(s32 *dx, s32 *dy) { if (!g_view) return 0; *dx = g_view->mdx; *dy = g_view->mdy; return g_view->focused && g_view->mcap; }
static void present(void) {
    u32 fb = REG(0x600800) ? REG(0x600800) : g_av_fb;   /* CRTC start address (physical) */
    u32 pitch = (kel[0x20C / 4] & 0xFFFF) ? (kel[0x20C / 4] & 0xFFFF) : g_av_pitch ? g_av_pitch : 1280, f = g_av_format;   /* the colour surface's pitch: AvSetDisplayMode's is half of it */
    int bpp = (f == 0x12 || f == 0x1E || f == 0x06 || f == 0x07) ? 4 : 2;
    for (u32 y = 0; y < 480; y++) {
        u8 *row = PHYS(fb + y * pitch); u32 *o = &g_pix[y * 640];
        if ((u32)row + 640 * bpp > 0x84000000u) break;
        if (bpp == 4) for (u32 x = 0; x < 640; x++) o[x] = ((u32 *)row)[x] | 0xFF000000;
        else if (f == 0x1C || f == 0x02 || f == 0x03) for (u32 x = 0; x < 640; x++) { u32 p = ((u16 *)row)[x]; o[x] = ((p & 0x7C00) << 9) | ((p & 0x03E0) << 6) | ((p & 0x1F) << 3); }
        else for (u32 x = 0; x < 640; x++) { u32 p = ((u16 *)row)[x]; o[x] = ((p & 0xF800) << 8) | ((p & 0x07E0) << 5) | ((p & 0x1F) << 3); }
    }
    { extern int g_gl; if (g_view && !g_gl) { memcpy_(g_view->pix, g_pix, sizeof g_pix); g_view->frame++; } }
}
/* saves what the window shows to frame.bmp (Milestone 77: lets the picture be checked without looking at the screen) */
static void save_bmp(void) {
    static u8 h[54]; u32 n = 640 * 480 * 4;
    memset_(h, 0, 54); h[0] = 'B'; h[1] = 'M'; *(u32 *)(h + 2) = 54 + n; *(u32 *)(h + 10) = 54; *(u32 *)(h + 14) = 40;
    *(s32 *)(h + 18) = 640; *(s32 *)(h + 22) = -480; *(u16 *)(h + 26) = 1; *(u16 *)(h + 28) = 32; *(u32 *)(h + 34) = n;
    HANDLE f = CreateFileA("frame.bmp", 0x40000000, 1, 0, 2, 0x80, 0); if (f == INVALID_HANDLE) return;
    DWORD w; WriteFile(f, h, 54, &w, 0); WriteFile(f, g_pix, n, &w, 0); CloseHandle(f);
}
static void start_viewer(void) {
    HANDLE m = CreateFileMappingA(INVALID_HANDLE, 0, 0x04, 0, sizeof(VIEW), "BruteForcePCLoader_View");
    g_view = m ? MapViewOfFile(m, 0x2 | 0x4, 0, 0, sizeof(VIEW)) : 0;
    if (!g_view) { out("Could not create the shared picture memory\r\n"); return; }
    memset_(g_view, 0, sizeof(VIEW)); g_view->magic = 0x56584642; g_view->w = 640; g_view->h = 480; g_view->loader_pid = GetCurrentProcessId();
    {   HANDLE rm = CreateFileMappingA(INVALID_HANDLE, 0, 0x04, 0, sizeof(RING) + RING_WORDS * 4, "BruteForcePCLoader_Ring");
        g_ringp = rm ? MapViewOfFile(rm, 0x2 | 0x4, 0, 0, sizeof(RING) + RING_WORDS * 4) : 0;
        if (g_ringp) { memset_(g_ringp, 0, sizeof(RING)); g_ringp->magic = RING_MAGIC; if (GetFileAttributesA("software.txt") != 0xFFFFFFFF) g_ringp->gl_failed = 2; } }
    u32 si[17]; u32 pi[4]; memset_(si, 0, sizeof si); si[0] = sizeof si;
    /* the command line names the soundtrack (Milestone 83: the viewer loops the game's music\default.wma) */
    extern char g_gamedir[600]; static char cmd[800]; { u32 n = 0; const char *p = "bfview.exe \""; while (*p) cmd[n++] = *p++;
        for (p = g_gamedir; *p && n < 760; ) cmd[n++] = *p++; p = "music\\default.wma\""; while (*p) cmd[n++] = *p++; cmd[n] = 0; }
    g_flip_stall_on = GetFileAttributesA("flipstall.txt") != 0xFFFFFFFF; out(g_flip_stall_on ? "Flip stall: on (flipstall.txt)\r\n" : "Flip stall: off\r\n");
    if (GetFileAttributesA("noviewer.txt") != 0xFFFFFFFF) { out("Viewer not started (noviewer.txt)\r\n"); return; }
    if (CreateProcessA("bfview.exe", cmd, 0, 0, 0, 0, 0, 0, si, pi)) { g_view->viewer_pid = pi[2]; CloseHandle((HANDLE)pi[0]); CloseHandle((HANDLE)pi[1]); out("Viewer started (bfview.exe)\r\n"); }
    else { out("Could not start bfview.exe, error "); dec(GetLastError()); out(" - the picture is still saved to frame.bmp\r\n"); }
}
volatile u32 g_mem_ready;   /* set by gpu_init once 0x80000000 is mapped */
static DWORD __stdcall window_thread(void *p) {
    u32 last = 0, t = 0, seen_viewer = 0;
    while (!g_mem_ready) Sleep(10);
    for (;;) {
        present(); g_win_loops++;
        { extern int g_gl; if (g_win_loops % 300 == 0 && !g_gl) save_bmp(); }   /* GPU path: the viewer saves frame.bmp */
        if (g_view) {
            if (g_view->closed) { out("Window closed - stopping.\r\n"); log_flush(); STOP(0); }
            if (g_view->focused) seen_viewer = 1;
            if (++t % 60 == 0) {   /* title: what the GPU stand-in has seen */
                char *s = g_view->title; const char *a = "Brute Force (PC loader) - draws/s ";
                u32 k = 0; while (a[k]) { s[k] = a[k]; k++; }
                u32 v = g_draws - last; last = g_draws; char d[12]; int i = 11; d[i] = 0; do { d[--i] = '0' + v % 10; v /= 10; } while (v);
                for (int j = i; d[j]; j++) s[k++] = d[j]; s[k] = 0;
            }
        }
        Sleep(16);
    }
}
/* called by the 1 ms ticker */
static u32 g_stat_t;
/* Milestone 86: the ticker spins on this instead of sleeping 1 ms, so the game's push-buffer waits (Direct3D spins
   at 0x371DD3 until GET moves) end at once; the 1 ms sleeps made the movie loop's timing jitter */
IMP DWORD WINAPI GetCurrentProcessId(void);
u32 *gpu_pad(void) { if (g_ringp && !g_ringp->pad[24]) g_ringp->pad[24] = GetCurrentProcessId(); return g_ringp ? g_ringp->pad : 0; }   /* heartbeats for the viewer's freeze probe: [20] canary, [21] ticker, [22] kernel calls */
u32 gpu_vsync_count(void) { return g_ringp ? g_ringp->pad[9] : 0; }   /* the monitor's refreshes, counted by the viewer */
int gpu_has_work(void) { return !g_nop_wait && !g_flip_stall && NV(0x800044) != NV(0x800040); }
void gpu_process(u32 now_ms) {
    {   /* the GPU path: wait (up to 8 s) for the viewer to say whether it can draw, then report fences it has reached */
        extern int g_gl; static int asked; void render_gl_init(RING *);
        if (!asked && g_ringp) { if (g_ringp->gl_ok || g_ringp->gl_failed || now_ms > 8000) { asked = 1; render_gl_init(g_ringp);
                void net_prestart(void); net_prestart(); }   /* Milestone 98b: the match-list helper, once the viewer has its renderer (started together, the viewer's OpenGL failed) */
            else return; }
        if (g_gl) { static u32 last; u32 f = g_ringp->fence_done; if (f != last) { last = f; u32 dev = *(volatile u32 *)0x37DD88; if (dev > 0x10000) { volatile u32 *gt = *(u32 **)(dev + 0x30); if ((u32)gt > 0x10000) *gt = f; } } }
    }
    { static int b; if (!b) { b = 1; void render_bench(void); render_bench(); } }
    {   static u64 last; u64 t0 = __builtin_ia32_rdtsc();
        u32 st = g_nop_wait ? 2 : (NV(0x800044) == NV(0x800040) ? 1 : 0);   /* 0 work, 1 nothing to do, 2 waiting on a NOP interrupt */
        if (last) g_tick_state[st] += t0 - last;   /* the time since the previous tick, charged to the state at its start */
        pb_run(); u64 t1 = __builtin_ia32_rdtsc(); g_pb_cyc += t1 - t0; last = t1; g_tick_state[3] += t1 - t0; }
    if (!g_hist_done && now_ms > 40000) {   /* one-time survey of the methods used (Milestone 77) */
        g_hist_done = 1; out("  [gpu] method survey (method:count, values = last):\r\n");
        for (u32 i = 0; i < 0x800; i++) if (g_hist[i]) { out("    "); hex(i * 4); out(" "); dec(g_hist[i]); out(" last "); hex(kel[i]); out("\r\n"); }
        out("    by subchannel:"); for (u32 i = 0; i < 8; i++) { out(" "); dec(g_hist_sub[i]); } out("\r\n");
    }
    if (now_ms - g_stat_t >= (now_ms < 60000 ? 5000u : 15000u)) {
        g_stat_t = now_ms;
        if (g_ringp) { extern u32 g_vblanks, g_vb_sent; out("  [present] vblanks "); dec(g_vblanks); out(" sent "); dec(g_vb_sent); out(" flips "); dec(g_ringp->pad[4]); out(" memory "); dec(g_ringp->pad[5]); out(" fbo "); dec(g_ringp->pad[6]); out(" throttled "); dec(g_ringp->pad[7]); out(" slowest us "); dec(g_ringp->pad[8]); out(" sound underrun frames "); dec(g_ringp->pad[10]); out(" refills "); dec(g_ringp->pad[11]); out(" lowest fill "); dec(g_ringp->pad[12]); g_ringp->pad[13] = 0; out(" | render-to-texture binds "); dec(g_ringp->pad[40]); out(" feedback "); dec(g_ringp->pad[41]); out(" ambiguous "); dec(g_ringp->pad[42]); out(" pixel-count reports "); dec(g_ringp->pad[43]); out(" stale surfaces skipped "); dec(g_ringp->pad[44]); g_ringp->pad[8] = 0; out("\r\n"); }
    out("  [gpu] methods "); dec(g_methods); out(", clears "); dec(g_clears); out(", draws "); dec(g_draws); out(", flip stalls "); dec(g_flips); out(", nop irqs "); dec(g_nops); out(", triangles "); dec(g_r_tris); out(", pixels "); dec(g_r_pix); out(", raster ms "); dec(g_r_us / 1000); out(", vertices "); dec(g_r_verts); out(", vertex ms "); dec((u32)(g_vs_cyc / 3800000)); out(", gpu total ms "); dec((u32)(g_pb_cyc / 3800000)); if (g_ringp) { out(", viewer frames "); dec(g_ringp->frames); out(" draws "); dec(g_ringp->draws); { extern u64 g_ring_wait; out(" ring-full wait ms "); dec((u32)(g_ring_wait / 3800000)); out(" viewer busy ms "); dec(g_ringp->pad[2]); out(" idle ms "); dec(g_ringp->pad[3]); } out(" skipped "); dec(g_ringp->pad[0]); out(" failed "); dec(g_ringp->pad[1]); out(" ring used "); dec((g_ringp->wpos - g_ringp->rpos) & (RING_WORDS - 1)); out(" ["); out(g_ringp->info); out("]"); } out(", idle ms: with work "); dec((u32)(g_tick_state[0] / 3800000)); out(", nothing to do "); dec((u32)(g_tick_state[1] / 3800000)); out(", NOP wait "); dec((u32)(g_tick_state[2] / 3800000)); { void render_worker_report(void); if (now_ms > 60000) render_worker_report(); } out("  [gpu] flushes "); dec(g_fl_par); out(" parallel "); dec(g_fl_ser); out(" serial, triangles queued "); dec(g_fl_tris); if (g_r_ff) { out(", fixed-function vertices "); dec(g_r_ff); }
        out(", surface "); hex(kel[0x210 / 4]); out(" fmt "); hex(kel[0x208 / 4]); out(" pitch "); hex(kel[0x20C / 4]); out(", crtc start "); hex(REG(0x600800)); out(", last semaphore "); hex(g_sem_off); out("="); hex(g_sem_val); out("\r\n");
    }
}
void swap_header(int pe);
void gpu_start_window(void) { void render_start_workers(void); render_init(); start_viewer(); swap_header(1); CreateThread(0, 0, window_thread, 0, 0, 0); render_start_workers(); swap_header(0); }

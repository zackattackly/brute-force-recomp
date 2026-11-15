/* bfview.exe - the Brute Force PC loader's window (Milestone 77).
 * The loader (bflaunch.exe + bfcore.dll) starts this program and puts each frame in the shared memory
 * "BruteForcePCLoader_View". This program shows it, passes the keyboard state back while its window is in front,
 * and sets "closed" when the window is closed, which makes the loader stop. It also ends when the loader ends.
 * (A window inside the loader process itself made Windows freeze that process for about a second at a time.) */
typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef unsigned char u8; typedef int BOOL; typedef void *HANDLE; typedef u32 DWORD;
#define WINAPI __stdcall
#define K32 __declspec(dllimport)
K32 HANDLE WINAPI OpenFileMappingA(DWORD, BOOL, const char *);
K32 void *WINAPI MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, u32);
K32 HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD);
K32 DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
K32 void WINAPI Sleep(DWORD);
K32 DWORD WINAPI GetTickCount(void);
K32 void WINAPI ExitProcess(u32);
K32 void *WINAPI GetModuleHandleA(const char *);
typedef struct { u32 style; void *proc; int cls, wnd; void *inst, *icon, *cursor, *brush; const char *menu, *name; } WNDCLASSA;
typedef struct { void *hwnd; u32 msg; u32 wp, lp; u32 time; s32 x, y; } MSG;
typedef struct { u32 size; s32 w, h; u16 planes, bpp; u32 compr, img, xppm, yppm, used, important; } BMIH;
K32 u16 WINAPI RegisterClassA(const WNDCLASSA *);
K32 void *WINAPI CreateWindowExA(u32, const char *, const char *, u32, s32, s32, s32, s32, void *, void *, void *, void *);
K32 s32 WINAPI DefWindowProcA(void *, u32, u32, u32);
K32 BOOL WINAPI PeekMessageA(MSG *, void *, u32, u32, u32);
K32 BOOL WINAPI TranslateMessage(const MSG *);
K32 s32 WINAPI DispatchMessageA(const MSG *);
K32 void *WINAPI GetDC(void *);
K32 s32 WINAPI ReleaseDC(void *, void *);
K32 BOOL WINAPI GetClientRect(void *, s32 *);
K32 BOOL WINAPI AdjustWindowRect(s32 *, u32, BOOL);
K32 void *WINAPI LoadCursorA(void *, const char *);
K32 BOOL WINAPI SetWindowTextA(void *, const char *);
K32 short WINAPI GetAsyncKeyState(int);
K32 void *WINAPI GetForegroundWindow(void);
K32 BOOL WINAPI GetCursorPos(s32 *);
K32 BOOL WINAPI SetCursorPos(s32, s32);
K32 BOOL WINAPI ClientToScreen(void *, s32 *);
K32 s32 WINAPI ShowCursor(BOOL);
K32 BOOL WINAPI ClipCursor(const s32 *);
K32 s32 WINAPI StretchDIBits(void *, s32, s32, s32, s32, s32, s32, s32, s32, const void *, const void *, u32, u32);

typedef struct { u32 magic, frame, w, h, closed, viewer_pid, loader_pid, focused; u8 keys[256]; char title[128]; s32 mdx, mdy; u32 mcap; u32 pix[640 * 480]; } VIEW;
static VIEW *v;
int gl_init(void *wnd); int gl_pump(void);   /* bfgl.c: the GPU renderer (Milestone 79) */
static int g_gl;
static void *g_wnd;
static void paint(void) {
    BMIH bi = {sizeof bi, 640, -480, 1, 32, 0, 0, 0, 0, 0, 0};
    s32 rc[4]; GetClientRect(g_wnd, rc);
    void *dc = GetDC(g_wnd);
    StretchDIBits(dc, 0, 0, rc[2], rc[3], 0, 0, 640, 480, v->pix, &bi, 0, 0x00CC0020 /* SRCCOPY */);
    ReleaseDC(g_wnd, dc);
}
/* mouse capture (Milestone 87): a click in the window captures the mouse (hidden, kept in the window, movement sent
   to the loader as aim); F10 or leaving the window releases it. The capturing click itself does not fire. */
static int g_cap, g_cap_pending;
static void mouse_release(void) { if (g_cap || g_cap_pending) { g_cap = g_cap_pending = 0; v->mcap = 0; ClipCursor(0); ShowCursor(1); } }
static void mouse_center(s32 *c) { s32 rc[4]; GetClientRect(g_wnd, rc); c[0] = rc[2] / 2; c[1] = rc[3] / 2; ClientToScreen(g_wnd, c); }
static void mouse_capture(void) {
    if (g_cap || g_cap_pending) return;
    s32 rc[4]; GetClientRect(g_wnd, rc); s32 tl[2] = {0, 0}, br[2] = {rc[2], rc[3]}; ClientToScreen(g_wnd, tl); ClientToScreen(g_wnd, br);
    s32 clip[4] = {tl[0], tl[1], br[0], br[1]}; ClipCursor(clip); ShowCursor(0);
    s32 c[2]; mouse_center(c); SetCursorPos(c[0], c[1]); g_cap_pending = 1; }
/* full screen (Milestone 97): by default the window is borderless and covers its whole monitor; F11 or Alt+Enter switches to a
   normal window and back. windowed.txt = start in a window. The picture keeps its shape (the game draws widescreen itself). */
K32 s32 WINAPI SetWindowLongA(void *, int, s32);
K32 BOOL WINAPI SetWindowPos(void *, void *, s32, s32, s32, s32, u32);
K32 void *WINAPI MonitorFromWindow(void *, u32);
K32 BOOL WINAPI GetMonitorInfoA(void *, void *);
K32 BOOL WINAPI GetWindowRect(void *, s32 *);
static int g_full; static s32 g_wrect[4];
static void set_full(int on) {
    mouse_release();
    if (on) {
        if (!g_full) GetWindowRect(g_wnd, g_wrect);
        struct { u32 size; s32 mon[4], work[4]; u32 flags; } mi = {sizeof mi};
        if (!GetMonitorInfoA(MonitorFromWindow(g_wnd, 2 /* nearest */), &mi)) return;
        SetWindowLongA(g_wnd, -16 /* GWL_STYLE */, (s32)0x90000000 /* WS_POPUP | WS_VISIBLE */);
        SetWindowPos(g_wnd, 0, mi.mon[0], mi.mon[1], mi.mon[2] - mi.mon[0], mi.mon[3] - mi.mon[1], 0x0020 | 0x0040 /* FRAMECHANGED, SHOWWINDOW */);
    } else {
        SetWindowLongA(g_wnd, -16, 0x10CF0000 /* overlapped, visible */);
        s32 *r = g_wrect; if (r[2] - r[0] < 200) { r[0] = 200; r[1] = 100; r[2] = r[0] + 1296; r[3] = r[1] + 759; }
        SetWindowPos(g_wnd, 0, r[0], r[1], r[2] - r[0], r[3] - r[1], 0x0020 | 0x0040);
    }
    g_full = on;
}
static u32 g_wheel_up_until, g_wheel_dn_until;   /* Milestone 96: a wheel notch shows as a 120 ms press of key 0x0E (up) / 0x0F (down), codes Windows leaves unused */
static s32 WINAPI wndproc(void *h, u32 m, u32 wp, u32 lp) {
    if (m == 0x020A) { short d = (short)(wp >> 16); if (d > 0) g_wheel_up_until = GetTickCount() + 120; else if (d < 0) g_wheel_dn_until = GetTickCount() + 120; return 0; }   /* WM_MOUSEWHEEL */
    if (m == 0x0010) { mouse_release(); v->closed = 1; ExitProcess(0); }   /* WM_CLOSE: tell the loader, then end */
    if (m == 0x0201) mouse_capture();                                       /* WM_LBUTTONDOWN */
    if (m == 0x0008) mouse_release();                                       /* WM_KILLFOCUS */
    if ((m == 0x0104 || m == 0x0100) && wp == 0x79) { mouse_release(); return 0; }   /* F10 (a system key: no menu) */
    if ((m == 0x0100 && wp == 0x7A) || (m == 0x0104 && wp == 0x0D && (lp & (1u << 29)))) { if (!(lp & (1u << 30))) set_full(!g_full); return 0; }   /* F11 / Alt+Enter: full screen on or off */
    if (m == 0x0100 && wp == 0x7B) { extern volatile int g_shot_req; g_shot_req = 1; return 0; }   /* F12: screenshot (bfgl.c) */
    return DefWindowProcA(h, m, wp, lp);
}
/* music (Milestone 83): the soundtrack is a WMA file, played in a loop through MCI (Windows decodes it) */
K32 u32 WINAPI mciSendStringA(const char *, char *, u32, void *);
K32 char *WINAPI GetCommandLineA(void);
K32 u32 WINAPI GetFileAttributesA(const char *);
K32 BOOL WINAPI CloseHandle(HANDLE);
K32 HANDLE WINAPI CreateFileA(const char *, u32, u32, void *, u32, u32, void *);
K32 BOOL WINAPI WriteFile(HANDLE, const void *, u32, u32 *, void *);
static void music_start(void) {
    /* opt-in: the game's real music is streamed from data\sounds\*.xwb by its own DirectSound code (dsound.c);
       nothing in the game uses music\default.wma, so looping it would play over the real soundtrack */
    if (GetFileAttributesA("wmamusic.txt") == 0xFFFFFFFF || GetFileAttributesA("nosound.txt") != 0xFFFFFFFF) return;
    const char *c = GetCommandLineA(); while (*c && *c != 0x22) c++;   /* the quoted path after bfview.exe */
    if (!*c) return;
    static char s[900]; u32 n = 0; const char *p = "open "; while (*p) s[n++] = *p++;
    while (*c && n < 840) s[n++] = *c++;
    p = " type mpegvideo alias bfmusic"; while (*p) s[n++] = *p++; s[n] = 0;
    if (mciSendStringA(s, 0, 0, 0)) return;
    mciSendStringA("setaudio bfmusic volume to 350", 0, 0, 0);
    u32 e = mciSendStringA("play bfmusic repeat", 0, 0, 0);
    char st[64]; st[0] = 0; mciSendStringA("status bfmusic mode", st, sizeof st, 0);
    HANDLE f = CreateFileA("music_status.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (f != (HANDLE)-1) { u32 w, k = 0; while (st[k]) k++; if (e) { st[0] = 'E'; k = 1; } WriteFile(f, st, k, &w, 0); CloseHandle(f); }
}
void WINAPI start(void) {
    HANDLE m = 0;
    for (int i = 0; i < 100 && !m; i++) { m = OpenFileMappingA(0x6 /* read, write */, 0, "BruteForcePCLoader_View"); if (!m) Sleep(50); }
    if (!m || !(v = MapViewOfFile(m, 0x6, 0, 0, sizeof(VIEW))) || v->magic != 0x56584642) ExitProcess(1);
    HANDLE loader = OpenProcess(0x00100000 /* SYNCHRONIZE */, 0, v->loader_pid);
    WNDCLASSA wc = {3, (void *)wndproc, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, (const char *)32512), 0, 0, "BruteForcePCView"};
    RegisterClassA(&wc);
    s32 r[4] = {0, 0, 640, 480}; AdjustWindowRect(r, 0x00CF0000, 0);
    g_wnd = CreateWindowExA(0, "BruteForcePCView", "Brute Force (PC loader)", 0x10CF0000 /* visible overlapped */, 760, 60, r[2] - r[0], r[3] - r[1], 0, 0, wc.inst, 0);
    if (GetFileAttributesA("windowed.txt") == 0xFFFFFFFF) set_full(1);   /* full screen unless windowed.txt */
    g_gl = gl_init(g_wnd);
    { void audio_start(void); audio_start(); }
    music_start();
    u32 last = 0xFFFFFFFF; char title[128]; title[0] = 0;
    for (u32 t = 0;; t++) {
        MSG msg; while (PeekMessageA(&msg, 0, 0, 0, 1)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (loader && WaitForSingleObject(loader, 0) == 0) ExitProcess(0);   /* the loader has ended */
        int fg = GetForegroundWindow() == g_wnd; v->focused = fg;
        for (int k = 0; k < 256; k++) v->keys[k] = fg && (GetAsyncKeyState(k) & 0x8000) ? 1 : 0;
        { u32 nw = GetTickCount(); v->keys[0x0E] = fg && nw < g_wheel_up_until; v->keys[0x0F] = fg && nw < g_wheel_dn_until; }   /* the mouse wheel */
        if (!fg) mouse_release();
        if (g_cap_pending && !(GetAsyncKeyState(1) & 0x8000)) { g_cap_pending = 0; g_cap = 1; v->mcap = 1; }   /* captured once the click is let go */
        if (g_cap || g_cap_pending) { s32 p[2], c[2]; GetCursorPos(p); mouse_center(c); if (p[0] != c[0] || p[1] != c[1]) { if (g_cap) { v->mdx += p[0] - c[0]; v->mdy += p[1] - c[1]; } SetCursorPos(c[0], c[1]); } }
        if (g_gl) { u32 t0 = GetTickCount(); int any = 0; while (gl_pump() && GetTickCount() - t0 < 30) any = 1; extern void gl_account(u32 busy, int any); gl_account(GetTickCount() - t0, any); }
        else if (v->frame != last) { last = v->frame; paint(); }
        if (t % 30 == 0) { int same = 1, k = 0; for (; k < 127 && v->title[k]; k++) if (title[k] != v->title[k]) same = 0; if (title[k]) same = 0;
            if (!same && v->title[0]) { for (k = 0; k < 127 && v->title[k]; k++) title[k] = v->title[k]; title[k] = 0; SetWindowTextA(g_wnd, title); } }
        if (!g_gl) Sleep(8); else Sleep(0);
    }
}

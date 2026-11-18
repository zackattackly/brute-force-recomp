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
K32 DWORD WINAPI GetFileAttributesA(const char *);
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
        /* one pixel taller than the monitor: Windows then keeps compositing the window (an exact full-screen window is handed straight to
           the screen and tore without vsync); the compositor shows the newest finished frame at each refresh - triple buffering on any
           graphics card (bfgl.c presents without waiting). exactfull.txt = exactly the monitor's size. */
        s32 extra = GetFileAttributesA("exactfull.txt") != 0xFFFFFFFF ? 0 : 1;
        SetWindowPos(g_wnd, 0, mi.mon[0], mi.mon[1], mi.mon[2] - mi.mon[0], mi.mon[3] - mi.mon[1] + extra, 0x0020 | 0x0040 /* FRAMECHANGED, SHOWWINDOW */);
    } else {
        SetWindowLongA(g_wnd, -16, 0x10CF0000 /* overlapped, visible */);
        s32 *r = g_wrect; if (r[2] - r[0] < 200) { r[0] = 200; r[1] = 100; r[2] = r[0] + 1296; r[3] = r[1] + 759; }
        SetWindowPos(g_wnd, 0, r[0], r[1], r[2] - r[0], r[3] - r[1], 0x0020 | 0x0040);
    }
    g_full = on;
}
/* Milestone 103: a silver arrow cursor while the mouse is free (menus); a click on a menu option selects it (bfgl.c), a right
   click is Back. In gameplay a click captures the mouse for aiming as before; the pause menu (options on screen, the game not
   running) lets it go again. */
K32 void *WINAPI CreateIconIndirect(void *);
K32 void *WINAPI SetCursor(void *);
K32 void *WINAPI CreateBitmap(s32, s32, u32, u32, const void *);
K32 void *WINAPI CreateDIBSection(void *, const void *, u32, void **, void *, u32);
int gl_gameplay(void); int gl_menu_up(void); void gl_menu_click(s32 x, s32 y, int btn); void gl_menu_keys(u8 *keys); void gl_menu_hover(s32 x, s32 y);
static void *g_arrow; static int g_userfree;   /* g_userfree: F10 freed the mouse (no auto capture until a click) */
static int in_poly(float x, float y, const float *p, int n) { int c = 0; for (int i = 0, j = n - 1; i < n; j = i++) if (((p[i * 2 + 1] > y) != (p[j * 2 + 1] > y)) && (x < (p[j * 2] - p[i * 2]) * (y - p[i * 2 + 1]) / (p[j * 2 + 1] - p[i * 2 + 1]) + p[i * 2])) c = !c; return c; }
static void make_arrow(void) {   /* 32x32 with alpha: the classic arrow, silver (a vertical sheen), dark outline */
    static const float shape[14] = {0, 0, 0, 21, 5, 16, 9, 25, 12, 23.6f, 8, 15, 15, 15};
    float p[14]; for (int i = 0; i < 14; i++) p[i] = shape[i] * 1.12f + 1.0f;
    struct { u32 size; s32 w, h; u16 planes, bits; u32 comp, sz, xp, yp, used, imp; } bi = {40, 32, -32, 1, 32, 0, 0, 0, 0, 0, 0};
    u32 *px = 0; void *col = CreateDIBSection(0, &bi, 0, (void **)&px, 0, 0); if (!col || !px) return;
    static u8 inside[32][32];
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) inside[y][x] = (u8)in_poly(x + 0.5f, y + 0.5f, p, 7);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        u32 v = 0; int in = inside[y][x], edge = 0;
        for (int dy = -1; dy <= 1 && !in; dy++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, yy = y + dy; if (xx >= 0 && yy >= 0 && xx < 32 && yy < 32 && inside[yy][xx]) edge = 1; }
        if (in) { int bord = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= 32 || yy >= 32 || !inside[yy][xx]) bord = 1; }
            if (bord) v = 0xFF2A2D33; else { float t = (float)(x - y * 0.35f) / 12.0f; s32 l = (s32)(238 - 70 * (t < 0 ? 0 : t > 1 ? 1 : t) - y * 1.6f); if (x + y * 0.2f < 4.5f) l += 14; if (l > 250) l = 250; if (l < 120) l = 120;
                v = 0xFF000000u | (u32)l << 16 | (u32)(l + 3 > 255 ? 255 : l + 3) << 8 | (u32)(l + 10 > 255 ? 255 : l + 10); } }
        else if (edge) v = 0x60000000;   /* a soft shadow line */
        px[y * 32 + x] = v; }
    static u8 mask[32 * 4]; void *mb = CreateBitmap(32, 32, 1, 1, mask);
    struct { s32 icon; u32 hx, hy; void *mask, *color; } ii = {0, 1, 1, mb, col};
    g_arrow = CreateIconIndirect(&ii); }
static u32 g_wheel_up_until, g_wheel_dn_until;   /* Milestone 96: a wheel notch shows as a 120 ms press of key 0x0E (up) / 0x0F (down), codes Windows leaves unused */
static s32 WINAPI wndproc(void *h, u32 m, u32 wp, u32 lp) {
    if (m == 0x020A) { short d = (short)(wp >> 16); if (d > 0) g_wheel_up_until = GetTickCount() + 120; else if (d < 0) g_wheel_dn_until = GetTickCount() + 120; return 0; }   /* WM_MOUSEWHEEL */
    if (m == 0x0010) { mouse_release(); v->closed = 1; ExitProcess(0); }   /* WM_CLOSE: tell the loader, then end */
    if (m == 0x0201) {   /* WM_LBUTTONDOWN: a menu option (options on screen, not in play), else capture for aiming */
        g_userfree = 0;
        if (g_gl && !g_cap && gl_menu_up() && !gl_gameplay()) gl_menu_click((short)(lp & 0xFFFF), (short)(lp >> 16), 1); else mouse_capture(); }
    if (m == 0x0200 && g_gl && !g_cap && gl_menu_up() && !gl_gameplay()) gl_menu_hover((short)(lp & 0xFFFF), (short)(lp >> 16));   /* WM_MOUSEMOVE: highlight what the arrow is on */
    if (m == 0x0204 && g_gl && !g_cap && gl_menu_up() && !gl_gameplay()) gl_menu_click((short)(lp & 0xFFFF), (short)(lp >> 16), 2);   /* WM_RBUTTONDOWN: Back */
    if (m == 0x0020 && (lp & 0xFFFF) == 1 && g_arrow && !g_cap) { SetCursor(g_arrow); return 1; }   /* WM_SETCURSOR in the client area: the silver arrow */
    if (m == 0x0008) mouse_release();                                       /* WM_KILLFOCUS */
    if ((m == 0x0104 || m == 0x0100) && wp == 0x79) { mouse_release(); g_userfree = 1; return 0; }   /* F10 (a system key: no menu): free until the next click */
    if ((m == 0x0100 && wp == 0x7A) || (m == 0x0104 && wp == 0x0D && (lp & (1u << 29)))) { if (!(lp & (1u << 30))) set_full(!g_full); return 0; }   /* F11 / Alt+Enter: full screen on or off */
    if (m == 0x0100 && wp == 0x72 && !(lp & (1u << 30))) { extern volatile int g_perf_toggle; g_perf_toggle = 1; return 0; }   /* F3: the performance panel (bfgl.c) */
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
    make_arrow();   /* Milestone 103 */
    { void audio_start(void); audio_start(); }
    music_start();
    u32 last = 0xFFFFFFFF; char title[128]; title[0] = 0;
    for (u32 t = 0;; t++) {
        MSG msg; while (PeekMessageA(&msg, 0, 0, 0, 1)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (loader && WaitForSingleObject(loader, 0) == 0) ExitProcess(0);   /* the loader has ended */
        int fg = GetForegroundWindow() == g_wnd; v->focused = fg;
        for (int k = 0; k < 256; k++) if (k < 0x88 || k > 0x8B) v->keys[k] = fg && (GetAsyncKeyState(k) & 0x8000) ? 1 : 0;   /* 0x88..0x8B: the menu mouse's own keys (gl_menu_keys), never touched here: a held one flickering to 0 between the two writes read as several presses */
        { u32 nw = GetTickCount(); v->keys[0x0E] = fg && nw < g_wheel_up_until; v->keys[0x0F] = fg && nw < g_wheel_dn_until; }   /* the mouse wheel */
        if (!fg) mouse_release();
        if (g_gl) {   /* Milestone 103: menus - the mouse buttons are not game buttons there (clicks pick options); the pause menu frees the mouse */
            if (!g_cap) { v->keys[1] = v->keys[2] = v->keys[4] = 0; }
            if (g_cap && gl_menu_up() && !gl_gameplay()) mouse_release();
            /* auto capture (user): in play, with the window in front and no menu options on the screen, the mouse is taken for
               aiming without a click; F10 leaves it free until the next click in the window */
            if (fg && !g_cap && !g_cap_pending && !g_userfree && gl_gameplay() && !gl_menu_up() && !(GetAsyncKeyState(1) & 0x8000)) mouse_capture();
            { u8 mk[256]; mk[0x88] = mk[0x89] = mk[0x8A] = mk[0x8B] = 0; gl_menu_keys(mk); for (int k = 0x88; k <= 0x8B; k++) { u8 want = fg ? mk[k] : 0; if (v->keys[k] != want) v->keys[k] = want; } } }
        if (g_cap_pending && !(GetAsyncKeyState(1) & 0x8000)) { g_cap_pending = 0; g_cap = 1; v->mcap = 1; }   /* captured once the click is let go */
        if (g_cap || g_cap_pending) { s32 p[2], c[2]; GetCursorPos(p); mouse_center(c); if (p[0] != c[0] || p[1] != c[1]) { if (g_cap) { v->mdx += p[0] - c[0]; v->mdy += p[1] - c[1]; } SetCursorPos(c[0], c[1]); } }
        if (g_gl) { u32 t0 = GetTickCount(); int any = 0; while (gl_pump() && GetTickCount() - t0 < 30) any = 1; extern void gl_account(u32 busy, int any); gl_account(GetTickCount() - t0, any); }
        else if (v->frame != last) { last = v->frame; paint(); }
        if (t % 30 == 0) { int same = 1, k = 0; for (; k < 127 && v->title[k]; k++) if (title[k] != v->title[k]) same = 0; if (title[k]) same = 0;
            if (!same && v->title[0]) { for (k = 0; k < 127 && v->title[k]; k++) title[k] = v->title[k]; title[k] = 0; SetWindowTextA(g_wnd, title); } }
        if (!g_gl) Sleep(8); else Sleep(0);
    }
}

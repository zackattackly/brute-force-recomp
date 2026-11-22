/* Controller input (Milestone 77): the game's XInput functions (XAPI, on top of the Xbox USB stack) are replaced
 * with jumps to these, which read the PC's controller through xinput1_4.dll, with the keyboard as a fallback.
 * XGetDevices / XGetDeviceChanges stay the game's own: they only read the gamepad type's connected bitmasks at
 * 0x37ECD0 (current, changed, previous), which XInitDevices below sets to "port 1 connected".
 *
 * Keyboard (read by bfview.exe, only while its window is in front):
 *   arrows = d-pad, Enter = START, Backspace = BACK, Space = A, Esc = B, F = X, R = Y,
 *   W/A/S/D = left stick, I/J/K/L = right stick, Q / E = left / right trigger, Z = white, C = black,
 *   1 / 3 = left / right stick click.
 * Mouse (Milestone 87; click in the window to capture it, F10 or leaving the window releases it):
 *   movement turns the character directly, left button = fire, middle button = grenade, right button held = aim mode
 *   (the game's zoom, slower mouse). */
#include "util.h"

#define NTAPI __stdcall
#define GAMEPAD_TYPE 0x37ECD0
IMP void *WINAPI LoadLibraryA(const char *);
IMP void *WINAPI GetProcAddress(void *, const char *);
void *gpu_window(void);
IMP DWORD WINAPI GetFileAttributesA(const char *);
/* switch files (loader\*.txt) looked up at most once a second each: dozens of per-frame GetFileAttributesA calls on the OneDrive folder
   cost frames. Keyed by the name literal's address. */
static DWORD fattr_cached(const char *nm) {
    static const char *k[96]; static DWORD v[96]; static u32 t[96]; static int n; u32 now = GetTickCount();
    for (int i = 0; i < n; i++) if (k[i] == nm) { if (now - t[i] > 1000) { t[i] = now; v[i] = GetFileAttributesA(nm); } return v[i]; }
    DWORD r = GetFileAttributesA(nm); if (n < 96) { k[n] = nm; v[n] = r; t[n] = now; n++; } return r; }

typedef struct { u32 packet; u16 buttons; u8 lt, rt; short lx, ly, rx, ry; } PCSTATE;
typedef struct { u16 l, r; } PCVIB;
static u32 (WINAPI *pXInputGetState)(u32, PCSTATE *);
static u32 (WINAPI *pXInputSetState)(u32, PCVIB *);
static u32 g_packet, g_last_sig, g_logged;
u32 g_last_pad[3];   /* the last state given to the game: buttons, analog A B X Y, analog black white LT RT (for the reboot log) */
/* PC XInputGetState can take milliseconds (and longer with no controller), so a thread of its own polls it
   every 4 ms (every 2 s while nothing is connected) and the game reads the copy */
/* Milestone 86: the PC controller is polled by bfview.exe and handed over in the ring (pad[30] connected, pad[31..33]
   the XINPUT_GAMEPAD, pad[34] rumble back). Polled here, a check with no controller connected loaded helper DLLs, and
   every other thread of this process stayed suspended for ~1.5 s each time (every 2 s): the sound and picture stutter. */
u32 *gpu_pad(void);
extern void *g_player_char;
extern u32 g_mouse_on;   /* the mouse is aiming (set each XInputGetState; read by the aim-assist hooks below) */
static PCSTATE g_pc; static volatile int g_pc_ok;
static void pc_read(void) { u32 *pd = gpu_pad(); if (!pd || !pd[30]) { g_pc_ok = 0; return; } u32 w[3] = {pd[31], pd[32], pd[33]}; memcpy_(&g_pc.buttons, w, 12); g_pc_ok = 1; }

#pragma pack(push, 1)
typedef struct { u32 packet; u16 buttons; u8 analog[8]; short lx, ly, rx, ry; } XSTATE;   /* Xbox XINPUT_STATE */
#pragma pack(pop)

static void NTAPI XInitDevices_(u32 n, void *types) {
    u32 *t = (u32 *)GAMEPAD_TYPE; t[0] = 1; t[1] = 1; t[2] = 0;   /* port 1: connected, changed */
    out("  XInitDevices: gamepad on port 1"); out(" (PC controller via XInput in bfview.exe, keyboard fallback)\r\n");
}
static u32 NTAPI XInputOpen_(void *type, u32 port, u32 slot, void *params) { out("  XInputOpen port "); dec(port); out("\r\n"); return port == 0 ? 0x4A500000 + port : 0; }
static void NTAPI XInputClose_(u32 h) { }

int view_key(int vk);   /* gpu.c: the keyboard as bfview.exe sees it, while its window is in front */
static int key(int vk) { return view_key(vk); }
static short axis(int neg, int pos) { return (short)(pos ? 32767 : neg ? -32768 : 0); }
static int readable(const void *p, u32 n);
/* Milestone 96 probe (menuprobe.txt): where the menu system keeps the open menus. The menu system is [[0x469b58]+8]->vfunc 0x78
   (TriggerMenu_Open sends it message 0x15 = open menu by name). Every 2 s: each pointer field of it (and one level deeper)
   whose object holds a known menu name hash within its first 0x80 bytes is logged with the offsets. */
static const u32 g_menu_hashes[] = {0xf745f94a, 0xf6551df5, 0xf228042f, 0xfdbbd519, 0xf9728897, 0x1854a812, 0xec00d7fa, 0xe61ae053, 0x14761942, 0x16ddaa42, 0xf7b53d0d, 0xff959b89, 0xe13a3df2, 0xe3366b5d, 0x0130d4e8, 0x1ca7cb7b, 0xeff8bf10, 0xfa2e32b7, 0xebad668d, 0xe3cb03c9, 0xedfe1721, 0x03046cea, 0x105c24bb, 0xe37b5c35, 0x1a6884c5, 0xe5a73e1d, 0xfdcd29b4, 0xf3b06f04, 0x135e17fc, 0x15d8de40, 0xecf59471, 0x0d441c5c, 0x1f05fbd7, 0xe8e14fcf, 0x0ec41ef2, 0x0cfeaf6f, 0x14539172, 0xe7c4ccb0, 0x16043841, 0xf4059fd2, 0x07ba31d5, 0xe2a3c814, 0xf5750fe9, 0x1d6f7c4e, 0x12ba26a5, 0x01ccac63, 0xfdcf2801, 0xebb7c7d6, 0xf4fe7eb7, 0xe3976152, 0x0ae93fa4, 0xf2b3b861, 0x17b116b5, 0xfe55e38f, 0xe83f2e8a, 0xf0ea44dc, 0xebe31566, 0xe2e425f0, 0x0680b053, 0x0f8780c5, 0x148ed17f, 0x1d89e1e9, 0x0736fc78, 0x0e31ccee};
static int is_menu_hash(u32 v) { for (u32 i = 0; i < sizeof g_menu_hashes / 4; i++) if (g_menu_hashes[i] == v) return 1; return 0; }
u8 *menu_system(void) {
    u32 a = *(u32 *)0x469b58; if (!a || !readable((void *)(a + 8), 4)) return 0; u8 *ui = *(u8 **)(a + 8); if (!ui || !readable(ui, 4)) return 0;
    u32 *vt = *(u32 **)ui; if (!readable(vt + 0x78 / 4, 4)) return 0;
    return ((u8 *(__thiscall *)(void *))vt[0x78 / 4])(ui); }
/* Milestone 103 probe (menusel.txt): the top menu object (the list's last node +8) every 150 ms: its small whole numbers (< 64)
   that changed since the last look, and the same one level down (pointer fields to readable memory) -> the log */
void menu_sel_probe(void) {
    {   /* Milestone 103: the open menu's busy flag (+0x88: 1 while it animates a move, 0 when it takes the next press) for the
           viewer's mouse stepping: RING pad[57] = 1 + flag (0 = no menu) */
        u32 *pd = gpu_pad(); if (pd) { u32 st = 0; u8 *m = menu_system();
            if (m && readable(m + 8, 4)) { u8 *sen = *(u8 **)(m + 8); if (sen && readable(sen + 4, 4)) { u8 *node = *(u8 **)(sen + 4);
                if (node && node != sen && readable(node + 8, 4)) { u8 *mo = *(u8 **)(node + 8); if (mo && readable(mo + 0x88, 4)) st = 1 + (*(u32 *)(mo + 0x88) ? 1 : 0); } } }
            pd[57] = st; } }
    static u32 on = 2, tl; if (on == 2) on = fattr_cached("menusel.txt") != 0xFFFFFFFF; if (!on) return;
    u32 now = GetTickCount(); if (now - tl < 150) return; tl = now;
    u8 *m = menu_system(); if (!m || !readable(m + 8, 4)) return; u8 *sen = *(u8 **)(m + 8); if (!sen || !readable(sen + 4, 4)) return;
    u8 *node = *(u8 **)(sen + 4); if (!node || node == sen || !readable(node + 8, 4)) return; u8 *menu = *(u8 **)(node + 8); if (!menu || !readable(menu, 0x400)) return;
    static u8 *lastm; static u32 snap[0x100], sub[0x40][0x40]; static u32 subp[0x40]; static u32 nl;
    if (menu != lastm) { lastm = menu; out("  [menusel] menu "); hex((u32)menu); out(" name "); hex(*(u32 *)(menu + 0x24)); out("\r\n"); for (int i = 0; i < 0x100; i++) snap[i] = ((u32 *)menu)[i]; for (int i = 0; i < 0x40; i++) subp[i] = 0; return; }
    if (nl > 600) return;
    /* small numbers (< 64) and pointers (0x8xxxxxxx) that changed; floats (animations) are left out */
    #define PTRV(a) (((a) >= 0x00100000u && (a) < 0x04000000u) || ((a) >= 0x80000000u && (a) < 0x84000000u))   /* menu objects live at 0x06xxxxxx-style low addresses too */
    #define INTERESTING(a, b) (((a) < 64 && (b) < 64) || (PTRV(a) && PTRV(b)) || ((a) >= 0x04000000u && (a) < 0x08000000u && (b) >= 0x04000000u && (b) < 0x08000000u))
    for (int i = 0; i < 0x100; i++) { u32 v = ((u32 *)menu)[i]; if (v != snap[i] && INTERESTING(v, snap[i]) && i != 0x88 / 4) { nl++; out("  [menusel] +"); hex(i * 4); out(" "); hex(snap[i]); out(" -> "); hex(v); out("\r\n"); } snap[i] = v; }
    for (int i = 0; i < 0x40; i++) { u32 p = ((u32 *)menu)[i]; if (p < 0x00100000u || (p >= 0x08000000u && p < 0x80000000u) || p >= 0x84000000u || !readable((void *)p, 0x100)) continue;
        if (subp[i] != p) { subp[i] = p; for (int k = 0; k < 0x40; k++) sub[i][k] = ((u32 *)p)[k]; continue; }
        for (int k = 0; k < 0x40; k++) { u32 v = ((u32 *)p)[k]; if (v != sub[i][k] && INTERESTING(v, sub[i][k])) { nl++; out("  [menusel] +"); hex(i * 4); out("->+"); hex(k * 4); out(" "); hex(sub[i][k]); out(" -> "); hex(v); out("\r\n"); } sub[i][k] = v; } }
    #undef INTERESTING
    #undef PTRV
    }
static void menu_probe(void) {
    static u32 on = 2, tl, n; u32 nw = GetTickCount(); if (on == 2) on = fattr_cached("menuprobe.txt") != 0xFFFFFFFF; if (!on || nw - tl < 1000) return; tl = nw; n++;
    u8 *m = menu_system(); if (!m || !readable(m, 0x200)) { out("  [menuprobe] no menu system\r\n"); return; }
    {   /* the open menus: a list at system +4 (sentinel node [system+8]; nodes: +0 next, +4 prev, +8 menu object), pushed at the back by vfunc 0 */
        u8 *sen = *(u8 **)(m + 8); out("  [menulist]"); int k = 0;
        if (readable(sen, 12)) for (u8 *nd = *(u8 **)sen; nd && nd != sen && k < 8 && readable(nd, 12); nd = *(u8 **)nd, k++) { u8 *mo = *(u8 **)(nd + 8); out(" menu "); hex((u32)mo);
            if (readable(mo, 0x200)) for (u32 q = 0; q < 0x200; q += 4) if (is_menu_hash(*(u32 *)(mo + q))) { out(" +"); hex(q); out("="); hex(*(u32 *)(mo + q)); } }
        out("\r\n"); }
    out("  [menuprobe] system "); hex((u32)m); { u32 *vt = *(u32 **)m; out(" vtable "); hex((u32)vt); if (readable(vt, 0x40)) for (int k = 0; k < 16; k++) { out(" "); hex(vt[k]); } } out(":");
    for (u32 o = 4; o < 0x400; o += 4) { if (is_menu_hash(*(u32 *)(m + o))) { out(" +"); hex(o); out("="); hex(*(u32 *)(m + o)); }
        u8 *p = *(u8 **)(m + o); if ((u32)p < 0x01000000 || (u32)p > 0x7FFE0000u || !readable(p, 0x200)) continue;   /* heap only (not code / vtables) */
        for (u32 q = 0; q < 0x200; q += 4) if (is_menu_hash(*(u32 *)(p + q))) { out(" +"); hex(o); out("->+"); hex(q); out("="); hex(*(u32 *)(p + q)); }
        for (u32 q = 0; q < 0x100; q += 4) { u8 *p2 = *(u8 **)(p + q); if ((u32)p2 < 0x01000000 || (u32)p2 > 0x7FFE0000u || !readable(p2, 0x200)) continue;
            for (u32 r = 0; r < 0x200; r += 4) if (is_menu_hash(*(u32 *)(p2 + r))) { out(" +"); hex(o); out("->+"); hex(q); out("->+"); hex(r); out("="); hex(*(u32 *)(p2 + r)); } } }
    out("\r\n"); }
/* ---- Milestone 96: key bindings, and the KEY BINDINGS screen in the pause menu ----
   g_kb[] holds one key (Windows virtual-key code, mouse buttons included) per action; loader\keybinds.txt keeps them (one
   "code" per line, in action order). Enter is always START too, so the menus can be reached whatever is bound.
   The screen (menu KB_MENU, added to the menus file by tools\kbmenu_patch.py) is a copy of the Controls help panel: while it
   is the top menu, this code owns the keyboard (the game gets no input): Up/Down (or W/S) choose a line, Enter/Space waits for
   the new key (Esc cancels), Esc/Backspace leaves (the game gets an A press, which closes the panel). The list is drawn into the
   panel's message control (named KB_MSG) with the game's UIText_SetText (0x10A7D0: esi = control, edi = UTF-16 text). */
#define KB_MENU 0x4b42a002u
#define KB_MSG 0x4b42a005u
enum { KB_FWD, KB_BACKW, KB_LEFT, KB_RIGHT, KB_SPRINT, KB_A, KB_B, KB_X, KB_Y, KB_WHITE, KB_BLACK, KB_LT, KB_RT, KB_START, KB_BACKBTN,
       KB_DUP, KB_DDOWN, KB_DLEFT, KB_DRIGHT, KB_L3, KB_R3, KB_INTERACT, KB_N };
static int g_x_src;   /* what pressed X from the keyboard: 1 Reload, 2 Interact (0: nothing / the controller) */
static int interact_target(void);
static const char *const g_kb_label[KB_N] = {"Move forward", "Move back", "Move left", "Move right", "Sprint", "Jump / accept (A)",
    "Inventory / back (B)", "Reload (X)", "Switch weapon (Y)", "Special skill (White)", "Medkit (Black)", "Use item (left trigger)",
    "Fire (right trigger)", "Pause (START)", "Objectives (BACK)", "Squad: up (D-pad)", "Squad: down (D-pad)", "Squad: left (D-pad)",
    "Squad: right (D-pad)", "Left stick click", "Right stick click", "Interact / pick up"};
static const u8 g_kb_default[KB_N] = {'W', 'S', 'A', 'D', 0xA0, 0x20, 'V', 'F', 'R', 'Z', 'C', 'Q', 0x01, 0x1B, 0x08, 0x26, 0x28, 0x25, 0x27, '1', '3', 'E'};
static u8 g_kb[KB_N]; static int g_kb_loaded;
static void kb_load(void) {
    if (g_kb_loaded) return; g_kb_loaded = 1; for (int i = 0; i < KB_N; i++) g_kb[i] = g_kb_default[i];
    IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE); IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *); IMP BOOL WINAPI CloseHandle(HANDLE);
    HANDLE f = CreateFileA("keybinds.txt", 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return;
    char t[512]; DWORD n = 0; ReadFile(f, t, 511, &n, 0); CloseHandle(f); int k = 0, v = 0, any = 0;
    for (DWORD i = 0; i <= n && k < KB_N; i++) { char c = i < n ? t[i] : 10; if (c >= 0x30 && c <= 0x39) { v = v * 10 + c - 0x30; any = 1; } else if (c == 10) { if (any && v > 0 && v < 256) g_kb[k] = (u8)v; if (any || c == 10) k++; v = 0; any = 0; } }
    if (k < KB_N && g_kb[KB_RT] == 'E') g_kb[KB_RT] = 0x01;   /* a file from before Interact (E): its old default Fire on E moves to the left button */
    g_kb[KB_START] = 0x1B;   /* Milestone 97: Pause (START) is always Esc (user); it is not in the list (Enter is START too) */
    out("  [keybinds] loaded keybinds.txt\r\n"); }
static void kb_save(void) {
    IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE); IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *); IMP BOOL WINAPI CloseHandle(HANDLE);
    char t[1400]; u32 q = 0;
    for (int i = 0; i < KB_N; i++) { u32 v = g_kb[i]; char d[4]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) t[q++] = d[--z];
        t[q++] = 0x20; t[q++] = 0x23; t[q++] = 0x20; for (const char *l = g_kb_label[i]; *l; l++) t[q++] = *l; t[q++] = 13; t[q++] = 10; }
    HANDLE f = CreateFileA("keybinds.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (f == (HANDLE)-1) return; DWORD w; WriteFile(f, t, q, &w, 0); CloseHandle(f); }
static int kb_key(int a) { kb_load(); return g_kb[a] && key(g_kb[a]); }
static const char *kb_name(u32 vk, char *buf) {   /* a short readable name for a virtual-key code */
    static const struct { u8 vk; const char *n; } t[] = {{0x01, "Left mouse"}, {0x02, "Right mouse"}, {0x04, "Middle mouse"}, {0x05, "Mouse 4"}, {0x06, "Mouse 5"},
        {0x0E, "Wheel up"}, {0x0F, "Wheel down"},   /* from bfview.exe: a wheel notch is a short press of these */
        {0x08, "Backspace"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x10, "Shift"}, {0x11, "Ctrl"}, {0x12, "Alt"}, {0x14, "Caps Lock"}, {0x1B, "Esc"}, {0x20, "Space"},
        {0x21, "Page Up"}, {0x22, "Page Down"}, {0x23, "End"}, {0x24, "Home"}, {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"}, {0x28, "Down"}, {0x2D, "Insert"}, {0x2E, "Delete"},
        {0xA0, "Left Shift"}, {0xA1, "Right Shift"}, {0xA2, "Left Ctrl"}, {0xA3, "Right Ctrl"}, {0xA4, "Left Alt"}, {0xA5, "Right Alt"},
        {0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, "-"}, {0xBE, "."}, {0xBF, "/"}, {0xC0, "`"}, {0xDB, "["}, {0xDC, "\\"}, {0xDD, "]"}, {0xDE, "'"}};
    for (u32 i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i].vk == vk) return t[i].n;
    if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A)) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= 0x60 && vk <= 0x69) { buf[0] = 'N'; buf[1] = 'u'; buf[2] = 'm'; buf[3] = ' '; buf[4] = (char)(0x30 + vk - 0x60); buf[5] = 0; return buf; }
    if (vk >= 0x70 && vk <= 0x7B) { u32 f = vk - 0x6F; buf[0] = 'F'; if (f >= 10) { buf[1] = '1'; buf[2] = (char)(0x30 + f - 10); buf[3] = 0; } else { buf[1] = (char)(0x30 + f); buf[2] = 0; } return buf; }
    if (!vk) return "(none)";
    buf[0] = 'K'; buf[1] = 'e'; buf[2] = 'y'; buf[3] = ' '; { u32 v = vk; buf[4] = (char)(0x30 + v / 100); buf[5] = (char)(0x30 + v / 10 % 10); buf[6] = (char)(0x30 + v % 10); buf[7] = 0; } return buf; }
u8 *menu_system(void);
static u8 *menu_top(void) {   /* the top (most recently opened) menu object, or 0; its name hash is at +0x24 */
    u8 *m = menu_system(); if (!m || !readable(m + 8, 4)) return 0; u8 *sen = *(u8 **)(m + 8); if (!sen || !readable(sen, 12)) return 0;
    u8 *nd = *(u8 **)(sen + 4); if (!nd || nd == sen || !readable(nd, 12)) return 0; u8 *mo = *(u8 **)(nd + 8); return mo && readable(mo, 0x28) ? mo : 0; }
static int kb_text_ok(u8 *p) {   /* the control's text (string at +0x60) still reads "Load" (the panel's starting text): inline or through a pointer */
    if (!readable(p + 0x60, 0x10)) return 0;
    for (u32 o = 0x60; o < 0x70; o += 2) { u16 *w = (u16 *)(p + o); if (w[0] == 'L' && w[1] == 'o' && w[2] == 'a' && w[3] == 'd') return 1; }
    for (u32 o = 0x60; o < 0x70; o += 4) { u16 *w = *(u16 **)(p + o); if ((u32)w > 0x10000 && (u32)w < 0x7FFE0000u && readable(w, 8) && w[0] == 'L' && w[1] == 'o' && w[2] == 'a' && w[3] == 'd') return 1; }
    return 0; }
static void kb_dump(u8 *p) {   /* probe: where a control keeps its text - inline UTF-16 "Load" or a pointer to it, within 0x200 bytes; and its vtable */
    out("  [keybinds] control "); hex((u32)p); out(" vtable "); hex(*(u32 *)p); out(" text:");
    for (u32 o = 0; o < 0x200; o += 2) { if (!readable(p + o, 8)) break; u16 *w = (u16 *)(p + o); if (w[0] == 'L' && w[1] == 'o' && w[2] == 'a' && w[3] == 'd') { out(" inline +"); hex(o); } }
    for (u32 o = 0; o < 0x200; o += 4) { if (!readable(p + o, 4)) break; u16 *w = *(u16 **)(p + o); if ((u32)w > 0x10000 && (u32)w < 0x7FFE0000u && readable(w, 8) && w[0] == 'L' && w[1] == 'o' && w[2] == 'a' && w[3] == 'd') { out(" ptr at +"); hex(o); }
        u8 *q = *(u8 **)(p + o); if ((u32)q > 0x01000000 && (u32)q < 0x7FFE0000u && readable(q, 0x40)) for (u32 r = 0; r < 0x40; r += 4) { u16 *w2 = *(u16 **)(q + r); if ((u32)w2 > 0x10000 && (u32)w2 < 0x7FFE0000u && readable(w2, 8) && w2[0] == 'L' && w2[1] == 'o' && w2[2] == 'a' && w2[3] == 'd') { out(" ptr at +"); hex(o); out("->+"); hex(r); } } }
    out("\r\n"); }
static u8 *kb_find_msg(u8 *mo) {   /* the message control of the open screen: an object reachable from the menu that holds KB_MSG */
    for (u32 o = 0; o < 0x400; o += 4) { if (!readable(mo + o, 4)) break; u8 *p = *(u8 **)(mo + o); if ((u32)p < 0x01000000 || (u32)p > 0x7FFE0000u || !readable(p, 0x100)) continue;
        for (u32 q = 0; q < 0x100; q += 4) if (*(u32 *)(p + q) == KB_MSG) { int ok = kb_text_ok(p); out("  [keybinds] message control at menu+"); hex(o); out(" (name at +"); hex(q); out(ok ? ") text found\r\n" : ") text NOT found\r\n"); if (ok) return p; }
        for (u32 q = 0; q < 0x100; q += 4) { u8 *p2 = *(u8 **)(p + q); if ((u32)p2 < 0x01000000 || (u32)p2 > 0x7FFE0000u || !readable(p2, 0x100)) continue;
            for (u32 r = 0; r < 0x100; r += 4) if (*(u32 *)(p2 + r) == KB_MSG) { int ok = kb_text_ok(p2); kb_dump(p2); out("  [keybinds] message control at menu+"); hex(o); out("->+"); hex(q); out(" (name at +"); hex(r); out(ok ? ") text found\r\n" : ") text NOT found\r\n"); if (ok) return p2; } } }
    return 0; }
static void ui_set_text(u8 *ctl, const u16 *text) {
    u32 fn = 0x10A7D0;
    __asm__ volatile("pushl %%ebp\n\t pushl %%ebx\n\t call *%%eax\n\t popl %%ebx\n\t popl %%ebp" : "+S"(ctl), "+D"(text), "+a"(fn) : : "ecx", "edx", "memory", "cc"); }
/* the screen's text lives in the loaded string table (the control holds no copy; it is looked up when drawn): every UTF-16
   copy of the starting text "Loading key bindings..." (1223 characters, padded with spaces by kbmenu_patch.py) is found once by
   scanning the game's memory and then overwritten in place, padded with spaces to the same length */
#define KB_BODY_LEN 1223
#define KB_NCOPY 16
static u16 *g_kb_str[KB_NCOPY]; static int g_kb_nstr = -1; static u32 g_kb_cap[KB_NCOPY];
static u16 g_kb_w[1600];   /* the text we build (its own copy must not be mistaken for the game's) */
typedef struct { u32 base, abase, aprot; u32 size, state, prot, type; } MBI_;
IMP u32 WINAPI VirtualQuery(const void *, void *, u32);
static int kb_match16(const u8 *p, const char *pat) { for (int k = 0; pat[k]; k++) if (p[2 * k] != (u8)pat[k] || p[2 * k + 1]) return 0; return 1; }
static void kb_find_strings(void) {   /* every committed readable region of the process; UTF-16 copies are used, narrow ones only logged.
       The text starts with the panel's original words, or with one of our own list headers once it has been written. */
    static const char pat[] = "Loading key bindings", pat2[] = "Up/Down: choose", pat3[] = "Press the new key"; g_kb_nstr = 0; u32 nnarrow = 0;
    for (u32 a = 0x10000; a < 0xF0000000u; ) {
        MBI_ m; if (!VirtualQuery((void *)a, &m, sizeof m)) break; u32 next = m.base + m.size; if (next <= a) break;
        if (m.state == 0x1000 && (m.prot & 0xEE) && !(m.prot & 0x101)) {   /* committed; readable; not guard / no-access */
            u8 *p = (u8 *)m.base; u32 n = m.size;
            for (u32 i = 0; i + 48 < n; i++) if ((p[i] == 'L' || p[i] == 'U' || p[i] == 'P') && (p[i + 2] == 'a' || (p[i + 1] == 0 && (p[i + 2] == 'o' || p[i + 2] == 'p' || p[i + 2] == 'r')))) {   /* UTF-16 "Lo"/"Up"/"Pr" or narrow "Loa" */
                if (p[i + 1] == 0) { int ok = kb_match16(p + i, pat) || kb_match16(p + i, pat2) || kb_match16(p + i, pat3);
                    /* Milestone 97: only the full padded buffer (the string table's): inside the Xbox's memory, a 0 right after
                       character 1223 is not required (unknown layout), but spaces just before it are. A shorter copy the game makes to draw the text was also matched;
                       writing 1223 characters into it overran it (crash) and showed both texts at once (double overlay). */
                    /* each copy is written only up to its own end (its 0): the copy the menu draws from is made when it opens and
                       can be shorter than the string table's padded one */
                    u16 *d = (u16 *)(p + i); u32 da = (u32)d, cap = 0;
                    if (ok && (da < 0x10000 || da >= 0x84000000u || (i & 1))) ok = 0;
                    if (ok && da >= (u32)g_kb_w && da < (u32)(g_kb_w + 1600)) ok = 0;   /* our own text buffer */
                    if (ok) { while (cap < KB_BODY_LEN && i + cap * 2 + 2 <= n && d[cap]) cap++; if (cap < 40) ok = 0; }
                    if (ok && g_kb_nstr < KB_NCOPY) { g_kb_cap[g_kb_nstr] = cap; g_kb_str[g_kb_nstr++] = d; out("  [keybinds] screen text (UTF-16) at "); hex(da); out(", "); dec(cap); out(" characters\r\n"); } }
            } else if (p[i] == 'L' && p[i + 1] == 'o' && p[i + 2] == 'a' && 0) {   /* narrow copies: not logged any more */ int ok = 1; for (int k = 0; pat[k]; k++) if (p[i + k] != (u8)pat[k]) { ok = 0; break; } if (ok && nnarrow++ < 4) { out("  [keybinds] narrow copy at "); hex((u32)(p + i)); out("\r\n"); } } }
        a = next; }
    if (!g_kb_nstr) out("  [keybinds] screen text not found in memory\r\n"); }
static int kb_write_text(const u16 *t) {   /* writes every copy that still starts with one of our texts; returns how many */
    int done = 0;
    for (int s = 0; s < g_kb_nstr; s++) { u16 *d = g_kb_str[s]; u32 cap = g_kb_cap[s]; if (!readable(d, cap * 2)) continue;
        if (!kb_match16((u8 *)d, "Loading key bindings") && !kb_match16((u8 *)d, "Up/Down: choose") && !kb_match16((u8 *)d, "Press the new key")) continue;
        u32 i = 0; for (; t[i] && i < cap; i++) d[i] = t[i]; for (; i < cap; i++) d[i] = ' '; done++; }   /* never past the copy's own length */
    return done; }
static int g_kb_open;   /* our screen is the top menu: the game gets no keyboard input */
static void kb_screen(u16 *pb, u8 *an, XSTATE *s) {
    static u8 *ctl, *last_mo; static int cur, waiting, dirty, closing_t; static u8 prev[256], need_release; static u32 close_until;
    u8 *mo = menu_top(); u32 top = mo ? *(u32 *)(mo + 0x24) : 0; u32 now = GetTickCount();
    if (now < close_until) { an[0] = 255; *pb = 0; return; }   /* the A press that closes the panel */
    if (top != KB_MENU) {
        /* Milestone 97: on closing, the original one-line text goes back into the string table. The menu splits its text into one
           buffer per line when it opens: with our list (several lines) in the table, the next opening had a buffer per line and
           only the first could be found, so the screen did not change any more. With one line, every opening is like the first. */
        if (g_kb_open && g_kb_nstr > 0) { static const char orig[] = "Loading key bindings..."; u16 t[32]; int k = 0; for (; orig[k]; k++) t[k] = (u8)orig[k]; t[k] = 0;
            int m = 0; for (int s = 0; s < g_kb_nstr; s++) if (g_kb_cap[s] == KB_BODY_LEN) { g_kb_str[m] = g_kb_str[s]; g_kb_cap[m] = g_kb_cap[s]; m++; } g_kb_nstr = m;   /* full-length copies only: the menu's per-line pieces are freed when it closes */
            kb_write_text(t); out("  [keybinds] screen closed: text reset\r\n"); }
        g_kb_open = 0; last_mo = 0; ctl = 0; waiting = 0; return; }
    kb_load();
    if (mo != last_mo) { last_mo = mo; ctl = 0; kb_find_strings(); cur = 0; waiting = 0; dirty = 1; need_release = 1; (void)kb_find_msg; }   /* found again at every opening: the menu's own copies are made anew */
    g_kb_open = 1;
    u8 down[256]; int any = 0; for (int k = 1; k < 256; k++) { down[k] = (u8)key(k); if (down[k]) any = 1; }
    if (need_release) { if (!any) need_release = 0; for (int k = 0; k < 256; k++) prev[k] = down[k]; *pb = 0; for (int i = 0; i < 8; i++) an[i] = 0; s->lx = s->ly = s->rx = s->ry = 0; return; }
    #define PRESSED(k) (down[k] && !prev[k])
    if (waiting) {
        if (PRESSED(0x1B)) { waiting = 0; dirty = 1; }
        else for (int k = 1; k < 256; k++) if (PRESSED(k) && k != 0x10 && k != 0x11 && k != 0x12) {   /* the plain Shift/Ctrl/Alt codes come with the left/right ones */
            g_kb[cur] = (u8)k; waiting = 0; dirty = 1; kb_save(); out("  [keybinds] "); out(g_kb_label[cur]); out(" -> "); { char nb[16]; out(kb_name(k, nb)); } out("\r\n"); break; }
    } else {
        if (PRESSED(0x26) || PRESSED('W')) { cur = cur > 0 ? cur - 1 : KB_N - 1; if (cur == KB_START) cur--; dirty = 1; }   /* Pause (START) is skipped: always Esc */
        if (PRESSED(0x28) || PRESSED('S')) { cur = cur < KB_N - 1 ? cur + 1 : 0; if (cur == KB_START) cur++; dirty = 1; }
        if ((PRESSED(0x0D) || PRESSED(0x20)) && g_kb_nstr > 0) { waiting = 1; dirty = 1; }   /* never rebind blind: only while the list can be shown */
        if (PRESSED(0x1B) || PRESSED(0x08)) { close_until = now + 150; }
        if (PRESSED('R') && down[0xA2]) { for (int i = 0; i < KB_N; i++) g_kb[i] = g_kb_default[i]; kb_save(); dirty = 1; }   /* Left Ctrl + R: defaults */
    }
    for (int k = 0; k < 256; k++) prev[k] = down[k];
    *pb = 0; for (int i = 0; i < 8; i++) an[i] = 0; s->lx = s->ly = s->rx = s->ry = 0;
    if (dirty && g_kb_nstr > 0) { dirty = 0;   /* the list: 13 lines around the cursor */
        u16 *w = g_kb_w; u32 q = 0; char nb[16];
        #define PUTS(str) { for (const char *c_ = (str); *c_ && q < 1590; c_++) w[q++] = (u8)*c_; }
        if (waiting) { PUTS("Press the new key for: ") PUTS(g_kb_label[cur]) PUTS("\n(Esc cancels)\n\n") }
        else { PUTS("Up/Down: choose   Enter: change   Esc: back\nLeft Ctrl + R: defaults\n\n") }
        int vis[KB_N], nv = 0, vc = 0; for (int i = 0; i < KB_N; i++) if (i != KB_START) { if (i == cur) vc = nv; vis[nv++] = i; }   /* the rows shown: all but Pause (START) */
        int first = vc - 6; if (first < 0) first = 0; if (first > nv - 13) first = nv - 13;
        for (int r = first; r < first + 13 && r < nv; r++) { int i = vis[r]; PUTS(i == cur ? "> " : "   ") PUTS(g_kb_label[i]) PUTS(":  ") PUTS(i == cur && waiting ? "..." : kb_name(g_kb[i], nb)) PUTS("\n") }
        w[q] = 0;
        if (!kb_write_text(w)) { kb_find_strings(); if (!kb_write_text(w)) { g_kb_nstr = 0; waiting = 0; out("  [keybinds] no copy of the screen text left to write\r\n"); } }
        (void)ui_set_text; (void)ctl; }
}
static u32 NTAPI XInputGetState_(u32 h, XSTATE *s) {
    menu_probe();
    pc_read(); PCSTATE p = g_pc; int have = g_pc_ok;   /* from the viewer */
    u16 b = 0; u8 an[8] = {0};
    if (have) {
        b = p.buttons & 0xFF;   /* d-pad, START, BACK, stick clicks: the same bits */
        an[0] = (p.buttons & 0x1000) ? 255 : 0; an[1] = (p.buttons & 0x2000) ? 255 : 0;
        an[2] = (p.buttons & 0x4000) ? 255 : 0; an[3] = (p.buttons & 0x8000) ? 255 : 0;
        an[4] = (p.buttons & 0x0200) ? 255 : 0;   /* black = right shoulder */
        an[5] = (p.buttons & 0x0100) ? 255 : 0;   /* white = left shoulder */
        an[6] = p.lt; an[7] = p.rt;
    }
    s->lx = p.lx; s->ly = p.ly; s->rx = p.rx; s->ry = p.ry;
    { void menu_sel_probe(void); menu_sel_probe(); }   /* Milestone 103 probe (menusel.txt) */
    {   /* the keyboard, through the key bindings (Milestone 96); Enter is always START as well */
        if (kb_key(KB_DUP)) b |= 1; if (kb_key(KB_DDOWN)) b |= 2; if (kb_key(KB_DLEFT)) b |= 4; if (kb_key(KB_DRIGHT)) b |= 8;
        /* Esc: START (pause) in gameplay, B (Back) in the menus (user). Gameplay = the camera heartbeat pad[55] seen in the last 0.3 s */
        static u32 esc_mode; int esc = key(0x1B), play = 0; { u32 *pd = gpu_pad(); if (pd && pd[55]) play = (s32)(GetTickCount() - pd[55]) < 300; }
        if (!esc) esc_mode = 0; else if (!esc_mode) esc_mode = play ? 1 : 2;   /* decided when pressed, kept while held */
        if (esc_mode == 1 || key(0x0D)) b |= 0x10; if (kb_key(KB_BACKBTN)) b |= 0x20; if (kb_key(KB_L3)) b |= 0x40; if (kb_key(KB_R3)) b |= 0x80;
        if (kb_key(KB_A)) an[0] = 255; if (kb_key(KB_B) || esc_mode == 2) an[1] = 255;
        /* Milestone 103: menu clicks (bfgl.c menu_tick): the viewer presses private key codes 0x88 up, 0x89 down, 0x8A A, 0x8B B */
        if (key(0x88)) b |= 1; if (key(0x89)) b |= 2; if (key(0x8A)) an[0] = 255; if (key(0x8B)) an[1] = 255; if (kb_key(KB_Y)) an[3] = 255;
        {   /* X does both reload and pick up / use (the game picks up when the reticle's object can be). Reload presses X with that
               object hidden from the game, so it always reloads; Interact (E by default) presses X with it kept, and only when there is one */
            int r = kb_key(KB_X), e = kb_key(KB_INTERACT) && interact_target(); if (r || e) an[2] = 255; g_x_src = r ? 1 : e ? 2 : 0; }
        if (kb_key(KB_BLACK)) an[4] = 255; if (kb_key(KB_WHITE)) an[5] = 255; if (kb_key(KB_LT)) an[6] = 255; if (kb_key(KB_RT)) an[7] = 255;
        if (kb_key(KB_LEFT) || kb_key(KB_RIGHT)) s->lx = axis(kb_key(KB_LEFT), kb_key(KB_RIGHT)); if (kb_key(KB_FWD) || kb_key(KB_BACKW)) s->ly = axis(kb_key(KB_BACKW), kb_key(KB_FWD));
        if (key('J') || key('L')) s->rx = axis(key('J'), key('L')); if (key('I') || key('K')) s->ry = axis(key('K'), key('I'));
    }
    {   /* the mouse (Milestone 87): its speed is the right stick (aim), left button = right trigger (fire), right button =
           left trigger (grenade). Speed is measured over at least 15 ms so the game's several reads per frame agree; a
           moving mouse starts past the game's dead zone so slow movements still turn. */
        int view_mouse(s32 *, s32 *); s32 mx, my;
        static s32 sens = -1;   /* loader\mouse_sens.txt: a number, default 50 (higher = faster) */
        if (sens < 0) { IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE); IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *); IMP BOOL WINAPI CloseHandle(HANDLE);
            sens = 50; HANDLE f = CreateFileA("mouse_sens.txt", 0x80000000, 1, 0, 3, 0x80, 0);
            if (f != (HANDLE)-1) { char t[16]; DWORD n = 0; ReadFile(f, t, 15, &n, 0); CloseHandle(f); s32 v = 0, any = 0; for (DWORD k = 0; k < n; k++) if (t[k] >= 0x30 && t[k] <= 0x39) { v = v * 10 + t[k] - 0x30; any = 1; } else if (any) break; if (any && v > 0) sens = v; }
            out("  mouse sensitivity "); dec(sens); out("\r\n"); }
        static s32 lx, ly, init; static s64 lt; static short srx, sry;
        g_mouse_on = view_mouse(&mx, &my);
        if (g_mouse_on) {
            s64 now; QueryPerformanceCounter(&now); static s64 qf; if (!qf) QueryPerformanceFrequency(&qf);
            if (!init) { init = 1; lx = mx; ly = my; lt = now; }
            s64 us = (now - lt) * 1000000 / qf;
            if (us >= 15000) { s32 vx = (s32)((s64)(mx - lx) * 1000000 / us), vy = (s32)((s64)(my - ly) * 1000000 / us);   /* pixels per second */
                lx = mx; ly = my; lt = now;
                #define STICK(v) (short)((v) == 0 ? 0 : (v) > 0 ? ((v) * sens + 7000 > 32767 ? 32767 : (v) * sens + 7000) : ((v) * sens - 7000 < -32767 ? -32767 : (v) * sens - 7000))
                srx = STICK(vx); sry = STICK(-vy); }
            (void)srx; (void)sry; extern s32 g_mouse_sens; g_mouse_sens = sens;   /* aiming is direct now (mouse_apply), not through the stick */
            if (key(0x01)) an[7] = 255;   /* left button: fire */
            if (key(0x04)) an[6] = 255;   /* middle button: grenade (right button = aim mode, in mouse_apply) */
        } else { init = 0; srx = sry = 0; }
        {   /* aim-rate probe (Milestone 87 diagnosis): every 0.5 s while the mouse moves: pixels moved, how far the player
               turned (milliradians, from the facing matrix), and whether a grenade (left trigger) was thrown */
            static float pfx, pfz, yaw; static s32 px0, have, nlog, lt; static u32 t0;
            u8 *c = (u8 *)g_player_char; if (an[6]) lt = 1;
            if (c && g_mouse_on) { float fx = *(float *)(c + 0x5BC + 24), fz = *(float *)(c + 0x5BC + 32);
                if (have) yaw += pfx * fz - pfz * fx; pfx = fx; pfz = fz;
                u32 t = GetTickCount(); if (!have) { have = 1; t0 = t; px0 = mx; yaw = 0; }
                if (t - t0 >= 500) { s32 dpx = mx - px0;
                    if ((dpx > 40 || dpx < -40) && nlog < 400) { nlog++; s32 my_ = (s32)(yaw * 1000); out("  [aimrate] px "); dec((u32)(dpx < 0 ? -dpx : dpx)); out(" turned mrad "); dec((u32)(my_ < 0 ? -my_ : my_));
                        out(" per 100px "); dec((u32)((my_ < 0 ? -my_ : my_) * 100 / (dpx < 0 ? -dpx : dpx))); out(lt ? " GRENADE\r\n" : "\r\n"); }
                    t0 = t; px0 = mx; yaw = 0; lt = 0; } }
            else have = 0; }
    }
    {   /* mousetest.txt (Milestone 87 calibration): with a player, 4 phases of 4 s: stick right, simulated mouse right, stick
           up, simulated mouse up; the log shows the signed turn (facing) and pitch change of each phase */
        static u32 on = 2, t0; static float pfx, pfz, yaw, p0; static s32 ph = -1; extern u32 g_mt_active;
        if (on == 2) on = fattr_cached("mousetest.txt") != 0xFFFFFFFF;
        u8 *c = (u8 *)g_player_char; extern volatile s32 g_fake_dx, g_fake_dy;
        if (on && c) { u32 t = GetTickCount(); float fx = *(float *)(c + 0x5BC + 24), fz = *(float *)(c + 0x5BC + 32);
            if (ph < 0) { ph = 0; t0 = t; yaw = 0; p0 = *(float *)(c + 0x5F4); }
            else yaw += pfx * fz - pfz * fx;
            pfx = fx; pfz = fz;
            g_mt_active = ph < 4;
            if (ph < 4) {
                if (ph == 0) s->rx = 20000; if (ph == 1) { g_mouse_on = 1; g_fake_dx += 40; } if (ph == 2) s->ry = 20000; if (ph == 3) { g_mouse_on = 1; g_fake_dy -= 3; }
                if (t - t0 > (ph == 1 ? 12000u : 4000u)) { s32 y = (s32)(yaw * 1000), pd = (s32)((*(float *)(c + 0x5F4) - p0) * 1000);
                    out("  [mousetest] phase "); dec(ph); out(ph == 0 ? " stick right" : ph == 1 ? " mouse right" : ph == 2 ? " stick up" : " mouse up");
                    out(": turn mrad "); out(y < 0 ? "-" : "+"); dec((u32)(y < 0 ? -y : y)); out(" pitch mrad "); out(pd < 0 ? "-" : "+"); dec((u32)(pd < 0 ? -pd : pd)); out("\r\n");
                    ph++; t0 = t; yaw = 0; p0 = *(float *)(c + 0x5F4); } } } }
    {   /* speedtest.txt (Milestone 87, sprint): with a player, 6 s of full forward stick; every 0.5 s the measured ground
           speed and the character's float fields between 2 and 12 */
        static u32 on = 2, t0, tl, n; static float px, pz; extern u32 g_mt_active;
        if (on == 2) on = fattr_cached("speedtest.txt") != 0xFFFFFFFF;
        u8 *c = (u8 *)g_player_char;
        (void)t0;
        if (on && c && n < 60) { u32 t = GetTickCount(); if (!tl) tl = t;   /* passive: logs while the player moves */
            float *p = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c) + 9;
            if (t - tl >= 500) { float dx = p[0] - px, dz = p[2] - pz; s32 sp = (s32)(dx * dx + dz * dz > 0 ? 1 : 0); (void)sp;
                float d2 = dx * dx + dz * dz, sq = d2; for (int it = 0; it < 12; it++) sq = sq > 0 ? 0.5f * (sq + d2 / sq) : 0; if (d2 == 0) sq = 0;
                s32 v = (s32)(sq / ((float)(t - tl) / 1000.0f) * 100); if (v > 50 && v < 3000) { out("  [speed] ground x100 "); dec((u32)v); out(" motion "); hex(*(u32 *)(c + 0x1FC)); out(" fields:");
                for (u32 o = 0x100; o < 0x860; o += 4) { float f = *(float *)(c + o); if (f > 0.3f && f < 12.0f) { s32 w = (s32)(f * 100); out(" "); hex(o); out("="); dec((u32)w); } }
                out("\r\n"); n++; } tl = t; px = p[0]; pz = p[2]; } } }
    { extern u32 g_seq_on; void seq_add(u32); if (g_seq_on) seq_add(2); }
    {   extern u32 g_sprinting, g_aiming; int moving = s->ly > 8000;   /* Milestone 95: sprint only forward (diagonals forward too), not back or sideways */
        {   /* Milestone 93: shooting (left button) also stops the sprint, and keeps it off for 0.5 s after the last shot */
            int view_key(int); static u32 tshot; if (g_mouse_on && view_key(0x01)) tshot = GetTickCount();
            int shooting = tshot && GetTickCount() - tshot < 500;
            { int lock_mode_ext(void); g_sprinting = kb_key(KB_SPRINT) && moving && !(g_mouse_on && g_aiming) && !shooting && !lock_mode_ext(); } } }   /* no sprint during a special (it shook the lock-on view) */
    kb_screen(&b, an, s);   /* Milestone 96: the KEY BINDINGS screen owns the keyboard while it is open */
    {   /* testing aid: while loader\auto_press.txt exists, START + A are tapped for 0.2 s every 4 s */
        static u32 t_chk, on; u32 t = GetTickCount();
        if (t - t_chk > 1000) { t_chk = t; on = fattr_cached("auto_press.txt") != 0xFFFFFFFF; }
        extern u32 g_mt_active; if (on && t % 4000 < 200 && !g_mt_active) { b |= 0x10; an[0] = 255; }
    }
    s->buttons = b; for (u32 i = 0; i < 8; i++) s->analog[i] = an[i];
    {   extern float g_pose_t; if (g_pose_t > 0.1f) an[7] = 0; }
    {   extern u32 g_switch_tick; if (an[3]) g_switch_tick = GetTickCount(); }   /* Y / R = weapon switch: the sprint pose pauses for its reach animation */   /* Milestone 93: no shot until the sprint pose has eased out (both hands back on the gun) */
    { extern u32 g_last_pad[3]; g_last_pad[0] = b; g_last_pad[1] = an[0] | an[1] << 8 | an[2] << 16 | (u32)an[3] << 24; g_last_pad[2] = an[4] | an[5] << 8 | an[6] << 16 | (u32)an[7] << 24; }
    u32 sig = b ^ (an[0] | an[1] << 8 | an[2] << 16 | (u32)an[3] << 24) ^ (an[4] + an[5] * 3 + an[6] * 7 + an[7] * 11) ^ ((u16)s->lx | (u32)(u16)s->ly << 16) ^ ((u16)s->rx * 5 + (u32)(u16)s->ry * 9);
    if (sig != g_last_sig) { g_last_sig = sig; g_packet++; if (g_logged < 40 && (b || an[0] || an[1])) { g_logged++; out("  input: buttons "); hex(b); out(" A "); dec(an[0]); out(" B "); dec(an[1]); out("\r\n"); } }
    s->packet = g_packet;
    return 0;
}
static u32 NTAPI XInputSetState_(u32 h, u8 *fb) {
    /* XINPUT_FEEDBACK: header { status, event, 58 reserved } then the rumble speeds (left, right) */
    u16 l = *(u16 *)(fb + 0x42), r = *(u16 *)(fb + 0x44);
    { u32 *pd = gpu_pad(); if (pd) pd[34] = l | (u32)r << 16; }   /* the viewer applies it */
    *(u32 *)fb = 0;
    HANDLE ev = *(HANDLE *)(fb + 4); if (ev) SetEvent(ev);
    return 0;
}

static int jmp_to(u32 at, void *fn, u8 first) {
    u8 *p = (u8 *)at;
    if (p[0] != first) { out("  input patch skipped (bytes differ) at "); hex(at); out("\r\n"); return 0; }
    p[0] = 0xE9; *(u32 *)(p + 1) = (u32)fn - (at + 5); return 1;
}
/* the game's clock (Milestone 85): XAPI's QueryPerformanceCounter is a bare rdtsc and QueryPerformanceFrequency
   says 733,333,333 Hz (the Xbox CPU). The PC's time-stamp counter runs several times faster, so movies, sound and
   the splash screen ran ~5x too fast. Both rdtsc entry points now return the PC's timer scaled to 733 MHz. */
IMP BOOL WINAPI QueryPerformanceCounter(s64 *);
IMP BOOL WINAPI QueryPerformanceFrequency(s64 *);
u64 bf_xbox_tsc(void) {
    static s64 f; s64 c; if (!f) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (u64)(c / f) * 733333333ull + (u64)(c % f) * 733333333ull / (u64)f;
}
static BOOL NTAPI QueryPerformanceCounter_(s64 *p) { *p = (s64)bf_xbox_tsc(); return 1; }
/* a bare "rdtsc; ret": callers may count on ecx being kept, as rdtsc keeps it */
__attribute__((naked)) static void rdtsc_(void) { __asm__ volatile("pushl %ecx\n\tcall _bf_xbox_tsc\n\tpopl %ecx\n\tret"); }

/* gamma ramp (Milestone 87): D3D_WriteGammaRamp (0x37834E, ramp in edx: red[256], green[256], blue[256]) feeds the
   video DAC byte by byte; here the ramp is kept and handed to the viewer, which applies it when it presents */
u8 g_gamma[768]; volatile u32 g_gamma_seq;
void gamma_set(const u8 *r) {
    for (u32 k = 0; k < 768; k++) g_gamma[k] = r[k]; g_gamma_seq++;
    static u32 nl; if (nl++ < 6) { out("  gamma ramp: R"); for (u32 k = 0; k < 256; k += 32) { out(" "); dec(r[k]); } out(" "); dec(r[255]); out(" G"); for (u32 k = 0; k < 256; k += 64) { out(" "); dec(r[256 + k]); } out(" B"); for (u32 k = 0; k < 256; k += 64) { out(" "); dec(r[512 + k]); } out("\r\n"); }
}
__attribute__((naked)) static void gamma_stub(void) { __asm__ volatile("pushl %edx\n\tcall _gamma_set\n\taddl $4, %esp\n\tret"); }
/* aim assist off while the mouse aims (Milestone 87). A character's lock-on target is the weak reference at +0x634/+0x638:
   while it is set, stick turning is ignored (0x11E5F4) and Character_Update turns toward the target (0x11D572), which
   made mouse aiming slow and sticky. 0xC4E70 / 0xC4EC0 link it (character in ebx, target in edi, 0 = clear); the
   reticule's tracking ring calls them with the enemy (0x158240). For the player, while the mouse is captured, they
   now run with target 0. The player is the character 0x11E570 is called for from CharacterType_HandlePlayerInput.
   loader\aimassist.txt keeps the game's own behaviour. */
void *g_player_char; u32 g_mouse_on, g_lock_hits, g_lock_calls;
/* direct mouse look (Milestone 87): through the stick the game caps turning at the character's turn rate (1.57 rad/s),
   so fast mouse movements were cut short. Instead, every frame (from the player hook below) the pixels moved since the
   last frame become the turn input (+0x5F8 = angle / (turn speed +0x7EC x frame time); Character_UpdateTurning applies
   turn speed x input x dt) and the pitch (+0x5F4, clamped to +-60 degrees like Character_SetAimPitch). The mouse no
   longer drives the right stick. g_yaw_sign / g_pitch_sign: directions (from the mousetest.txt calibration). */
s32 g_yaw_sign = -1, g_pitch_sign = 1;   /* calibrated: stick right turned -2690 mrad, mouse right (sign 1) +1237 */ s32 g_mouse_sens = 50; u32 g_mt_active;
volatile s32 g_fake_dx, g_fake_dy;   /* mousetest.txt: simulated movement */
/* weapon pose while sprinting (Milestone 93): the game has its own sprint motion, MT_DASH (8), next to MT_RUN (34). While the
   player sprints (Left Shift), a running player is switched to DASH, and back to RUN when the sprint ends, through
   CharacterType_RequestMotion (0x12EAB0: eax = motion id, ecx = character type [character +0xC], esi = character, one
   stack argument 0; returns al = 1 if the type has that motion). Current motion record at +0x1FC (its id at +0). */
static int req_motion(u8 *c, u32 id) { u32 r, f = 0x12EAB0;
    __asm__ volatile("pushl $0\n\t call *%4" : "=a"(r) : "a"(id), "c"(*(u32 *)(c + 0xC)), "S"(c), "D"(f) : "edx", "memory", "cc"); return r & 0xFF; }
static void pose_update(u8 *c) {
    extern u32 g_sprinting; static u32 off = 2; if (off == 2) off = fattr_cached("nodash.txt") != 0xFFFFFFFF; if (off) return;
    u8 *cur = *(u8 **)(c + 0x1FC), *rq = *(u8 **)(c + 0x200); u32 id = (u32)cur > 0x10000 ? *(u32 *)cur : 0xFFFFFFFF, rid = (u32)rq > 0x10000 ? *(u32 *)rq : 0xFFFFFFFF;
    static u32 n, ours; int r = -1;   /* ours: we switched RUN -> DASH (only then switch back) */
    if (g_sprinting && id == 34 && rid != 8) { r = req_motion(c, 8); if (r) ours = 1; }
    else if (!g_sprinting && ours && id == 8 && rid != 34) { r = req_motion(c, 34); ours = 0; }
    if (r >= 0 && n < 10) { n++; out("  [pose] motion "); dec(id); out(g_sprinting ? " -> DASH " : " -> RUN "); out(r ? "ok\r\n" : "refused\r\n"); } }
u32 g_switch_tick;   /* last weapon-switch press */ u32 g_sprint_pct = 50; float g_gunarm_t = 1.0f;   /* the gun arm's share of the sprint pose (0 during a weapon switch) */
u32 g_ctl_tick;   /* last time the player's input was applied (no pose during cutscenes) */
static int readable(const void *p, u32 n); static void fo(float v);
u32 g_input_tid; IMP DWORD WINAPI GetCurrentThreadId(void);
static int lock_char(void);   /* a special's lock-on is holding a target (defined below) */
static int special_steady(void);
static float g_projB[2][16]; static volatile u32 g_proj_t, g_proj_i; extern float g_hip_p11;
static int scoped(void) { u8 *pc = (u8 *)g_player_char; int z = pc && readable(pc + 0x7A8, 4) && *(u32 *)(pc + 0x7A8);   /* zoom level at player +0x7A8 */
    if (z && g_proj_t && fattr_cached("zoomflagonly.txt") == 0xFFFFFFFF) { float p11 = g_projB[g_proj_i & 1][5]; if (p11 < 0) p11 = -p11;   /* and the view really zoomed: the flag stayed set
        after a special / weapon change (the camera sat inside Flint) */
        if (p11 > 0.5f && p11 < g_hip_p11 * 1.4f) z = 0; }
    u32 *pd = gpu_pad(); if (pd) { pd[52] = 320; pd[53] = z ? 240 : 190; } return z; }   /* the viewer reads the depth under this pixel for the re-aim */
static float sqrt_(float x); static int special_on(void); static int special_steady(void); static int special_hold(void); static int lock_mode(void);
/* scope zoom-in without the pop: the scope camera's eye relative to the character (right, up, forward), learnt once it settles; on zoom-in the
   aim goes straight at the reticle's point from there, and for 0.5 s (the game's own zoom-in slide) cam_build shows that final view */
static float g_projB[2][16]; static volatile u32 g_proj_t, g_proj_i;   /* (defined with vp_watch) */
static int inv4(const float *m, float *o);
u8 *g_cover_block_tg; u32 g_cover_block_until; float g_cover_frac;   /* the lock's cover test: a target more than 45% covered is let go for 1 s */
int g_aim_part;   /* which part of a locked target the shot aims at (0 chest, 1 head, 2 hips, 3 left, 4 right) */
float g_zT[3], g_zM0[3]; u32 g_zT_t; float *g_zsd;   /* + the gun's position and the weapon's stored-direction slot (the trail) */   /* zoomed shot: the point the trail ends at (the hit ray is steered there, bray_wrap) */
float g_lockcam_yaw, g_lockcam_pitch; int g_lockcam_on;   /* the lock camera's direction (cam_build), which Flint follows (mouse_apply) */
static float g_turn_sgn = 1.0f; static float g_cam_yaw, g_yaw_off; static int g_yaw_act, g_yaw_own;   /* g_yaw_own: zoomed - the camera's yaw is g_cam_yaw (Flint twitched during the game's zoom animation) */   /* g_yaw_off: zoom - the camera's yaw relative to Flint's (eased; Flint is not turned) */   /* zoom: our camera's own yaw while it eases (cam_build) */
static float g_sc_off[3] = {0.0f, 0.444f, -0.485f}, g_sc_E[3], g_sc_D[3], g_sc_dp, g_sc_yaw, g_hip_E[3], g_hip_F[3]; static float g_game_F[3], g_zmouse, g_zyaw0;   /* zoom: the mouse's turn while zoomed in (turn-queue units), Flint's yaw at zoom-in */ static int g_sc_have, g_sc_phase; static u32 g_zoom_t, g_sc_t0, g_sc_t1;
/* g_sc_phase: 1 = gliding from the shoulder view to the final scope view (0.15 s) and holding it until the game's scope camera matches it,
   2 = blending into the game's scope camera (0.15 s), 0 = the game's scope camera */
static void char_basis(u8 *c, float *pos, float *R, float *F) {   /* position (+0x24) and the horizontal right / forward axes of the aim matrix */
    float *x = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c) + 9; float *A = (float *)(c + 0x5BC);
    for (int k = 0; k < 3; k++) pos[k] = x[k]; float fx = -A[2], fz = -A[8], l = sqrt_(fx * fx + fz * fz); if (l < 1e-4f) l = 1; F[0] = fx / l; F[1] = 0; F[2] = fz / l;
    float rx = A[0], rz = A[6], m = sqrt_(rx * rx + rz * rz); if (m < 1e-4f) m = 1; R[0] = rx / m; R[1] = 0; R[2] = rz / m; }
/* the special's target switching (not zoomed): characters the game considered for the lock (aim_is_character) are remembered; a mouse
   swipe left or right moves the lock to the nearest one on that side of the screen (g_forced), held until it is gone, another swipe
   or the special's end. noswipe.txt = off */
extern u32 g_rt; static u8 *g_cand[16]; static u32 g_cand_t[16]; u8 *g_forced;
static int char_is(u8 *q) { if (!q || q == (u8 *)g_player_char || (u32)q < 0x80000000u || ((u32)q & 3) || !readable(q, 0x10)) return 0;   /* game objects live at 0x80000000 and up (a low look-alike crashed in its vfunc) */
    { u32 vt = *(u32 *)q; if (vt < 0x10000 || vt > 0x500000 || !readable((void *)vt, 0x44) || ((u32 *)vt)[0x40 / 4] < 0x10000 || ((u32 *)vt)[0x40 / 4] > 0x400000) return 0; } u8 *k = *(u8 **)(q + 0xC); if (!(k && readable(k, 8) && *(u32 *)(k + 4) == 3 && readable(*(void **)q, 0x44))) return 0;
    return readable(q + 0x54, 4) && *(s32 *)(q + 0x54) != 0; }   /* alive: health (+0x54, 0 when dead - README Milestone 7). The lock held on to dead enemies */
u32 g_cand_seen;
void cand_add(u8 *q) { if (!char_is(q)) return; g_cand_seen++; u32 now = GetTickCount(), old = 0; int slot = -1;
    for (int i = 0; i < 16; i++) { if (g_cand[i] == q) { g_cand_t[i] = now; return; } if (slot < 0 || g_cand_t[i] < old) { slot = i; old = g_cand_t[i]; } }
    g_cand[slot] = q; g_cand_t[slot] = now; }
static float *char_xf(u8 *q) { return ((float *(__thiscall *)(void *))(*(u32 **)q)[0x40 / 4])(q); }
static void target_swipe(int dir, u8 *cur) {   /* dir +1 = right on screen */
    extern float g_eye_ours[3], g_cr[3], g_cb[3]; u32 now = GetTickCount(); float s0 = 0; int have0 = 0;
    if (char_is(cur)) { float *x = char_xf(cur); float v[3] = {x[9] - g_eye_ours[0], x[10] - g_eye_ours[1], x[11] - g_eye_ours[2]}; float f = -(v[0] * g_cb[0] + v[1] * g_cb[1] + v[2] * g_cb[2]); if (f > 0.5f) { s0 = (v[0] * g_cr[0] + v[1] * g_cr[1] + v[2] * g_cr[2]) / f; have0 = 1; } }
    u8 *best = 0; float bd = 1e9f;
    for (int i = 0; i < 16; i++) { u8 *q = g_cand[i]; if (!q || q == cur || now - g_cand_t[i] > 4000 || !char_is(q)) continue;
        float *x = char_xf(q); float v[3] = {x[9] - g_eye_ours[0], x[10] - g_eye_ours[1], x[11] - g_eye_ours[2]}; float f = -(v[0] * g_cb[0] + v[1] * g_cb[1] + v[2] * g_cb[2]); if (f < 1.0f) continue;
        float sd = (v[0] * g_cr[0] + v[1] * g_cr[1] + v[2] * g_cr[2]) / f, d = (sd - (have0 ? s0 : 0.0f)) * (float)dir; if (d > 0.01f && d < bd) { bd = d; best = q; } }
    { static u32 nl; if (nl++ < 40) { out("  [swipe] "); out(dir > 0 ? "right" : "left"); out(" from "); hex((u32)cur); out(" to "); hex((u32)best); out("\r\n"); } }
    if (best) g_forced = best; }
void mouse_apply(u8 *c) {
    {   /* frame cap (Nov 21): after the speed fixes the game ran ~170 frames/s against a 60 Hz screen; each shown frame was the newest
           of ~3 unevenly spaced ones (choppy aiming) and the viewer fell behind. As on the Xbox: one game frame per screen refresh (the
           vblank counter follows the monitor), at most 20 ms of waiting. nofpscap.txt = uncapped */
        extern u32 g_vblanks; static u32 lastvb; IMP void WINAPI Sleep(DWORD);
        if (fattr_cached("nofpscap.txt") == 0xFFFFFFFF) { u32 t0 = GetTickCount(); while (*(volatile u32 *)&g_vblanks == lastvb && GetTickCount() - t0 < 20) Sleep(0); }
        lastvb = *(volatile u32 *)&g_vblanks; }
    g_input_tid = GetCurrentThreadId(); (void)scoped();   /* every frame: the viewer samples depth under the right crosshair */
    if (readable(c + 0x490, 4) && *(float *)(c + 0x490) < 60.0f && fattr_cached("infinitespecial.txt") != 0xFFFFFFFF) *(float *)(c + 0x490) = 100.0f;   /* test: the special's energy refilled below 60 (it still drains: special_on sees the drop) */
    g_ctl_tick = GetTickCount(); (void)pose_update;   /* Milestone 93: the RUN/DASH switch is off (DASH is already the run of these characters; switching fought the game's own requests and froze the animation) */
    int view_mouse(s32 *, s32 *); s32 mx, my; view_mouse(&mx, &my); mx += g_fake_dx; my += g_fake_dy;
    static s32 lx, ly, init; static s64 lt, qf; s64 now; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&now);
    if (!init || c != g_player_char) { init = 1; lx = mx; ly = my; lt = now; return; }
    float dt = (float)(now - lt) / (float)qf; s32 dx = mx - lx, dy = my - ly; lx = mx; ly = my; lt = now;
    if (dt <= 0.0005f || dt > 0.25f) return;
    {   static u32 tl0, off0; u32 n0 = GetTickCount(); if (n0 - tl0 > 1000) { tl0 = n0; off0 = fattr_cached("nolocksteer.txt") != 0xFFFFFFFF; }
        if (!lock_mode()) g_forced = 0; else if (g_forced && !char_is(g_forced)) g_forced = 0;
        if (lock_char() && !scoped() && fattr_cached("noswipe.txt") == 0xFFFFFFFF) {   /* a swipe: 60 px of mouse within 0.25 s */
            static s32 acc; static u32 acc_t, cool; u32 nw = GetTickCount(); if (nw - acc_t > 250) { acc = 0; acc_t = nw; } acc += dx;
            if ((acc > 60 || acc < -60) && nw - cool > 400) { u8 *r = (u8 *)g_rt; u8 *cur = g_forced ? g_forced : (r && readable(r + 0x5D0, 4) ? *(u8 **)(r + 0x5D0) : 0); target_swipe(acc > 0 ? 1 : -1, cur); acc = 0; acc_t = nw; cool = nw; } }
        { s32 adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy; if (adx + ady > 3) { extern u32 g_mouse_move_t; g_mouse_move_t = GetTickCount(); } }   /* mouse input stops the lock */
        u8 *lock_target(void); if (!off0 && lock_target()) { g_yaw_own = 0; g_yaw_off = 0;
            dx = 0; dy = 0; } }   /* locked: the mouse only swipes; the camera turns to the target (cam_build) and Flint follows it (below) */   /* locked: the game's own lock turns Flint (ours on top of it was choppy); the camera follows her */   /* opt-in (locksteer.txt): taking the mouse made aiming choppy */   /* locked: the mouse only swipes; Flint and the camera are steered at the target below */   /* a chosen target: Flint is steered at it below (the mouse only swipes) */   /* (zoomed: the camera follows the lock's aim, not its own zoom yaw) */   /* locked on: the game steers the aim; our pitch and turn fought it (screen shake). nolocksteer.txt = off */
    float k = (float)g_mouse_sens * 0.00006f;   /* radians per pixel: 0.003 at the default 50 */
    {   /* Milestone 93: slower turning while sprinting (sprintsens.txt: percent, default 50) */
        extern u32 g_sprinting; static u32 pct = 50, tl; u32 nw = GetTickCount(); if (nw - tl > 1000) { tl = nw; pct = 50; HANDLE f = CreateFileA("sprintsens.txt", 0x80000000, 1, 0, 3, 0x80, 0);
            if (f != (HANDLE)-1) { char s[8] = {0}; DWORD n = 0; ReadFile(f, s, 7, &n, 0); CloseHandle(f); u32 v = 0; for (DWORD i = 0; i < n && s[i] >= 0x30 && s[i] <= 0x39; i++) v = v * 10 + (s[i] - 0x30); if (v) pct = v; } }
        { extern u32 g_sprint_pct; g_sprint_pct = pct; } if (g_sprinting) k *= (float)pct * 0.01f;
        { static u32 tl2; u32 n2 = GetTickCount(); if (g_sprinting && n2 - tl2 > 3000) { tl2 = n2; out("  [sens] sprinting: mouse x"); dec(pct); out("%\r\n"); } } }
    /* aim mode (Milestone 87): right button held = the game's zoom (Character_DoAction 16 = zoom in one level, 19 = zoom
       out; zoom level at +0x7A8; weapons without a scope have none) and a slower mouse */
    { int view_key(int); extern u32 g_aiming; static u32 fa, tfa; u32 nf = GetTickCount(); if (nf - tfa > 1000) { tfa = nf; fa = fattr_cached("forceaim.txt") != 0xFFFFFFFF; } g_aiming = view_key(0x02) || fa; }   /* forceaim.txt: test */   /* right button: our aim state (camera, see view_adjust) */
    /* turning: 1:1 with the mouse, but at most 7 rad/s; faster swipes are spread over the next frames (backlog <= 1.5 rad)
       so the body and the camera keep up */
    static float pend; pend += (float)(dx * g_yaw_sign) * k; if (g_zoom_t) g_zmouse += (float)(dx * g_yaw_sign) * k; if (pend > 3.0f) pend = 3.0f; if (pend < -3.0f) pend = -3.0f;
    if (pend != 0.0f && !*(u32 *)(c + 0x604) && !*(u32 *)(c + 0x638)) { float step = pend, lim = 60.0f * dt;   /* Milestone 95: 15 rad/s queued fast flicks (up to 3 rad behind the mouse) */ { extern u32 g_sprinting; extern u32 g_sprint_pct; if (g_sprinting) lim *= (float)g_sprint_pct * 0.01f; }   /* the turn-rate cap is slowed too while sprinting */   /* up to 15 rad/s (the camera is built from the character, so it keeps up) */ if (step > lim) step = lim; if (step < -lim) step = -lim; pend -= step;
        float ts = *(float *)(c + 0x7EC); if (ts < 0.001f) ts = 1.0f; *(float *)(c + 0x5F8) = step / (ts * dt);
        {   /* Milestone 95 probe: the mouse backlog (rad x1000), the game's turn speed, the input set, and how far the aim really turned since last time */
            static u32 tlp, np; static float lay; u32 nw = GetTickCount(); float *A = (float *)(c + 0x5BC); extern float atan2_ext(float, float); float ay = atan2_ext(A[6], A[0]);
            if (np < 20 && nw - tlp > 250 && (pend > 0.05f || pend < -0.05f)) { tlp = nw; np++; float da = ay - lay; while (da > 3.14159f) da -= 6.28318f; while (da < -3.14159f) da += 6.28318f;
                out("  [turnprobe] backlog "); fo(pend); out("step "); fo(step); out("turnspeed "); fo(ts); out("input "); fo(*(float *)(c + 0x5F8)); out("dt us "); dec((u32)(dt * 1e6f)); out(" aim moved "); fo(da); out("\r\n"); }
            lay = ay; }
        /* the game's follow camera turns by the stick input in +0x600 (it reads and clears it, 0x709A4); feed it the same
           turn, as "set turn input" would (turn = type rate +0x2D0 x input), so it keeps up with the character (Milestone 87) */
        { u8 *ty = *(u8 **)(c + 0xC); float tr = (u32)ty > 0x10000 ? *(float *)(ty + 0x2D0) : 1.57f; if (tr < 0.01f) tr = 1.57f; *(float *)(c + 0x600) = step / (tr * dt); } }
    else if (*(u32 *)(c + 0x604) || *(u32 *)(c + 0x638)) pend = 0;
    {   /* Milestone 93: the view pitch is ours (g_view_pitch, the mouse); the character's aim pitch follows it, except while
           sprinting, when it eases up to ~55 degrees so the aim IK raises the gun. The camera is built from the view pitch. */
        extern float g_view_pitch, g_sprint_t, g_pose_t; extern u32 g_sprinting; static u32 vinit; static u8 *vc;
        if (!vinit || vc != c) { vinit = 1; vc = c; g_view_pitch = *(float *)(c + 0x5F4); }
        g_view_pitch += (float)(-dy * g_pitch_sign) * k;
        {   /* zoom on our camera: zoomed in, our camera's eye eases from the shoulder to the scope's eye (cam_build), its direction stays ours.
               On zoom-in the view eases onto the world point that was under the reticle (pitch: g_view_pitch; yaw: the camera's own yaw,
               g_cam_yaw, with Flint turning after it); zooming out eases back to where the reticle was. noscopepoint.txt = the plain lift only */
            extern float g_hip_p11; int reticle_world(float *T); extern float atan2_ext(float, float);
            static int wz; static float lift_t, lift_a, RT[3], ygoal, zm, base_y, yoff_t; static u32 rt_t, ya_t; const float ysign = 1.0f;
            u32 nw = GetTickCount(); int z; { static u32 zon_t; if (scoped()) { z = 1; zon_t = nw; } else z = wz && nw - zon_t < 150; }   /* a zoom-out counts after 0.15 s off: a flickering zoom flag
                (or the special check) re-ran zoom-in / zoom-out every few frames - the pitch sawtooth (shake) while zoomed in a special */
            float P_[3], R_[3], F_[3]; char_basis(c, P_, R_, F_); float cy = atan2_ext(F_[0], F_[2]);
            float ease = dt * 10.0f; if (ease > 1.0f) ease = 1.0f;
            float mraw = (float)(dx * g_yaw_sign) * k;
            {   /* which way the turn queue turns Flint's yaw (this yaw convention): learnt from mouse turns outside the zoom */
                static float pcy, pm, corr; static int have; if (have && !z) { float d = cy - pcy; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; if (pm > 0.003f || pm < -0.003f) corr += pm * d; if (corr > 1.0f) corr = 1.0f; if (corr < -1.0f) corr = -1.0f; }
                pcy = cy; pm = mraw; have = 1; g_turn_sgn = corr < 0 ? -1.0f : 1.0f; }
            float mang = mraw * g_turn_sgn * ysign;   /* the mouse's turn this frame (radians of yaw) */
            if (!z) { float T[3]; if (reticle_world(T)) { RT[0] = T[0]; RT[1] = T[1]; RT[2] = T[2]; rt_t = nw | 1; } }
            if (z && !wz) {   /* zoom in */
                base_y = cy; zm = 0; ygoal = cy;
                float t = (50.0f / 240.0f) / g_hip_p11; float W = (g_view_pitch - lift_a) + (t - t * t * t / 3.0f);
                if (rt_t && nw - rt_t < 1000 && fattr_cached("noscopepoint.txt") == 0xFFFFFFFF) {
                    float E[3], D[3]; for (int q = 0; q < 3; q++) E[q] = P_[q] + R_[q] * g_sc_off[0] + (q == 1 ? g_sc_off[1] : 0.0f) + F_[q] * g_sc_off[2];
                    for (int q = 0; q < 3; q++) D[q] = RT[q] - E[q]; float h = sqrt_(D[0] * D[0] + D[2] * D[2]);
                    if (h > 1.0f) { W = atan2_ext(D[1], h); float yg = atan2_ext(D[0], D[2]), d = yg - cy; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; if (d < 0.3f && d > -0.3f) ygoal = cy + d; } }
                lift_t = W - (g_view_pitch - lift_a); if (lift_t > 0.4f) lift_t = 0.4f; if (lift_t < -0.4f) lift_t = -0.4f;
                yoff_t = ygoal - cy; g_cam_yaw = cy; g_yaw_own = 1;
                static u32 nl; if (nl++ < 20) { out("  [zoom] in: pitch lift mrad "); fo(lift_t); out("yaw mrad "); fo(ygoal - cy); out("\r\n"); } }
            if (!z && wz) {   /* zoom out: back to the shoulder view, turned back so the reticle lands where it was */
                g_yaw_off = g_cam_yaw + g_yaw_off - cy; while (g_yaw_off > 3.14159f) g_yaw_off -= 6.28318f; while (g_yaw_off < -3.14159f) g_yaw_off += 6.28318f;
                g_yaw_own = 0; lift_t = 0; yoff_t = 0; }   /* back on Flint's yaw from where the camera is, eased */
            if (z) { zm += mang; g_cam_yaw += mang; }
            {   float dl = (lift_t - lift_a) * ease; lift_a += dl; g_view_pitch += dl; }   /* the pitch eases (both ways) */
            if (lock_mode()) { yoff_t = 0; g_yaw_off = 0; g_yaw_own = 0; }   /* in a special the lock steers: no zoom yaw offset (it came back in the frames the lock dropped: shake) */
            g_yaw_off += (yoff_t - g_yaw_off) * ease; if (yoff_t == 0.0f && g_yaw_off < 0.0005f && g_yaw_off > -0.0005f) g_yaw_off = 0.0f;   /* eased both ways; the mouse turns Flint and the camera together */
            if (g_yaw_own && !*(u32 *)(c + 0x604) && !*(u32 *)(c + 0x638) && fattr_cached("noflintfollow.txt") == 0xFFFFFFFF) {   /* zoomed: Flint turns after the camera,
                    a third of the gap per frame (no overshoot), so the game's own aim, its red reticle and its hit ray follow the crosshair */
                float er = g_cam_yaw + g_yaw_off - cy; while (er > 3.14159f) er -= 6.28318f; while (er < -3.14159f) er += 6.28318f; if (er < 0.5f && er > -0.5f) pend = er * g_turn_sgn * 0.35f; }
            wz = z; }
        u8 *lock_target(void); u8 *lt_ = lock_target();
        { extern float g_lockcam_yaw, g_lockcam_pitch; extern int g_lockcam_on;
          if (lt_ && g_lockcam_on && fattr_cached("locksteer.txt") == 0xFFFFFFFF) {   /* Flint follows the lock camera: a third of the gap per frame */
            extern float atan2_ext(float, float); float P_[3], R_[3], F_[3]; char_basis(c, P_, R_, F_); float er = g_lockcam_yaw - atan2_ext(F_[0], F_[2]);
            while (er > 3.14159f) er -= 6.28318f; while (er < -3.14159f) er += 6.28318f; if (!*(u32 *)(c + 0x604) && !*(u32 *)(c + 0x638) && er < 1.0f && er > -1.0f) pend = er * g_turn_sgn * 0.6f;
            g_view_pitch += (g_lockcam_pitch - g_view_pitch) * 0.6f; } }
        if (lt_ && char_is(lt_) && fattr_cached("locksteer.txt") != 0xFFFFFFFF) {   /* locked: Flint (and the camera with her) turned at the target, so it stays
                in front and centred: yaw through the turn queue (a third of the gap per frame), pitch eased */
            extern float atan2_ext(float, float); float P_[3], R_[3], F_[3]; char_basis(c, P_, R_, F_); float *x = char_xf(lt_);
            float v[3] = {x[9] - P_[0], x[10] + 0.35f - (P_[1] + 0.7f), x[11] - P_[2]}, h = sqrt_(v[0] * v[0] + v[2] * v[2]);
            if (h > 0.5f) { float er = atan2_ext(v[0], v[2]) - atan2_ext(F_[0], F_[2]); while (er > 3.14159f) er -= 6.28318f; while (er < -3.14159f) er += 6.28318f;
                static u8 *gt_; static u32 gt0; u32 gn = GetTickCount(); if (gt_ != lt_) { gt_ = lt_; gt0 = gn; } float ramp = (gn - gt0) / 300.0f; if (ramp > 1.0f) ramp = 1.0f;   /* a new lock: the turn eases in (the game swings too: together they overshot) */
                /* moving targets: the target's own yaw rate (its bearing's change since last frame) is added, so Flint keeps pace instead of trailing */
                static float pby; static u8 *pbt; float by = atan2_ext(v[0], v[2]), ff = 0; if (pbt == lt_) { ff = by - pby; while (ff > 3.14159f) ff -= 6.28318f; while (ff < -3.14159f) ff += 6.28318f; if (ff > 0.2f || ff < -0.2f) ff = 0; } pby = by; pbt = lt_;
                if (!*(u32 *)(c + 0x604) && !*(u32 *)(c + 0x638)) pend = (er * (0.12f + 0.43f * ramp) + ff) * g_turn_sgn;   /* stronger (user): up to 0.55 of the gap per frame */
                float wp = atan2_ext(v[1], h); static float wps; static u8 *wpt; if (wpt != lt_) { wpt = lt_; wps = g_view_pitch; }
                { static float pwp; static u8 *pwt; float fp = (pwt == lt_) ? wp - pwp : 0; if (fp > 0.1f || fp < -0.1f) fp = 0; pwp = wp; pwt = lt_; wps += (wp - wps) * 0.15f + fp; }
                g_view_pitch += (wps - g_view_pitch) * 0.45f; } }   /* the target pitch filtered (~0.2 s): Flint's and the target's bobbing made it twitch (zoomed shake) */
        if (g_view_pitch > 1.047f) g_view_pitch = 1.047f; if (g_view_pitch < -1.047f) g_view_pitch = -1.047f;
        float st = dt * 6.0f; if (st > 1) st = 1; static u32 nogunup = 2; if (nogunup == 2) nogunup = fattr_cached("gunup.txt") == 0xFFFFFFFF;   /* opt-in: the run animation ignores the aim pitch, so this did not raise the gun */
        g_sprint_t += ((g_sprinting && !nogunup ? 1.0f : 0.0f) - g_sprint_t) * st;
        { extern float g_pose_t, g_held_base; int heavy = g_held_base > 0.68f;   /* measured: minigun 0.78-0.81 m, Regen 0.39-0.59 m (picked mid-switch) */   /* Milestone 97: a heavy gun carried low (the RVG-50 minigun) keeps the game's own run animation (user) */
          int sp = g_sprinting && !heavy; float pt = dt * (sp ? 8.0f : 25.0f); if (pt > 1) pt = 1; g_pose_t += ((sp ? 1.0f : 0.0f) - g_pose_t) * pt; if (!sp && g_pose_t < 0.02f) g_pose_t = 0; }   /* eases in over ~0.3 s, out over ~0.1 s */
        float p = g_view_pitch * (1.0f - g_sprint_t) + 0.96f * g_sprint_t;   /* through Character_SetAimPitch (stdcall: character, pitch; clamps, updates the aim matrix) */
        if (p != *(float *)(c + 0x5F4)) { ((float (__stdcall *)(void *, float))0x11E7C0)(c, p); __asm__ volatile("fstp %%st(0)" ::: "memory"); } }
    {   /* Milestone 95: the body turns with the camera. The character transform (vfunc +0x40: 3x3 rotation, position +0x24) is
           what the model is drawn from; the game turns it toward the movement direction / aim at its own rate (up to ~45 degrees
           behind the aim). Its yaw is set to the aim's (+0x5BC) every frame. bodylag.txt = the game's own turning. */
        /* Writing the transform made him spin; the body's heading is +0x434 (radians 0..2pi, body yaw = pi - heading), its target +0x4A0.
           Walking, the game sets the target from the aim; standing, it leaves both until the gap is large. Both are set from the aim. */
        /* that froze him (standing turns are turn-in-place motions driven from these); kept opt-in only: bodyhead.txt */
        static u32 off = 2, tl; u32 nw = GetTickCount(); if (nw - tl > 1000) { tl = nw; off = fattr_cached("bodyhead.txt") == 0xFFFFFFFF; }
        if (!off && !*(u32 *)(c + 0x604) && !*(u32 *)(c + 0x638)) { float *A = (float *)(c + 0x5BC); extern float atan2_ext(float, float);
            float h = 3.14159265f - atan2_ext(A[6], A[0]); while (h < 0) h += 6.2831853f; while (h >= 6.2831853f) h -= 6.2831853f;
            *(float *)(c + 0x434) = h; *(float *)(c + 0x4A0) = h; } }
}
void input_report(void) { u8 *c = (u8 *)g_player_char; out("  [aim] player "); hex((u32)c); out(" mouse "); dec(g_mouse_on); out(" lock calls "); dec(g_lock_calls); out(" neutralized "); dec(g_lock_hits);
    if (c) { out(" +638 "); hex(*(u32 *)(c + 0x638)); out(" +604 "); hex(*(u32 *)(c + 0x604));
        float ts = *(float *)(c + 0x7EC), tr = 0, fr = *(float *)(c + 0x7DC); u8 *ty = *(u8 **)(c + 0xC); if ((u32)ty > 0x10000) tr = *(float *)(ty + 0x2D0);
        out(" turnspeed x1000 "); dec((u32)(s32)(ts * 1000)); out(" type turn x1000 "); dec((u32)(s32)(tr * 1000)); out(" friction x1000 "); dec((u32)(s32)(fr * 1000));
        out(" motion "); hex(*(u32 *)(c + 0x1FC)); out(" req "); hex(*(u32 *)(c + 0x200)); }
    { extern u32 g_last_pad[3]; out(" LT/RT "); dec((g_last_pad[2] >> 16) & 255); out("/"); dec(g_last_pad[2] >> 24); }
    out("\r\n"); }
__attribute__((naked)) static void move_stub(void) { __asm__ volatile(
    "movl (%esp), %edx\n\t cmpl $0x1332A0, %edx\n\t jb 1f\n\t cmpl $0x1351C0, %edx\n\t jae 1f\n\t movl %ecx, _g_player_char\n\t"
    "cmpl $0, _g_mouse_on\n\t je 1f\n\t pushl %eax\n\t pushl %ecx\n\t pushl %ecx\n\t call _mouse_apply\n\t addl $4, %esp\n\t popl %ecx\n\t popl %eax\n"
    "1:\n\t subl $8, %esp\n\t movl 8(%eax), %edx\n\t pushl $0x11E576\n\t ret"); }
/* Milestone 105: the reticle's object (+0x5E4) is not the enemy: it is one targeting object (type 16, the follow camera's
   vtable) that drives the lock-on and also picks what holding X picks up. Blanking it always (Milestone 87) made weapons on
   the ground impossible to pick up; keeping it brought the lock-on back. So it is blanked except while X is held (the
   pick-up / reload button): then the game sees it as usual and the weapon under the player is picked up. */
extern u32 g_last_pad[3];
static int x_held(void) { return ((g_last_pad[1] >> 16) & 255) > 30; }
/* special abilities (Flint's auto-lock): the lock-on and reticle targets are let through for 90 s after the special (White) is pressed
   (where the game keeps 'special active' is not known yet). nospeciallock.txt = blank them as before. */
/* special-state probe (always on until found): snapshots of the player character's first 0x1800 bytes; on each press of the
   special (White) the snapshot from just before is compared with the state 1.5 s later, and every 32-bit field that changed to a
   small integer value (flags, states) is logged; pressing again shows which flip back. -> [special] lines in bfloader.log */
static void special_probe(void) {
    static u8 snap[0x1800], before[0x1800]; static u32 tsnap, tpress, was, n; u8 *c = (u8 *)g_player_char; u32 now = GetTickCount();
    if (!c || !readable(c, 0x1800) || n >= 24) return;
    u32 w = ((g_last_pad[2] >> 8) & 255) > 30;
    if (w && !was) { for (u32 i = 0; i < 0x1800; i++) before[i] = snap[i]; tpress = now | 1; }
    was = w;
    if (tpress && now - tpress > 1500) { tpress = 0; n++; out("  [special] press "); dec(n); out(" changed small fields:");
        u32 k = 0; for (u32 o = 0; o < 0x1800 && k < 40; o += 4) { u32 a = *(u32 *)(before + o), b = *(u32 *)(c + o); if (a != b && a < 0x10000 && b < 0x10000) { out(" +"); hex(o); out(" "); dec(a); out(">"); dec(b); k++; } }
        out("\r\n  [special] press "); dec(n); out(" floats that went down (x1000):"); k = 0;   /* an energy amount draining while the special is on */
        for (u32 o = 0; o < 0x1800 && k < 40; o += 4) { float a = *(float *)(before + o), b = *(float *)(c + o); if (a > 0.001f && a < 100000.0f && b < a && b > -1.0f && a - b < a * 0.9f && a - b > a * 0.0005f) { out(" +"); hex(o); out(" "); dec((u32)(a * 1000)); out(">"); dec((u32)(b * 1000)); k++; } }
        out("\r\n"); }
    if (now - tsnap > 200) { tsnap = now; for (u32 i = 0; i < 0x1800; i++) snap[i] = c[i]; } }
static u32 g_special_drain_ms;
static int special_on(void) {   /* the special is on while its energy (player character +0x490, a float, 100 = full) is draining (found by probe) */
    static u32 tl, off, drain_ms; static float last = -1; static void *lastc; u32 now = GetTickCount();
    if (now - tl > 1000) { tl = now; off = fattr_cached("nospeciallock.txt") != 0xFFFFFFFF; }
    u8 *c = (u8 *)g_player_char; if (off || !c || !readable(c + 0x490, 4)) return 0;
    float e = *(float *)(c + 0x490); if ((void *)c != lastc) { lastc = c; last = e; drain_ms = 0; }
    if (e < last - 0.0001f) drain_ms = now | 1; last = e;
    { static u32 tlog, nlog; if (now - tlog > 500 && nlog < 400) { tlog = now; nlog++; out("  [energy] char "); hex((u32)c); out(" x1000 "); dec((u32)(e * 1000 + 0.5f)); out(drain_ms && now - drain_ms < 1500 ? " ON" : ""); out(" white "); dec(((g_last_pad[2] >> 8) & 255) > 30); out("\r\n"); } }
    g_special_drain_ms = drain_ms; return drain_ms && now - drain_ms < 1500; }   /* (the energy drops in steps, not every frame) */
static int special_steady(void) { special_on(); return g_special_drain_ms && GetTickCount() - g_special_drain_ms < 3000; }
static int special_hold(void) {   /* for the lock-on: the special counts as on through energy-step gaps (2.2 s) and from the moment White is pressed
    (the energy only starts dropping a moment later); the flickering check cleared the target the game was locking onto */
    static u32 tw; u32 now = GetTickCount(); if (((g_last_pad[2] >> 8) & 255) > 30) tw = now | 1;
    special_on(); return (g_special_drain_ms && now - g_special_drain_ms < 2200) || (tw && now - tw < 1500); }
u32 g_t16;   /* when the game last had its targeting object (type 16) as the reticle object: only lock-on specials (Flint's) make it */
extern u32 g_cand_seen; static u32 g_arch_lock[8], g_arch_no[8];   /* character types whose special locks onto characters (Flint) / does not (Tex's Berserker: aim assist yanked the aim) */
static void arch_add(u32 *l, u32 a) { for (int i = 0; i < 8; i++) { if (l[i] == a) return; if (!l[i]) { l[i] = a; return; } } }
static int arch_in(const u32 *l, u32 a) { for (int i = 0; i < 8; i++) if (l[i] == a) return 1; return 0; }
static int lock_mode(void) { u32 now = GetTickCount(); if (fattr_cached("noflintlock.txt") != 0xFFFFFFFF) return 0;   /* (user, Nov 21) the Flint lock-on fix is off for now: flintlock.txt = on */
    if (!(special_hold() && g_t16 && now - g_t16 < 1500)) return 0;
    u8 *pc = (u8 *)g_player_char; u32 a = pc && readable(pc + 0xC, 4) ? *(u32 *)(pc + 0xC) : 0; if (!a) return 0;
    if (arch_in(g_arch_lock, a)) return 1; if (arch_in(g_arch_no, a)) return 0;
    static u32 ua, ut, uc; if (ua != a || now - ut > 6000) { ua = a; ut = now; uc = g_cand_seen; }   /* unknown: a lock-on special for its first 3 s; no lock by then = not one */
    if (now - ut > 3000 && g_cand_seen - uc < 30) { ut = now - 2000; uc = g_cand_seen; }   /* no enemies offered yet: keep waiting */
    if (now - ut > 3000) { arch_add(g_arch_no, a); out("  [special] this character's special does not lock on: no lock-on handling\r\n"); return 0; } return 1; }   /* a lock-on special: other characters' specials
    (Tex's, ...) got the lock's mouse lock-out, steering, smoothing and sprint ban */
int lock_mode_ext(void) { return lock_mode(); }
int special_steady_ext(void) { return special_steady(); }   /* for the camera: a short gap between energy steps moved it back and forth (screen shake) */
static u32 g_lock_ms;   /* when the game last had a lock-on target (during a special) */
static int locked_now(void) { return g_lock_ms && GetTickCount() - g_lock_ms < 300; }
static u32 g_lock_t;   /* the lock-on target the game offered during a special */
u32 aim_is_character(u32 t) { int sp = lock_mode(); if (t) cand_add((u8 *)t); if (t && sp) { g_lock_ms = GetTickCount() | 1; g_lock_t = t; } return !x_held() && !sp; }   /* lock-on: cleared (1) unless X is held or a special is on */
u32 g_rt;   /* the reticle's object as the game had it (before it is hidden) */
static int lock_char_now(void) { extern u8 *g_forced; if (!lock_mode()) return 0; u8 *r = (u8 *)g_rt; if (!r || !readable(r + 0x5D0, 4)) return 0;   /* the targeting object's +0x5D0: the locked character */
    /* (a swiped-to target is used through lock_target; nothing is written into the game's lock any more - that froze it) */
    u8 *q = *(u8 **)(r + 0x5D0); if (!q || q == (u8 *)g_player_char || !readable(q, 0x10)) return 0; u8 *k = *(u8 **)(q + 0xC); int ok = k && readable(k, 8) && *(u32 *)(k + 4) == 3;
    if (ok) { u8 *pc = (u8 *)g_player_char; if (pc && readable(pc + 0xC, 4)) { u32 a = *(u32 *)(pc + 0xC); if (!arch_in(g_arch_lock, a)) { arch_add(g_arch_lock, a); out("  [special] this character's special locks on\r\n"); } } } return ok; }
static int lock_char(void) {   /* held through gaps under 0.5 s: the lock flickered (energy steps, the target dropping out for a frame) and each flip
                                  handed the aim between the game's steering and the mouse - the jitter while walking. A special's end still ends it */
    static u32 last; u32 now = GetTickCount(); if (!lock_mode()) { last = 0; return 0; }   /* from the White press: the energy only starts dropping later (the start of a lock fought the mouse) */   /* 2.2 s: past the energy-step gaps, short of the 3 s camera window */
    if (lock_char_now()) last = now | 1; return last && now - last < 500; }
static int interact_target(void) { u32 t = g_rt; if (!t || !readable((void *)t, 0x10)) return 0;   /* something under the reticle that is not a character */
    u32 c = *(u32 *)(t + 0xC); if (c && readable((void *)c, 8) && *(u32 *)(c + 4) == 3) return 0; return 1; }
/* the target the camera and the shots use: the game's lock (+0x5D0) held through its flips between nearby characters (it switched every
   few frames: the camera swung, shots split). Changes only when the game has wanted another for 0.8 s, or by a swipe. Nothing is written to
   the game (writing the held one back froze it once). nostickylock.txt = the game's choice each frame */
int body_center(u8 *q, float *o) {   /* the middle of a character's body: the mean of its body parts' positions (part objects 0x80 apart; the
    one at +0x3F8 is part 56, as for the player's skeleton), only parts within 3 m of the character. Tall, floating or fallen characters were shot over */
    if (!char_is(q) || !readable(q + 0x3F8, 4)) return 0; u32 lh = *(u32 *)(q + 0x3F8); if (lh < 0x80000000u + 56 * 0x80) return 0;
    u8 *base = (u8 *)(lh - 56 * 0x80); float *x = char_xf(q), s3[3] = {0, 0, 0}; int n = 0;
    for (int i = 0; i < 90; i++) { u8 *p = base + i * 0x80; if (!readable(p + 0x38, 12)) break; float *P = (float *)(p + 0x38);
        float dx = P[0] - x[9], dy = P[1] - x[10], dz = P[2] - x[11]; if (dx * dx + dy * dy + dz * dz > 9.0f || P[0] != P[0]) continue; s3[0] += P[0]; s3[1] += P[1]; s3[2] += P[2]; n++; }
    if (n < 5) return 0; for (int k = 0; k < 3; k++) o[k] = s3[k] / n; return 1; }
static int lock_in_range(u8 *q) { u8 *pc = (u8 *)g_player_char; if (!pc || !q) return 0; float *a = char_xf(q), *b = char_xf(pc); float dx = a[9] - b[9], dy = a[10] - b[10], dz = a[11] - b[11];
    return dx * dx + dy * dy + dz * dz < 45.0f * 45.0f; }   /* the hard lock's range */
u32 g_mouse_move_t;   /* when the mouse last moved (mouse_apply): mouse input stops the lock (user) */
static int lock_valid(u8 *q) { extern float g_eye_ours[3], g_cb[3]; if (!char_is(q) || !lock_in_range(q)) return 0; float *x = char_xf(q);
    float v[3] = {x[9] - g_eye_ours[0], x[10] - g_eye_ours[1], x[11] - g_eye_ours[2]}, l = sqrt_(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l < 0.5f) return 1;
    return -(v[0] * g_cb[0] + v[1] * g_cb[1] + v[2] * g_cb[2]) / l > 0.35f; }   /* alive, within 45 m, in front of the camera (~70 degrees) */
static u8 *lock_target_old(void);
static u8 *lock_target_(void) {   /* (user, Nov 21) the lock: the nearest valid character while the mouse is still; any mouse movement stops it.
    Kept until it dies, leaves range or view, or the mouse moves. oldlock.txt = the hard lock with swipes */
    if (fattr_cached("oldlock.txt") != 0xFFFFFFFF) return lock_target_old();
    static u8 *cur; u32 now = GetTickCount();
    if (!lock_mode() || now - g_mouse_move_t < 400) { cur = 0; return 0; }
    if (cur && lock_valid(cur)) return cur;
    u8 *best = 0; float bd = 1e9f, *pp = g_player_char ? char_xf((u8 *)g_player_char) : 0; if (!pp) return 0;
    u8 *r = (u8 *)g_rt; u8 *qg = (r && readable(r + 0x5D0, 4)) ? *(u8 **)(r + 0x5D0) : 0;
    for (int i = -1; i < 16; i++) { u8 *q = i < 0 ? qg : g_cand[i]; if (!q || (i >= 0 && now - g_cand_t[i] > 2000) || !lock_valid(q)) continue;
        float *x = char_xf(q), d = (x[9] - pp[9]) * (x[9] - pp[9]) + (x[10] - pp[10]) * (x[10] - pp[10]) + (x[11] - pp[11]) * (x[11] - pp[11]); if (d < bd) { bd = d; best = q; } }
    cur = best; return cur; }
static u8 *lock_target_old(void) { extern u8 *g_forced; static u8 *last_t; static u32 last_ok; u32 n0 = GetTickCount();
    if (!lock_char()) { if (lock_mode() && last_t && n0 - last_ok < 1000 && char_is(last_t)) return last_t; return 0; }   /* rides through lock dropouts (the camera sawtoothed) */
    if (g_forced && char_is(g_forced)) {   /* a swiped-to target: kept until the game has wanted another for 3 s (dead / out of reach) */
        static u32 fd_t; u8 *r0 = (u8 *)g_rt; u8 *qg0 = (r0 && readable(r0 + 0x5D0, 4)) ? *(u8 **)(r0 + 0x5D0) : 0;
        (void)qg0; (void)fd_t; if (!lock_in_range(g_forced)) g_forced = 0;   /* hard lock: kept until it dies, leaves range or the next swipe */
        if (g_forced) { last_t = g_forced; last_ok = n0; return g_forced; } }
    u8 *r = (u8 *)g_rt; u8 *qg = (r && readable(r + 0x5D0, 4)) ? *(u8 **)(r + 0x5D0) : 0; if (!char_is(qg)) qg = 0;
    static u8 *held; static u32 diff_t; u32 nw = GetTickCount();
    if (fattr_cached("nostickylock.txt") != 0xFFFFFFFF) return qg;
    if (held && char_is(held) && lock_in_range(held)) { last_t = held; last_ok = nw; (void)diff_t; return held; }   /* hard lock (user): only death, out of range (45 m) or a swipe ends it */
    held = qg; diff_t = 0; if (held) { last_t = held; last_ok = nw; } return held; }
u8 *lock_target(void) { u8 *t = lock_target_(); extern u8 *g_cover_block_tg; extern u32 g_cover_block_until; extern u8 *g_forced;
    if (t && t == g_cover_block_tg && (s32)(g_cover_block_until - GetTickCount()) > 0 && fattr_cached("coverunlock.txt") != 0xFFFFFFFF) { if (g_forced == t) g_forced = 0; return 0; }
    return t; }
u32 aim_reticle_keep(u32 t) { g_rt = t; if (t > 0x10000 && readable((void *)(t + 0xC), 4)) { u32 k = *(u32 *)(t + 0xC); if (k > 0x10000 && readable((void *)(k + 4), 4) && *(u32 *)(k + 4) == 16) g_t16 = GetTickCount() | 1; }
    return (x_held() && g_x_src != 1) || lock_mode() ? t : 0; }
__attribute__((naked)) static void lock_stub1(void) { __asm__ volatile(
    "incl _g_lock_calls\n\t cmpl _g_player_char, %ebx\n\t jne 2f\n\t cmpl $0, _g_mouse_on\n\t je 2f\n\t testl %edi, %edi\n\t jz 2f\n\t"
    "pushal\n\t pushl %edi\n\t call _aim_is_character\n\t addl $4, %esp\n\t testl %eax, %eax\n\t popal\n\t jz 2f\n\t"   /* (popal leaves the flags alone) */
    "incl _g_lock_hits\n\t pushl %edi\n\t xorl %edi, %edi\n\t call 2f\n\t popl %edi\n\t ret\n"
    "2:\n\t movl 0x638(%ebx), %eax\n\t pushl $0xC4E76\n\t ret"); }
__attribute__((naked)) static void lock_stub2(void) { __asm__ volatile(
    "incl _g_lock_calls\n\t cmpl _g_player_char, %ebx\n\t jne 2f\n\t cmpl $0, _g_mouse_on\n\t je 2f\n\t testl %edi, %edi\n\t jz 2f\n\t"
    "pushal\n\t pushl %edi\n\t call _aim_is_character\n\t addl $4, %esp\n\t testl %eax, %eax\n\t popal\n\t jz 2f\n\t"
    "incl _g_lock_hits\n\t pushl %edi\n\t xorl %edi, %edi\n\t call 2f\n\t popl %edi\n\t ret\n"
    "2:\n\t movl 0x638(%ebx), %eax\n\t pushl $0xC4EC6\n\t ret"); }
/* aim friction and magnetism (CharacterType_HandlePlayerInput): at 0x13389D, while character +0x7DC > 0 the stick is
   scaled by 0.75 (0x3C0550); from 0x1338CC the target under the reticle (+0x5E4) pulls the aim toward it. While the
   mouse aims: skip both (no friction, no reticle target) and continue at 0x1338D2. */
__attribute__((naked)) static void friction_stub(void) { __asm__ volatile(   /* (Milestone 105: the reticle's object is kept unless it is a character) */
    "cmpl $0, _g_mouse_on\n\t je 1f\n\t pushl %eax\n\t pushl %ecx\n\t pushl %edx\n\t pushl 0x5e4(%ebp)\n\t call _aim_reticle_keep\n\t addl $4, %esp\n\t movl %eax, %ebx\n\t popl %edx\n\t popl %ecx\n\t popl %eax\n\t pushl $0x1338D2\n\t ret\n"
    "1:\n\t flds 0x7dc(%ebp)\n\t pushl $0x1338A3\n\t ret"); }
static int hook6(u32 at, void *fn, const u8 *expect) { u8 *p = (u8 *)at; for (int k = 0; k < 6; k++) if (p[k] != expect[k]) return 0; p[0] = 0xE9; *(u32 *)(p + 1) = (u32)fn - (at + 5); p[5] = 0x90; return 1; }
/* follow camera (Milestone 87, over-the-shoulder aim): vtable 0x3A2640 +0x6C is FollowCamera_Update (0x702A0, thiscall,
   1 stack argument, returns al). The wrapper calls it, then (camdump.txt) logs the camera and player for field discovery. */
u32 g_cam_orig = 0x702A0;
extern u32 g_aiming; float *render_consts(void); float g_eye_ours[3];
/* eye position (Milestone 87): the game's shaders get the camera position (constant c31, used for fog and lighting) from
   its follow camera, which lags far behind on fast turns: nearby ground counted as far away and was drawn in full fog
   colour ("textures disappearing"). render.c swaps any constant equal to the follow camera's eye for ours. */
float g_eye_follow[3], g_eye_follow_prev[3]; u32 g_eye_swap;
static int GetFileAttributesA_cached_camoffset(void) { static int v = 2; if (v == 2) v = fattr_cached("gamecamera.txt") == 0xFFFFFFFF; return v; }   /* our camera by default; gamecamera.txt = the game's own */
/* Division-style camera (Milestone 87): right after FollowCamera_Update, the camera node (camera +0x48, vfunc +0x14:
   3x4 view, rows = camera right, up, back; translation = view space) is rebuilt from the player:
   - rotation = the character's aim matrix (+0x5BC) transposed: one shared yaw (and pitch) for camera and character,
     no follow lag, at any frame rate;
   - position = pivot + back x distance, pivot = character position (vfunc +0x40, +0x24) + height + right x shoulder.
   Only the position is eased between states (normal / aim). Every user of the camera (view, effects, muzzle flash,
   bullets) reads this node, so they all agree. Active while the mouse is captured (camtest.txt forces it). */
static int readable(const void *p, u32 n); static void fo(float v);
static float g_cam_state[3] = {3.6f, 1.0f, 0.5f};   /* distance, height, shoulder (eased) */
float g_cr[3], g_cu[3], g_cb[3]; u32 g_ourcam = 1; float g_view_pitch, g_sprint_t, g_pose_t;   /* our camera axes (right, up, back), set in cam_build */
static void cam_build(u8 *cam) {
    u8 *c = (u8 *)g_player_char; if (!c || *(u8 **)(cam + 0x148) != c) return;
    static u32 camtest = 2; if (camtest == 2) camtest = fattr_cached("camtest.txt") != 0xFFFFFFFF;
    if (!g_mouse_on && !camtest) return;
    {   int blk_has_rot(u8 *); u8 *cam_block(u8 *); extern u32 g_ourcam; g_ourcam = blk_has_rot(cam_block(cam));
        if (!g_ourcam) return; }   /* Milestone 91: a squad member taken over after a death keeps the game's camera (its layout differs) */
    if (scoped() && !special_steady() && fattr_cached("gamescope.txt") != 0xFFFFFFFF) {   /* (opt-in, gamescope.txt) zoomed in: the game's own scope camera (our shoulder camera sat
           4.4 m behind the gun; the game's scoped shot flies parallel to the view from the muzzle and landed low-left, magnified by the zoom) */
        {   static u32 lastsc; u32 n0 = GetTickCount(); if (n0 - lastsc > 200) for (int k = 0; k < 3; k++) { g_hip_E[k] = g_eye_ours[k]; g_hip_F[k] = -g_cb[k]; } lastsc = n0; }   /* the shoulder view, on the first scoped frame */
        float *nd = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48);
        for (int k = 0; k < 3; k++) { g_cr[k] = nd[k]; g_cu[k] = nd[3 + k]; g_cb[k] = nd[6 + k]; g_eye_ours[k] = -(nd[k] * nd[9] + nd[3 + k] * nd[10] + nd[6 + k] * nd[11]); }
        static u32 ns; if (ns++ < 3) out("  [cam] scoped: the game's scope camera\r\n");
        {   u32 nw = GetTickCount(); float P[3], R[3], F[3]; char_basis(c, P, R, F);
            if (g_zoom_t && nw - g_zoom_t > 900 && !g_sc_phase) {   /* settled: learn the eye's place */
                float d[3] = {g_eye_ours[0] - P[0], g_eye_ours[1] - P[1], g_eye_ours[2] - P[2]};
                g_sc_off[0] = d[0] * R[0] + d[2] * R[2]; g_sc_off[1] = d[1]; g_sc_off[2] = d[0] * F[0] + d[2] * F[2];
                if (!g_sc_have) { out("  [scope] learnt the scope eye (right, up, forward mm) "); fo(g_sc_off[0]); fo(g_sc_off[1]); fo(g_sc_off[2]); out("\r\n"); } g_sc_have = 1; }
            if (!g_sc_phase && !g_zoom_t && g_sc_have && fattr_cached("noscopesnap.txt") == 0xFFFFFFFF) {   /* the zoom's first frame (the glide starts
                   on the next): the shoulder view stays, not the game's raw scope camera for one frame (it flashed ~8 degrees low) */
                float *Eh = g_hip_E, *Fh = g_hip_F, fx = Fh[0], fz = Fh[2], hl = sqrt_(fx * fx + fz * fz); if (hl > 1e-4f) { fx /= hl; fz /= hl;
                    float U[3] = {-fx * Fh[1], hl, -fz * Fh[1]}, Rv[3] = {Fh[1] * U[2] - Fh[2] * U[1], Fh[2] * U[0] - Fh[0] * U[2], Fh[0] * U[1] - Fh[1] * U[0]};
                    if (Rv[0] * nd[0] + Rv[1] * nd[1] + Rv[2] * nd[2] < 0) { Rv[0] = -Rv[0]; Rv[1] = -Rv[1]; Rv[2] = -Rv[2]; }
                    for (int k = 0; k < 3; k++) { nd[k] = Rv[k]; nd[3 + k] = U[k]; nd[6 + k] = -Fh[k]; g_cr[k] = Rv[k]; g_cu[k] = U[k]; g_cb[k] = -Fh[k]; g_eye_ours[k] = Eh[k]; }
                    nd[9] = -(Rv[0] * Eh[0] + Rv[1] * Eh[1] + Rv[2] * Eh[2]); nd[10] = -(U[0] * Eh[0] + U[1] * Eh[1] + U[2] * Eh[2]); nd[11] = (Fh[0] * Eh[0] + Fh[1] * Eh[1] + Fh[2] * Eh[2]); } }
            if (g_sc_phase) {   /* one continuous move: glide to the final view, hold it, blend into the game's camera */
                extern float atan2_ext(float, float); g_game_F[0] = -nd[6]; g_game_F[1] = -nd[7]; g_game_F[2] = -nd[8]; float gF[3] = {-nd[6], -nd[7], -nd[8]}, gE[3] = {g_eye_ours[0], g_eye_ours[1], g_eye_ours[2]}, Ff[3], E[3], Fd[3]; int write = 1;
                {   float fx = g_sc_D[0], fz = g_sc_D[2], hl = sqrt_(fx * fx + fz * fz); if (hl < 1e-4f) hl = 1e-4f; fx /= hl; fz /= hl;
                    float pv = atan2_ext(g_sc_D[1], hl) + g_sc_dp, cp, sp; __asm__("fcos" : "=t"(cp) : "0"(pv)); __asm__("fsin" : "=t"(sp) : "0"(pv)); Ff[0] = fx * cp; Ff[1] = sp; Ff[2] = fz * cp; }
                u32 t = nw - g_sc_t0;
                if (g_sc_phase == 1) {
                    if (t < 150) { float a = t / 150.0f; a = a * a * (3.0f - 2.0f * a); for (int k = 0; k < 3; k++) { E[k] = g_hip_E[k] + (g_sc_E[k] - g_hip_E[k]) * a; Fd[k] = g_hip_F[k] + (Ff[k] - g_hip_F[k]) * a; } }
                    else { for (int k = 0; k < 3; k++) { E[k] = g_sc_E[k]; Fd[k] = Ff[k]; }
                        float dv = gF[0] * Ff[0] + gF[1] * Ff[1] + gF[2] * Ff[2], de = sqrt_((gE[0] - g_sc_E[0]) * (gE[0] - g_sc_E[0]) + (gE[1] - g_sc_E[1]) * (gE[1] - g_sc_E[1]) + (gE[2] - g_sc_E[2]) * (gE[2] - g_sc_E[2]));
                        if ((t > 350 && dv > 0.99998f && de < 0.1f) || t > 1500) { g_sc_phase = 2; g_sc_t1 = nw;
                            static u32 nl; if (nl++ < 20) { out("  [scope] hold ms "); dec(t); out(t > 1500 ? " (timed out)" : " (matched)"); out(" angle mrad "); fo(sqrt_(2.0f * (1.0f - (dv < 1.0f ? dv : 1.0f)))); out("eye mm "); fo(de); out("\r\n"); } } } }
                if (g_sc_phase == 2) { float b = (nw - g_sc_t1) / 150.0f; if (b >= 1.0f) { g_sc_phase = 0; write = 0; }
                    else { b = b * b * (3.0f - 2.0f * b); for (int k = 0; k < 3; k++) { E[k] = g_sc_E[k] + (gE[k] - g_sc_E[k]) * b; Fd[k] = Ff[k] + (gF[k] - Ff[k]) * b; } } }
                if (write) { float fl = sqrt_(Fd[0] * Fd[0] + Fd[1] * Fd[1] + Fd[2] * Fd[2]); for (int k = 0; k < 3; k++) Fd[k] /= fl;
                    float fx = Fd[0], fz = Fd[2], hl = sqrt_(fx * fx + fz * fz); if (hl > 1e-4f) { fx /= hl; fz /= hl;
                        float Fv[3] = {Fd[0], Fd[1], Fd[2]}, U[3] = {-fx * Fd[1], hl, -fz * Fd[1]}, Rv[3] = {Fv[1] * U[2] - Fv[2] * U[1], Fv[2] * U[0] - Fv[0] * U[2], Fv[0] * U[1] - Fv[1] * U[0]};
                        if (Rv[0] * nd[0] + Rv[1] * nd[1] + Rv[2] * nd[2] < 0) { Rv[0] = -Rv[0]; Rv[1] = -Rv[1]; Rv[2] = -Rv[2]; }
                        for (int k = 0; k < 3; k++) { nd[k] = Rv[k]; nd[3 + k] = U[k]; nd[6 + k] = -Fv[k]; g_cr[k] = Rv[k]; g_cu[k] = U[k]; g_cb[k] = -Fv[k]; g_eye_ours[k] = E[k]; }
                        nd[9] = -(Rv[0] * E[0] + Rv[1] * E[1] + Rv[2] * E[2]); nd[10] = -(U[0] * E[0] + U[1] * E[1] + U[2] * E[2]); nd[11] = (Fv[0] * E[0] + Fv[1] * E[1] + Fv[2] * E[2]); } } } }
        {   /* probe: does the scope camera follow the aim pitch? 2 s after each zoom-in, every 100 ms: its forward pitch, the aim pitch, ours */
            static u32 t0, tl, lastf, nz; u32 nw = GetTickCount(); extern float g_view_pitch; if (nw - lastf > 300) { t0 = nw; tl = 0; nz++; } lastf = nw;
            if (nz <= 12 && nw - t0 < 2000 && nw - tl >= 100) { tl = nw; float fy = -nd[7]; extern float atan2_ext(float, float); float sp = atan2_ext(fy, sqrt_(1.0f - fy * fy));
                out("  [scopecam] ms "); dec(nw - t0); out(" cam pitch mrad "); fo(sp); out("aim +5F4 "); fo(*(float *)(c + 0x5F4)); out("ours "); fo(g_view_pitch);
                out("eye "); fo(g_eye_ours[0]); fo(g_eye_ours[1]); fo(g_eye_ours[2]); out("\r\n"); } }
        return; }
    float *pos = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c) + 9;   /* +0x24 */
    float *A = (float *)(c + 0x5BC);   /* columns: right, up, back */
    float r[3] = {A[0], A[3], A[6]}, u[3] = {A[1], A[4], A[7]}, b[3] = {A[2], A[5], A[8]};
    {   /* Milestone 93: rebuild the axes from the character's yaw and our view pitch (the character's own pitch may be raised for the sprint pose) */
        extern float g_view_pitch; float fx = -b[0], fz = -b[2], fl = fx * fx + fz * fz;
        if (fl > 1e-6f) { float sq; __asm__("fsqrt" : "=t"(sq) : "0"(fl)); fx /= sq; fz /= sq; float pv = g_view_pitch, cp, sp;
            {   /* the lock camera (Flint's special, not zoomed): turns smoothly toward the locked character (the game's lock does not turn
                   Flint; turning her directly was choppy), eased in from the previous view. nolockcam.txt = off */
                extern float atan2_ext(float, float); extern float g_lockcam_yaw, g_lockcam_pitch; extern int g_lockcam_on; static float lsy, lsp; static int lon; static s64 llt, lqf;
                s64 lnw; if (!lqf) QueryPerformanceFrequency(&lqf); QueryPerformanceCounter(&lnw); float ldt = llt ? (float)(lnw - llt) / (float)lqf : 0.0f; llt = lnw; if (ldt > 0.1f) ldt = 0.1f;
                u8 *lock_target(void); u8 *tq = lock_target();
                if (tq && char_is(tq) && !scoped() && fattr_cached("nolockcam.txt") == 0xFFFFFFFF) { float *x = char_xf(tq);
                    /* both positions smoothed (~0.12 s): the walk animations' bob made the camera shake when either moved */
                    static float tf[3], pf[3]; static u8 *tfq; float kf = ldt * 8.0f; if (kf > 1.0f) kf = 1.0f; if (tfq != tq || !lon) { tfq = tq; for (int q = 0; q < 3; q++) { tf[q] = x[9 + q]; pf[q] = pos[q]; } }
                    for (int q = 0; q < 3; q++) { tf[q] += (x[9 + q] - tf[q]) * kf; pf[q] += (pos[q] - pf[q]) * kf; }
                    float v[3] = {tf[0] - pf[0], tf[1] + 0.35f - (pf[1] + 1.0f), tf[2] - pf[2]}, h = sqrt_(v[0] * v[0] + v[2] * v[2]);
                    if (h > 1.0f) { float yt = atan2_ext(v[0], v[2]), pt = atan2_ext(v[1], h);
                        if (!lon) { lon = 1; float hb = sqrt_(g_cb[0] * g_cb[0] + g_cb[2] * g_cb[2]); lsy = atan2_ext(-g_cb[0], -g_cb[2]); lsp = atan2_ext(-g_cb[1], hb); }
                        float k = ldt * 14.0f; if (k > 1.0f) k = 1.0f; float d = yt - lsy;   /* stronger (user) */ while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; lsy += d * k; lsp += (pt - lsp) * k;
                        float s1, c1; __asm__("fsin" : "=t"(s1) : "0"(lsy)); __asm__("fcos" : "=t"(c1) : "0"(lsy)); fx = s1; fz = c1; pv = lsp;
                        g_lockcam_yaw = lsy; g_lockcam_pitch = lsp; g_lockcam_on = 1; } }
                else { lon = 0; g_lockcam_on = 0; } }
            if (g_yaw_own || g_yaw_off != 0.0f) { extern float atan2_ext(float, float); float yw0 = (g_yaw_own ? g_cam_yaw : atan2_ext(fx, fz)) + g_yaw_off, s1, c1; __asm__("fsin" : "=t"(s1) : "0"(yw0)); __asm__("fcos" : "=t"(c1) : "0"(yw0)); fx = s1; fz = c1; }   /* zoom: the camera's yaw offset from Flint's */
            {   /* a special's lock-on: the game steers the aim toward the target and, while walking, wobbles about it; the camera follows a
                   smoothed direction (time constant ~0.12 s) instead of every wobble. The aim itself (shots) is unchanged. nolocksmooth.txt = off */
                extern float atan2_ext(float, float); static float sy, sp_; static int on; static s64 lt2, qf2; s64 nw2; if (!qf2) QueryPerformanceFrequency(&qf2); QueryPerformanceCounter(&nw2);
                float dt2 = lt2 ? (float)(nw2 - lt2) / (float)qf2 : 0.0f; lt2 = nw2; if (dt2 > 0.1f) dt2 = 0.1f;
                float yw = atan2_ext(fx, fz);
                { static int wl; static u32 lt0; int lk = lock_char() && fattr_cached("locksmooth.txt") != 0xFFFFFFFF;   /* opt-in; only with a locked character (Tex's Berserker also makes the targeting object, has no target: the smoothing flicked on and off - shake) */ u32 nl0 = GetTickCount(); if (lk && !wl) lt0 = nl0; wl = lk; on = on && lk; if (!lk) lt0 = 0;   /* the whole special: the lock flickered every few frames and each flip snapped the camera (shake) */
                if (lk && fattr_cached("nolocksmooth.txt") == 0xFFFFFFFF) {   /* locked: always smoothed (the game's lock aim shakes about the target) */   /* the lock catching (0.4 s, eased in from the previous view) and zoomed: smoothed */   /* zoomed: the game's tracking wobble is magnified - smoothed */   /* opt-in: the walking jitter was the sprint; smoothing only lagged behind a moving target */
                    if (!on) { on = 1; extern float g_cb[3]; float hb = g_cb[0] * g_cb[0] + g_cb[2] * g_cb[2]; float sq2; __asm__("fsqrt" : "=t"(sq2) : "0"(hb)); sy = atan2_ext(-g_cb[0], -g_cb[2]); sp_ = atan2_ext(-g_cb[1], sq2); }   /* from the view shown last frame: no snap */
                    float k = dt2 * (nl0 - lt0 < 250 || scoped() ? 12.0f : 22.0f);   /* faster (user) */ if (k > 1) k = 1; float d = yw - sy; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f;
                    sy += d * k; sp_ += (pv - sp_) * k; pv = sp_; float s1, c1; __asm__("fsin" : "=t"(s1) : "0"(sy)); __asm__("fcos" : "=t"(c1) : "0"(sy)); fx = s1; fz = c1; }
                else on = 0; } }
            __asm__("fcos" : "=t"(cp) : "0"(pv)); __asm__("fsin" : "=t"(sp) : "0"(pv));
            float F[3] = {fx * cp, sp, fz * cp}, U[3] = {-fx * sp, cp, -fz * sp};
            float R[3] = {F[1] * U[2] - F[2] * U[1], F[2] * U[0] - F[0] * U[2], F[0] * U[1] - F[1] * U[0]};   /* right = forward x up */
            if (R[0] * r[0] + R[1] * r[1] + R[2] * r[2] < 0) { R[0] = -R[0]; R[1] = -R[1]; R[2] = -R[2]; }   /* keep the game's handedness */
            for (int k = 0; k < 3; k++) { r[k] = R[k]; u[k] = U[k]; b[k] = -F[k]; } } }
    {   static u32 yt = 2; if (yt == 2) yt = fattr_cached("camyaw.txt") != 0xFFFFFFFF;   /* test: turn the drawn camera 30 degrees about the world up axis */
        if (yt) { float cs = 0.8660254f, sn = 0.5f; float R[3], B[3], U[3];
            R[0] = r[0] * cs + r[2] * sn; R[1] = r[1]; R[2] = -r[0] * sn + r[2] * cs; B[0] = b[0] * cs + b[2] * sn; B[1] = b[1]; B[2] = -b[0] * sn + b[2] * cs; U[0] = u[0] * cs + u[2] * sn; U[1] = u[1]; U[2] = -u[0] * sn + u[2] * cs;
            for (int k = 0; k < 3; k++) { r[k] = R[k]; b[k] = B[k]; u[k] = U[k]; } } }
    {   /* test (camorbit.txt, degrees, read every second): turn our camera around the character to look at the pose */
        static float orb; static u32 to; u32 nt = GetTickCount(); if (nt - to > 1000) { to = nt; orb = 0; HANDLE f = CreateFileA("camorbit.txt", 0x80000000, 1, 0, 3, 0x80, 0);
            if (f != (HANDLE)-1) { char s[16] = {0}; DWORD n = 0; ReadFile(f, s, 15, &n, 0); CloseHandle(f); float x = 0; int ng = 0; for (DWORD i = 0; i < n; i++) { if (s[i] == 0x2D) ng = 1; else if (s[i] >= 0x30 && s[i] <= 0x39) x = x * 10 + (s[i] - 0x30); } orb = (ng ? -x : x) * 0.0174533f; } }
        if (orb != 0) { float cs, sn; __asm__("fcos" : "=t"(cs) : "0"(orb)); __asm__("fsin" : "=t"(sn) : "0"(orb)); float R2[3], B2[3], U2[3];
            R2[0] = r[0] * cs + r[2] * sn; R2[1] = r[1]; R2[2] = -r[0] * sn + r[2] * cs; B2[0] = b[0] * cs + b[2] * sn; B2[1] = b[1]; B2[2] = -b[0] * sn + b[2] * cs; U2[0] = u[0] * cs + u[2] * sn; U2[1] = u[1]; U2[2] = -u[0] * sn + u[2] * cs;
            for (int k = 0; k < 3; k++) { r[k] = R2[k]; b[k] = B2[k]; u[k] = U2[k]; } } }
    static s64 lt, qf; s64 now; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&now);
    float dt = lt ? (float)(now - lt) / (float)qf : 0.0f; lt = now; if (dt > 0.1f) dt = 0.1f;
    extern u32 g_sprinting; float goal[3];
    if (g_aiming && fattr_cached("forceaim.txt") != 0xFFFFFFFF) { goal[0] = 3.2f; goal[1] = 1.0f; goal[2] = 0.5f; }   /* test: wider view of the aim pose */
    else if (g_aiming) { goal[0] = 1.6f; goal[1] = 1.15f; goal[2] = 0.65f; } else if (g_sprinting) { goal[0] = 4.2f; goal[1] = 1.15f; goal[2] = 0.45f; } else { goal[0] = 3.6f; goal[1] = 1.0f; goal[2] = 0.5f; }
    if (special_steady() && fattr_cached("specialcenter.txt") != 0xFFFFFFFF) goal[2] = 0.0f;
    if (scoped()) { goal[0] = -g_sc_off[2]; goal[1] = g_sc_off[1]; goal[2] = g_sc_off[0]; }   /* zoomed in: the eye eases to the scope's (behind, up, right of Flint's position) */   /* opt-in: centring during the special made the screen shake */   /* a special (Flint's auto-lock): the camera centred behind the
       character like the game's own, so the game's target box (placed from its camera) lines up; nospecialcenter.txt = stay on the shoulder */
    float ease = dt * 8.0f; if (ease > 1.0f) ease = 1.0f;   /* position only */
    { extern float g_aim_t; float at = dt * 10.0f; if (at > 1.0f) at = 1.0f; g_aim_t += ((g_aiming ? 1.0f : 0.0f) - g_aim_t) * at;
      u32 *pd = gpu_pad(); if (pd) { pd[54] = (u32)((0.8f + 0.45f * (1.0f - g_aim_t)) * 1000.0f); pd[55] = GetTickCount() | 1; } }   /* pad[55]: gameplay heartbeat (the viewer moves the HUD into the corners only in gameplay, not in menus) */   /* hip 1.25, aiming 0.8 */   /* reticle scale for the viewer (Milestone 90) */
    for (int k = 0; k < 3; k++) g_cam_state[k] += (goal[k] - g_cam_state[k]) * ease;
    static float py_s; static u8 *py_c; { float k6 = dt * 12.0f; if (k6 > 1.0f) k6 = 1.0f; float d6 = pos[1] - py_s;   /* the pivot height smoothed (~0.1 s): in specials
        the character's base position bobbed and the camera bounced with it (18 reversals, up to 6 cm a frame). Big moves (jumps, falls) are followed at once */
        if (py_c != c || d6 > 0.6f || d6 < -0.6f || fattr_cached("nocamheightsmooth.txt") != 0xFFFFFFFF) py_s = pos[1]; else py_s += d6 * k6; py_c = c; }
    float pv3[3] = {pos[0], py_s, pos[2]};
    float eye[3]; for (int k = 0; k < 3; k++) eye[k] = pv3[k] + (k == 1 ? g_cam_state[1] : 0.0f) + r[k] * g_cam_state[2] + b[k] * g_cam_state[0];
    float eye_want[3] = {eye[0], eye[1], eye[2]}, st_pivot[3] = {pos[0], py_s + g_cam_state[1], pos[2]};
    {   /* Milestone 102: camera collision through the viewer (the game's world raycast 0xDDD10 never saw the walls, Milestone 94):
           the viewer draws the solid surfaces into a small depth view from the pivot toward the camera and sends back the
           nearest distance (RING pad[80]); the camera stops in front of it at once and eases back out. nocamcollide.txt = off */
        void cam_collide(const float *st, float *eye, float dt); if (!scoped()) cam_collide(st_pivot, eye, dt); }
    for (int k = 0; k < 3; k++) { g_eye_ours[k] = eye[k]; g_cr[k] = r[k]; g_cu[k] = u[k]; g_cb[k] = b[k]; }
    {   /* the lock's cover test: 9 depth samples over the locked character (head / chest / hips x left / middle / right, bfgl.c reads them);
           a sample with something more than 0.6 m nearer than the character is covered. More than 45% covered for 0.25 s = the lock lets it go */
        u32 *pd = gpu_pad(); u8 *lock_target_(void); u8 *tg = lock_target_();
        if (pd && tg && g_proj_t) { float *x = char_xf(tg), *P = g_projB[g_proj_i & 1], V[16], VP[16], IV[16];
            const float *ax[3] = {r, u, b}; for (int q = 0; q < 3; q++) { for (int k = 0; k < 3; k++) V[q * 4 + k] = ax[q][k]; V[q * 4 + 3] = -(ax[q][0] * eye[0] + ax[q][1] * eye[1] + ax[q][2] * eye[2]); } V[12] = V[13] = V[14] = 0; V[15] = 1;
            for (int q = 0; q < 4; q++) for (int k = 0; k < 4; k++) { float a2 = 0; for (int m = 0; m < 4; m++) a2 += P[q * 4 + m] * V[m * 4 + k]; VP[q * 4 + k] = a2; }
            float C[3] = {x[9], x[10] + 0.35f, x[11]}, cl[4], cr[4], cu2[4];
            for (int q = 0; q < 4; q++) { cl[q] = VP[q * 4 + 3]; cr[q] = VP[q * 4 + 3]; cu2[q] = VP[q * 4 + 3]; for (int k = 0; k < 3; k++) { cl[q] += VP[q * 4 + k] * C[k]; cr[q] += VP[q * 4 + k] * (C[k] + r[k] * 0.25f); cu2[q] += VP[q * 4 + k] * (C[k] + (k == 1 ? 0.4f : 0.0f)); } }
            if (cl[3] > 0.5f && cr[3] > 0.5f && cu2[3] > 0.5f) { float scx = 320 + 320 * cl[0] / cl[3], scy = 240 - 240 * cl[1] / cl[3], sxp = 320 + 320 * cr[0] / cr[3] - scx, syp = scy - (240 - 240 * cu2[1] / cu2[3]);
                if (sxp < 0) sxp = -sxp; if (syp < 0) syp = -syp; if (sxp < 1) sxp = 1; if (syp < 1) syp = 1;
                s32 icx = (s32)scx, icy = (s32)scy; if (icx >= 0 && icx < 640 && icy >= 0 && icy < 480) {
                    pd[96] = (u32)icx << 16 | (u32)icy; pd[97] = (u32)sxp << 16 | (u32)syp;
                    static u32 lastc; if (pd[107] != lastc && inv4(VP, IV)) { lastc = pd[107]; int cov = 0;
                        float dt = -((C[0] - eye[0]) * b[0] + (C[1] - eye[1]) * b[1] + (C[2] - eye[2]) * b[2]);
                        for (int i = 0; i < 9; i++) { u32 db = pd[98 + i]; float d = *(float *)&db; if (!(d > 0.0f && d < 0.99999f)) continue;
                            s32 px = icx + (i % 3 - 1) * (s32)sxp, py = icy + (i / 3 - 1) * (s32)syp; float sc[4] = {(px - 320.0f) / 320.0f, (240.0f - py) / 240.0f, d, 1.0f}, X[4];
                            for (int q = 0; q < 4; q++) { X[q] = 0; for (int k = 0; k < 4; k++) X[q] += IV[q * 4 + k] * sc[k]; } if (X[3] == 0) continue;
                            float ds = -((X[0] / X[3] - eye[0]) * b[0] + (X[1] / X[3] - eye[1]) * b[1] + (X[2] / X[3] - eye[2]) * b[2]); if (ds < dt - 0.6f) cov++; }
                        g_cover_frac = cov / 9.0f; static u32 bad_t; u32 nw = GetTickCount();
                        if (g_cover_frac > 0.45f) { if (!bad_t) bad_t = nw | 1; if (nw - bad_t > 250) { g_cover_block_tg = tg; g_cover_block_until = nw + 1000; bad_t = 0;
                            static u32 nl; if (nl++ < 30) { out("  [cover] target "); hex((u32)tg); out(" covered "); dec(cov); out("/9: let go\r\n"); } } } else bad_t = 0; } }
                else pd[96] = 0; } else pd[96] = 0; }
        else if (pd) pd[96] = 0; }
    {   /* probe (the target box): during a lock-on special, every 300 ms: each field of the targeting object (g_rt, type 16) holding a
           character, with that character's screen position (x, y of 640x480), and the target shots go to - which field is the box? */
        static u32 tb, nb; u32 nw = GetTickCount(); u8 *rt0 = (u8 *)g_rt;
        if (lock_mode() && rt0 && nb < 120 && nw - tb > 300 && readable(rt0, 0x800)) { tb = nw; nb++; out("  [box]");
            for (u32 o = 0; o < 0x800; o += 4) { u8 *q = *(u8 **)(rt0 + o); if (!char_is(q)) continue; float *x = char_xf(q);
                float v[3] = {x[9] - eye[0], x[10] + 0.35f - eye[1], x[11] - eye[2]}, f = -(v[0] * b[0] + v[1] * b[1] + v[2] * b[2]);
                out(" +"); hex(o); out("="); hex((u32)q); if (f > 0.5f) { s32 sx = (s32)(320 + 320 * (v[0] * r[0] + v[1] * r[1] + v[2] * r[2]) / f / 0.75f), sy = (s32)(240 - 240 * (v[0] * u[0] + v[1] * u[1] + v[2] * u[2]) / f / 0.57f);
                    out(" @"); if (sx < 0) { out("-"); sx = -sx; } dec((u32)sx); out(","); if (sy < 0) { out("-"); sy = -sy; } dec((u32)sy); } }
            out("\r\n"); } }
    {   /* probe (zoom jumps): for 1 s after each zoom in or out, every frame: ms, eye relative to Flint (right, up, back mm), camera yaw and
           pitch, Flint's yaw (mrad), zoom level, the yaw override and the projection's P11 (field of view) */
        extern float atan2_ext(float, float); static int lz = -1; static u32 t0, nl; int zz = scoped(); u32 nw = GetTickCount();
        if (zz != lz) { t0 = nw; lz = zz; } if (nw - t0 < 1000 && nl < 1500) { nl++; float *A = (float *)(c + 0x5BC);
            float d[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
            out("  [zoomf] "); out(zz ? "in " : "out "); dec(nw - t0); out(" eye r/u/b "); fo(d[0] * r[0] + d[2] * r[2]); fo(d[1]); fo(d[0] * b[0] + d[2] * b[2]);
            out("cam yaw "); fo(atan2_ext(-b[0], -b[2])); out("pitch "); fo(atan2_ext(-b[1], sqrt_(b[0] * b[0] + b[2] * b[2]))); out("flint yaw "); fo(atan2_ext(-A[2], -A[8]));
            out("act "); dec((u32)g_yaw_act); { float *pj = g_projB[g_proj_i & 1]; out(" P11 "); fo(pj[5] < 0 ? -pj[5] : pj[5]); } out("\r\n"); } }
    {   /* probe (special + zoom shake): every frame while a special is on and zoomed (up to 900 lines): ms, lock, camera yaw / pitch,
           Flint's yaw and aim pitch (+0x5F4), our view pitch, eye relative to Flint (right, up, back mm) */
        extern float atan2_ext(float, float); extern float g_view_pitch; static u32 n9, t9; u32 nw = GetTickCount();
        static int wl9; static u32 nlk; int lk9 = special_hold(); if (lk9 && !wl9) { t9 = nw; nlk++; if (nlk > 10) n9 = 0; } wl9 = lk9;   /* from each special's start: 1.5 s, 10 times */
        if (lk9 && nw - t9 < 1500 && n9 < 1500) { n9++; float *A = (float *)(c + 0x5BC); float d[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
            out("  [lockf] "); out(scoped() ? "zoom " : ""); { u8 *r_ = (u8 *)g_rt; out("tgt "); hex(r_ && readable(r_ + 0x5D0, 4) ? *(u32 *)(r_ + 0x5D0) : 0); out(" "); } dec(nw - t9); out(" lock "); dec((u32)lock_char()); out(" cam yaw "); fo(atan2_ext(-b[0], -b[2])); out("pitch "); fo(atan2_ext(-b[1], sqrt_(b[0] * b[0] + b[2] * b[2])));
            out("flint yaw "); fo(atan2_ext(-A[2], -A[8])); out("aim pitch "); fo(*(float *)(c + 0x5F4)); out("view pitch "); fo(g_view_pitch);
            out("eye "); fo(d[0] * r[0] + d[2] * r[2]); fo(d[1]); fo(d[0] * b[0] + d[2] * b[2]); out("\r\n"); } }
    {   /* probe (lock jitter): during a special, per 250 ms: the largest frame-to-frame change and the direction reversals of the camera's
           yaw, pitch, eye (sideways / up) and of the character's position, and the raw aim yaw (+0x5BC) */
        extern float atan2_ext(float, float); static float ly, lp, lay, le[3], lc[3], dly, dlp, dlay, dle, dlc; static float mx[5]; static u32 rv[5], tl, n, have;
        if (special_steady()) { u32 nw = GetTickCount(); float *A = (float *)(c + 0x5BC);
            float yw = atan2_ext(-b[0], -b[2]), pt = atan2_ext(-b[1], sqrt_(b[0] * b[0] + b[2] * b[2])), ay = atan2_ext(-A[2], -A[8]);
            if (have) { float d[5]; d[0] = yw - ly; d[1] = pt - lp; d[2] = ay - lay; for (int k = 0; k < 3; k++) if (d[k] > 3.14159f) d[k] -= 6.28318f; else if (d[k] < -3.14159f) d[k] += 6.28318f;
                d[3] = (eye[0] - le[0]) * r[0] + (eye[2] - le[2]) * r[2]; d[4] = (pos[0] - lc[0]) * r[0] + (pos[2] - lc[2]) * r[2];
                float prev[5] = {dly, dlp, dlay, dle, dlc};
                for (int k = 0; k < 5; k++) { float a = d[k] < 0 ? -d[k] : d[k]; if (a > mx[k]) mx[k] = a; if (d[k] * prev[k] < 0 && a > 0.0005f) rv[k]++; }
                dly = d[0]; dlp = d[1]; dlay = d[2]; dle = d[3]; dlc = d[4]; }
            ly = yw; lp = pt; lay = ay; for (int k = 0; k < 3; k++) { le[k] = eye[k]; lc[k] = pos[k]; } have = 1;
            if (nw - tl > 250 && n < 300) { tl = nw; n++; out("  [lockjit] lock "); dec((u32)lock_char()); out(" max/frame: cam yaw mrad "); fo(mx[0]); out("pitch "); fo(mx[1]); out("aim yaw "); fo(mx[2]);
                out("eye side mm "); fo(mx[3]); out("char side mm "); fo(mx[4]); out(" reversals "); for (int k = 0; k < 5; k++) { dec(rv[k]); out(" "); } out("\r\n"); for (int k = 0; k < 5; k++) { mx[k] = 0; rv[k] = 0; } } }
        else have = 0; }
    { void cam_probe_send(const float *st, const float *eye_want); cam_probe_send(st_pivot, eye_want); }   /* the probe for the next frames, with this frame's final camera */
    {   /* Milestone 93 test (partmove.txt): lift both hand parts (+0x3F8, +0x3FC) by 0.6 m - does the drawn mesh follow? */
        static u32 on = 2; if (on == 2) on = fattr_cached("partmove.txt") != 0xFFFFFFFF;
        if (on) for (int h = 0; h < 2; h++) { u8 *p = *(u8 **)(c + 0x3F8 + h * 4); int part_like(u8 *, float *); if (part_like(p, pos)) ((float *)(p + 0x38))[1] += 0.6f; } }
    {   /* Milestone 93 probe (partprobe.txt): the character's bone parts - objects with a rotation at +0x14 and a position
           (+0x38) near the character - found directly or in pointer vectors (begin, end) inside the character */
        static u32 on = 2, done, t0; if (on == 2) on = fattr_cached("partprobe.txt") != 0xFFFFFFFF;
        if (on && done < 2) { if (!t0) t0 = GetTickCount(); if (GetTickCount() - t0 > 20000 + done * 3000) { done++;
            int part_like(u8 *, float *); out("  [parts] char "); hex((u32)c); out(" pos "); fo(pos[0]); fo(pos[1]); fo(pos[2]); out("\r\n");
            for (u32 o = 0; o < 0x1000; o += 4) { u8 *p = *(u8 **)(c + o); if (part_like(p, pos)) { out("   direct +"); hex(o); out(" "); hex((u32)p); float *q = (float *)(p + 0x38); out(" rel "); fo(q[0] - pos[0]); fo(q[1] - pos[1]); fo(q[2] - pos[2]); out("\r\n"); }
                u32 bgn = *(u32 *)(c + o), end = *(u32 *)(c + o + 4);
                if (end > bgn && end - bgn <= 0x400 && !((end - bgn) & 3) && bgn > 0x10000 && end < 0x7FFE0000u && readable((void *)bgn, end - bgn)) {
                    u32 n = (end - bgn) / 4, cnt = 0; for (u32 k = 0; k < n; k++) if (part_like(*(u8 **)(bgn + k * 4), pos)) cnt++;
                    if (cnt >= 8) { out("   vector +"); hex(o); out(" n "); dec(n); out(" parts "); dec(cnt); out("\r\n");
                        for (u32 k = 0; k < n; k++) { u8 *p = *(u8 **)(bgn + k * 4); if (!part_like(p, pos)) continue; float *q = (float *)(p + 0x38); out("     ["); dec(k); out("] "); hex((u32)p); out(" rel "); fo(q[0] - pos[0]); fo(q[1] - pos[1]); fo(q[2] - pos[2]); out("\r\n"); } } } } } } }
    /* the game's follow camera is a look-at camera whose eye position ([character +0x6B8] + 0x238) springs after the
       character and trailed far behind on fast turns; everything it drives (streaming, the eye constants) lagged with it.
       It now starts every update from our eye (Milestone 87). */
    { static u32 ab = 2; if (ab == 2) ab = fattr_cached("camrotgame.txt") != 0xFFFFFFFF || fattr_cached("camposgame.txt") != 0xFFFFFFFF;
      u8 *cam_block(u8 *); u8 *blk = cam_block(cam);   /* Milestone 91: through the camera object, for any character */
      int blk_has_rot(u8 *); int isrot = 0; if (!blk_has_rot(blk)) { static u32 n; if (n++ < 5) { out("  [cam] block "); hex((u32)blk); out(" of "); hex((u32)c); out(" holds no rotation: not written\r\n"); } }
      else if (0) {   /* Milestone 91: only a block that already holds a rotation matrix. After the
            player dies the game hands control to another squad member whose +0x6B8 block is not a camera block yet;
            writing floats over its pointers crashed the game (0x2C2720) or hid the world */
          float *W = (float *)(blk + 0x214); isrot = 1;
          for (int k = 0; k < 3 && isrot; k++) { float l = W[k * 3] * W[k * 3] + W[k * 3 + 1] * W[k * 3 + 1] + W[k * 3 + 2] * W[k * 3 + 2]; if (!(l > 0.9f && l < 1.1f)) isrot = 0; }
          if (isrot) { float d01 = W[0] * W[3] + W[1] * W[4] + W[2] * W[5], d02 = W[0] * W[6] + W[1] * W[7] + W[2] * W[8]; if (d01 > 0.1f || d01 < -0.1f || d02 > 0.1f || d02 < -0.1f) isrot = 0; }
          if (!isrot) { static u32 n; if (n++ < 5) { out("  [cam] block "); hex((u32)blk); out(" of "); hex((u32)c); out(" holds no rotation: not written\r\n"); } } }
      isrot = blk_has_rot(blk);
      {   static u32 nb, tl; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; nb = fattr_cached("noblockwrite.txt") != 0xFFFFFFFF; } if (nb) isrot = 0; }   /* A/B (Milestone 92): leave the game camera's world transform alone */
      {   /* Milestone 91 probe: which pointer (in the camera object, or one level down) leads to blk+0x214, the block that worked */
          static u32 done = 2; if (isrot && done < 2) { done++; u32 tgt = (u32)blk + 0x214; out("  [camblk] target "); hex(tgt); out(" cam "); hex((u32)cam);
              for (u32 o = 0; o < 0x400; o += 4) { u32 p = *(u32 *)(cam + o); if (p <= tgt && tgt - p < 0x800) { out(" cam+"); hex(o); out("->+"); hex(tgt - p); }
                  if (p > 0x10000 && p < 0x7FFE0000u && (p & 3) == 0 && readable((void *)p, 0x200)) for (u32 q = 0; q < 0x200; q += 4) { u32 p2 = *(u32 *)(p + q); if (p2 <= tgt && tgt - p2 < 0x400) { out(" cam+"); hex(o); out("+"); hex(q); out("->+"); hex(tgt - p2); } } }
              for (u32 o = 0; o < 0xC00; o += 4) { u32 p = *(u32 *)(c + o); if (p <= tgt && tgt - p < 0x800 && o != 0x6B8) { out(" char+"); hex(o); out("->+"); hex(tgt - p); } }
              out("\r\n"); } }
      if (!ab && isrot) {
          /* the game camera's world transform ([character +0x6B8] + 0x214: rotation = view rotation transposed, eye at
             +0x238): the game prepares what is visible from it, so it must match our camera exactly */
          float *W = (float *)(blk + 0x214); for (int k = 0; k < 3; k++) { W[k * 3 + 0] = r[k]; W[k * 3 + 1] = u[k]; W[k * 3 + 2] = b[k]; }
          float *st = (float *)(blk + 0x238); st[0] = eye[0]; st[1] = eye[1]; st[2] = eye[2]; } }
    float *node = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48);
    {   /* A/B tests (Milestone 87): camrotgame.txt keeps the game camera's rotation (our position); camposgame.txt keeps the
           game camera's position (our rotation) */
        static u32 rg = 2, pg = 2; if (rg == 2) { rg = fattr_cached("camrotgame.txt") != 0xFFFFFFFF; pg = fattr_cached("camposgame.txt") != 0xFFFFFFFF; }
        if (pg) for (int k = 0; k < 3; k++) eye[k] = -(node[k] * node[9] + node[3 + k] * node[10] + node[6 + k] * node[11]);
        if (rg) for (int k = 0; k < 3; k++) { r[k] = node[k]; u[k] = node[3 + k]; b[k] = node[6 + k]; } }
    for (int k = 0; k < 3; k++) { node[k] = r[k]; node[3 + k] = u[k]; node[6 + k] = b[k]; }
    node[9] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]); node[10] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]); node[11] = -(b[0] * eye[0] + b[1] * eye[1] + b[2] * eye[2]);
}
/* node-getter callers (cullfind.txt): wraps the camera transform's vfunc +0x14 and logs each new return address */
u32 g_node_get_orig; static u32 g_ng_seen[64], g_ng_n;
u32 g_seq[200], g_seq_n, g_seq_on;   /* event sequence: 1 = camera rebuilt (cam_after), 2 = player turned, else = node read (return address) */
void seq_add(u32 e) { if (g_seq_on && g_seq_n && g_seq[g_seq_n - 1] == e) return; if (g_seq_on && g_seq_n < 200) g_seq[g_seq_n++] = e; if (g_seq_on && g_seq_n == 200) { g_seq_on = 0; out("  [seq]"); for (u32 k = 0; k < 200; k++) { out(" "); if (g_seq[k] == 1) out("CAM"); else if (g_seq[k] == 2) out("TURN"); else hex(g_seq[k]); } out("\r\n"); g_seq_n = 201; } }
void node_get_log(u32 ret) { seq_add(ret); for (u32 k = 0; k < g_ng_n; k++) if (g_ng_seen[k] == ret) return; if (g_ng_n < 64) { g_ng_seen[g_ng_n++] = ret; } }
__attribute__((naked)) static void node_get_stub(void) { __asm__ volatile(
    "pushal\n\t pushl 32(%esp)\n\t call _node_get_log\n\t addl $4, %esp\n\t popal\n\t jmp *_g_node_get_orig"); }
int cam_skip(u8 *cam);
u8 *g_cam_obj;   /* the player's camera object (for the field-of-view probe) */
void cam_after(u8 *cam) {
    if (g_player_char && *(u8 **)(cam + 0x148) == (u8 *)g_player_char) g_cam_obj = cam;
    {   static u32 done; if (!done && fattr_cached("cullfind.txt") != 0xFFFFFFFF) { done = 1; u32 *vt = *(u32 **)(cam + 0x48); DWORD op;
            IMP BOOL WINAPI VirtualProtect(void *, u32, DWORD, DWORD *); VirtualProtect(&vt[5], 4, 0x40, &op); g_node_get_orig = vt[5]; vt[5] = (u32)node_get_stub; out("  [nodeget] wrapped vtable "); hex((u32)vt); out("\r\n"); } }
    {   /* follow-camera lag probe (camlag.txt): angle between the game's camera (node, before we rebuild it) and the
           character's facing, largest per 0.5 s; camstiff.txt sets the follow camera's springs (+0x128, +0x130) to 400 */
        static u32 on = 2, stiff = 2; static float stiffv = 120.0f; if (on == 2) { on = fattr_cached("camlag.txt") != 0xFFFFFFFF; stiff = fattr_cached("camstiff.txt") != 0xFFFFFFFF;
            if (stiff) { IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE); IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *); IMP BOOL WINAPI CloseHandle(HANDLE);
                HANDLE f = CreateFileA("camstiff.txt", 0x80000000, 1, 0, 3, 0x80, 0); if (f != (HANDLE)-1) { char t[16]; DWORD n = 0; ReadFile(f, t, 15, &n, 0); CloseHandle(f); s32 v = 0; for (DWORD k = 0; k < n; k++) if (t[k] >= 0x30 && t[k] <= 0x39) v = v * 10 + t[k] - 0x30; if (v > 0) stiffv = (float)v; } } }
        u8 *c = (u8 *)g_player_char;
        if (stiff && c && *(u8 **)(cam + 0x148) == c) { *(float *)(cam + 0x128) = stiffv; *(float *)(cam + 0x130) = stiffv; }
        if (on && c && *(u8 **)(cam + 0x148) == c) { float *node = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48);
            float *A = (float *)(c + 0x5BC); float d = node[0] * A[0] + node[1] * A[3] + node[2] * A[6];   /* cos of the yaw difference (right axes) */
            static float worst = 1.0f; static u32 t0; if (d < worst) worst = d; u32 t = GetTickCount();
            if (t - t0 > 500) { extern u32 g_mt_active; if (g_mt_active) { s32 v = (s32)(worst * 1000); out("  [camlag] min cos x1000 "); if (v < 0) { out("-"); v = -v; } dec((u32)v); out("\r\n"); } worst = 1.0f; t0 = t; } } }
    if (g_player_char && *(u8 **)(cam + 0x148) == (u8 *)g_player_char) { u32 *pd = gpu_pad(); if (pd) pd[55] = GetTickCount() | 1; }   /* gameplay heartbeat for the HUD layout, mouse or controller (Milestone 97) */
    {   static u32 t0; if (g_player_char && *(u8 **)(cam + 0x148) == (u8 *)g_player_char) { if (!t0) t0 = GetTickCount(); if (!g_seq_n && GetTickCount() - t0 > 20000) g_seq_on = 1; seq_add(1); } }
    {   /* eye probe (eyeprobe.txt): which shader constants hold a camera position: ours (built last frame) or the follow
           camera's (its node before we rebuild it) */
        static u32 on = 2; if (on == 2) on = fattr_cached("eyeprobe.txt") != 0xFFFFFFFF;
        u8 *c = (u8 *)g_player_char;
        if (on && c && *(u8 **)(cam + 0x148) == c) { extern float g_eye_ours[3]; static float fe[3]; static u32 t0, n;
            float *nd = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48);
            float *C = render_consts(); u32 t = GetTickCount();
            if (t - t0 > 1500 && n < 12) { t0 = t; n++; out("  [eye] ours "); for (int k = 0; k < 3; k++) { s32 v = (s32)(g_eye_ours[k] * 100); out(" "); if (v < 0) { out("-"); v = -v; } dec((u32)v); }
                out(" follow"); for (int k = 0; k < 3; k++) { s32 v = (s32)(fe[k] * 100); out(" "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out(" | consts:");
                for (int q = 0; q < 192; q++) { float *v4 = C + q * 4; float d1 = 0, d2 = 0; for (int k = 0; k < 3; k++) { d1 += (v4[k] - g_eye_ours[k]) * (v4[k] - g_eye_ours[k]); d2 += (v4[k] - fe[k]) * (v4[k] - fe[k]); }
                    if (d1 < 0.25f) { out(" c"); dec(q); out("=ours"); } else if (d2 < 0.25f) { out(" c"); dec(q); out("=follow"); } }
                out("\r\n"); }
            for (int k = 0; k < 3; k++) fe[k] = -(nd[k] * nd[9] + nd[3 + k] * nd[10] + nd[6 + k] * nd[11]); } }
    {   u8 *c = (u8 *)g_player_char; static u32 ct = 2; if (ct == 2) ct = fattr_cached("camtest.txt") != 0xFFFFFFFF;
        if (c && *(u8 **)(cam + 0x148) == c) { float *nd = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48);
            for (int k = 0; k < 3; k++) { g_eye_follow_prev[k] = g_eye_follow[k]; g_eye_follow[k] = -(nd[k] * nd[9] + nd[3 + k] * nd[10] + nd[6 + k] * nd[11]); }
            g_eye_swap = GetFileAttributesA_cached_camoffset() && (g_mouse_on || ct); } }
    {   /* statefind.txt: during the fast-turn test, search Xbox memory for the follow camera's right/back vectors */
        static u32 on = 2, n, t0; if (on == 2) on = fattr_cached("statefind.txt") != 0xFFFFFFFF; extern u32 g_mt_active; extern volatile s32 g_fake_dx;
        u8 *c = (u8 *)g_player_char;
        if (on && c && *(u8 **)(cam + 0x148) == c && g_mt_active && g_fake_dx > 200 && n < 3 && GetTickCount() - t0 > 2000) { t0 = GetTickCount(); n++;
            float *nd = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48);
            {   float e[3]; for (int k = 0; k < 3; k++) e[k] = -(nd[k] * nd[9] + nd[3 + k] * nd[10] + nd[6 + k] * nd[11]);
                out("  [state] eye "); for (int k = 0; k < 3; k++) { s32 v = (s32)(e[k] * 100); out(" "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out(" found at:"); u32 hits = 0;
                IMP u32 WINAPI VirtualQuery(const void *, void *, u32); u32 mbi[7];
                for (u32 a = 0x10000; a < 0x7FFE0000u && hits < 24; ) { if (!VirtualQuery((void *)a, mbi, sizeof mbi)) break; u32 base = mbi[0], size = mbi[3], state = mbi[4], prot = mbi[5];
                    if (state == 0x1000 && (prot & 0xEE) && !(prot & 0x101)) { for (float *p = (float *)base; p + 3 <= (float *)(base + size) && hits < 24; p++) { float d0 = p[0] - e[0]; if (d0 < 0.01f && d0 > -0.01f) { float d1 = p[1] - e[1], d2 = p[2] - e[2]; if (d1 < 0.01f && d1 > -0.01f && d2 < 0.01f && d2 > -0.01f) { out(" "); hex((u32)p); hits++; } } } }
                    a = base + size; }
                out("\r\n");
                /* who points at the hit (0x068DAF38 last run): pointers within 0x800 below it in the camera, character and the objects they point to */
                { u32 tgt = 0; for (u32 a2 = 0x06000000u; a2 < 0x08000000u && !tgt; a2 += 4) { (void)a2; break; }
                  u8 *objs[2] = {cam, c}; const char *nm[2] = {"cam", "char"};
                  for (int oi = 0; oi < 2; oi++) for (u32 o = 0; o < 0x860; o += 4) { u32 p = *(u32 *)(objs[oi] + o); if (p > 0x10000 && p < 0x7FFE0000u) { 
                      for (u32 so = 0; so < 0x400; so += 4) { float *q = (float *)(p + so); IMP u32 WINAPI VirtualQuery(const void *, void *, u32); u32 mb[7]; if (so == 0 && (!VirtualQuery((void *)p, mb, sizeof mb) || mb[4] != 0x1000 || (mb[5] & 0x101) || mb[0] + mb[3] < p + 0x400)) break;
                          float d0 = q[0] - e[0], d1 = q[1] - e[1], d2 = q[2] - e[2]; if (d0 < 0.01f && d0 > -0.01f && d1 < 0.01f && d1 > -0.01f && d2 < 0.01f && d2 > -0.01f) { out("  [state] "); out(nm[oi]); out("+"); hex(o); out(" -> "); hex(p); out(" +"); hex(so); out("\r\n"); } } } } } }
            for (int which = 0; which < 0; which++) { u32 *key = (u32 *)(nd + (which ? 6 : 0)); out(which ? "  [state] back " : "  [state] right "); out("node "); hex((u32)nd); out(" found at:"); u32 hits = 0;
                IMP u32 WINAPI VirtualQuery(const void *, void *, u32); u32 mbi[7];
                for (u32 a = 0x10000; a < 0x7FFE0000u && hits < 24; ) { if (!VirtualQuery((void *)a, mbi, sizeof mbi)) break; u32 base = mbi[0], size = mbi[3], state = mbi[4], prot = mbi[5];
                    if (state == 0x1000 && (prot & 0xEE) && !(prot & 0x101)) { for (u32 *p = (u32 *)base; p + 3 <= (u32 *)(base + size) && hits < 24; p++) if (p[0] == key[0] && p[1] == key[1] && p[2] == key[2] && (float *)p != nd + (which ? 6 : 0)) { out(" "); hex((u32)p); hits++; } }
                    a = base + size; }
                out("\r\n"); } } }
    {   /* blockdiff.txt: during the fast-turn test, the floats of [character +0x6B8] (0x300 bytes) that changed since the last frame */
        static u32 on = 2, n; static float prev[0xC0]; static int have; if (on == 2) on = fattr_cached("blockdiff.txt") != 0xFFFFFFFF; extern u32 g_mt_active; extern volatile s32 g_fake_dx;
        u8 *c = (u8 *)g_player_char;
        if (on && c && *(u8 **)(cam + 0x148) == c) { float *blk = *(float **)(c + 0x6B8);
            if (g_mt_active && g_fake_dx > 200 && n < 3) { if (have) { n++;
                { float *nd = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48); out("  [nodeNow]"); for (int k = 0; k < 12; k++) { s32 v = (s32)(nd[k] * 1000); out(" "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out("\r\n"); }
                out("  [blk]"); for (u32 w = 0; w < 0xC0; w++) if (blk[w] != prev[w] && blk[w] > -100000.0f && blk[w] < 100000.0f) { s32 v = (s32)(blk[w] * 1000); out(" "); hex(w * 4); out("="); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out("\r\n"); } }
            for (u32 w = 0; w < 0xC0; w++) prev[w] = blk[w]; have = 1; } }
    {   /* respawn probe (Milestone 91): when the camera's target or the player pointer changes, log 40 samples every 100 ms */
        static u32 lt, lp, left, ts; u32 tg = *(u32 *)(cam + 0x148), pl = (u32)g_player_char, now = GetTickCount();
        if (pl != lp) { out("  [respawn] player "); hex(lp); out(" -> "); hex(pl); out("\r\n"); lt = tg; lp = pl; left = 0; ts = 0; } (void)lt;
        if (left && now - ts >= 100) { ts = now; left--; out("  [respawn] cam "); hex((u32)cam); out(" target "); hex(tg); out(" player "); hex(pl); out(" skip "); dec((u32)cam_skip(cam));
            out(" eye "); fo(g_eye_ours[0]); fo(g_eye_ours[1]); fo(g_eye_ours[2]);
            if (pl) { u8 *c = (u8 *)pl; float *x = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c); out(" pos "); fo(x[9]); fo(x[10]); fo(x[11]);
                u8 *cam_block(u8 *); u8 *blk = cam_block(cam);   /* Milestone 91: through the camera object, for any character */ out(" blk "); hex((u32)blk); }
            if (tg) { u8 *t = (u8 *)tg; float *x = ((float *(__thiscall *)(void *))(*(u32 **)t)[0x40 / 4])(t); out(" tpos "); fo(x[9]); fo(x[10]); fo(x[11]); }
            float *node = ((float *(__thiscall *)(void *))(*(u32 **)(cam + 0x48))[0x14 / 4])(cam + 0x48); out(" node t "); fo(node[9]); fo(node[10]); fo(node[11]); out("\r\n"); } }
    if (GetFileAttributesA_cached_camoffset()) cam_build(cam);
    {   /* Milestone 93: apply the arm pose before anything is drawn (the rifle is drawn from data prepared before the
           character's palette is built); the palette hook applies it again only where it was not applied yet */
        extern u8 *g_pbones; void pose_apply(u8 *); static u32 early = 2; if (early == 2) early = fattr_cached("noearlypose.txt") == 0xFFFFFFFF;
        if (early && g_pbones && g_player_char && *(u8 **)(cam + 0x148) == (u8 *)g_player_char) pose_apply(g_pbones); }
    static u32 done, t0, chk = 2; if (chk == 2) chk = fattr_cached("camdump.txt") != 0xFFFFFFFF; extern u32 g_mt_active; if (!chk || done >= 10 || !g_mt_active) return;
    u8 *c = (u8 *)g_player_char; if (!c || *(u8 **)(cam + 0x148) != c) return;
    u32 t = GetTickCount(); if (done && t - t0 < 400) return; t0 = t; done++;
    {   /* pointed-to objects: their changing floats (first 0x200 bytes) */
        static float snap[96][128]; static u32 ptrs[96], np;
        if (!np) for (u32 o = 0; o < 0x254 && np < 96; o += 4) { u32 p = *(u32 *)(cam + o); if (p >= 0x80010000u && p < 0x84000000u && p != (u32)c && p != (u32)cam) { ptrs[np++] = (o << 16) | 0; snap[np - 1][0] = 0; *(u32 *)&snap[np - 1][127] = p; } }
        for (u32 q = 0; q < np; q++) { u8 *obj = (u8 *)*(u32 *)&snap[q][127]; u32 o = ptrs[q] >> 16;
            if (done > 1) { int any = 0; for (u32 w = 0; w < 126; w++) { float f = *(float *)(obj + w * 4); if (f != snap[q][w] && f > -100000.0f && f < 100000.0f && snap[q][w] > -100000.0f && snap[q][w] < 100000.0f) { if (!any) { out("  [camptr] cam+"); hex(o); out(" -> "); hex((u32)obj); out(":"); any = 1; } s32 v = (s32)(f * 100); out(" "); hex(w * 4); out("="); if (v < 0) { out("-"); v = -v; } dec((u32)v); } } if (any) out("\r\n"); }
            for (u32 w = 0; w < 126; w++) snap[q][w] = *(float *)(obj + w * 4); } }
    {   static float prev[0x95]; static int have; if (have) { int any = 0; for (u32 w = 0; w < 0x95; w++) { float f = *(float *)(cam + w * 4); if (f != prev[w] && f > -100000.0f && f < 100000.0f) { if (!any) { out("  [camchg]"); any = 1; } s32 v = (s32)(f * 1000); out(" "); hex(w * 4); out("="); if (v < 0) { out("-"); v = -v; } dec((u32)v); } } if (any) out("\r\n"); }
        for (u32 w = 0; w < 0x95; w++) prev[w] = *(float *)(cam + w * 4); have = 1; }
    out("  [camdump] camera "); hex((u32)cam); out(" floats:");
    for (u32 o = 0; o < 0; o += 4) { float f = *(float *)(cam + o); s32 v = (s32)(f * 100); if (f > -100000.0f && f < 100000.0f && v) { out(" "); hex(o); out("="); if (v < 0) { out("-"); v = -v; } dec((u32)v); } }
    out("\r\n  [camdump] player floats:");
    for (u32 o = 0; o < 0x80; o += 4) { float f = *(float *)(c + o); s32 v = (s32)(f * 100); if (f > -100000.0f && f < 100000.0f && v) { out(" "); hex(o); out("="); if (v < 0) { out("-"); v = -v; } dec((u32)v); } }
    out("\r\n"); }
static int readable(const void *p, u32 n) {   /* committed, readable memory (our own code must never fault) */
    /* performance (Nov 21): one VirtualQuery per call, thousands per frame from the pose / weapon / aim code, kept the game thread in the
       Windows kernel ~88% of the time in busy scenes. The answer is cached per 4 KB page for 1 s (the game's memory map does not change
       during play). uncachedread.txt = the old way. */
    IMP u32 WINAPI VirtualQuery(const void *, void *, u32);
    u32 a = (u32)p, last = a + (n ? n : 1) - 1; if (last < a) return 0;
    static u32 tag[4096], when[4096]; static u8 ok[4096]; static u32 tchk, off_; u32 now = GetTickCount();
    if (now - tchk > 1000) { tchk = now; off_ = fattr_cached("uncachedread.txt") != 0xFFFFFFFF; }
    if (off_) { u32 mb[7]; if (!VirtualQuery(p, mb, sizeof mb) || mb[4] != 0x1000 || (mb[5] & 0x101) || !(mb[5] & 0xEE)) return 0; return a + n <= mb[0] + mb[3]; }
    for (u32 pg = a >> 12; pg <= (last >> 12); pg++) {
        u32 s = pg & 4095;
        if (tag[s] != pg + 1 || now - when[s] > 1000) { u32 mb[7]; int r = VirtualQuery((void *)(pg << 12), mb, sizeof mb) && mb[4] == 0x1000 && !(mb[5] & 0x101) && (mb[5] & 0xEE);
            tag[s] = pg + 1; when[s] = now; ok[s] = (u8)r; }
        if (!ok[s]) return 0; }
    return 1; }
int part_like(u8 *p, float *pos) {   /* rotation (unit rows) at +0x14 and a position within 3 m of pos at +0x38 */
    if ((u32)p <= 0x10000 || (u32)p >= 0x7FFE0000u || ((u32)p & 3) || !readable(p + 0x14, 0x30)) return 0; float *W = (float *)(p + 0x14);
    for (int k = 0; k < 3; k++) { float l = W[k * 3] * W[k * 3] + W[k * 3 + 1] * W[k * 3 + 1] + W[k * 3 + 2] * W[k * 3 + 2]; if (!(l > 0.9f && l < 1.1f)) return 0; }
    float *q = (float *)(p + 0x38), d = (q[0] - pos[0]) * (q[0] - pos[0]) + (q[1] - pos[1]) * (q[1] - pos[1]) + (q[2] - pos[2]) * (q[2] - pos[2]); return d < 9.0f; }
int blk_has_rot(u8 *blk) {   /* Milestone 91: a block holding the game camera's world rotation at +0x214 */
    if ((u32)blk <= 0x10000 || (u32)blk >= 0x7FFE0000u || !readable(blk + 0x214, 0x28)) return 0; float *W = (float *)(blk + 0x214);
    for (int k = 0; k < 3; k++) { float l = W[k * 3] * W[k * 3] + W[k * 3 + 1] * W[k * 3 + 1] + W[k * 3 + 2] * W[k * 3 + 2]; if (!(l > 0.9f && l < 1.1f)) return 0; }
    float d01 = W[0] * W[3] + W[1] * W[4] + W[2] * W[5], d02 = W[0] * W[6] + W[1] * W[7] + W[2] * W[8]; return d01 < 0.1f && d01 > -0.1f && d02 < 0.1f && d02 > -0.1f; }
/* the game camera's world transform (Milestone 91): [[camera +0x50] +0x5C]: rotation at +0x14, eye at +0x38. Returned as
   "block - 0x200" so the old offsets (+0x214 rotation, +0x238 eye) still apply. (It used to be reached through character
   +0x6B8, a buffer that only happened to lie just before it for the first character.) */
u8 *cam_block(u8 *cam) { u32 a = *(u32 *)(cam + 0x50); if (a <= 0x10000 || a >= 0x7FFE0000u || !readable((void *)a, 0x60)) return 0;
    u32 b = *(u32 *)(a + 0x5C); if (b <= 0x10000 || b >= 0x7FFE0000u) return 0; return (u8 *)(b - 0x200); }
int cam_skip(u8 *cam) {   /* camskip.txt: the player's follow camera is not updated by the game while our camera is active */
    static u32 on = 2; if (on == 2) on = fattr_cached("gamecamera.txt") == 0xFFFFFFFF;   /* with our camera, the game's follow update is skipped for the player */
    static u32 ct = 2; if (ct == 2) ct = fattr_cached("camtest.txt") != 0xFFFFFFFF;
    if (!(on && g_player_char && *(void **)(cam + 0x148) == g_player_char && (g_mouse_on || ct))) return 0;
    u8 *c = (u8 *)g_player_char;
    {   /* Milestone 92: the game's follow update now always runs (its other per-frame work, e.g. the health bar, depends on
           it); cam_after then overwrites the camera. camskip2.txt brings back the skip (A/B) */
        static u32 sk, tl; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; sk = fattr_cached("camskip2.txt") != 0xFFFFFFFF; } if (!sk) return 0; }
    {   /* Milestone 91: after the camera's target changes (a squad member taken over), the game's follow update runs for
           1.5 s: it starts at the character and pulls back; skipped, the game kept thinking the camera was inside the
           character and hid him */
        static u32 lastc, tchange; u32 now = GetTickCount(); if ((u32)c != lastc) { lastc = (u32)c; tchange = now; }
        if (now - tchange < 1500) return 0; }
    if (blk_has_rot(cam_block(cam))) return 1;
    {   /* no camera block at +0x6B8 (a squad member taken over after a death): the game's camera runs; log which fields of the
           character and of the camera point to blocks holding a rotation at +0x214 (to support it later) */
        static u32 lc, n; if (lc != (u32)c || (n < 3 && (GetTickCount() & 0xFFF) < 20)) { if (lc != (u32)c) n = 0; lc = (u32)c; n++; out("  [cam] no camera block at +0x6B8 of "); hex((u32)c); out("; character fields:");
            for (u32 o = 0; o < 0xC00; o += 4) { u32 p = *(u32 *)(c + o); if (p > 0x10000 && p < 0x7FFE0000u && (p & 3) == 0 && blk_has_rot((u8 *)p)) { out(" +"); hex(o); } }
            out("; camera fields:"); for (u32 o = 0; o < 0x400; o += 4) { u32 p = *(u32 *)(cam + o); if (p > 0x10000 && p < 0x7FFE0000u && (p & 3) == 0 && blk_has_rot((u8 *)p)) { out(" +"); hex(o); out("="); hex(p); } }
            u8 *b0 = *(u8 **)(c + 0x6B8); out("; +6B8 block "); hex((u32)b0); out("\r\n"); } }
    return 0; }
__attribute__((naked)) static void cam_stub(void) { __asm__ volatile(
    "pushl %ecx\n\t pushl %ecx\n\t call _cam_skip\n\t addl $4, %esp\n\t popl %ecx\n\t testl %eax, %eax\n\t jz 3f\n\t"
    "pushl %ecx\n\t pushl %ecx\n\t call _cam_after\n\t addl $4, %esp\n\t popl %ecx\n\t movb $1, %al\n\t ret $4\n"
    "3:\n\t pushl %ecx\n\t pushl 8(%esp)\n\t call *_g_cam_orig\n\t popl %ecx\n\t pushl %eax\n\t pushl %ecx\n\t call _cam_after\n\t addl $4, %esp\n\t popl %eax\n\t ret $4"); }
/* over-the-shoulder camera (Milestone 87, aiming like The Division). Camera_ApplyView (0x6C2C0) builds the view
   matrix into the global 0x4A5858 and at 0x6C370 uploads it (shader constants c2..c5, and view x projection c10..c13);
   all world drawing uses it. While the mouse is captured the view is moved: 0.55 m right of the game's camera, and
   while the right button is held 0.65 m right and 1.6 m closer (eased). The view also turns slightly back so that the
   screen centre (the reticle) meets the character's aim line about 25 m ahead. Row-vector matrices: v_view = v_world * V. */
u32 g_aiming; static float g_cam_t;
u32 g_view_dump;
void view_adjust(void) {
    if (g_view_dump) { static u32 n, t0; u32 t = GetTickCount(); if (n < 6 && t - t0 > 3000) { t0 = t; n++; float *V = (float *)0x4A5858; out("  [view]");
        for (int k = 0; k < 16; k++) { s32 v = (s32)(V[k] * 1000); out(k % 4 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out("\r\n"); } return; }
    return;   /* the offset is now applied to the camera transform (cam_xform_adjust), for the view and the effects alike */
    static u32 camtest = 2; if (camtest == 2) camtest = fattr_cached("camtest.txt") != 0xFFFFFFFF;
    if (!g_mouse_on && !camtest) { g_cam_t = 0; return; }
    static s64 lt, qf; s64 now; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&now);
    float dt = lt ? (float)(now - lt) / (float)qf : 0.0f; lt = now; if (dt > 0.1f) dt = 0.1f;
    float goal = g_aiming ? 1.0f : 0.0f, step = dt * 6.0f;   /* ~0.17 s to ease in or out */
    if (g_cam_t < goal) { g_cam_t += step; if (g_cam_t > goal) g_cam_t = goal; } else if (g_cam_t > goal) { g_cam_t -= step; if (g_cam_t < goal) g_cam_t = goal; }
    float d = 0.55f + 0.10f * g_cam_t, f = 1.6f * g_cam_t, th = d / 25.0f;   /* right, forward (m); turn-in (rad) */
    float cs = 1.0f - th * th * 0.5f, sn = th;
    /* the game stores the view transposed for the shaders' dp4: rows give view x, y, z (translation in element 3 of
       rows 0-2, bottom row 0 0 0 1); view space: +x right, +y up, +z forward. V' = Ry(th) * T(-d, 0, -f) * V */
    float *V = (float *)0x4A5858;
    V[3] -= d; V[11] -= f;
    for (int k = 0; k < 4; k++) { float r0 = V[k], r2 = V[8 + k]; V[k] = cs * r0 + sn * r2; V[8 + k] = -sn * r0 + cs * r2; }
}
__attribute__((naked)) static void view_stub(void) { __asm__ volatile(
    "pushal\n\t call _view_adjust\n\t popal\n\t movl $0x4a5858, %edx\n\t pushl $0x6C375\n\t ret"); }
/* camera transform (Milestone 87, second approach): every user of the camera's world transform multiplies it by the
   constant matrix 0x46D034 with Mat_Mul 0x2338C0 (cdecl out, a, b): Camera_ApplyView (view), the effects (0xE9D1E,
   0xEA343: muzzle flash, tracers). Shifting that result shifts the camera for all of them; the follow camera's own call
   (returns to 0x7135F) is left alone so the offset cannot feed back. Result: 3x3 rotation, position at +0x24. */
u32 g_xf_log;
void cam_xform_adjust(float *m, float *node) {
    if (g_xf_log) { static u32 n, t0; u32 t = GetTickCount(); if (g_player_char && n < 4 && t - t0 > 3000) { t0 = t; n++;
        { out("  [node]"); for (int k = 0; k < 12; k++) { s32 v = (s32)(node[k] * 1000); out(k % 3 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); }
          out("  conv"); float *cv = (float *)0x46D034; for (int k = 0; k < 12; k++) { s32 v = (s32)(cv[k] * 1000); out(k % 3 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out("\r\n"); }
        {   u8 *c = (u8 *)g_player_char; if (c) { float *x = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c); out("  [char] xform");
                for (int k = 0; k < 12; k++) { s32 v = (s32)(x[k] * 1000); out(k % 3 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); }
                out("  facing@5BC"); float *fm = (float *)(c + 0x5BC); for (int k = 0; k < 9; k++) { s32 v = (s32)(fm[k] * 1000); out(k % 3 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); }
                s32 pv = (s32)(*(float *)(c + 0x5F4) * 1000); out("  pitch "); if (pv < 0) { out("-"); pv = -pv; } dec((u32)pv); out("\r\n"); } }
        out("  [camxf]");
        for (int k = 0; k < 12; k++) { s32 v = (s32)(m[k] * 1000); out(k % 3 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); }
        float *V = (float *)0x4A5858; out("  view rows:"); for (int k = 0; k < 12; k++) { s32 v = (s32)(V[k] * 1000); out(k % 4 ? " " : " | "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out("\r\n"); } return; }
    return;   /* Milestone 87: the camera is now built in cam_after (camera node), not shifted here */
    static u32 camtest = 2; if (camtest == 2) camtest = fattr_cached("camtest.txt") != 0xFFFFFFFF;
    if (!g_mouse_on && !camtest) { g_cam_t = 0; return; }
    static s64 lt, qf; s64 now; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&now);
    float dt = lt ? (float)(now - lt) / (float)qf : 0.0f; lt = now; if (dt > 0.1f) dt = 0.1f;
    float goal = g_aiming ? 1.0f : 0.0f, step = dt * 6.0f;
    if (g_cam_t < goal) { g_cam_t += step; if (g_cam_t > goal) g_cam_t = goal; } else if (g_cam_t > goal) { g_cam_t -= step; if (g_cam_t < goal) g_cam_t = goal; }
    float d = 0.55f + 0.10f * g_cam_t, f = 1.6f * g_cam_t, th = d / 25.0f, cs = 1.0f - th * th * 0.5f, sn = th;
    m[9] -= d; m[11] -= f;
    for (int k = 0; k < 3; k++) { float r0 = m[k], r2 = m[6 + k]; m[k] = cs * r0 + sn * r2; m[6 + k] = -sn * r0 + cs * r2; }
    { float t0 = m[9], t2 = m[11]; m[9] = cs * t0 + sn * t2; m[11] = -sn * t0 + cs * t2; }
}
__attribute__((naked)) static void mul_stub(void) { __asm__ volatile(
    "cmpl $0x46d034, 8(%esp)\n\t jne 1f\n\t cmpl $0x7135F, (%esp)\n\t je 1f\n\t"
    "pushl 12(%esp)\n\t pushl 12(%esp)\n\t pushl 12(%esp)\n\t call 1f\n\t addl $12, %esp\n\t"
    "pushl %eax\n\t pushl 16(%esp)\n\t pushl %eax\n\t call _cam_xform_adjust\n\t addl $8, %esp\n\t popl %eax\n\t ret\n"
    "1:\n\t pushl %ebx\n\t movl 0x10(%esp), %ecx\n\t pushl $0x2338C5\n\t ret"); }
/* sprint (Milestone 87): Left Shift while moving and not aiming. The velocity update 0x12C6C0 (character at [ebp+8],
   velocity +0x470/+0x474/+0x478) eases the velocity toward a desired horizontal velocity ([ebp+0x14]: x at +0, z at +8);
   for the sprinting player that desired velocity is scaled by 1.45 (run ~8.5 m/s -> ~12). The camera eases back a little. */
u32 g_sprinting;
void sprint_scale(u8 *c, float *desired) { if (c != (u8 *)g_player_char) return;
    if (g_sprinting) { desired[0] *= 1.45f; desired[2] *= 1.45f; }
    else if (g_aiming && g_mouse_on) { desired[0] *= 0.6f; desired[2] *= 0.6f; } }   /* Milestone 90: slower while aiming (aiming also cancels sprint) */
__attribute__((naked)) static void sprint_stub(void) { __asm__ volatile(
    "pushal\n\t pushl 0x14(%ebp)\n\t pushl 0x8(%ebp)\n\t call _sprint_scale\n\t addl $8, %esp\n\t popal\n\t"
    "movl 0x8(%ebp), %ecx\n\t flds 0x478(%ecx)\n\t pushl $0x12CA51\n\t ret"); }
/* player bullets (Milestone 90): Weapon_SpawnProjectile (0x22BAC0) gets the muzzle position (esp+0x18) and direction
   (esp+0x30) from 0x232200; the hook at 0x22BC64 sees both before they are copied into the projectile. Weapon at esp+0x114,
   its owner at weapon+8. */
static void fo(float v) { s32 x = (s32)(v * 1000); if (x < 0) { out("-"); x = -x; } dec((u32)x); out(" "); }
/* Milestone 90: shots go where the reticle points. The reticle is a fixed HUD element (screen 320, 190 of 640x480); the
   viewer reads the depth there (pad[50]); the point is unprojected with the inverse of the view x projection constants
   (found by testing which 4 constant rows project our camera's forward axis to the screen centre) and the shot is aimed
   from the muzzle at it, with a random offset inside a disk (hip fire: wide, aiming: tight). */
float g_aim_t;   /* 0 = hip, 1 = aiming (eased in cam_build) */
u8 *g_pweapon;   /* the player's weapon, recorded when he fires */
static int inv4(const float *m, float *o) {   /* Gauss-Jordan, row-major 4x4 */
    float a[4][8]; for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i ? 1.0f : 0.0f);
    for (int c = 0; c < 4; c++) { int p = c; for (int r = c + 1; r < 4; r++) if ((a[r][c] < 0 ? -a[r][c] : a[r][c]) > (a[p][c] < 0 ? -a[p][c] : a[p][c])) p = r;
        if (a[p][c] == 0) return 0; if (p != c) for (int j = 0; j < 8; j++) { float t = a[p][j]; a[p][j] = a[c][j]; a[c][j] = t; }
        float iv = 1.0f / a[c][c]; for (int j = 0; j < 8; j++) a[c][j] *= iv;
        for (int r = 0; r < 4; r++) if (r != c) { float k = a[r][c]; if (k != 0) for (int j = 0; j < 8; j++) a[r][j] -= k * a[c][j]; } }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o[i * 4 + j] = a[i][j + 4]; return 1; }
static int vp_slot(float *C) {   /* first 4 constant rows that project eye + 10 x forward to the centre at w ~ 10 */
    static int cached = -1; float P[4], Q[4];
    for (int k = 0; k < 3; k++) { P[k] = g_eye_ours[k] - g_cb[k] * 10.0f; Q[k] = P[k] + g_cr[k] * 2.0f; } P[3] = Q[3] = 1;
    if (cached >= 0) { float we = 0, wp = 0; for (int k = 0; k < 4; k++) { float e = k < 3 ? g_eye_ours[k] : 1.0f; we += C[(cached + 3) * 4 + k] * e; wp += C[(cached + 3) * 4 + k] * P[k]; }
        if (we > -0.6f && we < 0.6f && wp > 7 && wp < 13) return cached; }   /* known rows: accept while the camera turns */
    for (int pass = 0; pass < 2; pass++) for (int s = pass ? 0 : (cached < 0 ? 0 : cached); s <= (pass ? 188 : (cached < 0 ? -1 : cached)); s++) {
        float c[4], d[4]; for (int r = 0; r < 4; r++) { c[r] = 0; d[r] = 0; for (int k = 0; k < 4; k++) { c[r] += C[(s + r) * 4 + k] * P[k]; d[r] += C[(s + r) * 4 + k] * Q[k]; } }
        if (c[3] > 8 && c[3] < 12 && c[0] / c[3] < 0.03f && c[0] / c[3] > -0.03f && c[1] / c[3] < 0.03f && c[1] / c[3] > -0.03f && d[0] / d[3] > 0.02f) { cached = s; return s; } }
    return -1; }
static int vp_matches(const float *M) {   /* does view-projection M still belong to our camera: eye + 10 x forward at the centre, w ~ 10 */
    float P[4] = {g_eye_ours[0] - g_cb[0] * 10.0f, g_eye_ours[1] - g_cb[1] * 10.0f, g_eye_ours[2] - g_cb[2] * 10.0f, 1.0f}, c[4];
    for (int r = 0; r < 4; r++) { c[r] = 0; for (int k = 0; k < 4; k++) c[r] += M[r * 4 + k] * P[k]; }
    return c[3] > 8 && c[3] < 12 && c[0] / c[3] < 0.05f && c[0] / c[3] > -0.05f && c[1] / c[3] < 0.05f && c[1] / c[3] > -0.05f; }
/* Milestone 97: the camera's projection, caught whenever the game uploads its view-projection (render.c calls this for every 4 rows
   just written): rows that put our camera's point 10 ahead at the centre (w ~ 10) and a point to its right to the right are the
   view-projection; Proj = VP x inverse(our view). Shots then use Proj x our view at that moment (the constants at the time of a
   shot are usually another pass's: a fast-firing minigun found them almost never in E01). */
static float g_projB[2][16]; static volatile u32 g_proj_t, g_proj_i;   /* double-buffered: a shot never reads one half written */
#define g_projM g_projB[g_proj_i & 1]
float g_hip_p11 = 1.732f;   /* set from the caught projection (vp_watch); 60 degrees until then */
static void our_view(float *V) { const float *ax[3] = {g_cr, g_cu, g_cb}; for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) V[r * 4 + k] = ax[r][k]; V[r * 4 + 3] = -(ax[r][0] * g_eye_ours[0] + ax[r][1] * g_eye_ours[1] + ax[r][2] * g_eye_ours[2]); } V[12] = V[13] = V[14] = 0; V[15] = 1; }
void vp_watch(const float *M) {
    extern u32 g_mouse_on; if (!g_mouse_on) return;
    float P[4], Q[4], c[4], d[4]; for (int k = 0; k < 3; k++) { P[k] = g_eye_ours[k] - g_cb[k] * 10.0f; Q[k] = P[k] + g_cr[k] * 2.0f; } P[3] = Q[3] = 1;
    for (int r = 0; r < 4; r++) { c[r] = 0; d[r] = 0; for (int k = 0; k < 4; k++) { c[r] += M[r * 4 + k] * P[k]; d[r] += M[r * 4 + k] * Q[k]; } }
    /* tight: the camera's own view-projection puts the point exactly there; an object's (world x view x projection) only nearly */
    if (!(c[3] > 9.9f && c[3] < 10.1f && c[0] / c[3] < 0.004f && c[0] / c[3] > -0.004f && c[1] / c[3] < 0.004f && c[1] / c[3] > -0.004f && d[3] > 9.5f && d[3] < 10.5f && d[0] / d[3] > 0.02f)) return;
    float V[16], iV[16]; our_view(V); if (!inv4(V, iV)) return;
    u32 nb = (g_proj_i + 1) & 1; float *pm = g_projB[nb];
    for (int r = 0; r < 4; r++) for (int cc = 0; cc < 4; cc++) { float a = 0; for (int k = 0; k < 4; k++) a += M[r * 4 + k] * iV[k * 4 + cc]; pm[r * 4 + cc] = a; }
    g_proj_i = nb; g_proj_t = GetTickCount() | 1;
    { float p11 = pm[5] < 0 ? -pm[5] : pm[5]; if (!scoped() && p11 > 0.5f && p11 < 5.0f) g_hip_p11 = p11; }   /* the unzoomed view's 1 / tan(half the vertical field of view) (scope lift) */
    {   /* probe: the projection's field of view over time (the view seemed to pull back after sprinting): P[0][0], P[1][1] and the
           camera object's floats that look like an angle (0.2 .. 3 rad) or a zoom, logged when P[1][1] changes by 2% */
        extern u8 *g_cam_obj; extern u32 g_sprinting; static float lp; static u32 n; float p11 = pm[5] < 0 ? -pm[5] : pm[5];
        if (n < 400 && (lp == 0 || p11 > lp * 1.02f || p11 < lp * 0.98f)) { n++; lp = p11; out("  [fov] P00 "); fo(pm[0]); out("P11 "); fo(pm[5]); out("sprint "); dec(g_sprinting);
            if (g_cam_obj && readable(g_cam_obj, 0x400)) { out(" cam"); for (u32 o = 0; o < 0x400; o += 4) { float f = *(float *)(g_cam_obj + o); if (f > 0.2f && f < 3.2f) { out(" +"); hex(o); out("="); fo(f); } } }
            out("\r\n"); } } }
/* Milestone 102: camera collision through the viewer.
   cam_probe_send: every 3D vertex the game draws comes out of its vertex program as o0 = screen position (x, y in pixels of
   the 640x480 frame, y down) and w = depth along the camera (the projection caught by vp_watch, P = M x inverse(our view)).
   The viewer forms in = (gl_x * w, gl_y * w, w, 1) from it (gl_x, gl_y: the frame's NDC, gl_y up); this matrix takes that
   back to the world (view = A in, world = inverse(V) view) and on into the probe's clip space (a GL camera at the pivot,
   looking at where the camera wants to be, 10 degrees wide). RING pad: [60] sequence (odd while written), [61] on,
   [62] near, [63] far, [64..79] the matrix (row-major), [80] the distance the viewer found, [81] its counter. */
static float sqrt_(float x);
static void mul4(const float *a, const float *b, float *o) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float s = 0; for (int k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c]; o[r * 4 + c] = s; } }
#define PROBE_TAN 0.0875f   /* tan(5 degrees): half the probe's view */
#define PROBE_NEAR 0.7f     /* the pivot sits inside the player's own body (and gun) */
void cam_probe_send(const float *st, const float *eye_want) {
    u32 *pd = gpu_pad(); if (!pd) return;
    static u32 off = 2; if (off == 2) off = fattr_cached("nocamcollide.txt") != 0xFFFFFFFF;
    if (off || !g_proj_t) { pd[61] = 0; return; }
    const float *P = g_projB[g_proj_i & 1]; float P00 = P[0], P11 = P[5], P02 = P[2], P12 = P[6], P32 = P[14], P33 = P[15];
    if (P00 > -1e-4f && P00 < 1e-4f) return; if (P11 > -1e-4f && P11 < 1e-4f) return; if (P32 > -1e-4f && P32 < 1e-4f) return;
    float A[16] = {1.0f / P00, 0, -(P02 / P00) / P32, (P02 / P00) * P33 / P32,
                   0, -1.0f / P11, -(P12 / P11) / P32, (P12 / P11) * P33 / P32,
                   0, 0, 1.0f / P32, -P33 / P32,
                   0, 0, 0, 1};
    /* (y: gl_y = -m_y, the frame's rows run down: m_y w = -in.y, so y_view = (-in.y - P12 z) / P11) */
    float V[16], iV[16]; our_view(V); if (!inv4(V, iV)) return;
    float f[3] = {eye_want[0] - st[0], eye_want[1] - st[1], eye_want[2] - st[2]}, L = sqrt_(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (L < 0.5f) { pd[61] = 0; return; }
    for (int k = 0; k < 3; k++) f[k] /= L;
    float up[3] = {0, 1, 0}; if (f[1] > 0.95f || f[1] < -0.95f) { up[0] = 1; up[1] = 0; }
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]}, sl = sqrt_(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); for (int k = 0; k < 3; k++) s[k] /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    float Vp[16] = {s[0], s[1], s[2], -(s[0] * st[0] + s[1] * st[1] + s[2] * st[2]),
                    u[0], u[1], u[2], -(u[0] * st[0] + u[1] * st[1] + u[2] * st[2]),
                    -f[0], -f[1], -f[2], f[0] * st[0] + f[1] * st[1] + f[2] * st[2],
                    0, 0, 0, 1};
    float n = PROBE_NEAR, fa = L + 0.6f, t = 1.0f / PROBE_TAN;
    float Pp[16] = {t, 0, 0, 0,  0, t, 0, 0,  0, 0, -(fa + n) / (fa - n), -2.0f * fa * n / (fa - n),  0, 0, -1, 0};
    float T1[16], T2[16], M[16]; mul4(Pp, Vp, T1); mul4(T1, iV, T2); mul4(T2, A, M);
    u32 sq = pd[60] | 1; pd[60] = sq; __sync_synchronize();
    for (int k = 0; k < 16; k++) pd[64 + k] = *(u32 *)&M[k];
    pd[62] = *(u32 *)&n; pd[63] = *(u32 *)&fa; pd[61] = 1;
    __sync_synchronize(); pd[60] = sq + 1; }
void cam_collide(const float *st, float *eye, float dt) {
    static u32 off = 2; if (off == 2) off = fattr_cached("nocamcollide.txt") != 0xFFFFFFFF; if (off) return;
    if (lock_mode() && fattr_cached("specialnocollide.txt") != 0xFFFFFFFF) return;   /* opt-in: skipping collision in a special made it shake */   /* only during Flint's lock-on: skipping it in every special made the specials shake (found by testing) */   /* every special (the probe pulled the camera in and out: shake) */   /* from the White press (before the energy drops the probe pulled the camera in and out: shake at a lock's start) */   /* the centred camera's probe hit the character itself: the camera jumped (shake) */
    u32 *pd = gpu_pad(); if (!pd) return;
    float dv[3] = {eye[0] - st[0], eye[1] - st[1], eye[2] - st[2]}, L = sqrt_(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]); if (L < 0.5f) return;
    static u32 lastc, lastt; static float wall = 1e9f; u32 now = GetTickCount();
    if (pd[81] != lastc) { lastc = pd[81]; lastt = now; wall = *(float *)&pd[80]; }    float want = L; if (now - lastt < 400 && wall < L + 0.3f) { want = wall - 0.3f; if (want < 0.45f) want = 0.45f; if (want > L) want = L; }   /* a fresh measurement: stop 0.3 m in front of the surface */
    static float cur = -1; if (cur < 0 || want < cur) cur = want; else { float e = dt * 3.0f; if (e > 1) e = 1; cur += (want - cur) * e; }   /* in at once, out gently */
    if (cur > L) cur = L;
    for (int k = 0; k < 3; k++) eye[k] = st[k] + dv[k] / L * cur;
    { static u32 tl, nl; if (nl < 200 && cur < L - 0.05f && now - tl > 500) { tl = now; nl++; out("  [camcol] wall at "); fo(wall); out("camera at "); fo(cur); out("of "); fo(L); out("\r\n"); } } }
static u32 g_rng = 12345; static float frand(void) { g_rng = g_rng * 1664525u + 1013904223u; return (float)(g_rng >> 8) / 16777216.0f; }
static float sqrt_(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static float sin_(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static float cos_(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
/* the world point under the over-the-shoulder reticle (320, 190): the viewer's depth there (pad[50]) unprojected with the caught
   projection x our view. 0 when there is none (sky, no projection yet) */
int screen_world(float sy, float *T);
int reticle_world(float *T) { return screen_world(190.0f, T); }
int screen_world(float sy, float *T) {   /* the world point under (320, sy) of 640x480: the depth sample (pad[50], taken at pad[52], pad[53]) */
    u32 *pd = gpu_pad(); if (!pd || !pd[51] || pd[53] != (u32)sy || !g_proj_t || (s32)(GetTickCount() - g_proj_t) > 30000) return 0;
    float V[16], VP[16], IV[16]; our_view(V); const float *pj = g_projB[g_proj_i & 1];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float a = 0; for (int k = 0; k < 4; k++) a += pj[r * 4 + k] * V[k * 4 + c]; VP[r * 4 + c] = a; }
    if (!vp_matches(VP) || !inv4(VP, IV)) return 0;
    u32 dbits = pd[50]; float d01 = *(float *)&dbits; if (!(d01 > 0) || d01 > 0.99999f) return 0;
    float sc[4] = {0.0f, (240.0f - sy) / 240.0f, d01, 1.0f}, X[4];
    for (int r = 0; r < 4; r++) { X[r] = 0; for (int k = 0; k < 4; k++) X[r] += IV[r * 4 + k] * sc[k]; }
    if (X[3] == 0) return 0; for (int k = 0; k < 3; k++) T[k] = X[k] / X[3]; return 1; }
void aim_fix(u8 *w, float *pos, float *dir) {
    extern u32 g_ourcam; if (!g_ourcam) return;   /* the game's camera: the game's own aiming */
    u8 *own = w ? *(u8 **)(w + 8) : 0;
    { static u32 nd; if (nd < 2) { nd++; out("  [aimdiag] weapon "); hex((u32)w); out(" owner "); hex((u32)own); out(" player "); hex((u32)g_player_char); out(" mouse "); dec(g_mouse_on); u32 *pp = gpu_pad(); out(" depth reads "); dec(pp ? pp[51] : 0);
            if (w) for (u32 o = 0; o < 0x400; o += 4) if (*(u32 *)(w + o) == (u32)g_player_char) { out(" w+"); hex(o); }
            if (own) for (u32 o = 0; o < 0x1000; o += 4) if (*(u32 *)(own + o) == (u32)g_player_char) { out(" own+"); hex(o); }
            if (g_player_char) for (u32 o = 0; o < 0x1000; o += 4) { u32 v = *(u32 *)((u8 *)g_player_char + o); if (v == (u32)own) { out(" player+"); hex(o); out("=own"); } if (v == (u32)w) { out(" player+"); hex(o); out("=weapon"); } }
            out("\r\n"); } }
    /* Milestone 92: the shooter is [[[weapon +0x1B0] +8] +0x1D4] (as Weapon_SpawnProjectile finds it). The old test (weapon +8 ==
       character +8) matched every character's shots: +8 is shared, so the AI's shots were re-aimed at the player's reticle. */
    u8 *shooter = 0; { u32 a = w && readable(w + 0x1B0, 4) ? *(u32 *)(w + 0x1B0) : 0; if (a > 0x10000 && a < 0x7FFE0000u && readable((void *)(a + 8), 4)) { u32 b = *(u32 *)(a + 8);
        if (b > 0x10000 && b < 0x7FFE0000u && readable((void *)(b + 0x1D4), 4)) shooter = *(u8 **)(b + 0x1D4); } }
    {   /* logged when the shooter / player pair changes (Milestone 97: after a death the shots went astray; the old log stopped at 12) */
        static u32 ls, lp, nl; if (((u32)shooter != ls || (u32)g_player_char != lp) && nl < 200) { nl++; ls = (u32)shooter; lp = (u32)g_player_char;
            out("  [shooter] weapon "); hex((u32)w); out(" +8 "); hex((u32)own); out(" shooter "); hex((u32)shooter); out(" player "); hex((u32)g_player_char); out(shooter == (u8 *)g_player_char ? " = PLAYER\r\n" : "\r\n"); } }
    if (!shooter || shooter != (u8 *)g_player_char) return;
    { extern u8 *g_pweapon; g_pweapon = w; }
    {   /* probe (Milestone 97): which character field holds the weapon in use: offsets (0..0x1000) holding the firing weapon; the
           candidates are narrowed (kept only if they matched every weapon fired so far) and logged when the weapon changes */
        static u32 cand[64], nc = 0xFFFFFFFF, lastw; u8 *ch = (u8 *)g_player_char;
        if ((u32)w != lastw && readable(ch, 0x1000)) { lastw = (u32)w;
            if (nc == 0xFFFFFFFF) { nc = 0; for (u32 o = 0; o < 0x1000 && nc < 64; o += 4) if (*(u32 *)(ch + o) == (u32)w) cand[nc++] = o; }
            else { u32 m = 0; for (u32 i = 0; i < nc; i++) if (*(u32 *)(ch + cand[i]) == (u32)w) cand[m++] = cand[i]; nc = m; }
            out("  [curweapon] fired "); hex((u32)w); out(", character fields holding it:"); for (u32 i = 0; i < nc; i++) { out(" +"); hex(cand[i]); } out("\r\n"); } }
    static u32 camtest = 2; if (camtest == 2) camtest = fattr_cached("camtest.txt") != 0xFFFFFFFF;
    if (!g_mouse_on && !camtest) return;
    static u32 off, tl; { u32 t = GetTickCount(); if (t - tl > 1000) { tl = t; off = fattr_cached("noaimfix.txt") != 0xFFFFFFFF; } } { int sp = lock_mode(), lk = locked_now(); static u32 ns; if (sp && ns++ < 60) { out("  [specialshot] lock "); dec(lk); out(lk ? " -> the game aims" : " -> crosshair aim"); out("\r\n"); }
      if (off) return;
      /* (zoomed in: re-aimed at the screen centre, the scope's crosshair - see g_scoped) */
      if (sp) {   /* during a special (Flint's auto-lock): aim from the muzzle at the locked character's chest (the game's own aim missed
                     at long range); no locked character: the game aims. nolockaim.txt = always the game's aim */
          u8 *t = 0; u32 ty = 0;   /* the target the game has locked (targeting object +0x5D0) first: the last character the lock-on check saw changed
                                      between nearby enemies every frame (shots jumped between them) */
          { u8 *lock_target(void); u8 *q = lock_target(); if (q) { t = q; ty = 3; } }
          if (ty != 3) { t = (u8 *)g_lock_t; if (locked_now() && t && readable(t, 0x10)) { u8 *k = *(u8 **)(t + 0xC); if (k && readable(k, 8)) ty = *(u32 *)(k + 4); } }
          if (ty != 3) { u8 *r2 = (u8 *)g_rt; u32 ty2 = 0; if (r2 && readable(r2, 0x10)) { u8 *k = *(u8 **)(r2 + 0xC); if (k && readable(k, 8)) ty2 = *(u32 *)(k + 4); }
              { static u32 nr; if (nr++ < 40) { out("  [lockshot] reticle object "); hex((u32)r2); out(" type "); dec(ty2); out("\r\n"); } }
              if (ty2 == 3) { t = r2; ty = 3; }   /* the reticle's object (aim_reticle_keep) when it is a character */
              else if (ty2 == 16 && readable(r2, 0x800)) {   /* the targeting object (type 16): the locked character is one of its fields */
                  for (u32 o = 0; o < 0x800; o += 4) { u8 *q = *(u8 **)(r2 + o); if (!char_is(q)) continue;
                      u8 *k = *(u8 **)(q + 0xC); if (!k || !readable(k, 8) || *(u32 *)(k + 4) != 3) continue;
                      { static u32 nf; if (nf++ < 20) { out("  [lockshot] targeting object +"); hex(o); out(" -> character "); hex((u32)q); out("\r\n"); } }
                      t = q; ty = 3; break; } } }
          {   /* a shot in a gap of the lock (before it catches, or when it drops for a moment) goes at the last locked character, up to 1 s */
              static u8 *lt9; static u32 lt9_t; u32 n9 = GetTickCount(); if (ty == 3 && char_is(t)) { lt9 = t; lt9_t = n9; } else if (lt9 && n9 - lt9_t < 1000 && char_is(lt9)) { t = lt9; ty = 3; } }
          { static u32 nq; if (nq++ < 40) { out("  [lockshot] locked "); dec(locked_now()); out(" target "); hex((u32)t); out(" type "); dec(ty); out("\r\n"); } }
          if (ty == 3 && char_is(t) && fattr_cached("nolockaim.txt") == 0xFFFFFFFF) {
              float *x = ((float *(__thiscall *)(void *))(*(u32 **)t)[0x40 / 4])(t); extern int g_aim_part; static const float ap[5][2] = {{0.25f, 0.0f}, {0.75f, 0.0f}, {0.0f, 0.0f}, {0.35f, -0.3f}, {0.35f, 0.3f}};   /* chest, head, hips, left, right */
              float sxp = -(x[11] - pos[2]), szp = x[9] - pos[0], slp = sqrt_(sxp * sxp + szp * szp); if (slp > 1e-3f) { sxp /= slp; szp /= slp; }
              float T[3] = {x[9] + sxp * ap[g_aim_part][1], x[10] + ap[g_aim_part][0], x[11] + szp * ap[g_aim_part][1]};   /* the character's position is at the hips (0.7 m below the muzzle): +0.35 = chest (+1.1 went over the head) */
              float v[3] = {T[0] - pos[0], T[1] - pos[1], T[2] - pos[2]}, l = sqrt_(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
              if (l > 0.5f) { float od[3] = {dir[0], dir[1], dir[2]};
                  { static u32 nd; if (nd++ < 40) { float dt = (od[0] * v[0] + od[1] * v[1] + od[2] * v[2]) / l; out("  [lockshot] target dist x10 "); dec((u32)(l * 10)); out(" game-vs-ours cos x1000 "); dec((u32)(dt * 1000 + 1000)); out(" (1000+)"); out(" target y-muzzle y x100 "); dec((u32)((x[10] - pos[1]) * 100 + 10000)); out(" (10000+)\r\n"); } }
                  int bodyc = 0; { int body_center(u8 *, float *); float Bc[3]; if (fattr_cached("nobodyaim.txt") == 0xFFFFFFFF && body_center(t, Bc)) { for (int k2 = 0; k2 < 3; k2++) { T[k2] = Bc[k2]; v[k2] = T[k2] - pos[k2]; } l = sqrt_(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); bodyc = 1; } }   /* the body's middle */
                  if (!bodyc)                   {   /* the game's own lock aim knows each character's shape (tall / floating ones were shot over at a fixed height): when it
                         already points at the target (within ~6 degrees) the shot keeps that direction, at the target's distance */
                      float cs = (od[0] * v[0] + od[1] * v[1] + od[2] * v[2]) / l; if (cs > 0.9945f && fattr_cached("fixedaimheight.txt") == 0xFFFFFFFF) { for (int k2 = 0; k2 < 3; k2++) { v[k2] = od[k2] * l; T[k2] = pos[k2] + v[k2]; } } }
                  for (int k2 = 0; k2 < 3; k2++) dir[k2] = v[k2] / l;
                  { extern float g_zT[3], g_zM0[3]; extern u32 g_zT_t; extern float *g_zsd; for (int k2 = 0; k2 < 3; k2++) { g_zT[k2] = T[k2]; g_zM0[k2] = pos[k2]; } g_zT_t = GetTickCount() | 1;   /* the hit ray (bray_wrap) is steered at the chest too */
                    g_zsd = 0; for (int ix = 0; ix < 4; ix++) { float *sd = (float *)(w + 0x25C + ix * 12); if (readable(sd, 12) && sd[0] == od[0] && sd[1] == od[1] && sd[2] == od[2]) { g_zsd = sd; break; } } }
                  for (int ix = 0; ix < 4; ix++) { float *sd = (float *)(w + 0x25C + ix * 12); if (readable(sd, 12) && sd[0] == od[0] && sd[1] == od[1] && sd[2] == od[2]) { sd[0] = dir[0]; sd[1] = dir[1]; sd[2] = dir[2]; break; } } } }
          return; } }
    if (scoped() && !special_steady() && fattr_cached("gamescope.txt") != 0xFFFFFFFF) return;
    if (scoped() && !lock_mode() && fattr_cached("scopedepthaim.txt") == 0xFFFFFFFF) {   /* zoomed (our camera): the bullet starts on the
            crosshair's own line beside the gun and flies along it - exact at any distance, no depth reading (aiming at the depth point
            from the gun, 19 cm off the line, landed above). scopedepthaim.txt = the depth re-aim */
        float F[3] = {-g_cb[0], -g_cb[1], -g_cb[2]}, m[3] = {pos[0] - g_eye_ours[0], pos[1] - g_eye_ours[1], pos[2] - g_eye_ours[2]}, t = m[0] * F[0] + m[1] * F[1] + m[2] * F[2];
        if (t > 0.1f && t < 5.0f) { float od[3] = {dir[0], dir[1], dir[2]}, M0[3] = {pos[0], pos[1], pos[2]};
            for (int q = 0; q < 3; q++) { pos[q] = g_eye_ours[q] + F[q] * t; dir[q] = F[q]; }
            /* the trail is drawn from the gun (24 cm off the line) along the weapon's stored direction: aimed at where the shot lands (the
               crosshair's point: depth there, else 150 m out) so it meets the crosshair instead of running parallel, low and left */
            float TD[3]; { int screen_world(float sy, float *T); float Tp[3], L = 100.0f; if (0 && screen_world(240.0f, Tp)) {   /* a fixed 100 m: the depth reading swung the shots (near and far readings) */ float e[3] = {Tp[0] - g_eye_ours[0], Tp[1] - g_eye_ours[1], Tp[2] - g_eye_ours[2]}; float dd = e[0] * F[0] + e[1] * F[1] + e[2] * F[2]; if (dd > 2.0f && dd < 400.0f) L = dd; }
                for (int q = 0; q < 3; q++) { TD[q] = g_eye_ours[q] + F[q] * L - M0[q]; g_zT[q] = g_eye_ours[q] + F[q] * L; } g_zT_t = GetTickCount() | 1; float tl = sqrt_(TD[0] * TD[0] + TD[1] * TD[1] + TD[2] * TD[2]); for (int q = 0; q < 3; q++) TD[q] /= tl; }
            for (int q = 0; q < 3; q++) { pos[q] = M0[q]; dir[q] = TD[q]; g_zM0[q] = M0[q]; } g_zsd = 0;
            for (int ix = 0; ix < 4; ix++) { float *sd = (float *)(w + 0x25C + ix * 12); if (readable(sd, 12) && sd[0] == od[0] && sd[1] == od[1] && sd[2] == od[2]) { g_zsd = sd; break; } }
            { static u32 nl; if (nl++ < 24) { out("  [scopeshot] pos "); fo(M0[0]); fo(M0[1]); fo(M0[2]); out("dir "); fo(TD[0]); fo(TD[1]); fo(TD[2]); out("muzzles"); for (int ix = 0; ix < 4; ix++) { float *mz = (float *)(w + 0x22C + ix * 12); if (readable(mz, 12)) { out(" |"); fo(mz[0]); fo(mz[1]); fo(mz[2]); } } out("\r\n"); } }   /* the game ignores a moved start (the hole was 45 px left): the bullet goes from the gun at the crosshair's point, like the trail */
            for (int ix = 0; ix < 4; ix++) { float *sd = (float *)(w + 0x25C + ix * 12); if (readable(sd, 12) && sd[0] == od[0] && sd[1] == od[1] && sd[2] == od[2]) { sd[0] = TD[0]; sd[1] = TD[1]; sd[2] = TD[2]; break; } }
            static u32 nl; if (nl++ < 20) { out("  [scopeshot] on the crosshair line, moved mm "); fo(m[0] - F[0] * t); fo(m[1] - F[1] * t); fo(m[2] - F[2] * t); out("\r\n"); } return; } }   /* only with the game's scope camera (opt-in); on our camera zoomed shots are re-aimed at the crosshair's point like hip shots (they flew parallel from the gun, above the eye: high) */   /* zoomed in: the game's scope camera, the game's aim */
    u32 *pd = gpu_pad(); if (!pd || !pd[51]) return;
    float *C = render_consts(); int s = vp_slot(C);
    static float lastVP[16]; static int have; static u32 vp_t; float VP[16], IV[16]; u32 tnow = GetTickCount();
    static u32 st_ok, st_old, st_none, st_t; if (tnow - st_t > 2000 && (st_ok + st_old + st_none)) { st_t = tnow; out("  [aim] shots re-aimed by search "); dec(st_ok); out(", with the caught projection "); dec(st_old); out(", left to the game (none found) "); dec(st_none); out("\r\n"); st_ok = st_old = st_none = 0; }
    /* Milestone 97: when the rows are found, their projection part (Proj = VP x inverse(our view then)) is kept; when they are not
       (a fast-firing minigun often catches the constants of another draw), VP = Proj x our view now: right while turning and after a death */
    float V[16]; our_view(V);
    int scope_vp = 0;
    if (scoped() && C && fattr_cached("scopeslots.txt") != 0xFFFFFFFF) {   /* test only: slots 10-13 were not the scope's view (shots ~60 degrees off) */   /* zoomed in: the game draws from its own scope camera; its view-projection
        is in the vertex shader constants (slots 10-13) - our camera's did not match the scope's picture (shots low and left) */
        for (int k = 0; k < 16; k++) VP[k] = C[40 + k]; scope_vp = 1; static u32 ns; if (ns++ < 5) out("  [aim] scoped: the scope camera's view-projection (slots 10-13)\r\n"); }
    if (!scope_vp) {
    if (g_proj_t && (s32)(tnow - g_proj_t) < 30000) {   /* signed: it may have been caught just after tnow was read */   /* the projection caught at upload x our view now (the projection itself rarely changes) */
        const float *pj = g_projB[g_proj_i & 1];
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float a = 0; for (int k = 0; k < 4; k++) a += pj[r * 4 + k] * V[k * 4 + c]; VP[r * 4 + c] = a; }
        if (!vp_matches(VP)) { st_none++;   /* why (Milestone 97 probe): the forward point's screen offset and depth */
            static u32 nb; if (nb++ < 30) { float P[4] = {g_eye_ours[0] - g_cb[0] * 10.0f, g_eye_ours[1] - g_cb[1] * 10.0f, g_eye_ours[2] - g_cb[2] * 10.0f, 1.0f}, c[4];
                for (int r = 0; r < 4; r++) { c[r] = 0; for (int k = 0; k < 4; k++) c[r] += VP[r * 4 + k] * P[k]; }
                out("  [aim] check failed: x "); fo(c[3] != 0 ? c[0] / c[3] : 99); out("y "); fo(c[3] != 0 ? c[1] / c[3] : 99); out("w "); fo(c[3]); out("age ms "); dec(tnow - g_proj_t); out("\r\n"); }
            return; } st_old++; }
    else if (s < 0) { static u32 ns; if (g_proj_t && ns++ < 10) { out("  [aim] caught projection too old: "); dec(tnow - g_proj_t); out(" ms\r\n"); } st_none++; return; }
    else if (s >= 0) { for (int k = 0; k < 16; k++) lastVP[k] = VP[k] = C[s * 4 + k]; have = 1; vp_t = tnow; st_ok++; }
    else { st_none++; return; }
    }
    if (!inv4(VP, IV)) return;
    u32 dbits = pd[50]; float d01 = *(float *)&dbits; if (!(d01 > 0) || d01 > 1) d01 = 1;
    if (d01 > 0.99999f) d01 = 0.99999f;
    /* spread: radius in pixels (640x480), hip fire wide, aiming tight */
    float rad = 18.0f * (1.0f - g_aim_t) + 1.2f * g_aim_t; if (scoped()) rad = 1.2f;   /* zoomed: tight (zooming with the zoom key alone counted as hip fire) */   /* the hip-fire reticle (scale 1.25) has a ring radius of ~20 px */
    {   /* Milestone 96: shotguns keep the hip-fire spread when aiming (user). A weapon type that fires several rays in the same
           millisecond (pellets) is remembered as multi-pellet. */
        static u32 lw_, lt_, multi[16], nm; u32 now = GetTickCount(); u32 ty = readable(w + 0x228, 4) ? *(u32 *)(w + 0x228) : 0;
        if ((u32)w == lw_ && now == lt_ && ty) { int k = 0; while (k < (int)nm && multi[k] != ty) k++; if (k == (int)nm && nm < 16) { multi[nm++] = ty; out("  [aim] multi-pellet weapon type "); hex(ty); out(": hip spread when aiming\r\n"); } }
        lw_ = (u32)w; lt_ = now;
        for (u32 k = 0; k < nm; k++) if (multi[k] == ty) rad = 18.0f; }
    float a = frand() * 6.2831853f, rr = rad * sqrt_(frand());
    float ry = scoped() ? 240.0f : 190.0f;   /* the scope's crosshair is the screen centre; the over-the-shoulder reticle sits at y 190 */
    float px = 320.0f + rr * cos_(a), py = ry + rr * sin_(a);
    if (scoped()) { static u32 nz; if (nz++ < 40) { out("  [scopeshot] offset px x100 "); fo((px - 320.0f) / 1000.0f * 100.0f); fo((py - 240.0f) / 1000.0f * 100.0f); out(" depth "); fo(d01); out("\r\n"); } }
    float sc[4] = {(px - 320.0f) / 320.0f, (240.0f - py) / 240.0f, d01, 1.0f}, X[4];
    for (int r = 0; r < 4; r++) { X[r] = 0; for (int k = 0; k < 4; k++) X[r] += IV[r * 4 + k] * sc[k]; }
    if (X[3] == 0) return; float T[3] = {X[0] / X[3], X[1] / X[3], X[2] / X[3]};
    float v[3] = {T[0] - pos[0], T[1] - pos[1], T[2] - pos[2]}, l = sqrt_(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l < 0.01f) return;
    float od[3] = {dir[0], dir[1], dir[2]};
    for (int k = 0; k < 3; k++) dir[k] = v[k] / l;
    /* the bullet trail (0x2212E0, after the ray) is drawn from the weapon's own stored direction (+0x25C + barrel x 12), not
       from the copy the ray gets: write the new direction there too, so the trail follows the shot. notracerfix.txt = off */
    if (fattr_cached("notracerfix.txt") == 0xFFFFFFFF) for (int ix = 0; ix < 4; ix++) { float *sd = (float *)(w + 0x25C + ix * 12);
        if (readable(sd, 12) && sd[0] == od[0] && sd[1] == od[1] && sd[2] == od[2]) { sd[0] = dir[0]; sd[1] = dir[1]; sd[2] = dir[2]; break; } }
    static u32 n; if (n < 16) { n++; out("  [aim] slot "); dec((u32)s); out(" d "); fo(d01 * 1000); out("dist "); float e[3] = {T[0] - g_eye_ours[0], T[1] - g_eye_ours[1], T[2] - g_eye_ours[2]}; fo(sqrt_(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]));
        out("old "); fo(od[0]); fo(od[1]); fo(od[2]); out("new "); fo(dir[0]); fo(dir[1]); fo(dir[2]); out("muzzle-eye "); fo(pos[0] - g_eye_ours[0]); fo(pos[1] - g_eye_ours[1]); fo(pos[2] - g_eye_ours[2]); out("\r\n"); }
}
void fb_log(u8 *w, u32 type) { static u32 n; if (n < 10) { n++; out("  [fire] weapon "); hex((u32)w); out(" bullet type "); dec(type); out(" owner "); hex(w ? *(u32 *)(w + 8) : 0); out("\r\n"); } }
__attribute__((naked)) static void fb_stub(void) { __asm__ volatile(
    "pushal\n\t pushl %eax\n\t pushl %ecx\n\t call _fb_log\n\t addl $8, %esp\n\t popal\n\t subl $0x60, %esp\n\t pushl %ebx\n\t xorb %bl, %bl\n\t pushl $0x22F1D6\n\t ret"); }
/* Milestone 93 probe (vsclog.txt): who uploads vertex shader constants in big blocks (bone palettes): return address,
   register, dword count; 8 s of counts after 40 s, then a summary */
static u32 vsc_ra[48], vsc_n[48], vsc_reg[48], vsc_cnt[48], vsc_k, vsc_t0, vsc_done, vsc_on = 2;
void vsc_log(u32 ra, u32 reg, u32 cnt) {
    if (vsc_on == 2) vsc_on = fattr_cached("vsclog.txt") != 0xFFFFFFFF; if (!vsc_on || vsc_done || !g_player_char) return;
    u32 t = GetTickCount(); if (!vsc_t0) vsc_t0 = t; if (t - vsc_t0 < 15000) return;
    if (t - vsc_t0 > 23000) { vsc_done = 1; out("  [vsc] uploads of >= 12 dwords (return address, register, dwords, calls):\r\n"); for (u32 k = 0; k < vsc_k; k++) { out("    "); hex(vsc_ra[k]); out(" r"); dec(vsc_reg[k]); out(" n"); dec(vsc_cnt[k]); out(" x"); dec(vsc_n[k]); out("\r\n"); } return; }
    if (cnt < 12) return; u32 k = 0; while (k < vsc_k && !(vsc_ra[k] == ra && vsc_reg[k] == reg && vsc_cnt[k] == cnt)) k++;
    if (k == vsc_k && vsc_k < 48) { vsc_ra[k] = ra; vsc_reg[k] = reg; vsc_cnt[k] = cnt; vsc_k++; } if (k < 48) vsc_n[k]++; }
__attribute__((naked)) static void vsc_stub(void) { __asm__ volatile(
    "pushal\n\t pushl 36(%esp)\n\t pushl %ecx\n\t pushl 40(%esp)\n\t call _vsc_log\n\t addl $12, %esp\n\t popal\n\t"
    "pushl %esi\n\t pushl %edi\n\t movl 0x37bd40, %edi\n\t pushl $0x36F6C8\n\t ret"); }
/* Milestone 93 probe (bonedump.txt): the skin palette builder 0x95460(bones, index table, count) copies, for each slot,
   the 3x4 matrix at bones + 8 + index x 72. Dumps each big skeleton once after 15 s of play, plus the player's parts. */
void bone_log(u8 *bones, u8 *idx, u32 cnt) {
    static u32 on = 2, t0, seen[8], ns; if (on == 2) on = fattr_cached("bonedump.txt") != 0xFFFFFFFF;
    if (!on || !g_player_char || cnt < 20 || ns >= 4) return; u32 t = GetTickCount(); if (!t0) t0 = t; if (t - t0 < 15000) return;
    for (u32 k = 0; k < ns; k++) if (seen[k] == (u32)bones) return; seen[ns++] = (u32)bones;
    out("  [bones] array "); hex((u32)bones); out(" slots "); dec(cnt); out(" index:"); for (u32 k = 0; k < cnt; k++) { out(" "); dec(idx[k]); } out("\r\n");
    u32 mx = 0; for (u32 k = 0; k < cnt; k++) if (idx[k] > mx) mx = idx[k];
    for (u32 b = 0; b <= mx; b++) { float *m = (float *)(bones + 8 + b * 72); out("   b"); dec(b); out(":"); for (int q = 0; q < 12; q++) fo(m[q]); u32 *h = (u32 *)(bones + b * 72); out(" hdr "); hex(h[0]); out(" "); hex(h[1]); out("\r\n"); }
    u8 *c = (u8 *)g_player_char; float *pos = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c) + 9;
    out("   player parts (char field: address, rotation, position):\r\n");
    static const u32 fl[] = {0x3E8, 0x3EC, 0x3F0, 0x3F4, 0x3F8, 0x3FC, 0x400, 0x404, 0x420, 0x424};
    for (int f = 0; f < 10; f++) { u8 *p = *(u8 **)(c + fl[f]); int part_like(u8 *, float *); if (!part_like(p, pos)) continue; float *W = (float *)(p + 0x14); out("    +"); hex(fl[f]); out(" "); hex((u32)p); out(":"); for (int q = 0; q < 12; q++) fo(W[q]); out("\r\n"); } }
/* Milestone 93: arm poses on the skin palette. The player's skeleton (#f18f3616): 27 R Clavicle, 29 R UpperArm,
   52 R Forearm, 55/59 R_Arm_Bone01/03, 57 R Hand, 65-69 75-79 85-89 R fingers; left = right - 1. Bone records are 72
   bytes: +4 part object (joint world position at part +0x38), +8 skin matrix 3x4 (rows: R | t, world space).
   A chain is turned rigidly about its shoulder joint: M' = [Q | S - Q S] M. Applied once per frame per bone (a bone that
   already holds our last output is left alone). posetest.txt: "<right deg> <left deg>" about the character's right axis. */
static const u8 arm_r[] = {29, 52, 55, 57, 59, 65, 66, 67, 68, 69, 75, 76, 77, 78, 79, 85, 86, 87, 88, 89};
static float pose_last[90][12];
static void rot_axis(float *Q, const float *a, float ang) { float c, s; __asm__("fcos" : "=t"(c) : "0"(ang)); __asm__("fsin" : "=t"(s) : "0"(ang)); float t = 1 - c, x = a[0], y = a[1], z = a[2];
    Q[0] = t * x * x + c; Q[1] = t * x * y - s * z; Q[2] = t * x * z + s * y; Q[3] = t * x * y + s * z; Q[4] = t * y * y + c; Q[5] = t * y * z - s * x; Q[6] = t * x * z - s * y; Q[7] = t * y * z + s * x; Q[8] = t * z * z + c; }
/* the gun forearm (pivot: the elbow, bone 52's joint) with hand, twist and fingers; the free arm (pivot: shoulder, bone 28) */
static const u8 fore_r[] = {52, 57, 59, 65, 66, 67, 68, 69, 75, 76, 77, 78, 79, 85, 86, 87, 88, 89};
static const u8 arm_l[] = {28, 51, 54, 56, 58, 60, 61, 62, 63, 64, 70, 71, 72, 73, 74, 80, 81, 82, 83, 84};
static float gun_last[12]; u8 *g_gun_part; float g_gun_orig[12];
static void xform_about(float *o, const float *m, const float *Q, const float *S) {   /* 3x4 (rows R|t) -> [Q | S - QS] m */
    for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) o[r * 4 + k] = Q[r * 3] * m[k] + Q[r * 3 + 1] * m[4 + k] + Q[r * 3 + 2] * m[8 + k];
        o[r * 4 + 3] = Q[r * 3] * (m[3] - S[0]) + Q[r * 3 + 1] * (m[7] - S[1]) + Q[r * 3 + 2] * (m[11] - S[2]) + S[r]; } }
static void pose_list(u8 *bones, const u8 *list, u32 n, float ang, const float *axis, int gun) {
    u8 *pv = *(u8 **)(bones + list[0] * 72 + 4); if (!readable(pv + 0x38, 12)) return; float *S = (float *)(pv + 0x38), Q[9]; rot_axis(Q, axis, ang);
    for (u32 i = 0; i < n; i++) { int b = list[i]; float *m = (float *)(bones + 8 + b * 72), *L = pose_last[b];
        int same = 1; for (int q = 0; q < 12; q++) if (m[q] != L[q]) { same = 0; break; } if (same) continue;
        float o[12]; xform_about(o, m, Q, S); for (int q = 0; q < 12; q++) { m[q] = o[q]; L[q] = o[q]; } }
    extern u8 *g_pweapon; u8 *gp = gun && g_pweapon ? *(u8 **)(g_pweapon + 0x6C) : 0;   /* the rifle's part: rotation +0x14 (3x3, rows), position +0x38 */
    if (gp && readable(gp + 0x14, 0x30)) { float *R = (float *)(gp + 0x14), *T = (float *)(gp + 0x38), m[12], o[12];
        for (int r = 0; r < 3; r++) { m[r * 4] = R[r * 3]; m[r * 4 + 1] = R[r * 3 + 1]; m[r * 4 + 2] = R[r * 3 + 2]; m[r * 4 + 3] = T[r]; }
        int same = 1; for (int q = 0; q < 12; q++) if (m[q] != gun_last[q]) { same = 0; break; }
        if (!same) { xform_about(o, m, Q, S); for (int r = 0; r < 3; r++) { R[r * 3] = o[r * 4]; R[r * 3 + 1] = o[r * 4 + 1]; R[r * 3 + 2] = o[r * 4 + 2]; T[r] = o[r * 4 + 3]; } for (int q = 0; q < 12; q++) gun_last[q] = o[q]; } } }
/* the free arm during the sprint (Milestone 93): bent at the elbow, swung from the shoulder with the right leg's phase
   (knee - hip along the character's forward direction): right leg forward = left arm forward */
static const u8 fore_l[] = {51, 56, 58, 60, 61, 62, 63, 64, 70, 71, 72, 73, 74, 80, 81, 82, 83, 84};
static void pose_free(u8 *bones, float a_sh, float a_el, const float *axis) {
    u8 *ps = *(u8 **)(bones + 28 * 72 + 4), *pe = *(u8 **)(bones + 51 * 72 + 4); if (!readable(ps + 0x38, 12) || !readable(pe + 0x38, 12)) return;
    float S[3], E[3], Qs[9], Qe[9]; for (int k = 0; k < 3; k++) { S[k] = ((float *)(ps + 0x38))[k]; E[k] = ((float *)(pe + 0x38))[k]; }
    rot_axis(Qs, axis, a_sh); rot_axis(Qe, axis, a_el);
    for (u32 i = 0; i < sizeof arm_l; i++) { int b = arm_l[i]; float *m = (float *)(bones + 8 + b * 72), *L = pose_last[b];
        int same = 1; for (int q = 0; q < 12; q++) if (m[q] != L[q]) { same = 0; break; } if (same) continue;
        int fore = 0; for (u32 j = 0; j < sizeof fore_l; j++) if (fore_l[j] == b) fore = 1;
        float t[12], o[12]; if (fore) { xform_about(t, m, Qe, E); xform_about(o, t, Qs, S); } else xform_about(o, m, Qs, S);
        for (int q = 0; q < 12; q++) { m[q] = o[q]; L[q] = o[q]; } } }
static float leg_phase(u8 *bones, const float *fwd) {   /* right leg: + forward, - back (about -0.6..0.6) */
    u8 *ph = *(u8 **)(bones + 4 * 72 + 4), *pk = *(u8 **)(bones + 9 * 72 + 4); if (!readable(ph + 0x38, 12) || !readable(pk + 0x38, 12)) return 0;
    float *H = (float *)(ph + 0x38), *Kn = (float *)(pk + 0x38), d[3] = {Kn[0] - H[0], Kn[1] - H[1], Kn[2] - H[2]}, l = sqrt_(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    return l > 0.01f ? (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / l : 0; }
/* one arm (Milestone 93): every bone of the arm turns a_sh about the shoulder joint; the forearm bones (and the rifle,
   if gun) first turn a_el about the elbow. Each bone is changed once per frame (pose_last). */
static float *part_pos(u8 *bones, int b);
static float g_htwist;   /* hand twist about the forearm at the wrist, for the next pose_arm (radians) */
static float g_abd;   /* outward swing of the arm (radians, about the forward axis, toward the character's right) for the next pose_arm */
static void pose_arm(u8 *bones, const u8 *arm, u32 na, const u8 *fore, u32 nf, float a_sh, float a_el, const float *axis, int gun) {
    u8 *ps = *(u8 **)(bones + arm[0] * 72 + 4), *pe = *(u8 **)(bones + fore[0] * 72 + 4); if (!readable(ps + 0x38, 12) || !readable(pe + 0x38, 12)) return;
    float S[3], E[3], Qs[9], Qe[9]; for (int k = 0; k < 3; k++) { S[k] = ((float *)(ps + 0x38))[k]; E[k] = ((float *)(pe + 0x38))[k]; }
    rot_axis(Qs, axis, a_sh); rot_axis(Qe, axis, a_el);
    if (g_abd != 0) {   /* forward f = up x right (sign checked so that "down" swings toward the right axis) */
        float f[3] = {axis[2], 0, -axis[0]};   /* up (0,1,0) x right */
        float fl = sqrt_(f[0] * f[0] + f[2] * f[2]); if (fl > 1e-4f) { f[0] /= fl; f[2] /= fl;
            float sg = (f[2] * axis[0] - f[0] * axis[2]) >= 0 ? 1.0f : -1.0f;   /* f x (0,-1,0) = (f[2], 0, -f[0]): which way "down" turns */ float Qa[9], T[9]; rot_axis(Qa, f, g_abd * sg);
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) T[i * 3 + j] = Qa[i * 3] * Qs[j] + Qa[i * 3 + 1] * Qs[3 + j] + Qa[i * 3 + 2] * Qs[6 + j];
            for (int q = 0; q < 9; q++) Qs[q] = T[q]; } }
    for (u32 i = 0; i < na; i++) { int b = arm[i]; float *m = (float *)(bones + 8 + b * 72), *L = pose_last[b];
        int same = 1; for (int q = 0; q < 12; q++) if (m[q] != L[q]) { same = 0; break; } if (same) continue;
        int fo2 = 0; for (u32 j = 0; j < nf; j++) if (fore[j] == b) fo2 = 1;
        float t[12], o[12], h0[12]; const float *src = m;
        if (fo2 && g_htwist != 0 && b != fore[0] && b != 58 && b != 59 && b != 55 && b != 54) {   /* hand and fingers: twist about the forearm (elbow -> wrist) at the wrist */
            float *Wp = part_pos(bones, fore[1]); if (Wp) { float ax2[3] = {Wp[0] - E[0], Wp[1] - E[1], Wp[2] - E[2]}, l = sqrt_(ax2[0] * ax2[0] + ax2[1] * ax2[1] + ax2[2] * ax2[2]);
                if (l > 1e-4f) { for (int k = 0; k < 3; k++) ax2[k] /= l; float Qh[9]; rot_axis(Qh, ax2, g_htwist); xform_about(h0, m, Qh, Wp); src = h0; } } }
        if (fo2) { xform_about(t, src, Qe, E); xform_about(o, t, Qs, S); } else xform_about(o, src, Qs, S);
        for (int q = 0; q < 12; q++) { m[q] = o[q]; L[q] = o[q]; } }
    u8 *pick_weapon_part(u8 *); u8 *gp = gun ? pick_weapon_part(*(u8 **)(bones + fore[1] * 72 + 4)) : 0;
    if (gp && readable(gp + 0x14, 0x30)) { float *R = (float *)(gp + 0x14), *T = (float *)(gp + 0x38), m[12], t[12], o[12];
        for (int r = 0; r < 3; r++) { m[r * 4] = R[r * 3]; m[r * 4 + 1] = R[r * 3 + 1]; m[r * 4 + 2] = R[r * 3 + 2]; m[r * 4 + 3] = T[r]; }
        int same = 1; for (int q = 0; q < 12; q++) if (m[q] != gun_last[q]) { same = 0; break; }
        if (!same) { xform_about(t, m, Qe, E); xform_about(o, t, Qs, S); for (int r = 0; r < 3; r++) { R[r * 3] = o[r * 4]; R[r * 3 + 1] = o[r * 4 + 1]; R[r * 3 + 2] = o[r * 4 + 2]; T[r] = o[r * 4 + 3]; } for (int q = 0; q < 12; q++) gun_last[q] = o[q]; } } }
/* the sprint pose of the gun arm (Milestone 93, absolute): the arm swings out from the shoulder (g_abd), the forearm is
   turned about the elbow to point straight up, and the rifle is put in that hand, barrel up, keeping the offset it had
   from the hand that held it (the left, 56). The barrel axis is the rifle axis closest to the character's forward. */
/* Milestone 95: plain C. The x87 "fpatan" inline asm (input in st(1), popped) is mishandled by clang and could leave the FPU
   stack off, corrupting later float math (the arm IK landed 1-10 cm off). Error < 1e-5 rad. */
static float atan_unit(float z) { float z2 = z * z; return z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f + z2 * -0.01172120f))))); }
static float atan2_(float y, float x) {
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y; if (ax == 0 && ay == 0) return 0;
    if (ax >= ay) { float r = atan_unit(y / x); if (x < 0) r += y >= 0 ? 3.14159265f : -3.14159265f; return r; }
    return (y > 0 ? 1.57079633f : -1.57079633f) - atan_unit(x / y); }
float atan2_ext(float y, float x) { return atan2_(y, x); }
static void rot_between(float *Q, const float *a, const float *b) {   /* the rotation taking unit a to unit b */
    float ax[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}, s = sqrt_(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]), c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (s < 1e-6f) { for (int i = 0; i < 9; i++) Q[i] = (i % 4 == 0) ? 1.0f : 0.0f; return; } for (int k = 0; k < 3; k++) ax[k] /= s; rot_axis(Q, ax, atan2_(s, c)); }
static void mat3_vec(float *o, const float *Q, const float *v) { for (int r = 0; r < 3; r++) o[r] = Q[r * 3] * v[0] + Q[r * 3 + 1] * v[1] + Q[r * 3 + 2] * v[2]; }
/* the bind-pose joint positions of the player's skeleton #f18f3616 (model space, from its inverse bind matrices) */
const float bind_pos_ext[90][3] = {
    {-0.0000f, -0.0000f, -0.0000f},   /* 0 root */
    {-0.0000f, -0.0000f, -0.0000f},   /* 1 Bip01 */
    {0.0000f, -0.0000f, -0.0000f},   /* 2 Bip01 Pelvis */
    {-0.1141f, -0.0000f, 0.0000f},   /* 3 Bip01 L Thigh */
    {0.1141f, -0.0000f, 0.0000f},   /* 4 Bip01 R Thigh */
    {-0.0000f, 0.1003f, 0.0002f},   /* 5 Bip01 Spine */
    {-0.0955f, -0.0000f, 0.0000f},   /* 6 #0bc57686 */
    {0.0976f, -0.0000f, 0.0000f},   /* 7 #e5ca4be5 */
    {-0.2547f, -0.5162f, -0.1313f},   /* 8 Bip01 L Calf */
    {0.2547f, -0.5162f, -0.1313f},   /* 9 Bip01 R Calf */
    {-0.0000f, 0.3062f, 0.0001f},   /* 10 Bip01 Spine1 */
    {-0.1321f, -0.0697f, 0.1851f},   /* 11 #1b055735 */
    {0.1313f, -0.0709f, 0.1851f},   /* 12 #f50a6a56 */
    {-0.3408f, -0.9865f, 0.0539f},   /* 13 Bip01 L Foot */
    {0.3408f, -0.9865f, 0.0539f},   /* 14 Bip01 R Foot */
    {-0.0000f, 0.4782f, 0.0002f},   /* 15 Bip01 Spine2 */
    {-0.3982f, -1.1590f, -0.1191f},   /* 16 Bip01 L Toe0 */
    {0.0000f, 0.6761f, 0.0001f},   /* 17 Bip01 Neck */
    {0.3982f, -1.1590f, -0.1191f},   /* 18 Bip01 R Toe0 */
    {-0.1038f, 0.6405f, -0.0647f},   /* 19 L_Arm_Bone05 */
    {-0.1202f, 0.6538f, 0.1461f},   /* 20 L_Arm_Bone07 */
    {0.1086f, 0.6358f, -0.0647f},   /* 21 R_Arm_Bone05 */
    {0.1207f, 0.6491f, 0.1461f},   /* 22 R_Arm_Bone07 */
    {-0.2530f, 0.5554f, 0.1922f},   /* 23 #f2992043 */
    {0.2488f, 0.5554f, 0.1925f},   /* 24 #fe61bab3 */
    {0.0000f, 0.7996f, -0.0512f},   /* 25 Bip01 Head */
    {-0.0661f, 0.6761f, 0.0406f},   /* 26 Bip01 L Clavicle */
    {0.0663f, 0.6761f, 0.0406f},   /* 27 Bip01 R Clavicle */
    {-0.2278f, 0.6312f, 0.0462f},   /* 28 Bip01 L UpperArm */
    {0.2279f, 0.6312f, 0.0462f},   /* 29 Bip01 R UpperArm */
    {-0.0398f, 0.8679f, -0.1490f},   /* 30 #04a62930 */
    {0.0401f, 0.8679f, -0.1490f},   /* 31 #eaa91453 */
    {-0.0261f, 0.8829f, -0.1916f},   /* 32 #002bff10 */
    {0.0262f, 0.8829f, -0.1917f},   /* 33 #ee24c273 */
    {-0.0532f, 0.8928f, -0.1735f},   /* 34 #0efd825c */
    {0.0529f, 0.8928f, -0.1735f},   /* 35 #e0f2bf3f */
    {-0.0370f, 0.8679f, -0.1587f},   /* 36 #f2a60436 */
    {0.0369f, 0.8679f, -0.1587f},   /* 37 #1ca93955 */
    {-0.0371f, 0.8678f, -0.1594f},   /* 38 #0916c7cf */
    {0.0357f, 0.8678f, -0.1594f},   /* 39 #e719faac */
    {-0.0000f, 0.8509f, -0.1013f},   /* 40 #f5ddb1ef */
    {0.0000f, 0.7755f, -0.1822f},   /* 41 #e84b586c */
    {-0.0113f, 0.7740f, -0.1763f},   /* 42 #ef55bbdc */
    {0.0109f, 0.7741f, -0.1763f},   /* 43 #015a86bf */
    {-0.0229f, 0.7775f, -0.1633f},   /* 44 #e998baf5 */
    {0.0229f, 0.7775f, -0.1633f},   /* 45 #07978796 */
    {-0.0000f, 0.8211f, -0.1919f},   /* 46 #f003b628 */
    {-0.0003f, 0.7749f, -0.1364f},   /* 47 #f08d810f */
    {0.0000f, 0.7887f, -0.1822f},   /* 48 #ef9f7c54 */
    {-0.0104f, 0.7889f, -0.1810f},   /* 49 #1750d766 */
    {0.0101f, 0.7889f, -0.1810f},   /* 50 #f95fea05 */
    {-0.5186f, 0.3909f, -0.0443f},   /* 51 Bip01 L Forearm */
    {0.5188f, 0.3906f, -0.0433f},   /* 52 Bip01 R Forearm */
    {-0.0000f, 0.7832f, -0.0956f},   /* 53 #efd998c3 */
    {-0.2337f, 0.6263f, 0.0444f},   /* 54 L_Arm_Bone01 */
    {0.2417f, 0.6198f, 0.0420f},   /* 55 R_Arm_Bone01 */
    {-0.7315f, 0.2307f, -0.2350f},   /* 56 Bip01 L Hand */
    {0.7318f, 0.2302f, -0.2340f},   /* 57 Bip01 R Hand */
    {-0.7423f, 0.2226f, -0.2446f},   /* 58 L_Arm_Bone03 */
    {0.7425f, 0.2221f, -0.2436f},   /* 59 R_Arm_Bone03 */
    {-0.7349f, 0.2301f, -0.2881f},   /* 60 Bip01 L Finger0 */
    {-0.8059f, 0.1884f, -0.3358f},   /* 61 Bip01 L Finger1 */
    {-0.8110f, 0.1639f, -0.3084f},   /* 62 Bip01 L Finger2 */
    {-0.8130f, 0.1443f, -0.2842f},   /* 63 Bip01 L Finger3 */
    {-0.8070f, 0.1256f, -0.2586f},   /* 64 Bip01 L Finger4 */
    {0.7320f, 0.2285f, -0.2839f},   /* 65 Bip01 R Finger0 */
    {0.8060f, 0.1836f, -0.3365f},   /* 66 Bip01 R Finger1 */
    {0.8129f, 0.1655f, -0.3085f},   /* 67 Bip01 R Finger2 */
    {0.8111f, 0.1457f, -0.2829f},   /* 68 Bip01 R Finger3 */
    {0.8057f, 0.1236f, -0.2590f},   /* 69 Bip01 R Finger4 */
    {-0.7388f, 0.2170f, -0.3442f},   /* 70 Bip01 L Finger01 */
    {-0.8458f, 0.1558f, -0.3704f},   /* 71 Bip01 L Finger11 */
    {-0.8558f, 0.1287f, -0.3412f},   /* 72 Bip01 L Finger21 */
    {-0.8486f, 0.1119f, -0.3082f},   /* 73 Bip01 L Finger31 */
    {-0.8337f, 0.0945f, -0.2757f},   /* 74 Bip01 L Finger41 */
    {0.7384f, 0.2141f, -0.3395f},   /* 75 Bip01 R Finger01 */
    {0.8466f, 0.1529f, -0.3720f},   /* 76 Bip01 R Finger11 */
    {0.8567f, 0.1294f, -0.3417f},   /* 77 Bip01 R Finger21 */
    {0.8494f, 0.1120f, -0.3082f},   /* 78 Bip01 R Finger31 */
    {0.8333f, 0.0944f, -0.2772f},   /* 79 Bip01 R Finger41 */
    {-0.7506f, 0.1886f, -0.3962f},   /* 80 Bip01 L Finger02 */
    {-0.8620f, 0.1364f, -0.3894f},   /* 81 Bip01 L Finger12 */
    {-0.8753f, 0.1025f, -0.3632f},   /* 82 Bip01 L Finger22 */
    {-0.8679f, 0.0883f, -0.3260f},   /* 83 Bip01 L Finger32 */
    {-0.8473f, 0.0753f, -0.2861f},   /* 84 Bip01 L Finger42 */
    {0.7527f, 0.1872f, -0.3972f},   /* 85 Bip01 R Finger02 */
    {0.8624f, 0.1334f, -0.3913f},   /* 86 Bip01 R Finger12 */
    {0.8756f, 0.1024f, -0.3632f},   /* 87 Bip01 R Finger22 */
    {0.8670f, 0.0872f, -0.3261f},   /* 88 Bip01 R Finger32 */
    {0.8470f, 0.0762f, -0.2890f},   /* 89 Bip01 R Finger42 */
};
void joint_check(u8 *bones) { static u32 tl; u32 nw = GetTickCount(); if (nw - tl < 5000) return; tl = nw; static const int bb[4] = {29, 52, 57, 17}; out("  [joints] skin-based minus part object (mm):");
    for (int i = 0; i < 4; i++) { u8 *p = *(u8 **)(bones + bb[i] * 72 + 4); if (!readable(p + 0x38, 12)) continue; float *P = (float *)(p + 0x38), *m = (float *)(bones + 8 + bb[i] * 72), q[3]; extern const float bind_pos_ext[90][3];
        for (int r = 0; r < 3; r++) q[r] = m[r * 4] * bind_pos_ext[bb[i]][0] + m[r * 4 + 1] * bind_pos_ext[bb[i]][1] + m[r * 4 + 2] * bind_pos_ext[bb[i]][2] + m[r * 4 + 3];
        out(" b"); dec(bb[i]); out(" "); fo(q[0] - P[0]); fo(q[1] - P[1]); fo(q[2] - P[2]); } out("\r\n"); }
static int g_ik_skin;   /* Milestone 95: joints from the skin matrices (bind position x matrix), not the part objects (those lag a frame while turning) */
static float g_skinp[90][3];
static float *part_pos(u8 *bones, int b) {   /* the joint's position from its part object (what the sprint pose was tuned with) */
    if (g_ik_skin && b >= 0 && b < 90) { float *m = (float *)(bones + 8 + b * 72), *q = g_skinp[b];
        for (int r = 0; r < 3; r++) q[r] = m[r * 4] * bind_pos_ext[b][0] + m[r * 4 + 1] * bind_pos_ext[b][1] + m[r * 4 + 2] * bind_pos_ext[b][2] + m[r * 4 + 3]; return q; }
    u8 *p = *(u8 **)(bones + b * 72 + 4); return readable(p + 0x38, 12) ? (float *)(p + 0x38) : 0; }
static float g_twist = 0; float g_hand_h = 0.15f;   /* the hand above the shoulder (m) */   /* the wrist twist of the sprint pose (radians, about the vertical forearm) */
static void pose_gun_arm(u8 *bones, const float *right, float abd, float amount) {
    float *Sp = part_pos(bones, 29), *Ep = part_pos(bones, 52), *Wp = part_pos(bones, 57), *Lp = part_pos(bones, 56), *LFp = part_pos(bones, 61), *RFp = part_pos(bones, 66);
    if (!Sp || !Ep || !Wp || !Lp || !LFp || !RFp) return;
    float S[3], E[3], W[3], LH[3], RP[3]; for (int k = 0; k < 3; k++) { S[k] = Sp[k]; E[k] = Ep[k]; W[k] = Wp[k]; LH[k] = (Lp[k] + LFp[k]) * 0.5f; RP[k] = (Wp[k] + RFp[k]) * 0.5f; }   /* palms: wrist .. first finger joint */
    float f[3] = {right[2], 0, -right[0]}, fl = sqrt_(f[0] * f[0] + f[2] * f[2]); if (fl < 1e-4f) return; f[0] /= fl; f[2] /= fl;
    {   extern float g_cb[3]; if (-(g_cb[0] * f[0] + g_cb[2] * f[2]) < 0) { f[0] = -f[0]; f[2] = -f[2]; } }   /* f = the body's forward (sign only from the camera) */
    /* the elbow's goal: the hand at shoulder height straight in front of the shoulder, so the elbow sits one forearm length
       below the shoulder, forward by what is left of the upper arm, pulled in toward the chest by abd (metres) */
    float Qs[9]; {
        float *Np = part_pos(bones, 17); float in[3] = {0, 0, 0}; if (Np) { float d = (Np[0] - S[0]) * right[0] + (Np[2] - S[2]) * right[2]; float s = d >= 0 ? 1.0f : -1.0f; in[0] = right[0] * s; in[2] = right[2] * s; }
        float U = sqrt_((E[0] - S[0]) * (E[0] - S[0]) + (E[1] - S[1]) * (E[1] - S[1]) + (E[2] - S[2]) * (E[2] - S[2])), Lf = sqrt_((W[0] - E[0]) * (W[0] - E[0]) + (W[1] - E[1]) * (W[1] - E[1]) + (W[2] - E[2]) * (W[2] - E[2]));
        extern float g_hand_h; float dn = Lf - g_hand_h; if (dn > U * 0.95f) dn = U * 0.95f; if (dn < -U * 0.95f) dn = -U * 0.95f; float rest = U * U - dn * dn - abd * abd, fwd = rest > 0 ? sqrt_(rest) : 0;
        float g1[3] = {f[0] * fwd + in[0] * abd, -dn, f[2] * fwd + in[2] * abd}, c0[3] = {E[0] - S[0], E[1] - S[1], E[2] - S[2]};
        float gl = sqrt_(g1[0] * g1[0] + g1[1] * g1[1] + g1[2] * g1[2]), cl = U; if (gl < 1e-4f || cl < 1e-4f) return;
        for (int k = 0; k < 3; k++) { g1[k] /= gl; c0[k] /= cl; g1[k] = c0[k] + (g1[k] - c0[k]) * amount; } gl = sqrt_(g1[0] * g1[0] + g1[1] * g1[1] + g1[2] * g1[2]); for (int k = 0; k < 3; k++) g1[k] /= gl;
        rot_between(Qs, c0, g1); }
    float E1[3], W1[3], t[3]; for (int k = 0; k < 3; k++) t[k] = E[k] - S[k]; mat3_vec(E1, Qs, t); for (int k = 0; k < 3; k++) { E1[k] += S[k]; t[k] = W[k] - S[k]; } mat3_vec(W1, Qs, t); for (int k = 0; k < 3; k++) W1[k] += S[k];
    float d1[3] = {W1[0] - E1[0], W1[1] - E1[1], W1[2] - E1[2]}, dl = sqrt_(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]); if (dl < 1e-4f) return; for (int k = 0; k < 3; k++) d1[k] /= dl;
    float up[3] = {0, 1, 0}, goal[3], Qe[9], Qfull[9], I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int k = 0; k < 3; k++) goal[k] = d1[k] + (up[k] - d1[k]) * amount; { float gl = sqrt_(goal[0] * goal[0] + goal[1] * goal[1] + goal[2] * goal[2]); for (int k = 0; k < 3; k++) goal[k] /= gl; }
    rot_between(Qe, d1, goal); (void)Qfull; (void)I;
    float W2[3], Qt[9]; for (int k = 0; k < 3; k++) t[k] = W1[k] - E1[k]; mat3_vec(W2, Qe, t); for (int k = 0; k < 3; k++) W2[k] += E1[k];   /* the wrist after the pose */
    { float dirv[3] = {W2[0] - E1[0], W2[1] - E1[1], W2[2] - E1[2]}, l = sqrt_(dirv[0] * dirv[0] + dirv[1] * dirv[1] + dirv[2] * dirv[2]); if (l < 1e-4f) return; for (int k = 0; k < 3; k++) dirv[k] /= l; rot_axis(Qt, dirv, g_twist * amount); }
    { static u32 n, tl; u32 now = GetTickCount(); n++; if (now - tl > 2000) { tl = now; out("  [pose] gun arm applied x"); dec(n); out(" twist deg "); fo(g_twist * 57.2958f); out("amount "); fo(amount); out("\r\n"); n = 0; } }
    for (u32 i = 0; i < sizeof arm_r; i++) { int b = arm_r[i]; float *m = (float *)(bones + 8 + b * 72), *L = pose_last[b];
        int same = 1; for (int q = 0; q < 12; q++) if (m[q] != L[q]) { same = 0; break; } if (same) continue;
        int fo2 = 0; for (u32 j = 0; j < sizeof fore_r; j++) if (fore_r[j] == b) fo2 = 1; int hand = fo2 && b != 52 && b != 59;   /* the hand and fingers also twist at the wrist */
        float a[12], o[12], h[12]; xform_about(a, m, Qs, S); if (fo2) xform_about(o, a, Qe, E1); else for (int q = 0; q < 12; q++) o[q] = a[q];
        if (hand) { xform_about(h, o, Qt, W2); for (int q = 0; q < 12; q++) o[q] = h[q]; }
        for (int q = 0; q < 12; q++) { m[q] = o[q]; L[q] = o[q]; } }
    float RH[3], P1[3]; for (int k = 0; k < 3; k++) t[k] = RP[k] - S[k]; mat3_vec(P1, Qs, t); for (int k = 0; k < 3; k++) { P1[k] += S[k]; t[k] = P1[k] - E1[k]; } mat3_vec(RH, Qe, t); for (int k = 0; k < 3; k++) { RH[k] += E1[k]; t[k] = RH[k] - W2[k]; } mat3_vec(RH, Qt, t); for (int k = 0; k < 3; k++) RH[k] += W2[k];   /* the right palm after the pose and the twist */
    /* the rifle moves rigidly with the right hand (its grip stays as animated; the barrel turns up with the forearm) */
    u8 *pick_weapon_part(u8 *); u8 *gp = pick_weapon_part(*(u8 **)(bones + 57 * 72 + 4));   /* the weapon he fired with, else the one nearest the right hand */ if (!gp || !readable(gp + 0x14, 0x30)) return;
    float *R = (float *)(gp + 0x14), *T = (float *)(gp + 0x38), cur[12];
    for (int r = 0; r < 3; r++) { cur[r * 4] = R[r * 3]; cur[r * 4 + 1] = R[r * 3 + 1]; cur[r * 4 + 2] = R[r * 3 + 2]; cur[r * 4 + 3] = T[r]; }
    { int same = 1; for (int q = 0; q < 12; q++) if (cur[q] != gun_last[q]) { same = 0; break; } if (same) return; }
    { extern u8 *g_gun_part; extern float g_gun_orig[12]; g_gun_part = gp; for (int q = 0; q < 12; q++) g_gun_orig[q] = cur[q]; }   /* the animation's placement, restored when the pose ends */
    float a1[12], a2[12], a3[12]; xform_about(a1, cur, Qs, S); xform_about(a2, a1, Qe, E1); xform_about(a3, a2, Qt, W2);
    {   /* then turn the rifle about the right palm until its barrel points straight up (the barrel: hardpoint 0's direction,
           carried through the same turns) */
        extern u8 *g_picked_weapon; float bl[3] = {0, 0, 1};
        if (g_picked_weapon) { float l = 1.0f;   /* the barrel: the rifle part's local Z axis (from its fresh rotation), signed to point forward (it does while running) */
            if (l > 0.5f) { float b0[3], b1[3], b2[3], b3[3]; for (int r = 0; r < 3; r++) b0[r] = cur[r * 4 + 2];
                {   /* sign: from the (fresh) right palm toward the rifle part's origin, which lies on the barrel side of the grip */
                    float og[3] = {cur[3] - RP[0], cur[7] - RP[1], cur[11] - RP[2]}; if (og[0] * b0[0] + og[1] * b0[1] + og[2] * b0[2] < 0) { for (int k = 0; k < 3; k++) b0[k] = -b0[k]; bl[2] = -1; } }
                { static u32 tlg; u32 nw = GetTickCount(); if (nw - tlg > 2000) { tlg = nw; out("  [barrel] local "); fo(bl[0]); fo(bl[1]); fo(bl[2]); out(" world . forward "); fo(b0[0] * f[0] + b0[2] * f[2]); out(" . up "); fo(b0[1]); out(" . right "); fo(b0[0] * right[0] + b0[1] * right[1] + b0[2] * right[2]);
                    extern u8 *g_pweapon; out(" weapon "); hex((u32)g_picked_weapon); out(" fired "); hex((u32)g_pweapon); out("\r\n"); } }
                mat3_vec(b1, Qs, b0); mat3_vec(b2, Qe, b1); mat3_vec(b3, Qt, b2);
                float upv[3] = {0, 1, 0}, Qb[9], a4[12]; rot_between(Qb, b3, upv); xform_about(a4, a3, Qb, RH); for (int q = 0; q < 12; q++) a3[q] = a4[q];
                {   /* roll: the rifle's underside (the part of "down" across the barrel before the pose) faces forward, as in the reference */
                    float dn0[3] = {0, -1, 0}, k0 = dn0[0] * b0[0] + dn0[1] * b0[1] + dn0[2] * b0[2]; for (int k = 0; k < 3; k++) dn0[k] -= k0 * b0[k];
                    float d1[3], d2[3], d3[3], d4[3]; mat3_vec(d1, Qs, dn0); mat3_vec(d2, Qe, d1); mat3_vec(d3, Qt, d2); mat3_vec(d4, Qb, d3);
                    float hl = sqrt_(d4[0] * d4[0] + d4[2] * d4[2]); extern float g_cb[3]; float fw2[3] = {-g_cb[0], 0, -g_cb[2]}, fl3 = sqrt_(fw2[0] * fw2[0] + fw2[2] * fw2[2]);
                    if (hl > 1e-3f && fl3 > 1e-3f) { float h[3] = {d4[0] / hl, 0, d4[2] / hl}; fw2[0] /= fl3; fw2[2] /= fl3; static float rollofs; (void)rollofs;
                        float Qr[9], a5[12]; rot_between(Qr, h, fw2); xform_about(a5, a3, Qr, RH); for (int q = 0; q < 12; q++) a3[q] = a5[q]; } } } } }
    for (int r = 0; r < 3; r++) { R[r * 3] = a3[r * 4]; R[r * 3 + 1] = a3[r * 4 + 1]; R[r * 3 + 2] = a3[r * 4 + 2]; T[r] = a3[r * 4 + 3]; }
    for (int q = 0; q < 12; q++) gun_last[q] = a3[q]; (void)RH; (void)LH; }
/* the shouldered aim pose (Milestone 93): the rifle is placed along the view direction with its grip in front of the right
   shoulder; both hands keep their grip on it (their placement relative to the rifle, from the unposed frame) and each arm
   is solved with two-bone IK (elbow bending down and outward). aimpose.txt: grip forward, up, side (mm). */
static const u8 up_r[] = {29, 55}, fa_r[] = {52, 59}, hd_r[] = {57, 65, 66, 67, 68, 69, 75, 76, 77, 78, 79, 85, 86, 87, 88, 89};
static const u8 up_l[] = {28, 54}, fa_l[] = {51, 58}, hd_l[] = {56, 60, 61, 62, 63, 64, 70, 71, 72, 73, 74, 80, 81, 82, 83, 84};
static float aim_fwd = 0.30f, aim_up = 0.0f, aim_side = 0.0f, aim_fore = 0.32f;   /* aim_fore: the left hand's distance along the barrel from the grip */
static void m3mul(float *o, const float *a, const float *b) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j]; }
static void m3t(float *o, const float *a) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i * 3 + j] = a[j * 3 + i]; }
static void rows3(float *R, const float *m) { for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) R[r * 3 + k] = m[r * 4 + k]; }
static void apply_list(u8 *bones, const u8 *list, u32 n, const float *Q, const float *P) {   /* turn the bones Q about P (once per frame) */
    for (u32 i = 0; i < n; i++) { int b = list[i]; float *m = (float *)(bones + 8 + b * 72), o[12]; xform_about(o, m, Q, P); for (int q = 0; q < 12; q++) { m[q] = o[q]; pose_last[b][q] = o[q]; } } }
static void ik_arm(u8 *bones, const u8 *up, u32 nu, const u8 *fa, u32 nf, const u8 *hd, u32 nh, const float *Wt, const float *RHt, const float *pole, float amount) {
    float *Sp = part_pos(bones, up[0]), *Ep = part_pos(bones, fa[0]), *Wp = part_pos(bones, hd[0]); if (!Sp || !Ep || !Wp) return;
    float S[3], E[3], W[3]; for (int k = 0; k < 3; k++) { S[k] = Sp[k]; E[k] = Ep[k]; W[k] = Wp[k]; }
    float U = sqrt_((E[0] - S[0]) * (E[0] - S[0]) + (E[1] - S[1]) * (E[1] - S[1]) + (E[2] - S[2]) * (E[2] - S[2])), L = sqrt_((W[0] - E[0]) * (W[0] - E[0]) + (W[1] - E[1]) * (W[1] - E[1]) + (W[2] - E[2]) * (W[2] - E[2]));
    float T[3]; for (int k = 0; k < 3; k++) T[k] = W[k] + (Wt[k] - W[k]) * amount;   /* eased target */
    float d[3] = {T[0] - S[0], T[1] - S[1], T[2] - S[2]}, D = sqrt_(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (D < 1e-3f || U < 1e-3f || L < 1e-3f) return;
    { static u32 tl[2]; int li = up[0] == 28; u32 nw = GetTickCount(); if (nw - tl[li] > 2000) { tl[li] = nw; out(li ? "  [ik] left" : "  [ik] right"); out(" target/reach x1000 "); fo(D / (U + L)); out("upper "); fo(U); out("fore "); fo(L);
        float sd[3] = {T[0] - S[0], T[1] - S[1], T[2] - S[2]}; out("target - shoulder "); fo(sd[0]); fo(sd[1]); fo(sd[2]); out("\r\n"); } }
    if (D > (U + L) * 0.999f) D = (U + L) * 0.999f;
    { float dl = sqrt_(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); for (int k = 0; k < 3; k++) d[k] /= dl; }   /* Milestone 95: the length was recomputed inside the loop after d[0] changed, so d was not a unit vector (hands landed 1-10 cm off) */
    float a = (U * U + D * D - L * L) / (2 * U * D); if (a > 1) a = 1; if (a < -1) a = -1; float sa = sqrt_(1 - a * a);
    float pd = pole[0] * d[0] + pole[1] * d[1] + pole[2] * d[2], n[3] = {pole[0] - pd * d[0], pole[1] - pd * d[1], pole[2] - pd * d[2]}, nl = sqrt_(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (nl < 1e-4f) return; for (int k = 0; k < 3; k++) n[k] /= nl;
    float E2[3], W2[3]; for (int k = 0; k < 3; k++) { E2[k] = S[k] + d[k] * U * a + n[k] * U * sa; W2[k] = S[k] + d[k] * D; }
    float u0[3] = {(E[0] - S[0]) / U, (E[1] - S[1]) / U, (E[2] - S[2]) / U}, u1[3] = {(E2[0] - S[0]) / U, (E2[1] - S[1]) / U, (E2[2] - S[2]) / U}, Q1[9]; rot_between(Q1, u0, u1);
    float f0[3] = {W[0] - E[0], W[1] - E[1], W[2] - E[2]}, f1[3], f2[3] = {(W2[0] - E2[0]) / L, (W2[1] - E2[1]) / L, (W2[2] - E2[2]) / L}, Q2[9];
    mat3_vec(f1, Q1, f0); for (int k = 0; k < 3; k++) f1[k] /= L; rot_between(Q2, f1, f2);
    float Q21[9]; m3mul(Q21, Q2, Q1);
    /* the hand's rotation after Q2.Q1, and the turn that brings it to the target (eased) */
    float *hm = (float *)(bones + 8 + hd[0] * 72), RH0[9], RH1[9], RHt2[9], Q3[9], It[9]; rows3(RH0, hm); m3mul(RH1, Q21, RH0);
    m3t(It, RH1); m3mul(Q3, RHt, It);   /* Q3 = target . current^T */
    {   /* ease Q3: turn about its axis by amount */
        float c = (Q3[0] + Q3[4] + Q3[8] - 1) * 0.5f; if (c > 1) c = 1; if (c < -1) c = -1; float ax[3] = {Q3[7] - Q3[5], Q3[2] - Q3[6], Q3[3] - Q3[1]}, al = sqrt_(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (al > 1e-5f) { for (int k = 0; k < 3; k++) ax[k] /= al; rot_axis(Q3, ax, atan2_(al * 0.5f, c) * amount); } else { for (int i = 0; i < 9; i++) Q3[i] = (i % 4 == 0) ? 1.0f : 0.0f; } }
    (void)RHt2;
    /* upper arm: Q1 about S; forearm: Q1 about S then Q2 about E2; hand: then Q3 about the new wrist */
    float Wn[3], t0[3]; for (int k = 0; k < 3; k++) t0[k] = W[k] - S[k]; mat3_vec(Wn, Q1, t0); for (int k = 0; k < 3; k++) { Wn[k] += S[k]; t0[k] = Wn[k] - E2[k]; } { float t1[3]; mat3_vec(t1, Q2, t0); for (int k = 0; k < 3; k++) Wn[k] = E2[k] + t1[k]; }
    apply_list(bones, up, nu, Q1, S); apply_list(bones, fa, nf, Q1, S); apply_list(bones, fa, nf, Q2, E2);
    apply_list(bones, hd, nh, Q1, S); apply_list(bones, hd, nh, Q2, E2); apply_list(bones, hd, nh, Q3, Wn);
    {   /* probe (left arm): the wrist after the solve vs where the solve put it (W2) and vs the eased target; elbow vs E2 (mm) */
        static u32 tl, nik; u32 nw = GetTickCount(); if (up[0] == 28 && nik < 200 && nw - tl > 2000) { nik++; tl = nw; float *Wa = part_pos(bones, hd[0]), *Ea = part_pos(bones, fa[0]);
            if (Wa && Ea) { float a1 = sqrt_((Wa[0] - W2[0]) * (Wa[0] - W2[0]) + (Wa[1] - W2[1]) * (Wa[1] - W2[1]) + (Wa[2] - W2[2]) * (Wa[2] - W2[2]));
                float a2 = sqrt_((Wa[0] - T[0]) * (Wa[0] - T[0]) + (Wa[1] - T[1]) * (Wa[1] - T[1]) + (Wa[2] - T[2]) * (Wa[2] - T[2]));
                float a3 = sqrt_((Ea[0] - E2[0]) * (Ea[0] - E2[0]) + (Ea[1] - E2[1]) * (Ea[1] - E2[1]) + (Ea[2] - E2[2]) * (Ea[2] - E2[2]));
                float a4 = sqrt_((W[0] - T[0]) * (W[0] - T[0]) + (W[1] - T[1]) * (W[1] - T[1]) + (W[2] - T[2]) * (W[2] - T[2]));
                out("  [iksolve] wrist vs solved "); dec((u32)(a1 * 1000)); out(" vs target "); dec((u32)(a2 * 1000)); out(" elbow vs solved "); dec((u32)(a3 * 1000)); out(" start vs target "); dec((u32)(a4 * 1000)); out(" mm\r\n"); } } } }
/* the hip-fire grip (recorded while he fires without aiming or sprinting): wrists and hand rotations in the rifle's frame */
static u8 *grip_w; static int grip_ok; static float gripR_p[3], gripR_r[9], gripL_p[3], gripL_r[9];
void grip_record(u8 *bones) {
    u8 *pick_weapon_part(u8 *); extern u8 *g_picked_weapon; u8 *gp = pick_weapon_part(*(u8 **)(bones + 57 * 72 + 4)); if (!gp || !readable(gp + 0x14, 0x30)) return;
    float *GR = (float *)(gp + 0x14), *GT = (float *)(gp + 0x38), *WR = part_pos(bones, 57), *WL = part_pos(bones, 56); if (!WR || !WL) return;
    float GRt[9], RH[9]; m3t(GRt, GR);
    { float d[3] = {WR[0] - GT[0], WR[1] - GT[1], WR[2] - GT[2]}; mat3_vec(gripR_p, GRt, d); rows3(RH, (float *)(bones + 8 + 57 * 72)); m3mul(gripR_r, GRt, RH); }
    { float d[3] = {WL[0] - GT[0], WL[1] - GT[1], WL[2] - GT[2]}; mat3_vec(gripL_p, GRt, d); rows3(RH, (float *)(bones + 8 + 56 * 72)); m3mul(gripL_r, GRt, RH); }
    if (!grip_ok || grip_w != g_picked_weapon) { out("  [aimpose] hip-fire grip recorded for weapon "); hex((u32)g_picked_weapon); out(" (left wrist "); fo(sqrt_(gripL_p[0] * gripL_p[0] + gripL_p[1] * gripL_p[1] + gripL_p[2] * gripL_p[2])); out("mm from the rifle origin)\r\n"); }
    grip_w = g_picked_weapon; grip_ok = 1;
    {   static u32 ts; u32 nw = GetTickCount(); if (nw - ts > 3000) { ts = nw; HANDLE f = CreateFileA("aimgrip.bin", 0x40000000, 0, 0, 2, 0x80, 0);   /* kept for the next runs */
            if (f != (HANDLE)-1) { DWORD w; WriteFile(f, gripR_p, 12, &w, 0); WriteFile(f, gripR_r, 36, &w, 0); WriteFile(f, gripL_p, 12, &w, 0); WriteFile(f, gripL_r, 36, &w, 0); CloseHandle(f); } } } }
void grip_try(u8 *bones) {   /* record the grip when both wrists are close to the rifle's barrel line (left within 12 cm, right within 25 cm) */
    u8 *pick_weapon_part(u8 *); u8 *gp = pick_weapon_part(*(u8 **)(bones + 57 * 72 + 4)); if (!gp || !readable(gp + 0x14, 0x30)) return;
    float *GR = (float *)(gp + 0x14), *GT = (float *)(gp + 0x38), z[3] = {GR[2], GR[5], GR[8]}, *WR = part_pos(bones, 57), *WL = part_pos(bones, 56); if (!WR || !WL) return;
    float dl[3] = {WL[0] - GT[0], WL[1] - GT[1], WL[2] - GT[2]}, dr[3] = {WR[0] - GT[0], WR[1] - GT[1], WR[2] - GT[2]}, kl = dl[0] * z[0] + dl[1] * z[1] + dl[2] * z[2], kr = dr[0] * z[0] + dr[1] * z[1] + dr[2] * z[2];
    for (int k = 0; k < 3; k++) { dl[k] -= kl * z[k]; dr[k] -= kr * z[k]; }
    float el = sqrt_(dl[0] * dl[0] + dl[1] * dl[1] + dl[2] * dl[2]), er = sqrt_(dr[0] * dr[0] + dr[1] * dr[1] + dr[2] * dr[2]);
    { static u32 tl; u32 nw = GetTickCount(); if (nw - tl > 4000) { tl = nw; out("  [grip] wrists from the barrel line: left "); fo(el); out("right "); fo(er); out("\r\n"); } }
    if (el < 0.12f && er < 0.25f) grip_record(bones); }
static void grip_load(void) { static int tried; if (tried) return; tried = 1; HANDLE f = CreateFileA("aimgrip.bin", 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return;
    DWORD n = 0, t = 0; ReadFile(f, gripR_p, 12, &n, 0); t += n; ReadFile(f, gripR_r, 36, &n, 0); t += n; ReadFile(f, gripL_p, 12, &n, 0); t += n; ReadFile(f, gripL_r, 36, &n, 0); t += n; CloseHandle(f);
    if (t == 96) { grip_ok = 2; out("  [aimpose] hip-fire grip loaded from aimgrip.bin\r\n"); } }
static const u8 upper_body[] = {5, 10, 15, 17, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89};
static float aim_butt = 0.22f, aim_torso = 0.8f;   /* butt: distance behind the right wrist along the barrel; torso: share of the view pitch */
static void pose_aim(u8 *bones, const float *right, float amount) {
    if (pose_last[57][0] == ((float *)(bones + 8 + 57 * 72))[0] && pose_last[57][5] == ((float *)(bones + 8 + 57 * 72))[5]) return;   /* already posed this frame */
    u8 *pick_weapon_part(u8 *); u8 *gp = pick_weapon_part(*(u8 **)(bones + 57 * 72 + 4)); if (!gp || !readable(gp + 0x14, 0x30)) return;
    float *GRp = (float *)(gp + 0x14), *GTp = (float *)(gp + 0x38), GR[9], GT[3]; for (int q = 0; q < 9; q++) GR[q] = GRp[q]; for (int k = 0; k < 3; k++) GT[k] = GTp[k];
    {   /* the rifle part still holding our last output (not rewritten by the game): continue from its pre-pose placement, so the arms keep their pose */
        int same = 1; for (int r = 0; r < 3 && same; r++) if (GR[r * 3] != gun_last[r * 4] || GT[r] != gun_last[r * 4 + 3]) same = 0;
        if (same) { extern float g_gun_orig[12]; for (int r = 0; r < 3; r++) { GR[r * 3] = g_gun_orig[r * 4]; GR[r * 3 + 1] = g_gun_orig[r * 4 + 1]; GR[r * 3 + 2] = g_gun_orig[r * 4 + 2]; GT[r] = g_gun_orig[r * 4 + 3]; } } }
    {   /* the upper body tilts with the view pitch about the spine joint (looking up leans back, down hunches forward) */
        extern float g_view_pitch; float *Pv = part_pos(bones, 5); if (Pv) { float P5[3] = {Pv[0], Pv[1], Pv[2]}, Qt[9];
            {   /* yaw lock: turn the upper body about the vertical so the shoulder line matches the aim's right axis (the model turns with a lag) */
                float *Lsh = part_pos(bones, 28), *Rsh = part_pos(bones, 29);
                if (Lsh && Rsh) { float sx = Rsh[0] - Lsh[0], sz = Rsh[2] - Lsh[2], sl = sqrt_(sx * sx + sz * sz), rl = sqrt_(right[0] * right[0] + right[2] * right[2]);
                    if (sl > 1e-3f && rl > 1e-3f) { sx /= sl; sz /= sl; float rx = right[0] / rl, rz = right[2] / rl; float cr = sz * rx - sx * rz, dt2 = sx * rx + sz * rz; float yaw = atan2_(cr, dt2);
                        if (yaw > -1.2f && yaw < 1.2f) { float upv2[3] = {0, 1, 0}, Qy[9]; rot_axis(Qy, upv2, yaw * amount); apply_list(bones, upper_body, sizeof upper_body, Qy, P5); }
                        { static u32 tl; u32 nw = GetTickCount(); if (nw - tl > 2000) { tl = nw; out("  [aimpose] torso yaw lock deg "); fo(yaw * 57.3f); out("\r\n"); } } } } }
            rot_axis(Qt, right, g_view_pitch * aim_torso * amount); apply_list(bones, upper_body, sizeof upper_body, Qt, P5); } }
    float *Sp = part_pos(bones, 29), *WRp = part_pos(bones, 57), *WLp = part_pos(bones, 56), *RFp = part_pos(bones, 66); if (!Sp || !WRp || !WLp || !RFp) return;
    float RP[3]; for (int k = 0; k < 3; k++) RP[k] = (WRp[k] + RFp[k]) * 0.5f;
    extern float g_cb[3]; float dv[3] = {-g_cb[0], -g_cb[1], -g_cb[2]};   /* the view direction */
    grip_load(); float bar[3] = {GR[2], GR[5], GR[8]};
    if (grip_ok) { if (gripL_p[2] - gripR_p[2] < 0) for (int k = 0; k < 3; k++) bar[k] = -bar[k]; }   /* the support hand is toward the muzzle from the trigger hand */
    else { float og[3] = {GT[0] - RP[0], GT[1] - RP[1], GT[2] - RP[2]}; if (og[0] * bar[0] + og[1] * bar[1] + og[2] * bar[2] < 0) for (int k = 0; k < 3; k++) bar[k] = -bar[k]; }
    float Qa[9], R1[9]; rot_between(Qa, bar, dv); m3mul(R1, Qa, GR);
    {   /* roll: the rifle's "down" (world down across the barrel, before) ends up as close to world down as possible */
        float dn0[3] = {0, -1, 0}, k0 = -bar[1]; for (int k = 0; k < 3; k++) dn0[k] -= k0 * bar[k]; float dn1[3]; mat3_vec(dn1, Qa, dn0);
        float wd[3] = {0, -1, 0}, k1 = -dv[1]; for (int k = 0; k < 3; k++) wd[k] -= k1 * dv[k];
        float l1 = sqrt_(dn1[0] * dn1[0] + dn1[1] * dn1[1] + dn1[2] * dn1[2]), l2 = sqrt_(wd[0] * wd[0] + wd[1] * wd[1] + wd[2] * wd[2]);
        if (l1 > 1e-3f && l2 > 1e-3f) { for (int k = 0; k < 3; k++) { dn1[k] /= l1; wd[k] /= l2; } float Qr[9], R2[9]; rot_between(Qr, dn1, wd); m3mul(R2, Qr, R1); for (int q = 0; q < 9; q++) R1[q] = R2[q]; } }
    float D[9], GRt[9]; m3t(GRt, GR); m3mul(D, R1, GRt);   /* the rifle's turn: R1 = D . GR */
    float upw[3] = {0, 1, 0}, fh[3] = {dv[0], 0, dv[2]}, fl = sqrt_(fh[0] * fh[0] + fh[2] * fh[2]); if (fl < 1e-3f) return; fh[0] /= fl; fh[2] /= fl;
    float Pt[3]; for (int k = 0; k < 3; k++) Pt[k] = Sp[k] + dv[k] * aim_fwd + upw[k] * aim_up + right[k] * aim_side;   /* the grip target */
    extern u8 *g_picked_weapon;
    grip_load();
    { static u32 tl; u32 nw = GetTickCount(); if (nw - tl > 3000) { tl = nw; out(grip_ok ? "  [aimpose] using the hip-fire grip\r\n" : "  [aimpose] no hip-fire grip yet (fallback)\r\n"); } }
    if (grip_ok && (grip_w == g_picked_weapon || grip_ok == 2)) {   /* the recorded hip-fire grip: right wrist at the target, the rifle and the left hand where they were relative to it */
        float Tt2[3], WRt2[3], WLt2[3], RHRt2[9], RHLt2[9], a[3];
        {   /* the butt against the right shoulder: the butt is aim_butt behind the right wrist along the barrel (gun-local Z, signed) */
            float sz = (gripL_p[2] - gripR_p[2]) < 0 ? -1.0f : 1.0f, butt_l[3] = {gripR_p[0], gripR_p[1], gripR_p[2] - sz * aim_butt}, bw[3]; mat3_vec(bw, R1, butt_l);
            float St[3]; for (int k = 0; k < 3; k++) St[k] = Sp[k] + dv[k] * 0.04f + upw[k] * aim_up - right[k] * 0.04f + right[k] * aim_side;   /* the shoulder pocket */
            for (int k = 0; k < 3; k++) Tt2[k] = St[k] - bw[k]; mat3_vec(a, R1, gripR_p); for (int k = 0; k < 3; k++) WRt2[k] = Tt2[k] + a[k]; }
        mat3_vec(a, R1, gripL_p); for (int k = 0; k < 3; k++) WLt2[k] = Tt2[k] + a[k]; m3mul(RHRt2, R1, gripR_r); m3mul(RHLt2, R1, gripL_r);
        float poleR[3] = {right[0] * 0.6f, -1.0f, right[2] * 0.6f}, poleL[3] = {-right[0] * 0.6f, -1.0f, -right[2] * 0.6f};
        ik_arm(bones, up_r, sizeof up_r, fa_r, sizeof fa_r, hd_r, sizeof hd_r, WRt2, RHRt2, poleR, amount);
        ik_arm(bones, up_l, sizeof up_l, fa_l, sizeof fa_l, hd_l, sizeof hd_l, WLt2, RHLt2, poleL, amount);
        float Qe[9]; { float c = (D[0] + D[4] + D[8] - 1) * 0.5f; if (c > 1) c = 1; if (c < -1) c = -1; float ax[3] = {D[7] - D[5], D[2] - D[6], D[3] - D[1]}, al = sqrt_(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
            if (al > 1e-5f) { for (int k = 0; k < 3; k++) ax[k] /= al; rot_axis(Qe, ax, atan2_(al * 0.5f, c) * amount); } else for (int i = 0; i < 9; i++) Qe[i] = (i % 4 == 0) ? 1.0f : 0.0f; }
        float NR[9]; m3mul(NR, Qe, GR); float NT[3]; for (int k = 0; k < 3; k++) NT[k] = GT[k] + (Tt2[k] - GT[k]) * amount;
        for (int q = 0; q < 9; q++) GRp[q] = NR[q]; for (int k = 0; k < 3; k++) GTp[k] = NT[k];
        for (int r = 0; r < 3; r++) { gun_last[r * 4] = NR[r * 3]; gun_last[r * 4 + 1] = NR[r * 3 + 1]; gun_last[r * 4 + 2] = NR[r * 3 + 2]; gun_last[r * 4 + 3] = NT[r]; }
        { extern u8 *g_gun_part; extern float g_gun_orig[12]; g_gun_part = gp; for (int r = 0; r < 3; r++) { g_gun_orig[r * 4] = GR[r * 3]; g_gun_orig[r * 4 + 1] = GR[r * 3 + 1]; g_gun_orig[r * 4 + 2] = GR[r * 3 + 2]; g_gun_orig[r * 4 + 3] = GT[r]; } }
        return; }
    float Tt[3], v[3] = {GT[0] - RP[0], GT[1] - RP[1], GT[2] - RP[2]}, dvv[3]; mat3_vec(dvv, D, v); for (int k = 0; k < 3; k++) Tt[k] = Pt[k] + dvv[k];
    float WRt[3], WLt[3]; { float a[3] = {WRp[0] - RP[0], WRp[1] - RP[1], WRp[2] - RP[2]}, b[3]; mat3_vec(b, D, a); for (int k = 0; k < 3; k++) WRt[k] = Pt[k] + b[k]; }
    {   /* the left hand on the foregrip: along the barrel from the grip, a little under it (the standing animation holds the rifle in the right hand only) */
        float wd[3] = {0, -1, 0}, k1 = -dv[1]; for (int k = 0; k < 3; k++) wd[k] -= k1 * dv[k]; float l2 = sqrt_(wd[0] * wd[0] + wd[1] * wd[1] + wd[2] * wd[2]); if (l2 > 1e-3f) for (int k = 0; k < 3; k++) wd[k] /= l2;
        for (int k = 0; k < 3; k++) WLt[k] = Pt[k] + dv[k] * aim_fore + wd[k] * 0.05f; (void)Tt; }
    float RHR[9], RHL[9], RHRt[9], RHLt[9]; rows3(RHR, (float *)(bones + 8 + 57 * 72)); rows3(RHL, (float *)(bones + 8 + 56 * 72)); m3mul(RHRt, D, RHR); m3mul(RHLt, D, RHL);
    { static u32 tl; u32 nw = GetTickCount(); if (nw - tl > 2000) { tl = nw; out("  [aimpose] amount "); fo(amount); out("grip target - shoulder "); fo(Pt[0] - Sp[0]); fo(Pt[1] - Sp[1]); fo(Pt[2] - Sp[2]); out("rifle move "); fo(sqrt_((Tt[0] - GT[0]) * (Tt[0] - GT[0]) + (Tt[1] - GT[1]) * (Tt[1] - GT[1]) + (Tt[2] - GT[2]) * (Tt[2] - GT[2])));
        out("R wrist move "); fo(sqrt_((WRt[0] - WRp[0]) * (WRt[0] - WRp[0]) + (WRt[1] - WRp[1]) * (WRt[1] - WRp[1]) + (WRt[2] - WRp[2]) * (WRt[2] - WRp[2]))); out("L wrist move "); fo(sqrt_((WLt[0] - WLp[0]) * (WLt[0] - WLp[0]) + (WLt[1] - WLp[1]) * (WLt[1] - WLp[1]) + (WLt[2] - WLp[2]) * (WLt[2] - WLp[2])));
        out("shoulder y - char "); { u8 *cc = (u8 *)g_player_char; float *ps = ((float *(__thiscall *)(void *))(*(u32 **)cc)[0x40 / 4])(cc) + 9; fo(Sp[1] - ps[1]); } out("\r\n"); } }
    float poleR[3] = {right[0] * 0.6f, -1.0f, right[2] * 0.6f}, poleL[3] = {-right[0] * 0.6f, -1.0f, -right[2] * 0.6f};
    ik_arm(bones, up_r, sizeof up_r, fa_r, sizeof fa_r, hd_r, sizeof hd_r, WRt, RHRt, poleR, amount);
    ik_arm(bones, up_l, sizeof up_l, fa_l, sizeof fa_l, hd_l, sizeof hd_l, WLt, RHLt, poleL, amount);
    /* the rifle (eased: the same fraction of its turn and move) */
    float Qe[9]; { float c = (D[0] + D[4] + D[8] - 1) * 0.5f; if (c > 1) c = 1; if (c < -1) c = -1; float ax[3] = {D[7] - D[5], D[2] - D[6], D[3] - D[1]}, al = sqrt_(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (al > 1e-5f) { for (int k = 0; k < 3; k++) ax[k] /= al; rot_axis(Qe, ax, atan2_(al * 0.5f, c) * amount); } else for (int i = 0; i < 9; i++) Qe[i] = (i % 4 == 0) ? 1.0f : 0.0f; }
    float NR[9]; m3mul(NR, Qe, GR); float NT[3]; for (int k = 0; k < 3; k++) NT[k] = GT[k] + (Tt[k] - GT[k]) * amount;
    for (int q = 0; q < 9; q++) GRp[q] = NR[q]; for (int k = 0; k < 3; k++) GTp[k] = NT[k];
    { static u32 tl; u32 nw = GetTickCount(); if (nw - tl > 2000) { tl = nw; float *nw57 = (float *)(bones + 8 + 57 * 72); out("  [aimpose] weapon "); extern u8 *g_picked_weapon; hex((u32)g_picked_weapon); out(" rifle from target "); fo(NT[0] - Tt[0]); fo(NT[1] - Tt[1]); fo(NT[2] - Tt[2]);
        out("rifle origin - grip target "); fo(Tt[0] - Pt[0]); fo(Tt[1] - Pt[1]); fo(Tt[2] - Pt[2]); out("R hand skin t now "); fo(nw57[3] - Pt[0]); fo(nw57[7] - Pt[1]); fo(nw57[11] - Pt[2]); out("\r\n"); } }
    for (int r = 0; r < 3; r++) { gun_last[r * 4] = NR[r * 3]; gun_last[r * 4 + 1] = NR[r * 3 + 1]; gun_last[r * 4 + 2] = NR[r * 3 + 2]; gun_last[r * 4 + 3] = NT[r]; }
    { extern u8 *g_gun_part; extern float g_gun_orig[12]; g_gun_part = gp; for (int r = 0; r < 3; r++) { g_gun_orig[r * 4] = GR[r * 3]; g_gun_orig[r * 4 + 1] = GR[r * 3 + 1]; g_gun_orig[r * 4 + 2] = GR[r * 3 + 2]; g_gun_orig[r * 4 + 3] = GT[r]; } } }
/* the player's weapons, seen in Weapon_Update (0x22EE30); the one whose part is nearest the gun hand is in his hand */
u8 *g_pweapons[4]; u8 *g_pbones;   /* the player's bone records (seen at the palette builder) */
/* Milestone 95: the Razorback replaces the MK-ASLT (type name hash 0x0C3DB625). Probe: the type's fields */
void razor_probe(u8 *w) {
    static u32 done[8], nd; u8 *t = readable(w + 0x228, 4) ? *(u8 **)(w + 0x228) : 0; if (!t || !readable(t, 0x400)) return;
    for (u32 k = 0; k < nd; k++) if (done[k] == (u32)t) return; if (nd < 8) done[nd++] = (u32)t;
    out("  [razor] type "); hex((u32)t); out(" hash at:"); for (u32 o = 0; o < 0x400; o += 4) if (*(u32 *)(t + o) == 0x0C3DB625) { out(" +"); hex(o); }
    out(" | ints 48 at:"); for (u32 o = 0; o < 0x400; o += 4) if (*(u32 *)(t + o) == 48) { out(" +"); hex(o); }
    out(" | float 48 at:"); for (u32 o = 0; o < 0x400; o += 4) if (*(float *)(t + o) == 48.0f) { out(" +"); hex(o); }
    out(" | CC "); fo(*(float *)(t + 0xCC)); out("D0 "); fo(*(float *)(t + 0xD0)); out("D8 "); fo(*(float *)(t + 0xD8)); out("DC "); dec(*(u32 *)(t + 0xDC)); out("\r\n"); }
void razor_patch(u8 *w) {   /* the MK-ASLT's type (the only 48-round magazine): 30 rounds, 625 rpm (10.42 shots/s; the second rate scaled alike) */
    static u32 off = 2; if (off == 2) off = fattr_cached("norazorback.txt") != 0xFFFFFFFF; if (off) return;
    u8 *t = readable(w + 0x228, 4) ? *(u8 **)(w + 0x228) : 0; if (!t || !readable(t, 0x100)) return;
    if (*(u32 *)(t + 0xE4) == 48 && *(float *)(t + 0xCC) > 7.9f && *(float *)(t + 0xCC) < 8.1f) { *(u32 *)(t + 0xE4) = 30; *(float *)(t + 0xCC) = 625.0f / 60.0f; *(float *)(t + 0xD0) = 6.0f * 625.0f / 480.0f;
        out("  [razor] MK-ASLT type "); hex((u32)t); out(" patched: 30 rounds, 625 rpm\r\n"); }
    if (*(u32 *)(t + 0xE4) == 30 && *(float *)(t + 0xCC) > 10.4f && *(float *)(t + 0xCC) < 10.5f && readable(w + 0x1F8, 4) && *(s32 *)(w + 0x1F8) > 30) *(s32 *)(w + 0x1F8) = 30; }
void weapon_seen(u8 *w) {
    razor_patch(w);
    u8 *shooter = 0; u32 a = readable(w + 0x1B0, 4) ? *(u32 *)(w + 0x1B0) : 0; if (a > 0x10000 && a < 0x7FFE0000u && readable((void *)(a + 8), 4)) { u32 b = *(u32 *)(a + 8);
        if (b > 0x10000 && b < 0x7FFE0000u && readable((void *)(b + 0x1D4), 4)) shooter = *(u8 **)(b + 0x1D4); }
    if (!shooter || shooter != (u8 *)g_player_char) return;
    { /* Milestone 95: when the player's MK-ASLT's clip drops (a shot) or refills (a reload), list the sounds just played */
        u8 *t = readable(w + 0x228, 4) ? *(u8 **)(w + 0x228) : 0;
        if (t && readable(t, 0x100) && readable(w + 0x1F8, 4)) {
            static u8 *lw[4]; static s32 lc[4]; static u32 nshot, nrel; s32 c = *(s32 *)(w + 0x1F8); void snd_dump(const char *, u32);
            int k = 0; while (k < 3 && lw[k] && lw[k] != w) k++;
            int mk = *(u32 *)(t + 0xE4) == 30 && *(float *)(t + 0xCC) > 10.4f && *(float *)(t + 0xCC) < 10.5f;
            if (mk && lw[k] == w && c < lc[k] && (lc[k] - c > 1 || c == 0)) { void snd_razor_reload(void); snd_razor_reload(); }   /* the clip emptied: a reload starts */
            if (lw[k] == w && c != lc[k] && (c < lc[k] ? nshot < 40 : nrel < 8)) { if (c < lc[k]) nshot++; else nrel++;
                out("  [sndid] weapon "); hex((u32)w); out(" type "); hex((u32)t); out(" mag "); dec(*(u32 *)(t + 0xE4)); out(" clip "); dec((u32)lc[k]); out(" -> "); dec((u32)c); out(mk ? " MK-ASLT\r\n" : "\r\n");
                snd_dump(c < lc[k] ? "shot" : "reload done", c < lc[k] ? 150 : 5000); }
            lw[k] = w; lc[k] = c; } }
    for (int k = 0; k < 4; k++) if (g_pweapons[k] == w) return;
    for (int k = 0; k < 4; k++) if (!g_pweapons[k]) { g_pweapons[k] = w; return; } g_pweapons[0] = w; }
static u8 *bw[4]; static float bv[4][3]; static int bn[4];
void barrel_learn(void) {   /* while the arm pose is off: the barrel (hardpoint 0's direction, negated) in the rifle part's own axes */
    for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x6C, 4) || !readable(w + 0x25C, 12)) continue; u8 *p = *(u8 **)(w + 0x6C); if (!readable(p + 0x14, 0x24)) continue;
        float *R = (float *)(p + 0x14), *d = (float *)(w + 0x25C), l = sqrt_(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l < 0.5f) continue;
        float v[3]; for (int c = 0; c < 3; c++) v[c] = -(R[c] * d[0] + R[3 + c] * d[1] + R[6 + c] * d[2]) / l;
        int s = 0; while (s < 4 && bw[s] && bw[s] != w) s++; if (s == 4) s = 0; bw[s] = w; for (int c = 0; c < 3; c++) bv[s][c] = v[c]; bn[s] = 1;
        { static u32 tlg; u32 nw = GetTickCount(); if (nw - tlg > 3000) { tlg = nw; out("  [barrel learn] weapon "); hex((u32)w); out(" dir "); fo(d[0] / l); fo(d[1] / l); fo(d[2] / l); out(" local "); fo(v[0]); fo(v[1]); fo(v[2]); out("\r\n"); } } } }
int barrel_local(u8 *w, float *out) { for (int s = 0; s < 4; s++) if (bw[s] == w && bn[s]) { for (int c = 0; c < 3; c++) out[c] = bv[s][c]; return 1; } return 0; }
u8 *g_picked_weapon;
u8 *g_held_weapon;   /* chosen while the pose is off: the weapon nearer the right wrist than the neck */
float g_held_base = 0.35f;   /* the held gun's origin from the right wrist when it was chosen (m): its own resting distance */
void held_choose(u8 *bones) {
    float *Wr = part_pos(bones, 57), *Nk = part_pos(bones, 17); if (!Wr || !Nk) return; u8 *best = 0; float bs = 1e9f;
    {   /* Milestone 97: the weapon in his hands is the one whose part moves (the animation sways it every frame; a put-away one stays
           still). The nearest-to-the-wrist guess picked a put-away gun for the RVG-50 minigun, which is carried low by its side grip. */
        static float last[4][12]; static u32 moved[4]; static u8 *lw[4]; u32 nw = GetTickCount(); u8 *mv = 0; u32 mt = 0;
        for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x6C, 4)) continue; u8 *p = *(u8 **)(w + 0x6C); if (!readable(p + 0x14, 0x30)) continue;
            float cur[12]; for (int q = 0; q < 9; q++) cur[q] = ((float *)(p + 0x14))[q]; for (int q = 0; q < 3; q++) cur[9 + q] = ((float *)(p + 0x38))[q];
            if (lw[k] != w) { lw[k] = w; moved[k] = 0; } else { int ch = 0; for (int q = 0; q < 12; q++) if (cur[q] != last[k][q]) ch = 1; if (ch) moved[k] = nw; }
            for (int q = 0; q < 12; q++) last[k][q] = cur[q];
            if (moved[k] && nw - moved[k] < 250 && moved[k] > mt) { mt = moved[k]; mv = w; } }
        int nmov = 0; for (int k = 0; k < 4; k++) if (lw[k] && moved[k] && nw - moved[k] < 250) nmov++;
        if (mv && nmov == 1) {   /* exactly one gun moving: that is the one in his hands */
            if (mv != g_held_weapon) { g_held_weapon = mv; u8 *p = *(u8 **)(mv + 0x6C); float *P = (float *)(p + 0x38);
                g_held_base = sqrt_((P[0] - Wr[0]) * (P[0] - Wr[0]) + (P[1] - Wr[1]) * (P[1] - Wr[1]) + (P[2] - Wr[2]) * (P[2] - Wr[2]));
                out("  [pose] weapon in hand (moving) "); hex((u32)mv); out(", its origin from the right wrist mm "); fo(g_held_base); out("\r\n"); }
            return; } }
    for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x6C, 4)) continue; u8 *p = *(u8 **)(w + 0x6C); if (!readable(p + 0x38, 12)) continue; float *P = (float *)(p + 0x38);
        float dw = sqrt_((P[0] - Wr[0]) * (P[0] - Wr[0]) + (P[1] - Wr[1]) * (P[1] - Wr[1]) + (P[2] - Wr[2]) * (P[2] - Wr[2])), dn = sqrt_((P[0] - Nk[0]) * (P[0] - Nk[0]) + (P[1] - Nk[1]) * (P[1] - Nk[1]) + (P[2] - Nk[2]) * (P[2] - Nk[2]));
        if (dw - dn < bs) { bs = dw - dn; best = w; } }
    if (best && best != g_held_weapon) { g_held_weapon = best; u8 *p = *(u8 **)(best + 0x6C); float *P = (float *)(p + 0x38);
        g_held_base = sqrt_((P[0] - Wr[0]) * (P[0] - Wr[0]) + (P[1] - Wr[1]) * (P[1] - Wr[1]) + (P[2] - Wr[2]) * (P[2] - Wr[2]));   /* its resting distance (a low-carried heavy gun: no sprint pose) */
        out("  [pose] weapon in hand "); hex((u32)best); out(", its origin from the right wrist mm "); fo(g_held_base); out("\r\n"); } }
u8 *pick_weapon_part(u8 *hand) {
    if (g_held_weapon && readable(g_held_weapon + 0x6C, 4)) { u8 *p = *(u8 **)(g_held_weapon + 0x6C); if (readable(p + 0x38, 12)) { g_picked_weapon = g_held_weapon; return p; } }   /* the weapon in his hand */
    if (!readable(hand + 0x38, 12)) return 0; float *H = (float *)(hand + 0x38); u8 *best = 0; float bd = 1.0f;   /* within 1 m */
    { static u32 tl; u32 now = GetTickCount(); if (now - tl > 5000) { tl = now; out("  [weapons]"); for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x6C, 4)) continue; u8 *p = *(u8 **)(w + 0x6C); if (!readable(p + 0x38, 12)) continue; float *P = (float *)(p + 0x38);
        out(" "); hex((u32)w); out(" dist mm "); fo(sqrt_((P[0] - H[0]) * (P[0] - H[0]) + (P[1] - H[1]) * (P[1] - H[1]) + (P[2] - H[2]) * (P[2] - H[2]))); } out("\r\n"); } }
    for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x6C, 4)) continue; u8 *p = *(u8 **)(w + 0x6C); if (!readable(p + 0x38, 12)) continue;
        float *P = (float *)(p + 0x38), d = (P[0] - H[0]) * (P[0] - H[0]) + (P[1] - H[1]) * (P[1] - H[1]) + (P[2] - H[2]) * (P[2] - H[2]); if (d < bd) { bd = d; best = p; g_picked_weapon = w; } }
    return best; }
__attribute__((naked)) static void wup_stub(void) { __asm__ volatile(
    "pushal\n\t pushl %ecx\n\t call _weapon_seen\n\t addl $4, %esp\n\t popal\n\t"
    "pushl %esi\n\t movl %ecx, %esi\n\t movl 0x228(%esi), %eax\n\t pushl $0x22EE39\n\t ret"); }
static void pose_chain(u8 *bones, int left, float ang, const float *axis) {
    if (!left) pose_list(bones, fore_r, sizeof fore_r, ang, axis, 1); else pose_list(bones, arm_l, sizeof arm_l, ang, axis, 0); }
float g_body_gap;   /* aim yaw - the body's heading (radians), from the body turn below */
void pose_apply(u8 *bones) {
    {   /* probe: which threads build the player's palette, and which runs the player's input (mouse_apply) */
        extern u32 g_input_tid; static u32 tids[8], cnt[8], tl; u32 me = GetCurrentThreadId(); int k = 0; while (k < 8 && tids[k] && tids[k] != me) k++; if (k < 8) { tids[k] = me; cnt[k]++; }
        u32 nw = GetTickCount(); static u32 nth; if (nth < 3 && nw - tl > 3000) { tl = nw; nth++; out("  [threads] palette builders:"); for (int j = 0; j < 8 && tids[j]; j++) { out(" "); dec(tids[j]); out("x"); dec(cnt[j]); cnt[j] = 0; } out(" input thread "); dec(g_input_tid); out("\r\n"); } }
    static u32 tl, on; static float ar, al; u32 t = GetTickCount();
    if (t - tl > 1000) { tl = t; on = 0; HANDLE f = CreateFileA("posetest.txt", 0x80000000, 1, 0, 3, 0x80, 0);
        if (f != (HANDLE)-1) { char s[40] = {0}; DWORD n = 0; ReadFile(f, s, 39, &n, 0); CloseHandle(f); on = 1; float v[2] = {0, 0}; int k = 0, neg = 0, any = 0; float x = 0;
            for (DWORD i = 0; i <= n && k < 2; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) neg = 1; else if (ch >= 0x30 && ch <= 0x39) { x = x * 10 + (ch - 0x30); any = 1; } else if (any) { v[k++] = neg ? -x : x; x = 0; any = 0; neg = 0; } }
            ar = v[0] * 0.0174533f; al = v[1] * 0.0174533f; } }
    if (!g_player_char) return; u8 *c = (u8 *)g_player_char; u32 lh = *(u32 *)(c + 0x3F8); if (lh <= 0x10000 || !readable((void *)(bones + 4), 4)) return;
    if (*(u32 *)(bones + 4) != lh - 56 * 0x80) return;   /* bone 0's part = the player's part base */
    { static u32 seen; if (!seen) { seen = 1; out("  [pose] player skeleton drawn\r\n"); } extern u8 *g_pbones; g_pbones = bones; }
    {   /* Milestone 95: the lower body faces the aim. The game's body heading (character transform = bone 0) trails the aim while
           standing (it turns only by turn-in-place motions once the gap is large); the upper body already follows the aim. Every
           bone that is not upper body is turned about the vertical through the hips (bone 1) by aim yaw - bone 0 yaw.
           Once per frame per bone (pose_last). bodylag.txt = the game's own. */
        static u32 off = 2, tl; u32 nw = GetTickCount(); if (nw - tl > 1000) { tl = nw; off = fattr_cached("bodylag.txt") != 0xFFFFFFFF; }
        extern u32 g_ctl_tick; float *A = (float *)(c + 0x5BC), *m0 = (float *)(bones + 8);
        extern float g_pose_t;   /* not during the sprint pose (it pivots on the part objects; running, the body already follows the aim) */
        if (!off && g_mouse_on && nw - g_ctl_tick < 300 && g_pose_t < 0.001f) {
            float *W = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c); (void)m0;   /* the body's heading: the character transform (bone 0 may still hold our last output) */
            float d = atan2_(A[6], A[0]) - atan2_(W[6], W[0]); while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; g_body_gap = d;
            float *Pv = (float *)(bones + 8 + 72); float S[3] = {Pv[3], Pv[7], Pv[11]};   /* hips: bone 1's translation */
            /* the whole skeleton (the upper body follows the body heading too, not the aim), and the player's weapons' parts. Each one
               remembers the angle it holds: rewritten by the game -> turned by d; still our output -> turned by d - what it holds. */
            static float bang[90], wang[4]; static u32 nfresh, nheld, nwf, nwh, tlg; extern u8 *g_pweapons[4]; static float wl[4][12];
            #define TURN_BY(ang_, o_, m_) { float a_ = (ang_), cs_, sn_; __asm__("fcos" : "=t"(cs_) : "0"(a_)); __asm__("fsin" : "=t"(sn_) : "0"(a_)); float Q_[9] = {cs_, 0, -sn_, 0, 1, 0, sn_, 0, cs_}; xform_about(o_, m_, Q_, S); }
            for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x6C, 4)) continue; u8 *p = *(u8 **)(w + 0x6C); if (!readable(p + 0x14, 0x30)) continue;
                float *R = (float *)(p + 0x14), *T = (float *)(p + 0x38), m[12], o[12]; for (int r = 0; r < 3; r++) { m[r * 4] = R[r * 3]; m[r * 4 + 1] = R[r * 3 + 1]; m[r * 4 + 2] = R[r * 3 + 2]; m[r * 4 + 3] = T[r]; }
                int same = 1; for (int q = 0; q < 12; q++) if (m[q] != wl[k][q]) { same = 0; break; }
                float a = same ? d - wang[k] : d; if (same) nwh++; else nwf++; if (a < 1e-5f && a > -1e-5f && same) continue;
                TURN_BY(a, o, m); for (int r = 0; r < 3; r++) { R[r * 3] = o[r * 4]; R[r * 3 + 1] = o[r * 4 + 1]; R[r * 3 + 2] = o[r * 4 + 2]; T[r] = o[r * 4 + 3]; } for (int q = 0; q < 12; q++) wl[k][q] = o[q]; wang[k] = d; }
            for (int b = 0; b < 90; b++) { float *m = (float *)(bones + 8 + b * 72), *L = pose_last[b];
                int same = 1; for (int q = 0; q < 12; q++) if (m[q] != L[q]) { same = 0; break; }
                float a = same ? d - bang[b] : d; if (same) nheld++; else nfresh++; if (a < 1e-5f && a > -1e-5f) { bang[b] = d; continue; }
                float o[12]; TURN_BY(a, o, m); for (int q = 0; q < 12; q++) { m[q] = o[q]; L[q] = o[q]; } bang[b] = d; }
            #undef TURN_BY
            if (nw - tlg > 2000) { tlg = nw; out("  [bodyturn] gap mrad "); fo(d); out("bones fresh/held "); dec(nfresh); out("/"); dec(nheld); out(" weapons fresh/held "); dec(nwf); out("/"); dec(nwh); out("\r\n"); nfresh = nheld = nwf = nwh = 0; } }
        else { g_body_gap = 0; static u32 tlo; if (nw - tlo > 2000) { tlo = nw; out("  [bodyturn] off: mouse "); dec(g_mouse_on); out(" input age "); dec(nw - g_ctl_tick); out(" sprint pose x1000 "); fo(g_pose_t); out("\r\n"); } } }
    {   /* Milestone 95 probe: which transform trails the aim while turning. Yaw (mrad) of the aim's right axis, bone 0 and 1 skin
           matrices, bone 0's part object, the character transform (vfunc +0x40); logged when aim and bone 0 differ > 3 degrees */
        static u32 nl, tl; u32 nw = GetTickCount(); float *A = (float *)(c + 0x5BC), *m0 = (float *)(bones + 8), *m1 = (float *)(bones + 8 + 72);
        float ya = atan2_(A[6], A[0]), y0 = atan2_(m0[8], m0[0]), d = ya - y0; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f;
        if (nl < 150 && (d > 0.052f || d < -0.052f) && nw - tl > 150) { tl = nw; nl++; u8 *p0 = *(u8 **)(bones + 4); float *wx = ((float *(__thiscall *)(void *))(*(u32 **)c)[0x40 / 4])(c);
            out("  [turnlag] aim "); fo(ya); out("bone0 "); fo(y0); out("bone1 "); fo(atan2_(m1[8], m1[0]));
            if (readable(p0 + 0x14, 0x24)) { float *R = (float *)(p0 + 0x14); out("part0 rows "); fo(atan2_(R[6], R[0])); out("cols "); fo(atan2_(R[2], R[0])); }
            out("xform "); fo(atan2_(wx[6], wx[0])); fo(atan2_(wx[2], wx[0])); out("+4A0 "); fo(*(float *)(c + 0x4A0)); out("+434 "); fo(*(float *)(c + 0x434)); out("+5E8 "); fo(*(float *)(c + 0x5E8)); fo(*(float *)(c + 0x5F0));
            out("| char fields near bone0:"); for (u32 o = 0; o < 0x1000; o += 4) { float *f = (float *)(c + o); if (o == 0x5BC) continue;
                if ((f[0] - m0[0]) * (f[0] - m0[0]) < 1e-6f && (f[1] - m0[1]) * (f[1] - m0[1]) < 1e-6f && (f[2] - m0[2]) * (f[2] - m0[2]) < 1e-6f) { out(" +"); hex(o); out("r"); }
                if ((f[0] - m0[0]) * (f[0] - m0[0]) < 1e-6f && (f[3] - m0[4]) * (f[3] - m0[4]) < 1e-6f && (f[6] - m0[8]) * (f[6] - m0[8]) < 1e-6f) { out(" +"); hex(o); out("c"); } }
            out("\r\n"); } }
    {   /* weapon probe (gunprobe.txt): the character field holding his current weapon, and the weapon's part objects */
        static u32 on = 2, done, t0; if (on == 2) on = fattr_cached("gunprobe.txt") != 0xFFFFFFFF;
        extern u8 *g_pweapon; if (on && done < 2 && g_mouse_on && g_pweapon) { u32 t = GetTickCount(); if (!t0) t0 = t; if (t - t0 > 4000 + done * 2000) { done++;
            u8 *hand = *(u8 **)(c + 0x3FC); float *hp = (float *)(hand + 0x38); out("  [gun] gun hand part "); hex((u32)hand); out(" at "); fo(hp[0]); fo(hp[1]); fo(hp[2]); out("\r\n");
            for (u32 o = 0; o < 4; o += 4) { u32 w = (u32)g_pweapon; for (u32 f = 0; f < 0x1000; f += 4) if (*(u32 *)(c + f) == w) { out("   char+"); hex(f); out(" holds it"); }
                out("   weapon "); hex(w); out("\r\n");
                int part_like(u8 *, float *);
                for (u32 q = 0; q < 0x800; q += 4) { u8 *p = *(u8 **)(w + q); if (part_like(p, hp)) { float *pp = (float *)(p + 0x38); out("     w+"); hex(q); out(" part "); hex((u32)p); out(" rel hand "); fo(pp[0] - hp[0]); fo(pp[1] - hp[1]); fo(pp[2] - hp[2]); out("\r\n"); } }
                float *wx = ((float *(__thiscall *)(void *))(*(u32 **)w)[0x40 / 4])((void *)w); out("     weapon vfunc40 xform pos rel hand "); fo(wx[9] - hp[0]); fo(wx[10] - hp[1]); fo(wx[11] - hp[2]); out(" ptr "); hex((u32)wx); out("\r\n"); } } } }
    { void joint_check(u8 *); extern float g_pose_t; if (g_pose_t < 0.001f) joint_check(bones); }   /* only on unposed bones */
    {   static u32 fp, tlf; u32 nf = GetTickCount(); if (nf - tlf > 1000) { tlf = nf; fp = fattr_cached("forcepose.txt") != 0xFFFFFFFF; } if (fp) { extern float g_pose_t; g_pose_t = 1.0f; } }   /* test: hold the sprint pose */
    int noctl = 0;   /* no player input (cutscene, mouse not captured): no poses, but the Razorback's left hand still holds the gun (Milestone 95) */
    {   extern u32 g_ctl_tick; if (GetTickCount() - g_ctl_tick > 300 && fattr_cached("forcepose.txt") == 0xFFFFFFFF) { extern float g_pose_t, g_aim_t; g_pose_t = 0; noctl = 1; } }
    {   extern float g_pose_t; extern u8 *g_gun_part; extern float g_gun_orig[12];
        extern float g_aim_t; static u32 noaimpose2 = 2; if (noaimpose2 == 2) noaimpose2 = fattr_cached("aimpose_on.txt") == 0xFFFFFFFF;   /* the aim pose is off unless aimpose_on.txt exists (user: more trouble than it was worth) */
        if (g_pose_t < 0.001f && (g_aim_t < 0.001f || noaimpose2) && g_gun_part && readable(g_gun_part + 0x14, 0x30)) {   /* both poses off */   /* the pose has ended: if the rifle part still holds our last output, put the animation's placement back */
            float *R = (float *)(g_gun_part + 0x14), *T = (float *)(g_gun_part + 0x38); int same = 1;
            for (int r = 0; r < 3 && same; r++) { if (R[r * 3] != gun_last[r * 4] || R[r * 3 + 1] != gun_last[r * 4 + 1] || R[r * 3 + 2] != gun_last[r * 4 + 2] || T[r] != gun_last[r * 4 + 3]) same = 0; }
            static u32 nst; if (same) { for (int r = 0; r < 3; r++) { R[r * 3] = g_gun_orig[r * 4]; R[r * 3 + 1] = g_gun_orig[r * 4 + 1]; R[r * 3 + 2] = g_gun_orig[r * 4 + 2]; T[r] = g_gun_orig[r * 4 + 3]; } if (nst++ < 5) out("  [pose] rifle part was still posed after the sprint: restored\r\n"); }
            g_gun_part = 0; }
        if (g_pose_t < 0.001f && (g_aim_t < 0.001f || noaimpose2)) { void held_choose(u8 *); held_choose(bones);
            {   /* Milestone 95: holding the Razorback (the MK-ASLT's type), the left hand slides back along the gun toward the right hand.
                   razorhand.txt "<back mm> <down mm>" (read every second). Applied once per frame (pose_last check). */
                static float hb = 484, hd2 = 51, hs = -50; static u32 th; u32 nh = GetTickCount(); if (nh - th > 1000) { th = nh; hb = 484; hd2 = 51; hs = -50;   /* at ease, tuned with the user: 48.4 cm back, 5.1 cm down, 5 cm to his left */
                    HANDLE f = CreateFileA("razorhand.txt", 0x80000000, 1, 0, 3, 0x80, 0);   /* "<back mm> <down mm> <right mm>" */
                    if (f != (HANDLE)-1) { char s[48] = {0}; DWORD n = 0; ReadFile(f, s, 47, &n, 0); CloseHandle(f); float v[3] = {hb, hd2, hs}, x = 0; int k = 0, neg = 0, any = 0;
                        for (DWORD i = 0; i <= n && k < 3; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) neg = 1; else if (ch >= 0x30 && ch <= 0x39) { x = x * 10 + (ch - 0x30); any = 1; } else if (any) { v[k++] = neg ? -x : x; x = 0; any = 0; neg = 0; } }
                        hb = v[0]; hd2 = v[1]; hs = v[2]; } }
                /* the Razorback in his hands: of the player's weapons, the MK-ASLT type whose rifle part is nearest the right wrist
                   (within 60 cm). g_held_weapon flipped between guns, and the hand's target jumped ~30 cm between frames. */
                extern u8 *g_pweapons[4]; u8 *hw = 0; int rz = 0;
                { float *Wr = part_pos(bones, 57), best = 0.36f;   /* the held gun's origin is ~34 cm from the wrist; nearest within 60 cm */ if (Wr) for (int k = 0; k < 4; k++) { u8 *w = g_pweapons[k]; if (!w || !readable(w + 0x228, 4) || !readable(w + 0x6C, 4)) continue;
                    u8 *ty = *(u8 **)(w + 0x228), *p = *(u8 **)(w + 0x6C); if (!ty || !readable(ty, 0x100) || !(*(u32 *)(ty + 0xE4) == 30 && *(float *)(ty + 0xCC) > 10.4f && *(float *)(ty + 0xCC) < 10.5f)) continue;
                    if (!readable(p + 0x38, 12)) continue; float *P = (float *)(p + 0x38), d2 = (P[0] - Wr[0]) * (P[0] - Wr[0]) + (P[1] - Wr[1]) * (P[1] - Wr[1]) + (P[2] - Wr[2]) * (P[2] - Wr[2]);
                    if (d2 < best) { best = d2; hw = w; rz = 1; } } }
                /* weapon swaps: the hand lets go (from the switch press for 1.5 s, or while the gun is off its usual ~34 cm from the
                   wrist) and eases back on afterwards (in ~0.3 s, out ~0.1 s) */
                static float lh_t; static u32 lh_tl; { extern u32 g_switch_tick; u32 nq = GetTickCount(); float dtq = lh_tl ? (nq - lh_tl) * 0.001f : 0; lh_tl = nq; if (dtq > 0.1f) dtq = 0.1f;
                    float *Wr = part_pos(bones, 57); int near_ = 0; if (rz && Wr) { u8 *p = *(u8 **)(hw + 0x6C); float *P = (float *)(p + 0x38); near_ = (P[0] - Wr[0]) * (P[0] - Wr[0]) + (P[1] - Wr[1]) * (P[1] - Wr[1]) + (P[2] - Wr[2]) * (P[2] - Wr[2]) < 0.45f * 0.45f; }
                    int want = rz && near_ && !(g_switch_tick && nq - g_switch_tick < 1500); float rt = want ? 3.5f : 10.0f, a = dtq * rt; if (a > 1) a = 1;
                    lh_t += ((want ? 1.0f : 0.0f) - lh_t) * a; if (!want && lh_t < 0.02f) lh_t = 0;
                    {   /* probe: the hold while aiming or firing */
                        extern u32 g_aiming; int view_key(int); static u32 tlq, nq2; int fire = view_key(0x01);
                        if ((g_aiming || fire) && nq2 < 10 && nq - tlq > 400) { tlq = nq; nq2++; float dd = 0; if (rz && Wr) { u8 *p = *(u8 **)(hw + 0x6C); float *P = (float *)(p + 0x38); dd = sqrt_((P[0] - Wr[0]) * (P[0] - Wr[0]) + (P[1] - Wr[1]) * (P[1] - Wr[1]) + (P[2] - Wr[2]) * (P[2] - Wr[2])); }
                            out("  [lhold] aim "); dec(g_aiming); out(" fire "); dec((u32)fire); out(" razor "); dec((u32)rz); out(" gun-wrist mm "); dec((u32)(dd * 1000)); out(" hold x1000 "); dec((u32)(lh_t * 1000)); out("\r\n"); } }
                    if (lh_t <= 0) rz = 0; }
                float *m56 = (float *)(bones + 8 + 56 * 72); static float ikl[12]; int done = 1; for (int q = 0; q < 12; q++) if (m56[q] != ikl[q]) { done = 0; break; }
                g_ik_skin = fattr_cached("razorhand_parts.txt") == 0xFFFFFFFF;   /* A/B: the old part-object joints */
                float WLv[3], WRv[3], SLv[3], ELv[3], *WL = 0, *WR = 0, *SL = 0, *EL = 0, *t;
                if ((t = part_pos(bones, 56))) { for (int k = 0; k < 3; k++) WLv[k] = t[k]; WL = WLv; } if ((t = part_pos(bones, 57))) { for (int k = 0; k < 3; k++) WRv[k] = t[k]; WR = WRv; }
                if ((t = part_pos(bones, 28))) { for (int k = 0; k < 3; k++) SLv[k] = t[k]; SL = SLv; } if ((t = part_pos(bones, 51))) { for (int k = 0; k < 3; k++) ELv[k] = t[k]; EL = ELv; }
                if (rz && !done && (hb != 0 || hd2 != 0) && WL && WR && SL && EL && fattr_cached("norazorback.txt") == 0xFFFFFFFF) {
                    float d[3] = {WR[0] - WL[0], WR[1] - WL[1], WR[2] - WL[2]}, l = sqrt_(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    /* the hand is held at a fixed place on the gun: captured once in the rifle part's frame (+0x14 rotation rows, +0x38
                       position) while he stands calmly, then followed every frame, so the turn animations no longer move it */
                    u8 *gp = readable(hw + 0x6C, 4) ? *(u8 **)(hw + 0x6C) : 0; if (gp && !readable(gp + 0x14, 0x30)) gp = 0;
                    /* captured once (calm, at ease) in the rifle part's frame: the animation's wrist B and the three offset directions
                       (back along the gun = toward the right wrist, down, the character's right); then per frame
                       spot = B + back x b + down x d + right x r, with (b, d, r) blended between at ease (razorhand.txt) and at the ready
                       (razorhand_ready.txt) by how closely the barrel points along the aim (ready: within ~10 degrees; at ease: beyond ~25) */
                    static u8 *cw; static float Bp[3], Vb[3], Vd[3], Vr[3], Vz[3], Lr[9]; static u32 hold0; extern float g_body_gap; u32 nn = GetTickCount();
                    static float rb = 408, rd = 51, rs = -177; static u32 trd; if (nn - trd > 1000) { trd = nn; rb = 408; rd = 51; rs = -177; HANDLE f = CreateFileA("razorhand_ready.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                        if (f != (HANDLE)-1) { char s[48] = {0}; DWORD n = 0; ReadFile(f, s, 47, &n, 0); CloseHandle(f); float v[3] = {rb, rd, rs}, x = 0; int k = 0, neg = 0, any = 0;
                            for (DWORD i = 0; i <= n && k < 3; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) neg = 1; else if (ch >= 0x30 && ch <= 0x39) { x = x * 10 + (ch - 0x30); any = 1; } else if (any) { v[k++] = neg ? -x : x; x = 0; any = 0; neg = 0; } }
                            rb = v[0]; rd = v[1]; rs = v[2]; } }
                    if (cw != hw) { cw = 0; if (!hold0) hold0 = nn; }
                    if (l > 0.05f && l < 1.0f && gp) { float T[3], RH[9], pole[3], *GR = (float *)(gp + 0x14), *GT = (float *)(gp + 0x38);
                        int at_ease; { float *Am = (float *)(c + 0x5BC), cz0 = GR[2] * -Am[2] + GR[5] * -Am[5] + GR[8] * -Am[8]; if (cz0 < 0) cz0 = -cz0; at_ease = cz0 < 0.9f || GT[1] - SL[1] < -0.31f; }
                        if (!cw && !noctl && at_ease && nn - hold0 > 500 && g_body_gap < 0.03f && g_body_gap > -0.03f) {   /* captured only in play (an intro/cutscene pose gave a bad base) and at ease (a capture at the ready shifted every spot) */
                            float GRt[9], RHw[9], dl[3], w3[3]; m3t(GRt, GR);
                            for (int k = 0; k < 3; k++) dl[k] = WL[k] - GT[k]; mat3_vec(Bp, GRt, dl);
                            for (int k = 0; k < 3; k++) w3[k] = d[k] / l; mat3_vec(Vb, GRt, w3);
                            Vz[0] = 0; Vz[1] = 0; Vz[2] = Vb[2] < 0 ? -1.0f : 1.0f;   /* at ease, "back" runs along the barrel (local Z) toward the stock side: at ease the wrists sit side by side */
                            w3[0] = 0; w3[1] = -1; w3[2] = 0; mat3_vec(Vd, GRt, w3);
                            { float *Am = (float *)(c + 0x5BC), rx = Am[0], rzz = Am[6], rl = sqrt_(rx * rx + rzz * rzz); w3[0] = rl > 1e-3f ? rx / rl : 1; w3[1] = 0; w3[2] = rl > 1e-3f ? rzz / rl : 0; mat3_vec(Vr, GRt, w3); }
                            rows3(RHw, m56); m3mul(Lr, GRt, RHw);
                            cw = hw; hold0 = 0; out("  [razor] left hand base captured on the gun at "); fo(Bp[0] * 1000); fo(Bp[1] * 1000); fo(Bp[2] * 1000); out("\r\n"); }
                        if (!cw) goto lh_end;
                        float wr; {   /* readiness: |cos| between the barrel (local Z) and the aim's forward */
                            float *Am = (float *)(c + 0x5BC), z3[3] = {GR[2], GR[5], GR[8]}, cz = z3[0] * -Am[2] + z3[1] * -Am[5] + z3[2] * -Am[8]; if (cz < 0) cz = -cz;
                            /* hysteresis: ready once within ~14 degrees; at ease only after more than ~32 degrees for 0.25 s (recoil
                               shook the barrel between 0.92 and 1.0 and the hand slid between the two spots) */
                            /* and the gun raised: its origin 21-27 cm below the left shoulder at the ready, 32-48 cm at ease (the barrel alone
                               also matched the aim at ease when the camera was the game's own) */
                            float gh = GT[1] - SL[1];
                            static int rdy; static u32 tlow; if (cz > 0.97f && gh > -0.29f) { rdy = 1; tlow = 0; } else if (cz < 0.85f || gh < -0.31f) { if (!tlow) tlow = nn; if (nn - tlow > 250) rdy = 0; } else tlow = 0;
                            wr = rdy ? 1.0f : 0.0f;
                            static float sw; static u32 tsw; float dts = tsw ? (nn - tsw) * 0.001f : 0; tsw = nn; if (dts > 0.1f) dts = 0.1f; float al = dts * 6.0f; if (al > 1) al = 1; sw += (wr - sw) * al; wr = sw;
                            static u32 tlr, nlr; if (nlr < 400 && nn - tlr > 250) { tlr = nn; nlr++; extern u32 g_aiming; int view_key(int);
                                out("  [ready] barrel.aim x1000 "); dec((u32)(cz * 1000)); out(" ready x1000 "); dec((u32)(wr * 1000)); out(" barrel y x1000 "); fo(z3[1]); out("aim fwd y x1000 "); fo(-Am[5]);
                                out("gun above L shoulder mm "); fo(GT[1] - SL[1]); out("aim "); dec(g_aiming); out(" fire "); dec((u32)view_key(0x01)); out("\r\n"); } }
                        /* every frame from the animation's own left wrist in the gun's frame (no one-time anchor: one recorded in the other
                           stance shifted every spot), then the stance's offsets: back along the barrel toward the right hand, down, right */
                        {   float GRt[9], dl[3], Wg[3], w3[3], Dg[3], Rg[3], Zg[3] = {0, 0, 1}; m3t(GRt, GR);
                            for (int k = 0; k < 3; k++) dl[k] = WL[k] - GT[k]; mat3_vec(Wg, GRt, dl);
                            for (int k = 0; k < 3; k++) dl[k] = WR[k] - WL[k]; mat3_vec(w3, GRt, dl); if (w3[2] < 0) Zg[2] = -1;
                            w3[0] = 0; w3[1] = -1; w3[2] = 0; mat3_vec(Dg, GRt, w3);
                            { float *Am = (float *)(c + 0x5BC), rx = Am[0], rzz = Am[6], rl = sqrt_(rx * rx + rzz * rzz); w3[0] = rl > 1e-3f ? rx / rl : 1; w3[1] = 0; w3[2] = rl > 1e-3f ? rzz / rl : 0; mat3_vec(Rg, GRt, w3); }
                            float Lp[3]; for (int k = 0; k < 3; k++) { float le = Wg[k] + (Zg[k] * hb + Dg[k] * hd2 + Rg[k] * hs) * 0.001f, lr = Wg[k] + (Zg[k] * rb + Dg[k] * rd + Rg[k] * rs) * 0.001f; Lp[k] = le + (lr - le) * wr; }
                            mat3_vec(T, GR, Lp); for (int k = 0; k < 3; k++) T[k] += GT[k]; }
                        rows3(RH, m56);   /* the animation's own hand rotation */
                        (void)Bp; (void)Vb; (void)Vd; (void)Vr; (void)Vz; (void)Lr;
                        for (int k = 0; k < 3; k++) pole[k] = EL[k] - SL[k];
                        ik_arm(bones, up_l, sizeof up_l, fa_l, sizeof fa_l, hd_l, sizeof hd_l, T, RH, pole, lh_t);
                        if (lh_t > 0.99f) for (int it = 0; it < 3; it++) ik_arm(bones, up_l, sizeof up_l, fa_l, sizeof fa_l, hd_l, sizeof hd_l, T, RH, pole, 1.0f);   /* the first solve lands 1-10 cm off: repeat from where it ended */
                        for (int q = 0; q < 12; q++) ikl[q] = m56[q];
                        {   /* probe: the hand's distance from its target after the IK, the target's reach, and how far the gun moved
                               relative to the left shoulder since the last frame (mm, largest per 2 s) */
                            static float mxe, mxg, mxr, lg[3]; static u32 tlh; float *Wa = part_pos(bones, 56), e = 0, g = 0, rr = 0;
                            if (Wa) { float q0 = Wa[0] - T[0], q1 = Wa[1] - T[1], q2 = Wa[2] - T[2]; e = sqrt_(q0 * q0 + q1 * q1 + q2 * q2); }
                            { float r0 = GT[0] - SL[0], r1 = GT[1] - SL[1], r2 = GT[2] - SL[2]; float g0 = r0 - lg[0], g1 = r1 - lg[1], g2 = r2 - lg[2]; g = sqrt_(g0 * g0 + g1 * g1 + g2 * g2); lg[0] = r0; lg[1] = r1; lg[2] = r2;
                              float t0 = T[0] - SL[0], t1 = T[1] - SL[1], t2 = T[2] - SL[2]; rr = sqrt_(t0 * t0 + t1 * t1 + t2 * t2); }
                            if (e > mxe) mxe = e; if (g > mxg && g < 0.5f) mxg = g; if (rr > mxr) mxr = rr;
                            static u32 lhlog = 2; if (lhlog == 2) lhlog = fattr_cached("lefthandlog.txt") != 0xFFFFFFFF;
                            if (lhlog && nn - tlh > 2000) { tlh = nn; out("  [lefthand] off target mm "); dec((u32)(mxe * 1000)); out(" gun move vs shoulder per frame mm "); dec((u32)(mxg * 1000)); out(" farthest target from shoulder mm "); dec((u32)(mxr * 1000)); out("\r\n"); mxe = mxg = mxr = 0; } } } }
                lh_end: g_ik_skin = 0; }
            if (!noaimpose2) { void grip_try(u8 *); grip_try(bones); } } }   /* only while both poses are off: the two-handed grip whenever both hands are on the rifle */
    if (noctl) return;
    {   extern float g_pose_t; static u32 nopose = 2; if (nopose == 2) nopose = fattr_cached("nosprintpose.txt") != 0xFFFFFFFF;
        extern float g_aim_t; static u32 noaimpose = 2; if (noaimpose == 2) noaimpose = fattr_cached("aimpose_on.txt") == 0xFFFFFFFF;
        if (!on && !noaimpose && g_aim_t > 0.001f && g_pose_t < 0.001f) {   /* Milestone 93: shouldered aim (pose_aim) */
            static u32 tt; u32 now = GetTickCount(); if (now - tt > 1000) { tt = now; HANDLE f = CreateFileA("aimpose.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                if (f != (HANDLE)-1) { char s[48] = {0}; DWORD n = 0; ReadFile(f, s, 47, &n, 0); CloseHandle(f); float v[3] = {aim_butt * 1000, aim_up * 1000, aim_side * 1000}, x = 0; int k = 0, neg = 0, any = 0;
                    for (DWORD i = 0; i <= n && k < 3; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) neg = 1; else if (ch >= 0x30 && ch <= 0x39) { x = x * 10 + (ch - 0x30); any = 1; } else if (any) { v[k++] = neg ? -x : x; x = 0; any = 0; neg = 0; } }
                    aim_butt = v[0] * 0.001f; aim_up = v[1] * 0.001f; aim_side = v[2] * 0.001f; } }   /* aimpose.txt: butt behind the wrist, up, side (mm) */
            float *A = (float *)(c + 0x5BC), ax[3] = {A[0], A[3], A[6]}; pose_aim(bones, ax, g_aim_t); return; }
        {   /* weapon switch: the held gun leaves the right hand (its origin is normally ~34 cm from the wrist); no pose for 0.6 s
               (the switch animation moved the guns and the left arm while the pose was grabbing them) */
            static u32 sw_until; u32 nw = GetTickCount(); extern u8 *g_held_weapon; float *Wr = part_pos(bones, 57);
            if (g_held_weapon && Wr && readable(g_held_weapon + 0x6C, 4)) { u8 *p = *(u8 **)(g_held_weapon + 0x6C);
                if (readable(p + 0x38, 12)) { float *P = (float *)(p + 0x38), Pf[3] = {P[0], P[1], P[2]};
                    { extern float g_gun_orig[12]; int same = 1; for (int r = 0; r < 3 && same; r++) if (P[r] != gun_last[r * 4 + 3]) same = 0; if (same) { Pf[0] = g_gun_orig[3]; Pf[1] = g_gun_orig[7]; Pf[2] = g_gun_orig[11]; } }
                    float d2 = (Pf[0] - Wr[0]) * (Pf[0] - Wr[0]) + (Pf[1] - Wr[1]) * (Pf[1] - Wr[1]) + (Pf[2] - Wr[2]) * (Pf[2] - Wr[2]);
                    extern float g_held_base; float lim = g_held_base + 0.30f; if (lim < 0.55f) lim = 0.55f;   /* relative to this gun's own resting distance (the minigun is carried low) */
                    if (d2 > lim * lim) { if (nw >= sw_until) out("  [pose] weapon switch: pose off\r\n"); sw_until = nw + 600; } } }
            {   /* the switch: only the gun arm lets go (its own weight, eased), so the game's reach animation plays while the left arm keeps swinging */
                extern u32 g_switch_tick; extern float g_gunarm_t; int sw = (g_switch_tick && nw - g_switch_tick < 1500) || nw < sw_until;
                static s64 qf, lq; s64 qn; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qn); float dts = lq ? (float)(qn - lq) / (float)qf : 0; lq = qn; if (dts > 0.1f) dts = 0.1f;
                float tg = sw ? 0.0f : 1.0f, rate = sw ? 12.0f : 5.0f, a2 = dts * rate; if (a2 > 1) a2 = 1; g_gunarm_t += (tg - g_gunarm_t) * a2; if (sw && g_gunarm_t < 0.02f) g_gunarm_t = 0;
                if (g_gunarm_t < 0.02f) { void held_choose(u8 *); held_choose(bones); }   /* the gun arm is not posed: pick up the new weapon in his hand */ } }
        if (!on && !nopose && g_pose_t > 0.001f) { float *A = (float *)(c + 0x5BC), ax[3] = {A[0], A[3], A[6]};   /* Milestone 93: sprint pose */
            {   /* the body's own right axis (shoulder line, horizontal): the model turns a little behind the aim, so poses built on the
                   aim's axes lagged or led the torso while turning */
                /* from the root bone (Bip01, 1): its world rotation's X column is the model's right (the shoulder line twisted with every stride) */
                float *m1 = (float *)(bones + 8 + 1 * 72); float sx = m1[0], sz = m1[8], sl = sqrt_(sx * sx + sz * sz);
                float al2 = sqrt_(ax[0] * ax[0] + ax[2] * ax[2]); float dax = al2 > 1e-3f && sl > 1e-3f ? (sx * ax[0] + sz * ax[2]) / (sl * al2) : 0;
                if (dax > -0.707f && dax < 0.707f) sl = 0;   /* more than 45 degrees from the aim's right axis: not trusted, keep the aim's */
                if (sl > 1e-3f) { sx /= sl; sz /= sl; if (sx * ax[0] + sz * ax[2] < 0) { sx = -sx; sz = -sz; }
                    static float smx, smz; static u32 st; u32 nw = GetTickCount(); if (nw - st > 300) { smx = sx; smz = sz; } st = nw;   /* light smoothing */
                    smx += (sx - smx) * 0.5f; smz += (sz - smz) * 0.5f; float l2 = sqrt_(smx * smx + smz * smz); if (l2 > 1e-3f) { ax[0] = smx / l2; ax[1] = 0; ax[2] = smz / l2; } } }
            float fwd[3] = {ax[2], 0, -ax[0]}; { if (fwd[0] * -A[2] + fwd[2] * -A[8] < 0) { fwd[0] = -fwd[0]; fwd[2] = -fwd[2]; } } float ph = leg_phase(bones, fwd), D = 0.0174533f * g_pose_t;
            static float sel = 150.0f, sab = -100.0f;   /* hand 15 cm above the shoulder; elbow 10 cm out (user) */ static u32 st2; u32 now2 = GetTickCount(); if (now2 - st2 > 1000) { st2 = now2; HANDLE f = CreateFileA("sprintpose.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                if (f != (HANDLE)-1) { char s[40] = {0}; DWORD n = 0; ReadFile(f, s, 39, &n, 0); CloseHandle(f); float v[2] = {sel, sab}, x = 0; int k = 0, neg = 0, any = 0;
                    for (DWORD i = 0; i <= n && k < 2; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) neg = 1; else if (ch >= 0x30 && ch <= 0x39) { x = x * 10 + (ch - 0x30); any = 1; } else if (any) { v[k++] = neg ? -x : x; x = 0; any = 0; neg = 0; } }
                    sel = v[0]; sab = v[1]; { extern float g_hand_h; g_hand_h = sel * 0.001f; } { float v3 = 0; int k3 = 0, ng = 0, an = 0; float x3 = 0; for (DWORD i = 0; i <= n; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) ng = 1; else if (ch >= 0x30 && ch <= 0x39) { x3 = x3 * 10 + (ch - 0x30); an = 1; } else if (an) { if (++k3 == 3) v3 = ng ? -x3 : x3; x3 = 0; an = 0; ng = 0; } } g_twist = v3 * 0.0174533f; } } }
            { extern float g_gunarm_t; (void)sel; if (g_gunarm_t > 0.001f) pose_gun_arm(bones, ax, sab * 0.001f, g_pose_t * g_gunarm_t); }   /* sprintpose.txt second value: elbow pulled in toward the chest, mm (default 60) */
            static float lb = 85, ls = 60, lt2 = 60, lr = -70, lbk = 60, lab = 15;   /* Milestone 95: more bend, less sway (user) */ static u32 tl3; u32 n3 = GetTickCount(); if (n3 - tl3 > 1000) { tl3 = n3; HANDLE f = CreateFileA("leftpose.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                if (f != (HANDLE)-1) { char s[48] = {0}; DWORD n = 0; ReadFile(f, s, 47, &n, 0); CloseHandle(f); float v[6] = {lb, ls, lt2, lr, lbk, lab}, x = 0; int k = 0, neg = 0, any = 0;
                    for (DWORD i = 0; i <= n && k < 6; i++) { char ch = i < n ? s[i] : 0x20; if (ch == 0x2D) neg = 1; else if (ch >= 0x30 && ch <= 0x39) { x = x * 10 + (ch - 0x30); any = 1; } else if (any) { v[k++] = neg ? -x : x; x = 0; any = 0; neg = 0; } }
                    lb = v[0]; ls = v[1]; lt2 = v[2]; lr = v[3]; lbk = v[4]; lab = v[5]; } }
            {   /* smoothed: the leg phase jumps during quick turning steps */
                if (ph > 0.6f) ph = 0.6f; if (ph < -0.6f) ph = -0.6f;
                { float a0 = lr + ls * ph; (void)a0; }
                static float sm_sh, sm_el; static u32 st; u32 nw = GetTickCount(); float a_sh = lr + ls * ph, a_el = lb - lbk * (ph < 0 ? (-ph * 2.0f > 1.0f ? 1.0f : -ph * 2.0f) : 0.0f);
                {   static s64 qf, lq; s64 qn; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qn); float dts = lq ? (float)(qn - lq) / (float)qf : 1.0f; lq = qn;
                    if (nw - st > 300 || dts > 0.3f) { sm_sh = a_sh; sm_el = a_el; } else { float al = dts / (dts + 0.06f); sm_sh += (a_sh - sm_sh) * al; sm_el += (a_el - sm_el) * al; } }   /* time-based (60 ms), not per call */
                st = nw; if (sm_sh < -100.0f) sm_sh = -100.0f; if (sm_sh > -20.0f) sm_sh = -20.0f;   /* never behind and up (it read as flung out sideways) */
                {   /* jump probe: big frame-to-frame changes of the swing, with its inputs */
                    static float last_sh, last_ax0, last_ax2, lS[3]; static u32 nj; float *Sq = part_pos(bones, 28);
                    if (nj < 40 && (sm_sh - last_sh > 4 || last_sh - sm_sh > 4 || (Sq && ((Sq[0] - lS[0]) * (Sq[0] - lS[0]) + (Sq[2] - lS[2]) * (Sq[2] - lS[2])) > 0.0025f))) { nj++;
                        out("  [swingjump] sh "); fo(last_sh); out("-> "); fo(sm_sh); out("raw "); fo(a_sh); out("ph "); fo(ph); out("ax "); fo(last_ax0); fo(last_ax2); out("-> "); fo(ax[0]); fo(ax[2]);
                        if (Sq) { out("shoulder moved "); fo(Sq[0] - lS[0]); fo(Sq[2] - lS[2]); } out("\r\n"); }
                    last_sh = sm_sh; last_ax0 = ax[0]; last_ax2 = ax[2]; if (Sq) for (int k = 0; k < 3; k++) lS[k] = Sq[k]; }
                {   /* on the backswing the arm also swings out from the side (away from the hip) */
                    float back = ph < 0 ? (-ph * 2.0f > 1.0f ? 1.0f : -ph * 2.0f) : 0.0f; g_abd = -lab * back * 0.0174533f * g_pose_t; }
                g_htwist = lt2 * D; pose_arm(bones, arm_l, sizeof arm_l, fore_l, sizeof fore_l, sm_sh * D, sm_el * D, ax, 0); g_htwist = 0; g_abd = 0; }   /* the forearm straightens on the backswing (lbk) so it clears the torso */   /* leftpose.txt: bend, sway, hand twist, rest angle (degrees) */
            { static u32 n; if (n < 20 && (GetTickCount() & 0x1FF) < 20) { n++; out("  [pose] right leg phase x1000 "); fo(ph); out("\r\n"); } }
            return; } }
    if (!on) return; float *A = (float *)(c + 0x5BC), ax[3] = {A[0], A[3], A[6]};
    if (ar != 0) pose_chain(bones, 0, ar, ax); if (al != 0) pose_chain(bones, 1, al, ax); }
__attribute__((naked)) static void bone_stub(void) { __asm__ volatile(
    "pushal\n\t pushl 44(%esp)\n\t pushl 44(%esp)\n\t pushl 44(%esp)\n\t call _bone_log\n\t call _pose_apply\n\t addl $12, %esp\n\t popal\n\t"
    "pushl %ebp\n\t movl %esp, %ebp\n\t subl $0x904, %esp\n\t pushl $0x95469\n\t ret"); }
/* Milestone 95: the body turns with the aim. Character_Update (0x11D7F7) turns the body heading +0x434 toward the target
   +0x4A0 (set just before by 0x119FA0: from the aim while moving, left alone standing) by at most
   min(|gap| x 0.64, 1) x +0x5FC x dt, then rebuilds the character transform from it. For the player the target is the
   aim's heading (pi - aim yaw) and +0x5FC (a rate set per frame by 0x11E950) is 1000, so the step is the whole gap.
   bodylag.txt = the game's own turning. */
void body_target(u8 *c) {
    static u32 off = 2, tl; u32 nw = GetTickCount(); if (nw - tl > 1000) { tl = nw; off = fattr_cached("bodylag.txt") != 0xFFFFFFFF; }
    if (off || c != (u8 *)g_player_char || !g_mouse_on || *(u32 *)(c + 0x604) || *(u32 *)(c + 0x638)) return;
    float *A = (float *)(c + 0x5BC), h = 3.14159265f - atan2_ext(A[6], A[0]); while (h < 0) h += 6.2831853f; while (h >= 6.2831853f) h -= 6.2831853f;
    *(float *)(c + 0x4A0) = h; *(float *)(c + 0x5FC) = 1000.0f; }
__attribute__((naked)) static void body_stub(void) { __asm__ volatile(
    "movl 0xc(%esp), %esi\n\t pushal\n\t pushl %esi\n\t call _body_target\n\t addl $4, %esp\n\t popal\n\t flds 0x4a0(%esi)\n\t pushl $0x11D801\n\t ret"); }
/* Milestone 96: widescreen in the game itself. A camera's projection object holds the half-extents of the view at unit distance:
   +0x24 horizontal (1.0), +0x28 vertical (0.75; the game keeps +0x28 = +0x24 x 0.75 and the aspect +0x2c = 4/3). 0x6AB30 builds
   the projection from the viewport and +0x24, 0x6AD00 the left/right culling planes from +0x24, 0x6AD90 the top/bottom ones
   from +0x28. Hooked at both +0x24 users: a 4:3 object gets +0x24 = +0x28 x aspect, so the vertical view stays and the
   horizontal view (and its culling) widens; the 640x480 frame is then shown stretched to the wide window.
   aspect.txt: the aspect (e.g. 1.6 for 16:10; default 16:9 = 1.7778); nowide.txt: off. */
static float g_aspect = 0;
void cam_widen(u8 *pr) {
    if (g_aspect == 0) { g_aspect = 1.7777778f; IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE); IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *); IMP BOOL WINAPI CloseHandle(HANDLE);
        if (fattr_cached("nowide.txt") != 0xFFFFFFFF) g_aspect = 1.3333333f;
        else { HANDLE f = CreateFileA("aspect.txt", 0x80000000, 1, 0, 3, 0x80, 0); if (f != (HANDLE)-1) { char t[16] = {0}; DWORD n = 0; ReadFile(f, t, 15, &n, 0); CloseHandle(f);
            float v = 0, sc = 0; for (DWORD i = 0; i < n; i++) { if (t[i] >= 0x30 && t[i] <= 0x39) { if (sc) { v += (t[i] - 0x30) * sc; sc *= 0.1f; } else v = v * 10 + (t[i] - 0x30); } else if (t[i] == '.') sc = 0.1f; } if (v > 1.0f && v < 4.0f) g_aspect = v; } }
        out("  [wide] aspect x1000 "); dec((u32)(g_aspect * 1000)); out("\r\n"); }
    if (!readable(pr + 0x24, 8) || g_aspect <= 1.34f) return; float *h = (float *)(pr + 0x24), *v = (float *)(pr + 0x28);
    /* per object, what was set last: the gameplay camera recomputes +0x28 = +0x24 x 0.75 every frame, so widening its own output
       again made the view grow each frame (the world vanished into fog). That feedback is undone instead. */
    static u8 *obj[16]; static float lh[16], lv[16]; static u32 nob; int k = 0; while (k < (int)nob && obj[k] != pr) k++;
    if (k == (int)nob) { if (nob < 16) { obj[nob] = pr; lh[nob] = lv[nob] = 0; nob++; } else k = (int)(pr - (u8 *)0) % 16, obj[k] = pr, lh[k] = lv[k] = 0; }
    float d1 = *h - lh[k], d2 = *v - lh[k] * 0.75f, d3 = *v - lv[k];
    if (lh[k] > 0 && d1 > -1e-5f && d1 < 1e-5f && d3 > -1e-5f && d3 < 1e-5f) return;                 /* still what we set */
    if (lh[k] > 0 && d1 > -1e-5f && d1 < 1e-5f && d2 > -1e-5f && d2 < 1e-5f) { *v = lv[k]; return; } /* the game recomputed +0x28 from our +0x24: put it back */
    if (*v > 0.0001f) { float r = *h / *v; if (r > 1.32f && r < 1.345f) { *h = *v * g_aspect; lh[k] = *h; lv[k] = *v; } } }
/* Milestone 99 fix: the widening above stayed in the game's camera object, and the game's sprint zoom scales the value it finds
   there: each sprint multiplied the view by another 4:3 -> 16:9 step (the view kept pulling further back). Now +0x24 is widened
   only while 0x6AB30 / 0x6AD00 run (both end in a plain ret): the caller's return address is swapped for widen_after, which puts
   the game's own value back. oldwide.txt = the old persistent widening. */
static u32 g_wret[16]; static u8 *g_wobj[16]; static float g_wsave[16], g_wset[16]; static u32 g_wd;
void widen_begin(u8 *pr, u32 ret) {
    u32 d = g_wd < 16 ? g_wd : 15; g_wret[d] = ret; g_wobj[d] = 0;
    if (g_aspect == 0) cam_widen((u8 *)0);   /* reads aspect.txt once (and returns: no object) */
    if (readable(pr + 0x24, 8) && g_aspect > 1.34f) { float *h = (float *)(pr + 0x24), *v = (float *)(pr + 0x28);
        if (*v > 0.0001f) { float r = *h / *v; if (r > 1.32f && r < 1.345f) { g_wobj[d] = pr; g_wsave[d] = *h; *h = *v * g_aspect; g_wset[d] = *h; } }
        {   static u8 *lo[8]; static float lh[8]; static u32 tl, nl; u32 k = 0; while (k < 8 && lo[k] && lo[k] != pr) k++; if (k == 8) k = 0;
            float hv = g_wobj[d] ? g_wsave[d] : *h; u32 now = GetTickCount();
            if (nl < 300 && (lo[k] != pr || hv > lh[k] * 1.01f || hv < lh[k] * 0.99f || now - tl > 5000)) { nl++; tl = now; lo[k] = pr; lh[k] = hv;
                out("  [widen] obj "); hex((u32)pr); out(" ret "); hex(ret); out(" depth "); dec(g_wd); out(" h "); fo(hv); out("v "); fo(*v); out("aspect +2c "); fo(v[1]); out(g_wobj[d] ? "widened to " : "left "); fo(*h); out("\r\n"); } } }
    if (g_wd < 16) g_wd++; }
u32 widen_end(void) { if (g_wd) g_wd--; u32 d = g_wd;
    { static u32 n; if (n < 12 && g_wobj[d]) { n++; out("  [widen-end] obj "); hex((u32)g_wobj[d]); out(" h now "); fo(*(float *)(g_wobj[d] + 0x24)); out("set "); fo(g_wset[d]); out("saved "); fo(g_wsave[d]); out("ret "); hex(g_wret[d]); out("\r\n"); } }
    if (g_wobj[d] && readable(g_wobj[d] + 0x24, 4)) { float *h = (float *)(g_wobj[d] + 0x24); u32 a = *(u32 *)h, b = *(u32 *)&g_wset[d]; if (a == b) *h = g_wsave[d];
        { static u32 n; if (n < 12) { n++; out("  [widen-end2] bits "); hex(a); out(" vs "); hex(b); out(" after "); fo(*h); out("\r\n"); } } }
    return g_wret[d]; }
__attribute__((naked)) void widen_after(void) { __asm__ volatile(
    "pushl $0\n\t pushal\n\t call _widen_end\n\t movl %eax, 32(%esp)\n\t popal\n\t ret"); }   /* returns to the game's caller */
__attribute__((naked)) static void widen_stub1(void) { __asm__ volatile(   /* 0x6AB30: sub esp,0xc; test ecx,ecx */
    "pushal\n\t pushl 32(%esp)\n\t pushl %eax\n\t call _widen_begin\n\t addl $8, %esp\n\t popal\n\t movl $_widen_after, (%esp)\n\t"
    "subl $0xc, %esp\n\t testl %ecx, %ecx\n\t pushl $0x6AB35\n\t ret"); }
__attribute__((naked)) static void widen_stub2(void) { __asm__ volatile(   /* 0x6AD00: sub esp,0xc; fld dword [eax+0x24] */
    "pushal\n\t pushl 32(%esp)\n\t pushl %eax\n\t call _widen_begin\n\t addl $8, %esp\n\t popal\n\t movl $_widen_after, (%esp)\n\t"
    "subl $0xc, %esp\n\t flds 0x24(%eax)\n\t pushl $0x6AD06\n\t ret"); }
__attribute__((naked)) static void widen_old1(void) { __asm__ volatile(
    "pushal\n\t pushl %eax\n\t call _cam_widen\n\t addl $4, %esp\n\t popal\n\t subl $0xc, %esp\n\t testl %ecx, %ecx\n\t pushl $0x6AB35\n\t ret"); }
__attribute__((naked)) static void widen_old2(void) { __asm__ volatile(
    "pushal\n\t pushl %eax\n\t call _cam_widen\n\t addl $4, %esp\n\t popal\n\t subl $0xc, %esp\n\t flds 0x24(%eax)\n\t pushl $0x6AD06\n\t ret"); }
/* Milestone 101: hit markers. The hitscan ray 0x230860 calls the bullet ray 0x230040(weapon, hardpoint, ?, ?, result,
   &range) (stdcall, 6 arguments) at 0x2309ED; it returns the object hit and writes the character hit (if any) at result +0x34.
   When the shooter is the player and a character other than the player was hit, the hit sound plays (dsound.c), at most
   once per 70 ms (a shotgun's pellets give one sound). */
static void hit_note(u8 *w, u32 hit, u32 path);
static u32 (__stdcall *g_bray)(u32, u32, u32, u32, u32, u32) = (u32 (__stdcall *)(u32, u32, u32, u32, u32, u32))0x230040;
u32 __stdcall bray_wrap(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    {   /* the hit ray starts near Flint and points along her facing, not along the aim the loader chose: for a zoomed shot or a special's
           locked shot (g_zT set by aim_fix within 0.2 s) it is steered at that point. Only a ray starting within 3 m of Flint (the player's) */
        extern float g_zT[3]; extern u32 g_zT_t;
        if (g_zT_t && GetTickCount() - g_zT_t < 200 && g_player_char && a2 > 0x10000 && a3 > 0x10000 && readable((void *)a2, 12) && readable((void *)a3, 12)) {
            u8 *pc = (u8 *)g_player_char; float *pp = ((float *(__thiscall *)(void *))(*(u32 **)pc)[0x40 / 4])(pc) + 9;
            float *o = (float *)a2, *d = (float *)a3, e[3] = {o[0] - pp[0], o[1] - pp[1], o[2] - pp[2]};
            if (e[0] * e[0] + e[1] * e[1] + e[2] * e[2] < 9.0f) { float v[3] = {g_zT[0] - o[0], g_zT[1] - o[1], g_zT[2] - o[2]}, l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                if (l > 1.0f) { float sl; __asm__("fsqrt" : "=t"(sl) : "0"(l)); for (int q = 0; q < 3; q++) d[q] = v[q] / sl; } } } }
    if (scoped()) { static u32 nl; if (nl++ < 24) {   /* probe: the hitscan's arguments while zoomed - which are points/directions (floats) */
        extern float g_eye_ours[3]; out("  [bray] zoomed args"); u32 ar[6] = {a0, a1, a2, a3, a4, a5};
        for (int i = 1; i < 6; i++) { out(" |"); hex(ar[i]); if (ar[i] > 0x10000 && readable((void *)ar[i], 24)) { float *f = (float *)ar[i]; for (int q = 0; q < 6; q++) { s32 v = (s32)(f[q] * 1000); out(" "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } } }
        out(" | eye"); for (int q = 0; q < 3; q++) { s32 v = (s32)(g_eye_ours[q] * 1000); out(" "); if (v < 0) { out("-"); v = -v; } dec((u32)v); } out("\r\n"); } }
    float zr0 = (a5 > 0x10000 && readable((void *)a5, 4)) ? *(float *)a5 : 0;
    int steered_ = 0; { extern u32 g_zT_t; steered_ = g_zT_t && GetTickCount() - g_zT_t < 200; }
    u32 r = g_bray(a0, a1, a2, a3, a4, a5);
    if (steered_ && lock_mode() && a4 > 0x10000 && readable((void *)(a4 + 0x44), 4)) {   /* a locked shot that hit cover: the next shot tries another part of
        the target (head, hips, left, right; kept while it hits). Retrying within one shot crashed the game (the target hit twice) */
        extern int g_aim_part; u8 *lock_target(void); u8 *tg = lock_target(); u32 h0 = *(u32 *)(a4 + 0x44);
        (void)tg; (void)h0; g_aim_part = 0; }   /* (off: aiming at the head after a miss went over shorter enemies) */
    if (steered_ && lock_mode()) { static u32 nl; if (nl++ < 60) { u32 hit = (a4 > 0x10000 && readable((void *)(a4 + 0x44), 4)) ? *(u32 *)(a4 + 0x44) : 0; u8 *lock_target(void);
        out("  [lockray] hit "); hex(hit); out(hit && hit == (u32)lock_target() ? " = the target" : hit ? " (something else)" : " (nothing)"); out(" range left x10 "); if (a5 > 0x10000 && readable((void *)a5, 4)) dec((u32)(*(float *)a5 * 10)); out("\r\n"); } }
    {   /* zoomed: the trail (drawn after this from the weapon's stored direction) from the gun to where the hit ray ended (range left in *a5) */
        extern float g_zM0[3]; extern float *g_zsd; extern u32 g_zT_t;
        if (g_zsd && g_zT_t && GetTickCount() - g_zT_t < 200 && a2 > 0x10000 && a3 > 0x10000 && a5 > 0x10000 && readable((void *)a5, 4) && readable(g_zsd, 12)) {
            float *o = (float *)a2, *d = (float *)a3, R = *(float *)a5; if (!(R > 0.5f && R <= zr0)) R = zr0 > 1.0f ? zr0 : 100.0f;
            float v[3] = {o[0] + d[0] * R - g_zM0[0], o[1] + d[1] * R - g_zM0[1], o[2] + d[2] * R - g_zM0[2]}, l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (l > 0.25f) { float sl; __asm__("fsqrt" : "=t"(sl) : "0"(l)); for (int q = 0; q < 3; q++) g_zsd[q] = v[q] / sl; }
            static u32 nl; if (nl++ < 20) { out("  [bray] zoomed hit range x10 "); dec((u32)(R * 10)); out(" of "); dec((u32)(zr0 * 10)); out(r ? " hit" : " none"); out("\r\n"); } g_zsd = 0; } }
    /* the result: +0x34 the shooter, +0x44 the object hit (a character when [[obj +0xC] +4] == 3, as 0x2303A0 checks before
       its body-part damage) */
    if (r && a4 > 0x10000 && a4 < 0x7FFE0000u && readable((void *)(a4 + 0x44), 4)) { u32 hit = *(u32 *)(a4 + 0x44); if (hit) hit_note((u8 *)a0, hit, 1); }
    return r; }
/* the other bullet path (0x230400, 7 arguments, called at 0x2309CB; bullets that can pass through, up to arg6 hits): arg4 +4 /
   +8 = a list of result records, each with the character hit at +0x34. The slots are cleared before the call, so a character
   found there afterwards is a hit of this shot. */
static u32 (__stdcall *g_bray2)(u32, u32, u32, u32, u32, u32, u32) = (u32 (__stdcall *)(u32, u32, u32, u32, u32, u32, u32))0x230400;
static void hit_note(u8 *w, u32 hit, u32 path);
u32 __stdcall bray2_wrap(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6) {
    {   /* the pass-through bullet path (Flint's rifle?) gets the same steering as bray_wrap: origin a2, direction a3 - only when they look
           like that (a unit direction, an origin within 3 m of the player) and right after a steered shot */
        extern float g_zT[3]; extern u32 g_zT_t; static u32 nl;
        if (g_zT_t && GetTickCount() - g_zT_t < 200 && g_player_char && a2 > 0x10000 && a3 > 0x10000 && readable((void *)a2, 12) && readable((void *)a3, 12)) {
            u8 *pc = (u8 *)g_player_char; float *pp = ((float *(__thiscall *)(void *))(*(u32 **)pc)[0x40 / 4])(pc) + 9;
            float *o = (float *)a2, *d = (float *)a3, e[3] = {o[0] - pp[0], o[1] - pp[1], o[2] - pp[2]}, dl = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (e[0] * e[0] + e[1] * e[1] + e[2] * e[2] < 9.0f && dl > 0.9f && dl < 1.1f) { float v[3] = {g_zT[0] - o[0], g_zT[1] - o[1], g_zT[2] - o[2]}, l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                if (l > 1.0f) { float sl; __asm__("fsqrt" : "=t"(sl) : "0"(l)); for (int q = 0; q < 3; q++) d[q] = v[q] / sl; if (nl++ < 20) out("  [bray2] steered at the target\r\n"); } }
            else if (nl++ < 20) out("  [bray2] arguments do not look like origin / direction\r\n"); } }
    u32 *rec = 0, n = 0;
    if (a4 > 0x10000 && a4 < 0x7FFE0000u && readable((void *)(a4 + 4), 8)) { u32 b = *(u32 *)(a4 + 4), e = *(u32 *)(a4 + 8);
        if (b > 0x10000 && e > b && e - b <= 64 && readable((void *)b, e - b)) { rec = (u32 *)b; n = (e - b) / 4; if (n > a6) n = a6; } }
    u32 before[16]; if (n > 16) n = 16;   /* (each record's object hit at +0x44, as in the single-hit path; only a changed one is this shot's) */
    for (u32 i = 0; i < n; i++) before[i] = rec[i] > 0x10000 && rec[i] < 0x7FFE0000u && readable((void *)(rec[i] + 0x44), 4) ? *(u32 *)(rec[i] + 0x44) : 0;
    u32 r = g_bray2(a0, a1, a2, a3, a4, a5, a6);
    for (u32 i = 0; i < n; i++) if (rec[i] > 0x10000 && rec[i] < 0x7FFE0000u && readable((void *)(rec[i] + 0x44), 4)) { u32 h = *(u32 *)(rec[i] + 0x44); if (h && h != before[i]) hit_note((u8 *)a0, h, 2); }
    return r; }
static void hit_note(u8 *w, u32 hit, u32 path) {
    /* (game objects live at 0x80000000 and up: only readable() decides, no user-space range test) */
    if (!g_player_char || hit == (u32)g_player_char || hit < 0x10000 || !readable((void *)(hit + 0xC), 4)) return;
    {   u32 t = *(u32 *)(hit + 0xC); if (t < 0x10000 || !readable((void *)(t + 4), 4) || *(u32 *)(t + 4) != 3) return; }   /* a character ([[obj +0xC] +4] == 3, as the player) */
    u8 *shooter = 0;
    u32 a = w && readable(w + 0x1B0, 4) ? *(u32 *)(w + 0x1B0) : 0;
    if (a > 0x10000 && a < 0x7FFE0000u && readable((void *)(a + 8), 4)) { u32 b = *(u32 *)(a + 8); if (b > 0x10000 && b < 0x7FFE0000u && readable((void *)(b + 0x1D4), 4)) shooter = *(u8 **)(b + 0x1D4); }
    if (shooter != (u8 *)g_player_char) return;
    {   /* a dead body is still a character: its health (character +0x54, a float; 20.5 -> 0 over five rifle hits in the probe) must
           be above 0. The killing shot still counts: the last health seen per character is kept, and a hit that took it from above
           0 to 0 is one (the damage may already be applied when the ray returns). */
        if (!readable((void *)(hit + 0x54), 4)) return; float hp = *(float *)(hit + 0x54);
        static u32 who[16]; static float seen[16]; static u32 rr; u32 k = 0; while (k < 16 && who[k] != hit) k++;
        float before = k < 16 ? seen[k] : 1.0f; if (k == 16) { k = rr++ & 15; who[k] = hit; } seen[k] = hp;
        if (!(hp > 0.0f) && !(before > 0.0f)) return; }
    static u32 last; u32 now = GetTickCount(); if (now - last > 70) { last = now; void snd_hitmarker(void); snd_hitmarker(); }
    { u32 *pd = gpu_pad(); if (pd && fattr_cached("nohitmark.txt") == 0xFFFFFFFF) pd[56] = now | 1; }   /* the viewer draws the hit marker (bfgl.c) */
    static u32 nl; if (nl < 20) { nl++; out("  [hit] player hit character "); hex(hit); out(path == 2 ? " (pass-through path)\r\n" : "\r\n"); } }
void hs_fix(u32 *s) { aim_fix((u8 *)s[1], (float *)s[3], (float *)s[4]); }
__attribute__((naked)) static void hs_stub(void) { __asm__ volatile(
    "pushal\n\t leal 32(%esp), %eax\n\t pushl %eax\n\t call _hs_fix\n\t addl $4, %esp\n\t popal\n\t pushl $0x230860\n\t ret"); }
void bullet_fix(u8 *f) {
    u8 *w = *(u8 **)(f + 0x114); float *pos = (float *)(f + 0x18), *dir = (float *)(f + 0x30);
    aim_fix(w, pos, dir);
    extern u32 g_ourcam; if (!g_ourcam) return;   /* the game's camera: the game's own aiming */
    u8 *own = w ? *(u8 **)(w + 8) : 0;
    static u32 n; if (n < 12) { n++; out("  [bullet] own "); hex((u32)own); out(own == (u8 *)g_player_char ? " PLAYER" : ""); out(" pos "); fo(pos[0]); fo(pos[1]); fo(pos[2]); out(" dir "); fo(dir[0]); fo(dir[1]); fo(dir[2]);
        out(" eye "); fo(g_eye_ours[0]); fo(g_eye_ours[1]); fo(g_eye_ours[2]); out(" camfwd "); fo(-g_cb[0]); fo(-g_cb[1]); fo(-g_cb[2]); out(" camup "); fo(g_cu[0]); fo(g_cu[1]); fo(g_cu[2]);
        if (n == 1) { float *V = (float *)0x4A5858; out("\r\n  [view]"); for (int k = 0; k < 16; k++) fo(V[k]); float *C = render_consts(); out("\r\n  [consts 96..127]"); for (int k = 96 * 4; k < 128 * 4; k++) { if (k % 16 == 0) { out(" |"); dec(k / 4); out(":"); } fo(C[k]); } }
        out("\r\n"); }
}
__attribute__((naked)) static void bullet_stub(void) { __asm__ volatile(
    "pushal\n\t leal 32(%esp), %eax\n\t pushl %eax\n\t call _bullet_fix\n\t addl $4, %esp\n\t popal\n\t"
    "movl 0x20(%esp), %eax\n\t movl 0x1c(%esp), %ecx\n\t pushl $0x22BC6C\n\t ret"); }
void input_patch(void) {
    if (fattr_cached("nogamehooks.txt") != 0xFFFFFFFF) { out("Game hooks: all off (nogamehooks.txt)\r\n"); goto essential; }   /* test switch (Milestone 97): the clock and input patches stay */
    {   u8 *p = (u8 *)0x22BC64; static const u8 e[8] = {0x8B, 0x44, 0x24, 0x20, 0x8B, 0x4C, 0x24, 0x1C}; int ok = 1; for (int k = 0; k < 8; k++) if (p[k] != e[k]) ok = 0;
        if (ok) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)bullet_stub - (0x22BC64 + 5); p[5] = p[6] = p[7] = 0x90; out("Bullet hook: on\r\n"); } else out("Bullet hook: bytes differ\r\n"); }
    if (fattr_cached("noweaponhook.txt") == 0xFFFFFFFF) {   u8 *p = (u8 *)0x22EE30; static const u8 e[9] = {0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x28, 0x02, 0x00, 0x00}; int ok = 1; for (int k = 0; k < 9; k++) if (p[k] != e[k]) ok = 0;
        if (ok) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)wup_stub - (0x22EE30 + 5); for (int k = 5; k < 9; k++) p[k] = 0x90; out("Weapon update hook: on\r\n"); } else out("Weapon update hook: bytes differ\r\n"); }
    {   u8 *p = (u8 *)0x95460; static const u8 e[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x04, 0x09, 0x00, 0x00}; int ok = 1; for (int k = 0; k < 9; k++) if (p[k] != e[k]) ok = 0;
        if (ok) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)bone_stub - (0x95460 + 5); for (int k = 5; k < 9; k++) p[k] = 0x90; out("Bone probe: on\r\n"); } else out("Bone probe: bytes differ\r\n"); }
    {   static const u8 e1[5] = {0x83, 0xEC, 0x0C, 0x85, 0xC9}, e2[6] = {0x83, 0xEC, 0x0C, 0xD9, 0x40, 0x24}; u8 *p1 = (u8 *)0x6AB30, *p2 = (u8 *)0x6AD00; int ok = 1;
        for (int k = 0; k < 5; k++) if (p1[k] != e1[k]) ok = 0; for (int k = 0; k < 6; k++) if (p2[k] != e2[k]) ok = 0;
        /* Milestone 99: the in-game widening is OFF by default - with it the view kept pulling further back after sprinting (an A/B run
           with it off fixed that). wide.txt = culling-only widening + the viewer's squeeze; oldwide.txt = the Milestone 96 widening. */
        if (fattr_cached("wide.txt") == 0xFFFFFFFF && fattr_cached("oldwide.txt") == 0xFFFFFFFF) { out("Widescreen camera hooks: off (wide.txt turns them on)\r\n"); ok = 0; p1 = p2 = 0; }
        int old = fattr_cached("oldwide.txt") != 0xFFFFFFFF; u32 s1 = old ? (u32)widen_old1 : (u32)widen_stub1, s2 = old ? (u32)widen_old2 : (u32)widen_stub2;
        /* Milestone 99: by default only the culling planes (0x6AD00) are widened, and only while they are built; the projection
           (0x6AB30) stays the game's 4:3 one and the viewer squeezes the 3D scene horizontally (bfgl.c). The game copied the widened
           projection back into its own field of view, which compounded with the sprint zoom (the view kept pulling back). */
        if (ok && !old) { p2[0] = 0xE9; *(u32 *)(p2 + 1) = s2 - (0x6AD00 + 5); p2[5] = 0x90; out("Widescreen culling hook: on (projection left 4:3, squeezed by the viewer)\r\n"); ok = 0; p1 = 0; }
        if (ok) { p1[0] = 0xE9; *(u32 *)(p1 + 1) = s1 - (0x6AB30 + 5); p2[0] = 0xE9; *(u32 *)(p2 + 1) = s2 - (0x6AD00 + 5); p2[5] = 0x90; out("Widescreen camera hooks: on\r\n"); } else if (p1) out("Widescreen camera hooks: bytes differ\r\n"); }
    {   u8 *p = (u8 *)0x11D7F7; static const u8 e[10] = {0x8B, 0x74, 0x24, 0x0C, 0xD9, 0x86, 0xA0, 0x04, 0x00, 0x00}; int ok = 1; for (int k = 0; k < 10; k++) if (p[k] != e[k]) ok = 0;
        if (ok) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)body_stub - (0x11D7F7 + 5); for (int k = 5; k < 10; k++) p[k] = 0x90; out("Body turn hook: on\r\n"); } else out("Body turn hook: bytes differ\r\n"); }
    if (fattr_cached("vsclog.txt") != 0xFFFFFFFF) { static const u8 e[6] = {0x56, 0x57, 0x8B, 0x3D, 0x40, 0xBD}; if (hook6(0x36F6C0, vsc_stub, e)) out("VS constant probe: on\r\n"); }
    {   static const u8 e[6] = {0x83, 0xEC, 0x60, 0x53, 0x32, 0xDB}; if (hook6(0x22F1D0, fb_stub, e)) out("Fire probe: on\r\n"); else out("Fire probe: bytes differ\r\n"); }
    {   /* performance (Nov 21): D3D's write-combine flush 0x372180 sets bit 16 of the GPU register 0x100410 and polls it until clear - three
           reads/writes of a trapped page (three Windows exceptions) per call, called constantly: the game thread spent most of its time in
           exception dispatch (~42 fps in busy scenes). The emulated flush is done at once (kernel2.c reg_rd), so it becomes sfence; ret.
           wcflushtrap.txt = the original */
        u8 *p = (u8 *)0x372180; if (p[0] == 0xA1 && *(u32 *)(p + 1) == 0x37DD88 && p[0x2C] == 0xC3 && GetFileAttributesA("wcflushtrap.txt") == 0xFFFFFFFF) {
            p[0] = 0x0F; p[1] = 0xAE; p[2] = 0xF8; p[3] = 0xC3; out("Write-combine flush: no register trap\r\n"); } }
    {   u8 *p = (u8 *)0x2309ED; if (p[0] == 0xE8 && (u32)(p + 5) + *(s32 *)(p + 1) == 0x230040) { *(s32 *)(p + 1) = (s32)((u32)bray_wrap - (0x2309ED + 5)); out("Hit marker hook: on\r\n"); } else out("Hit marker hook: bytes differ\r\n"); }
    {   u8 *p = (u8 *)0x2309CB; if (p[0] == 0xE8 && (u32)(p + 5) + *(s32 *)(p + 1) == 0x230400) { *(s32 *)(p + 1) = (s32)((u32)bray2_wrap - (0x2309CB + 5)); out("Hit marker hook 2: on\r\n"); } else out("Hit marker hook 2: bytes differ\r\n"); }
    {   u8 *p = (u8 *)0x231868; if (p[0] == 0xE8 && (u32)(p + 5) + *(s32 *)(p + 1) == 0x230860) { *(s32 *)(p + 1) = (s32)((u32)hs_stub - (0x231868 + 5)); out("Hitscan aim hook: on\r\n"); } else out("Hitscan aim hook: bytes differ\r\n"); }
    {   u8 *p = (u8 *)0x12CA48; static const u8 e[9] = {0x8B, 0x4D, 0x08, 0xD9, 0x81, 0x78, 0x04, 0x00, 0x00}; int ok = 1; for (int k = 0; k < 9; k++) if (p[k] != e[k]) ok = 0;
        if (ok) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)sprint_stub - (0x12CA48 + 5); for (int k = 5; k < 9; k++) p[k] = 0x90; out("Sprint (Left Shift): on\r\n"); } }
    if (fattr_cached("camxflog.txt") != 0xFFFFFFFF) { u8 *p = (u8 *)0x2338C0; if (p[0] == 0x53 && p[1] == 0x8B && p[2] == 0x4C && p[3] == 0x24 && p[4] == 0x10) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)mul_stub - (0x2338C0 + 5); g_xf_log = fattr_cached("camxflog.txt") != 0xFFFFFFFF; out("Camera transform hook: on\r\n"); } }
    out(fattr_cached("gamecamera.txt") == 0xFFFFFFFF ? "Camera: Division-style (gamecamera.txt for the game's own)\r\n" : "Camera: the game's own (gamecamera.txt)\r\n");
    if (fattr_cached("viewdump.txt") != 0xFFFFFFFF) { u8 *p = (u8 *)0x6C370; if (p[0] == 0xBA) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)view_stub - (0x6C370 + 5); g_view_dump = 1; } }
    { u32 *vt = (u32 *)(0x3A2640 + 0x6C); if (*vt == 0x702A0) { *vt = (u32)cam_stub; out("Follow camera update wrapped\r\n"); } }
    if (fattr_cached("aimassist.txt") == 0xFFFFFFFF) {
        static const u8 e1[6] = {0x83, 0xEC, 0x08, 0x8B, 0x50, 0x08}, e2[6] = {0x8B, 0x83, 0x38, 0x06, 0x00, 0x00};
        static const u8 e3[6] = {0xD9, 0x85, 0xDC, 0x07, 0x00, 0x00};
        u32 n = hook6(0x11E570, move_stub, e1) + hook6(0xC4E70, lock_stub1, e2) + hook6(0xC4EC0, lock_stub2, e2) + hook6(0x13389D, friction_stub, e3);
        out("Aim assist off while the mouse aims: "); dec(n); out(" of 4 hooks\r\n"); }
essential:
    {   /* Milestone 99: XAPI's start-up disc authentication (0x2F801F: SCSI mode sense + security sectors through IOCTL 0x4D014).
           Our DVD drive cannot answer it; an original default.xbe then reboots to the dashboard (HalReturnToFirmware(2), so
           play.bat restarted it endlessly). Media-patched copies already jump past the result: do the same (jge -> jmp). */
        u8 *p = (u8 *)0x2F8257; if (p[0] == 0x7D && p[1] == 0x06) { p[0] = 0xEB; out("Disc authentication: skipped (original default.xbe)\r\n"); }
        else if (p[0] == 0xEB && p[1] == 0x06) out("Disc authentication: already skipped by this default.xbe\r\n"); }
    {   u8 *p = (u8 *)0x37834E; if (p[0] == 0x8B && p[1] == 0x08 && p[2] == 0x56) { p[0] = 0xE9; *(u32 *)(p + 1) = (u32)gamma_stub - (0x37834E + 5); out("Gamma ramp: captured\r\n"); } }
    {   u32 n = jmp_to(0x2F2986, QueryPerformanceCounter_, 0x8B) + jmp_to(0x377780, rdtsc_, 0x0F);
        out("Clock functions patched: "); dec(n); out(" of 2 (733 MHz time-stamp counter)\r\n"); }
    u32 n = jmp_to(0x37FCC0, XInitDevices_, 0xE9) + jmp_to(0x37FD54, XInputOpen_, 0x55) + jmp_to(0x37FDAA, XInputClose_, 0x8B)
          + jmp_to(0x37FDB6, XInputGetState_, 0x53) + jmp_to(0x37FE22, XInputSetState_, 0x8B);
    out("Input functions patched: "); dec(n); out(" of 5\r\n");
}

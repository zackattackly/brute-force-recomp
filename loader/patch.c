/* Game-file patches (Milestone 97). The download carries no game data: the files this project edits (the key-binding menu,
 * the Razorback's name) come as patches (patches\*.bfp, made by tools\make_patches.py) that are applied to the player's own
 * files. kernel2.c open_file calls patch_check before the game opens a file under xbox_hdd\p5 for reading; if a patch exists
 * and the file is still the original (size and FNV-1a hash), the patched version is written in its place (the original is
 * kept as <file>.orig). A file that is neither the original nor the patched version is left alone. */
#include "util.h"

IMP DWORD WINAPI GetFileAttributesA(const char *);
IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMP DWORD WINAPI GetFileSize(HANDLE, DWORD *);
IMP BOOL WINAPI CopyFileA(const char *, const char *, BOOL);
IMP void *WINAPI HeapAlloc(HANDLE, DWORD, u32);
IMP BOOL WINAPI HeapFree(HANDLE, DWORD, void *);
IMP HANDLE WINAPI GetProcessHeap(void);

static u32 fnv(const u8 *p, u32 n) { u32 h = 2166136261u; while (n--) h = (h ^ *p++) * 16777619u; return h; }
static u8 *load(const char *path, u32 *size) {
    HANDLE f = CreateFileA(path, 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return 0;
    u32 n = GetFileSize(f, 0); u8 *b = HeapAlloc(GetProcessHeap(), 0, n + 1); DWORD r = 0;
    if (b && !ReadFile(f, b, n, &r, 0)) r = 0; CloseHandle(f);
    if (!b || r != n) { if (b) HeapFree(GetProcessHeap(), 0, b); return 0; }
    *size = n; return b;
}
void patch_check(const char *w) {
    const char *k = w, *rel = 0;
    for (; *k; k++) if ((k[0] == 'x' || k[0] == 'X') && k[1] == 'b' && k[2] == 'o' && k[3] == 'x' && k[4] == '_' && k[5] == 'h' && k[6] == 'd' && k[7] == 'd' && k[8] == '\\') rel = k + 9;
    if (!rel || rel[0] != 'p' || rel[1] != '5') return;
    char pp[300]; u32 q = 0; const char *pre = "patches\\"; while (*pre) pp[q++] = *pre++;
    for (const char *s = rel; *s && q < 280; s++) { char c = *s; if (c == '\\') c = '_'; else if (c >= 'A' && c <= 'Z') c = (char)(c + 32); pp[q++] = c; }
    pp[q++] = '.'; pp[q++] = 'b'; pp[q++] = 'f'; pp[q++] = 'p'; pp[q] = 0;
    if (GetFileAttributesA(pp) == 0xFFFFFFFF) return;
    u32 pn, fn; u8 *p = load(pp, &pn); if (!p) return;
    u8 *f = load(w, &fn);
    if (f && pn >= 21 && *(u32 *)p == 0x31504642 /* BFP1 */) {
        u32 osz = *(u32 *)(p + 4), oh = *(u32 *)(p + 8), nsz = *(u32 *)(p + 12), nh = *(u32 *)(p + 16);
        if (fn == nsz && fnv(f, fn) == nh) { }   /* already patched */
        else if (fn == osz && fnv(f, fn) == oh) {
            u8 *o = HeapAlloc(GetProcessHeap(), 0, nsz + 1); u32 on = 0, i = 20, ok = o != 0;
            while (ok && i < pn && p[i]) {
                if (p[i] == 1 && i + 9 <= pn) { u32 off = *(u32 *)(p + i + 1), len = *(u32 *)(p + i + 5); if (off + len > fn || on + len > nsz) ok = 0; else { memcpy_(o + on, f + off, len); on += len; } i += 9; }
                else if (p[i] == 2 && i + 5 <= pn) { u32 len = *(u32 *)(p + i + 1); if (i + 5 + len > pn || on + len > nsz) ok = 0; else { memcpy_(o + on, p + i + 5, len); on += len; } i += 5 + len; }
                else ok = 0; }
            if (ok && on == nsz && fnv(o, on) == nh) {
                char bk[310]; u32 b = 0; while (w[b] && b < 300) { bk[b] = w[b]; b++; } bk[b++] = '.'; bk[b++] = 'o'; bk[b++] = 'r'; bk[b++] = 'i'; bk[b++] = 'g'; bk[b] = 0;
                if (GetFileAttributesA(bk) == 0xFFFFFFFF) CopyFileA(w, bk, 1);
                HANDLE h = CreateFileA(w, 0x40000000, 0, 0, 2, 0x80, 0);
                if (h != (HANDLE)-1) { DWORD wr; WriteFile(h, o, on, &wr, 0); CloseHandle(h); out("  [patch] applied "); out(pp); out("\r\n"); }
            } else { out("  [patch] "); out(pp); out(" did not apply\r\n"); }
            if (o) HeapFree(GetProcessHeap(), 0, o);
        } else { out("  [patch] "); out(w); out(" is a version this patch does not know; left alone\r\n"); }
    }
    if (f) HeapFree(GetProcessHeap(), 0, f); HeapFree(GetProcessHeap(), 0, p);
}

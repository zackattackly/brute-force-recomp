/* Xbox kernel replacements, second batch (Milestone 73): files, kernel waits, timers and DPCs,
 * interrupts (accepted but never fired), symbolic links, I/O manager no-ops, crypto, time fields.
 * Paths: D:\ and \Device\CdRom0\ -> the game folder; other drives -> folders next to the loader. */
#include "util.h"

#define NTAPI __stdcall
#define HDD "xbox_hdd"   /* the Xbox hard disk, as a folder next to the loader (the older "hdd" folder held a cache written by a broken build) */
#define ST_OK 0
#define ST_FAIL 0xC0000001
#define ST_TIMEOUT 0x102
#define ST_NOT_FOUND 0xC0000034
#define ST_PATH_NOT_FOUND 0xC000003A
#define ST_EOF 0xC0000011
#define ST_NO_MORE_FILES 0x80000006
#define ST_NO_SUCH_FILE 0xC000000F
#define NV(o) (*(volatile u32 *)(0xFD000000 + (o)))
volatile u32 *regp(u32 off);
#define REG(o) (*regp(o))   /* a GPU register, wherever it lives (trapped pages keep theirs in a shadow copy) */
#define ST_INVALID_DEVICE_REQUEST 0xC0000010
int net_enabled(void); u32 net_link(void);   /* net.c (Milestone 97) */

IMP HANDLE WINAPI CreateFileMappingA(HANDLE, void *, DWORD, DWORD, DWORD, const char *);
IMP void *WINAPI MapViewOfFileEx(HANDLE, DWORD, DWORD, DWORD, u32, void *);
IMP BOOL WINAPI SetFilePointerEx(HANDLE, s64, s64 *, DWORD);
IMP BOOL WINAPI GetFileSizeEx(HANDLE, s64 *);
IMP BOOL WINAPI GetFileAttributesExA(const char *, u32, void *);
IMP HANDLE WINAPI FindFirstFileA(const char *, void *);
IMP BOOL WINAPI FindNextFileA(HANDLE, void *);
IMP BOOL WINAPI FindClose(HANDLE);
IMP BOOL WINAPI DeleteFileA(const char *);
IMP BOOL WINAPI CreateDirectoryA(const char *, void *);
IMP BOOL WINAPI SetEndOfFile(HANDLE);
IMP BOOL WINAPI GetFileInformationByHandle(HANDLE, void *);
IMP BOOL WINAPI FileTimeToSystemTime(const u64 *, u16 *);
IMP BOOL WINAPI SystemTimeToFileTime(const u16 *, u64 *);

extern char g_gamedir[600], g_homedir[600];   /* both end in a backslash (set in bfcore.c) */
extern DWORD g_tls;
void *new_kpcr(u32 tlsSize);

typedef struct { u16 len, max; char *buf; } OSTR;                 /* Xbox OBJECT_STRING (ANSI) */
typedef struct { HANDLE root; OSTR *name; u32 attr; } OATTR;      /* Xbox OBJECT_ATTRIBUTES */
typedef struct { u32 status, info; } IOSB;

static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void scat(char *d, const char *s) { d += slen(d); while ((*d++ = *s++)) ; }
static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int prefix(const char *s, u32 n, const char *p) { u32 i = 0; for (; p[i]; i++) if (i >= n || up(s[i]) != up(p[i])) return 0; return (int)i; }

/* ---- handle table: remembers each open file's Windows path (for relative opens and directory listing) ---- */
typedef struct { HANDLE h; HANDLE find; u32 dir; char path[320]; } FH;
static FH g_fh[256];
static FH *fh_get(HANDLE h) { for (u32 i = 0; i < 256; i++) if (g_fh[i].h == h && h) return &g_fh[i]; return 0; }
IMP DWORD WINAPI GetFileAttributesA(const char *);
static void fh_add(HANDLE h, const char *p) { for (u32 i = 0; i < 256; i++) if (!g_fh[i].h) { g_fh[i].h = h; g_fh[i].find = 0; { DWORD a = GetFileAttributesA(p); g_fh[i].dir = a != 0xFFFFFFFF && (a & 0x10); } g_fh[i].path[0] = 0; scat(g_fh[i].path, p); return; } }
void kernel2_close(HANDLE h) { FH *f = fh_get(h); if (f) { if (f->find) FindClose(f->find); f->h = 0; } }

/* ---- symbolic links (\??\X: -> device path) ---- */
typedef struct { char link[64], target[128]; } SL;
static SL g_sl[16];

/* Converts an Xbox object path to a Windows path. */
static void map_path(OATTR *oa, char *w) {
    char p[400]; u32 n = 0; w[0] = 0;
    FH *rf = oa->root ? fh_get(oa->root) : 0;
    if (rf) { scat(w, rf->path); if (w[slen(w) - 1] != '\\') scat(w, "\\"); }
    const char *s = oa->name ? oa->name->buf : ""; u32 l = oa->name ? oa->name->len : 0;
    for (u32 i = 0; i < l && n < 390; i++) p[n++] = s[i] == '/' ? '\\' : s[i];
    p[n] = 0;
    if (rf) { scat(w, p[0] == '\\' ? p + 1 : p); return; }
    char *q = p; int k;
    if ((k = prefix(q, n, "\\??\\"))) q += k;
    if (q[0] && q[1] == ':') {              /* drive letter: follow symbolic links the game created */
        for (u32 i = 0; i < 16; i++) if (g_sl[i].link[0] && prefix(g_sl[i].link, slen(g_sl[i].link), "\\??\\") && up(g_sl[i].link[4]) == up(q[0])) {
            char t[400]; t[0] = 0; scat(t, g_sl[i].target); scat(t, q + 2); memcpy_(p, t, slen(t) + 1); q = p; break; }
    }
    u32 m = slen(q);
    if ((k = prefix(q, m, "\\Device\\CdRom0"))) { scat(w, g_gamedir); q += k; }
    else if ((k = prefix(q, m, "\\Device\\Harddisk0\\Partition"))) { scat(w, g_homedir); scat(w, HDD); CreateDirectoryA(w, 0); scat(w, "\\p"); char d[2] = {q[k], 0}; scat(w, d); CreateDirectoryA(w, 0); scat(w, "\\"); q += k + 1; }
    else if (q[0] && q[1] == ':') {
        int c = up(q[0]);
        if (c == 'D') scat(w, g_gamedir);
        else { scat(w, g_homedir); scat(w, HDD "\\"); CreateDirectoryA(w, 0); char d[2] = {(char)c, 0}; scat(w, d); CreateDirectoryA(w, 0); scat(w, "\\"); }
        q += 2;
    } else { scat(w, g_gamedir); }
    while (*q == '\\') q++;
    scat(w, q);
    u32 e = slen(w); while (e > 3 && w[e - 1] == '\\') w[--e] = 0;
}

static u32 err_status(void) {
    switch (GetLastError()) {
    case 2: return ST_NOT_FOUND; case 3: return ST_PATH_NOT_FOUND; case 80: case 183: return 0xC0000035;
    case 5: return 0xC0000022; case 32: return 0xC0000043; default: return ST_FAIL; }
}

static s32 open_file(HANDLE *h, u32 access, OATTR *oa, IOSB *io, u32 disp, u32 opts) {
    char w[400]; map_path(oa, w);
    u32 a = GENERIC_READ; if (access & (0x40000000 | 0x10000000 | 2 | 4 | 0x10000)) a |= GENERIC_WRITE;
    static const u32 map[6] = {2, 3, 1, 4, 5, 2};     /* NT disposition -> Win32 creation */
    u32 cr = map[disp < 6 ? disp : 1], created = disp == 2;
    if (opts & 1) {                                   /* FILE_DIRECTORY_FILE: create if asked, then open what is there (Milestone 77: */
        DWORD at = GetFileAttributesA(w);             /*  opening with CREATE_NEW after creating it failed, so saves could not be made) */
        int exists = at != 0xFFFFFFFF && (at & 0x10);
        if (exists && disp == 2) { if (io) io->status = 0xC0000035; out("  open "); out(w); out("  (directory exists)\r\n"); return 0xC0000035; }
        if (!exists && (disp == 2 || disp == 3 || disp == 5)) { CreateDirectoryA(w, 0); created = 1; } else created = 0;
        a = GENERIC_READ; cr = 3;                     /* OPEN_EXISTING */
    }
    if (!(a & GENERIC_WRITE) && !(opts & 1)) { void patch_check(const char *); patch_check(w); }
    HANDLE f = CreateFileA(w, a, 7, 0, cr, 0x02000080, 0);
    if (f == INVALID_HANDLE && (a & GENERIC_WRITE) && GetLastError() == 5) f = CreateFileA(w, GENERIC_READ, 7, 0, map[disp < 6 ? disp : 1], 0x02000080, 0);
    out("  open "); out(oa->name ? "" : "(null)"); { char t[200]; u32 l = oa->name ? oa->name->len : 0; if (l > 190) l = 190; memcpy_(t, oa->name ? oa->name->buf : "", l); t[l] = 0; out(t); }
    out(" -> "); out(w); out(f == INVALID_HANDLE ? "  NOT FOUND" : ""); { IMP DWORD WINAPI GetCurrentThreadId(void); out("  [t"); dec(GetCurrentThreadId()); out("]\r\n"); }
    if (f == INVALID_HANDLE) { u32 st = err_status(); if (io) io->status = st; return st; }
    fh_add(f, w); *h = f;
    if (io) { io->status = 0; io->info = created ? 2 : 1; }
    return ST_OK;
}
static s32 NTAPI NtCreateFile(HANDLE *h, u32 access, OATTR *oa, IOSB *io, s64 *alloc, u32 attrs, u32 share, u32 disp, u32 opts) { return open_file(h, access, oa, io, disp, opts); }
static s32 NTAPI NtOpenFile(HANDLE *h, u32 access, OATTR *oa, IOSB *io, u32 share, u32 opts) { return open_file(h, access, oa, io, 1, opts); }

static void seek(HANDLE h, s64 *off) { if (off && !(((u32 *)off)[1] == 0xFFFFFFFF && (((u32 *)off)[0] == 0xFFFFFFFE || ((u32 *)off)[0] == 0xFFFFFFFF))) SetFilePointerEx(h, *off, 0, 0); }
static s32 NTAPI NtReadFile(HANDLE h, HANDLE ev, void *apc, void *ctx, IOSB *io, void *buf, u32 len, s64 *off) {
    DWORD n = 0; FH *f = fh_get(h);
    if (f && f->dir) { memset_(buf, 0, len); io->status = 0; io->info = len; if (ev) SetEvent(ev); return ST_OK; }   /* raw volume read */
    seek(h, off);
    BOOL r = ReadFile(h, buf, len, &n, 0);
    u32 st = !r ? err_status() : (n == 0 && len) ? ST_EOF : ST_OK;
    { static u32 nl; u32 pl = f ? slen(f->path) : 0; int tgz = pl > 4 && f->path[pl - 1] == 'z' && f->path[pl - 2] == 'g' && f->path[pl - 3] == 't';
      if (!tgz && nl++ < 120) { out("  read "); out(f ? f->path + (slen(f->path) > 40 ? slen(f->path) - 40 : 0) : "?"); out(" len "); hex(len);
        out(" at "); if (off) hex(*(u32 *)off); else out("cur"); out(" -> "); hex(st); out(" got "); hex(n); out(ev ? " (event)" : ""); out(apc ? " (apc)" : ""); out("\r\n"); } }
    io->status = st; io->info = n; if (ev) SetEvent(ev); return st;
}
static s32 NTAPI NtWriteFile(HANDLE h, HANDLE ev, void *apc, void *ctx, IOSB *io, void *buf, u32 len, s64 *off) {
    DWORD n = 0; FH *f = fh_get(h);
    if (f && f->dir) { io->status = 0; io->info = len; if (ev) SetEvent(ev); return ST_OK; }   /* raw volume write: dropped */
    seek(h, off);
    BOOL r = WriteFile(h, buf, len, &n, 0);
    u32 st = r ? ST_OK : err_status(); io->status = st; io->info = n; if (ev) SetEvent(ev); return st;
}
static s32 NTAPI NtFlushBuffersFile(HANDLE h, IOSB *io) { FlushFileBuffers(h); io->status = 0; io->info = 0; return 0; }

/* FILE_NETWORK_OPEN_INFORMATION from Windows attribute data */
static void net_info(u32 *o, u32 *ad) {   /* ad: attrs, c(2), a(2), w(2), sizeHi, sizeLo */
    o[0] = ad[1]; o[1] = ad[2]; o[2] = ad[3]; o[3] = ad[4]; o[4] = ad[5]; o[5] = ad[6]; o[6] = ad[5]; o[7] = ad[6];
    o[8] = (ad[8] + 0xFFF) & ~0xFFF; o[9] = ad[7]; o[10] = ad[8]; o[11] = ad[7]; o[12] = ad[0]; o[13] = 0;
}
static s32 NTAPI NtQueryInformationFile(HANDLE h, IOSB *io, void *info, u32 len, u32 cls) {
    u32 bi[13]; memset_(info, 0, len); io->status = 0; io->info = len;
    if (!GetFileInformationByHandle(h, bi)) { io->status = ST_FAIL; return ST_FAIL; }
    u32 *o = info; u32 isdir = bi[0] & 0x10;
    switch (cls) {
    case 4:  /* FileBasicInformation */ o[0] = bi[1]; o[1] = bi[2]; o[2] = bi[3]; o[3] = bi[4]; o[4] = bi[5]; o[5] = bi[6]; o[6] = bi[5]; o[7] = bi[6]; o[8] = bi[0]; break;
    case 5:  /* FileStandardInformation */ o[0] = (bi[9] + 0xFFF) & ~0xFFF; o[1] = bi[8]; o[2] = bi[9]; o[3] = bi[8]; o[4] = 1; ((u8 *)o)[21] = isdir ? 1 : 0; break;
    case 14: /* FilePositionInformation */ { s64 p; SetFilePointerEx(h, 0, &p, 1); *(s64 *)o = p; break; }
    case 34: /* FileNetworkOpenInformation */ { u32 ad[9] = {bi[0], bi[1], bi[2], bi[3], bi[4], bi[5], bi[6], bi[8], bi[9]}; net_info(o, ad); break; }
    default: out("  NtQueryInformationFile class "); dec(cls); out(" (zeroed)\r\n");
    }
    return ST_OK;
}
static s32 NTAPI NtSetInformationFile(HANDLE h, IOSB *io, void *info, u32 len, u32 cls) {
    io->status = 0; io->info = 0;
    if (cls == 14) SetFilePointerEx(h, *(s64 *)info, 0, 0);
    else if (cls == 20 || cls == 19) { s64 p; SetFilePointerEx(h, 0, &p, 1); SetFilePointerEx(h, *(s64 *)info, 0, 0); SetEndOfFile(h); SetFilePointerEx(h, p, 0, 0); }
    else if (cls == 13) { FH *f = fh_get(h); if (f && *(u8 *)info) { out("  delete on close: "); out(f->path); out("\r\n"); } }
    else { out("  NtSetInformationFile class "); dec(cls); out(" ignored\r\n"); }
    return ST_OK;
}
static s32 NTAPI NtQueryFullAttributesFile(OATTR *oa, u32 *o) {
    char w[400]; u32 ad[9]; map_path(oa, w);
    if (!GetFileAttributesExA(w, 0, ad)) { u32 st = err_status(); static u32 nf; if (nf++ < 50) { out("  attributes "); out(w); out(" -> not found\r\n"); } return st; }
    net_info(o, ad); return ST_OK;
}
static s32 NTAPI NtQueryVolumeInformationFile(HANDLE h, IOSB *io, void *info, u32 len, u32 cls) {
    u32 *o = info; memset_(info, 0, len); io->status = 0; io->info = len;
    if (cls == 3 && len >= 24) { o[0] = 0x100000; o[2] = 0x80000; o[4] = 32; o[5] = 512; }       /* FileFsSizeInformation: 4 GB, 2 GB free */
    else if (cls == 1 && len >= 18) { o[2] = 0x12345678; }                                       /* FileFsVolumeInformation: serial */
    else if (cls == 4 && len >= 8) { o[0] = 7; }                                                 /* FileFsDeviceInformation: disk */
    else { out("  NtQueryVolumeInformationFile class "); dec(cls); out("\r\n"); }
    return ST_OK;
}
static s32 NTAPI NtQueryDirectoryFile(HANDLE h, HANDLE ev, void *apc, void *ctx, IOSB *io, void *info, u32 len, u32 cls, OSTR *mask, u8 restart) {
    FH *f = fh_get(h); u32 fd[80];
    if (!f) { io->status = ST_FAIL; return ST_FAIL; }
    BOOL ok;
    if (!f->find || restart) {
        if (f->find) FindClose(f->find);
        char p[400]; p[0] = 0; scat(p, f->path); scat(p, "\\");
        if (mask && mask->len) { u32 n = slen(p); memcpy_(p + n, mask->buf, mask->len); p[n + mask->len] = 0; } else scat(p, "*");
        f->find = FindFirstFileA(p, fd); ok = f->find != INVALID_HANDLE;
        if (!ok) { f->find = 0; io->status = ST_NO_SUCH_FILE; return ST_NO_SUCH_FILE; }
    } else ok = FindNextFileA(f->find, fd);
    while (ok && ((char *)fd)[44] == '.' && (((char *)fd)[45] == 0 || (((char *)fd)[45] == '.' && ((char *)fd)[46] == 0))) ok = FindNextFileA(f->find, fd);
    if (!ok) { io->status = ST_NO_MORE_FILES; return ST_NO_MORE_FILES; }
    const char *nm = (char *)fd + 44; u32 nl = slen(nm); u32 *o = info;
    memset_(info, 0, len < 0x40 ? len : 0x40);
    o[2] = fd[1]; o[3] = fd[2]; o[4] = fd[3]; o[5] = fd[4]; o[6] = fd[5]; o[7] = fd[6]; o[8] = fd[5]; o[9] = fd[6];
    o[10] = fd[8]; o[11] = fd[7]; o[12] = (fd[8] + 0xFFF) & ~0xFFF; o[13] = fd[7]; o[14] = fd[0]; o[15] = nl;
    if (0x40 + nl > len) nl = len - 0x40;
    memcpy_((u8 *)info + 0x40, nm, nl);
    io->status = 0; io->info = 0x40 + nl; if (ev) SetEvent(ev); return ST_OK;
}
static s32 NTAPI NtDeleteFile(OATTR *oa) { char w[400]; map_path(oa, w); out("  delete "); out(w); out("\r\n"); return DeleteFileA(w) ? ST_OK : err_status(); }
static s32 NTAPI NtFsControlFile(HANDLE h, HANDLE ev, void *apc, void *ctx, IOSB *io, u32 code, void *in, u32 inl, void *o, u32 outl) {
    out("  NtFsControlFile "); hex(code); out("\r\n"); if (o) memset_(o, 0, outl); io->status = 0; io->info = 0; return ST_OK;
}
static s32 NTAPI NtDeviceIoControlFile(HANDLE h, HANDLE ev, void *apc, void *ctx, IOSB *io, u32 code, void *in, u32 inl, void *o, u32 outl) {
    out("  NtDeviceIoControlFile "); hex(code); out("\r\n");
    if (o) memset_(o, 0, outl);
    if (code == 0x74004 && outl >= 0x20) { ((u32 *)o)[2] = 0x2EE00000; ((u32 *)o)[5] = 1; ((u8 *)o)[25] = 1; }   /* partition info: 750 MB */
    if (code == 0x70000 && outl >= 0x18) { ((u32 *)o)[0] = 1000; ((u32 *)o)[2] = 12; ((u32 *)o)[3] = 255; ((u32 *)o)[4] = 63; ((u32 *)o)[5] = 512; }
    io->status = 0; io->info = outl; return ST_OK;
}

/* ---- symbolic links ---- */
static void ostr_copy(char *d, u32 max, OSTR *s) { u32 n = s->len < max - 1 ? s->len : max - 1; memcpy_(d, s->buf, n); d[n] = 0; }
static s32 NTAPI IoCreateSymbolicLink(OSTR *link, OSTR *dev) {
    for (u32 i = 0; i < 16; i++) if (!g_sl[i].link[0]) { ostr_copy(g_sl[i].link, 64, link); ostr_copy(g_sl[i].target, 128, dev);
        out("  link "); out(g_sl[i].link); out(" -> "); out(g_sl[i].target); out("\r\n"); return ST_OK; }
    return ST_FAIL;
}
static s32 NTAPI IoDeleteSymbolicLink(OSTR *link) {
    char l[64]; ostr_copy(l, 64, link);
    for (u32 i = 0; i < 16; i++) if (g_sl[i].link[0] && prefix(g_sl[i].link, slen(g_sl[i].link), l) && slen(l) == slen(g_sl[i].link)) { g_sl[i].link[0] = 0; return ST_OK; }
    return ST_NOT_FOUND;
}
static s32 NTAPI NtOpenSymbolicLinkObject(HANDLE *h, OATTR *oa) {
    char l[64]; ostr_copy(l, 64, oa->name);
    for (u32 i = 0; i < 16; i++) if (g_sl[i].link[0] && prefix(g_sl[i].link, slen(g_sl[i].link), l) && slen(l) == slen(g_sl[i].link)) { *h = (HANDLE)(0x7F000000 + i); return ST_OK; }
    if (prefix(l, slen(l), "\\??\\D:") && slen(l) == 6) { *h = (HANDLE)0x7F0000FF; return ST_OK; }
    return ST_NOT_FOUND;
}
static s32 NTAPI NtQuerySymbolicLinkObject(HANDLE h, OSTR *t, u32 *ret) {
    u32 i = (u32)h - 0x7F000000; const char *s = i == 0xFF ? "\\Device\\CdRom0" : i < 16 ? g_sl[i].target : "";
    u32 n = slen(s); if (n > t->max) n = t->max; memcpy_(t->buf, s, n); t->len = n; if (ret) *ret = n; return ST_OK;
}

/* ---- dispatcher objects: waits poll the SignalState at +4 ---- */
typedef struct { u8 type, absolute, size, inserted; s32 signal; u32 wl[2]; } DHDR;
static int try_take(DHDR *o) {
    if ((u32)o < 0x10000) return WaitForSingleObject((HANDLE)o, 0) == 0;   /* a Windows handle passed as an object */
    for (;;) {
        s32 s = o->signal; if (s <= 0) return 0;
        s32 n = (o->type == 1 || o->type == 9 || o->type == 2 || o->type == 5) ? s - 1 : s;
        if (o->type == 1 || o->type == 9) n = 0;
        if (n == s || __sync_bool_compare_and_swap(&o->signal, s, n)) return 1;
    }
}
static s64 now100(void) { s64 c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return c / f * 10000000 + (c % f) * 10000000 / f; }
u64 g_kewait_cyc[8]; u32 g_kewait_n[8];   /* Ke waits by caller slot, for the status line */
static void *g_kewait_who[8];
static s32 wait_objs_(u32 n, DHDR **objs, u32 all, s64 *timeout, void *caller);
static s32 wait_objs(u32 n, DHDR **objs, u32 all, s64 *timeout, void *caller) {
    u64 t0 = __builtin_ia32_rdtsc(); s32 r = wait_objs_(n, objs, all, timeout, caller); u64 d = __builtin_ia32_rdtsc() - t0;
    u32 s = 0; for (; s < 8; s++) if (g_kewait_who[s] == caller || !g_kewait_who[s]) break;
    if (s < 8) { g_kewait_who[s] = caller; g_kewait_cyc[s] += d; g_kewait_n[s]++; }
    return r;
}
void kewait_report(void) {
    out("    Ke waits (caller: count, ms):"); for (u32 s = 0; s < 8 && g_kewait_who[s]; s++) { out(" "); hex((u32)g_kewait_who[s]); out(": "); dec(g_kewait_n[s]); out(", "); dec((u32)(g_kewait_cyc[s] / 3800000)); g_kewait_n[s] = 0; g_kewait_cyc[s] = 0; } out("\r\n");
}
static s32 wait_objs_(u32 n, DHDR **objs, u32 all, s64 *timeout, void *caller) {
    s64 end = 0; if (timeout) { if (*timeout < 0) end = now100() - *timeout; else { u64 t; GetSystemTimeAsFileTime(&t); end = now100() + (*timeout - (s64)t); } }
    for (u32 spin = 0;; spin++) {
        if (all) { u32 k = 0; for (; k < n; k++) if (objs[k]->signal <= 0 && (u32)objs[k] >= 0x10000) break; if (k == n) { for (k = 0; k < n; k++) try_take(objs[k]); return 0; } }
        else for (u32 k = 0; k < n; k++) if (try_take(objs[k])) return k;
        if (timeout && now100() >= end) return ST_TIMEOUT;
        Sleep(spin < 50 ? 0 : 1);
        if (spin == 2000) { static u32 nl; if (nl++ < 20) { out("  long Ke wait (2 s+) from "); hex((u32)caller); out(":"); for (u32 k = 0; k < n && k < 4; k++) { out(" "); hex((u32)objs[k]); if ((u32)objs[k] >= 0x10000) { out(" type "); dec(objs[k]->type); out(" signal "); dec(objs[k]->signal); } } out(timeout ? " (timeout)\r\n" : " (forever)\r\n"); } }
    }
}
static u32 g_waitlog;
static s32 NTAPI KeWaitForSingleObject(DHDR *o, u32 reason, u8 mode, u8 alertable, s64 *timeout) {
    if (g_waitlog++ < 20) { out("  KeWaitForSingleObject "); hex((u32)o); if ((u32)o >= 0x10000) { out(" type "); dec(o->type); out(" signal "); dec(o->signal); } out(timeout ? " (timeout)" : " (forever)"); out("\r\n"); }
    return wait_objs(1, &o, 0, timeout, __builtin_return_address(0));
}
static s32 NTAPI KeWaitForMultipleObjects(u32 n, DHDR **objs, u32 type, u32 reason, u8 mode, u8 alertable, s64 *timeout, void *wb) { return wait_objs(n, objs, type == 0, timeout, __builtin_return_address(0)); }
static s32 NTAPI KeSetEvent(DHDR *e, s32 incr, u8 wait) { s32 p = e->signal; e->signal = 1; return p; }

/* ---- DPCs and timers: run on the ticker thread ---- */
typedef struct { short type; u8 inserted, pad; u32 le[2]; void (NTAPI *routine)(void *, void *, void *, void *); void *ctx, *a1, *a2; } KDPC;
typedef struct { DHDR h; s64 due; u32 le[2]; KDPC *dpc; s32 period; } KTIMER;
static void NTAPI KeInitializeDpc(KDPC *d, void *routine, void *ctx) { memset_(d, 0, sizeof *d); d->type = 0x13; d->routine = routine; d->ctx = ctx; }
#define NQ 64
static KDPC *volatile g_dq[NQ]; static volatile u32 g_dlock;
static u8 NTAPI KeInsertQueueDpc(KDPC *d, void *a1, void *a2) {
    while (__sync_val_compare_and_swap(&g_dlock, 0, 1)) ;
    u8 r = 0;
    if (!d->inserted) for (u32 i = 0; i < NQ; i++) if (!g_dq[i]) { d->a1 = a1; d->a2 = a2; d->inserted = 1; g_dq[i] = d; r = 1; break; }
    g_dlock = 0; return r;
}
static u8 NTAPI KeRemoveQueueDpc(KDPC *d) {
    while (__sync_val_compare_and_swap(&g_dlock, 0, 1)) ;
    u8 r = 0; for (u32 i = 0; i < NQ; i++) if (g_dq[i] == d) { g_dq[i] = 0; d->inserted = 0; r = 1; }
    g_dlock = 0; return r;
}
static KTIMER *volatile g_tq[NQ];
static void NTAPI KeInitializeTimerEx(KTIMER *t, u32 type) { memset_(t, 0, sizeof *t); t->h.type = 8 + type; t->h.size = sizeof *t / 4; }
static u8 NTAPI KeCancelTimer(KTIMER *t) {
    while (__sync_val_compare_and_swap(&g_dlock, 0, 1)) ;
    u8 r = 0; for (u32 i = 0; i < NQ; i++) if (g_tq[i] == t) { g_tq[i] = 0; r = 1; }
    t->h.inserted = 0; g_dlock = 0; return r;
}
static u8 NTAPI KeSetTimerEx(KTIMER *t, s64 due, s32 period, KDPC *dpc) {
    u8 was = KeCancelTimer(t);
    if (due >= 0) { u64 st; GetSystemTimeAsFileTime(&st); due = -(due - (s64)st); if (due > 0) due = 0; }
    t->due = now100() - due; t->period = period; t->dpc = dpc; t->h.signal = 0; t->h.inserted = 1;
    while (__sync_val_compare_and_swap(&g_dlock, 0, 1)) ;
    for (u32 i = 0; i < NQ; i++) if (!g_tq[i]) { g_tq[i] = t; break; }
    g_dlock = 0; return was;
}
static u8 NTAPI KeSetTimer(KTIMER *t, s64 due, KDPC *dpc) { return KeSetTimerEx(t, due, 0, dpc); }
void kernel2_tick(void) {   /* called about once a millisecond by the ticker thread */
    KDPC *run[NQ]; u32 nr = 0; s64 now = now100();
    while (__sync_val_compare_and_swap(&g_dlock, 0, 1)) ;
    for (u32 i = 0; i < NQ; i++) {
        KTIMER *t = g_tq[i];
        if (t && now >= t->due) {
            t->h.signal = 1;
            if (t->dpc && !t->dpc->inserted) { t->dpc->a1 = (void *)(u32)now; t->dpc->a2 = (void *)(u32)(now >> 32); run[nr++] = t->dpc; }
            if (t->period) t->due += (s64)t->period * 10000; else { g_tq[i] = 0; t->h.inserted = 0; }
        }
    }
    for (u32 i = 0; i < NQ; i++) if (g_dq[i]) { run[nr++] = g_dq[i]; g_dq[i]->inserted = 0; g_dq[i] = 0; }
    g_dlock = 0;
    for (u32 i = 0; i < nr; i++) run[i]->routine(run[i], run[i]->ctx, run[i]->a1, run[i]->a2);
}

/* ---- interrupts: accepted, never fire (no real hardware) ---- */
static void NTAPI KeInitializeInterrupt(u32 *in, void *routine, void *ctx, u32 vector, u32 irql, u32 mode, u8 share) { in[0] = (u32)routine; in[1] = (u32)ctx; in[2] = vector; in[3] = irql; ((u8 *)in)[16] = 0; }
IMP BOOL WINAPI VirtualProtect(void *, u32, DWORD, DWORD *);
static u32 *g_intr[8];
static u8 NTAPI KeConnectInterrupt(u32 *in) {
    ((u8 *)in)[16] = 1;
    for (u32 i = 0; i < 8; i++) if (!g_intr[i] || g_intr[i] == in) { g_intr[i] = in; break; }
    out("  interrupt connected: vector "); hex(in[2]); out(" routine "); hex(in[0]); out("\r\n");
    return 1;
}
/* GPU vertical blank, 60 times a second: raise the CRTC interrupt and run the game's GPU interrupt routine
   (bus level 3 -> vector 0x33). Direct3D's routine then queues its DPC, which wakes anything waiting for vblank. */
void gpu_raise_irq(void) {   /* runs the game's GPU interrupt routine (on the ticker thread, like the vblank) */
    for (u32 i = 0; i < 8; i++) if (g_intr[i] && ((u8 *)g_intr[i])[16] && g_intr[i][2] == 0x33)
        ((u8 (NTAPI *)(void *, void *))g_intr[i][0])(g_intr[i], (void *)g_intr[i][1]);
}
/* any other device interrupt (Milestone 97: the network chip, vector 0x34). Its routine never runs twice at once, nor while
   the driver is inside KeSynchronizeExecution for the same interrupt (on the Xbox, raised IRQL keeps it out). */
static volatile u32 g_isr_lock;
void raise_vector(u32 vec) {
    for (u32 i = 0; i < 8; i++) if (g_intr[i] && ((u8 *)g_intr[i])[16] && g_intr[i][2] == vec) {
        while (__sync_lock_test_and_set(&g_isr_lock, 1)) Sleep(0);
        ((u8 (NTAPI *)(void *, void *))g_intr[i][0])(g_intr[i], (void *)g_intr[i][1]);
        __sync_lock_release(&g_isr_lock); }
}
u32 g_vblanks, g_vb_sent;
void vblank_tick(void) { REG(0x600100) |= 1; { extern volatile u32 g_flip_stall; g_flip_stall = 0; } gpu_raise_irq(); g_vblanks++;
    {   /* GPU path: tell the viewer when the shown surface changes (and every 100 ms, for CPU-drawn screens) */
        void render_gl_flip(u32); extern u32 g_flips_total; static u32 last, t_last; u32 gpu_display_addr(void); u32 a = gpu_display_addr(), t = GetTickCount();
        {   /* cadence probe (Milestone 86): per vblank, the shown buffer (A/B...) and whether its picture changed (upper
               case) or not (lower case); 240 vblanks are logged every 20 s */
            static char cad[244]; static u32 nc, lastsum, addrs[4], tprobe;
            u32 sum = 0; volatile u32 *p = (volatile u32 *)(0x80000000u + (a & 0x03FFFFFF)); for (u32 i = 0; i < 640 * 480; i += 4801) sum = sum * 31 + p[i];
            u32 k = 0; for (; k < 4 && addrs[k] && addrs[k] != a; k++) ; if (k < 4 && !addrs[k]) addrs[k] = a;
            if (t - tprobe >= 20000 || nc) { if (!nc) tprobe = t; cad[nc++] = (sum != lastsum ? 'A' : 'a') + (k < 4 ? k : 3);
                if (g_flips_total == last) cad[nc - 1] = '.';   /* no flip since the last vblank */
                if (nc == 240) { cad[nc] = 0; out("  [cadence] "); out(cad); out("\r\n"); nc = 0; } }
            lastsum = sum; }
        if (g_flips_total != last || t - t_last >= 100) { last = g_flips_total; t_last = t; render_gl_flip(a); g_vb_sent++; } } }
/* Audio (APU, 0xFE800000): plain memory, so the few status registers DirectSound polls are kept at "ready" values.
   0xFE820010 = voice processor PIO free space: DirectSound waits for >= 0x20 before each method (Milestone 76). */
#define APU(o) (*(volatile u32 *)(0xFE800000 + (o)))
void apu_tick(void) {
    APU(0x20010) = 0x80;
}
static u8 NTAPI KeDisconnectInterrupt(u32 *in) { ((u8 *)in)[16] = 0; for (u32 i = 0; i < 8; i++) if (g_intr[i] == in) g_intr[i] = 0; return 1; }
static u32 NTAPI HalGetInterruptVector(u32 level, u8 *irql) { if (irql) *irql = (u8)(26 - level); return level + 0x30; }
static u8 NTAPI KeSynchronizeExecution(u32 *in, u8 (NTAPI *routine)(void *), void *ctx) {
    if (!in || in[2] != 0x34) return routine(ctx);
    while (__sync_lock_test_and_set(&g_isr_lock, 1)) Sleep(0);   /* not while the network interrupt routine runs */
    u8 r = routine(ctx); __sync_lock_release(&g_isr_lock); return r; }
static s32 NTAPI KeSaveFloatingPointState(void *s) { return 0; }
static s32 NTAPI KeRestoreFloatingPointState(void *s) { return 0; }
static void NTAPI HalReadWritePCISpace(u32 bus, u32 slot, u32 reg, void *buf, u32 len, u8 write) {
    static u32 nl; if (nl < 30) { nl++; out("  HalReadWritePCISpace bus "); dec(bus); out(" slot "); hex(slot); out(" reg "); hex(reg); out(" len "); dec(len); out(write ? " write\r\n" : " read\r\n"); }
    if (write) return; memset_(buf, 0, len);
    if (bus == 0 && (slot & 0x1F) == 4 && net_enabled()) {   /* the network chip (device 4): nVidia MCPX NIC, registers at 0xFEF00000, interrupt 4 */
        u8 cfg[64]; memset_(cfg, 0, 64); *(u32 *)cfg = 0x01C310DE; *(u32 *)(cfg + 4) = 0x00B00007; *(u32 *)(cfg + 8) = 0x020000D1;
        *(u32 *)(cfg + 0x10) = 0xFEF00000; *(u32 *)(cfg + 0x14) = 0x0000E001; cfg[0x3C] = 4; cfg[0x3D] = 1;
        for (u32 i = 0; i < len && reg + i < 64; i++) ((u8 *)buf)[i] = cfg[reg + i]; }
}
static void NTAPI AvSendTVEncoderOption(void *regs, u32 opt, u32 param, u32 *result) { if (result) *result = 0; }
extern u32 g_av_pitch, g_av_format, g_av_fb;
static u32 NTAPI AvSetDisplayMode(void *regs, u32 step, u32 mode, u32 fmt, u32 pitch, u32 fb) {
    out("  AvSetDisplayMode mode "); hex(mode); out(" format "); hex(fmt); out(" pitch "); dec(pitch); out(" framebuffer "); hex(fb); out("\r\n");
    g_av_format = fmt; g_av_pitch = pitch; g_av_fb = fb;
    if (fb) REG(0x600800) = fb;   /* Milestone 86: like the hardware, the display starts at the frame buffer (the game's own CRTC write at 0x378344 is skipped at times; without this the window showed address 0) */
    return 0;
}
static u32 NTAPI PhyGetLinkState(u8 verify) { return net_link(); }   /* Milestone 97: with netplay.txt the System Link cable is plugged in */
static s32 NTAPI PhyInitialize(u8 force, void *p) { return 0; }

/* ---- I/O manager: there are no drivers, so these do nothing ---- */
static s32 NTAPI IoCreateDevice(void *drv, u32 ext, OSTR *name, u32 type, u8 excl, void **dev) {
    u8 *d = HeapAlloc(GetProcessHeap(), 8, 0x100 + ext); *(void **)(d + 0x18) = ext ? d + 0x100 : 0; *dev = d; return ST_OK;
}
static s32 NTAPI IoInvalidDeviceRequest(void *dev, void *irp) { return ST_INVALID_DEVICE_REQUEST; }
static s32 __fastcall IofCallDriver(void *dev, void *irp) { out("  IofCallDriver (no drivers)\r\n"); return ST_INVALID_DEVICE_REQUEST; }
static void __fastcall IofCompleteRequest(void *irp, u8 boost) { }
static void *NTAPI IoBuildSynchronousFsdRequest(u32 major, void *dev, void *buf, u32 len, s64 *off, void *ev, IOSB *io) { return 0; }
static void NTAPI IoStartPacket(void *dev, void *irp, u32 *key) { }
static void NTAPI IoStartNextPacket(void *dev) { }
static void NTAPI IoMarkIrpMustComplete(void *irp) { }
static s32 NTAPI ObReferenceObjectByName(OSTR *name, u32 attr, void *type, void *parse, void **obj) {
    char n[128]; ostr_copy(n, 128, name); out("  ObReferenceObjectByName "); out(n); out(" (not found)\r\n"); return ST_NOT_FOUND;
}
static void NTAPI HalInitiateShutdown(void) { out("HalInitiateShutdown - the game asked to power off\r\n"); log_flush(); STOP(0); }

/* ---- time ---- */
static void NTAPI RtlTimeToTimeFields(u64 *t, u16 *tf) {   /* TIME_FIELDS: Year Month Day Hour Minute Second Ms Weekday */
    u16 st[8]; FileTimeToSystemTime(t, st);
    tf[0] = st[0]; tf[1] = st[1]; tf[2] = st[3]; tf[3] = st[4]; tf[4] = st[5]; tf[5] = st[6]; tf[6] = st[7]; tf[7] = st[2];
}
static u8 NTAPI RtlTimeFieldsToTime(u16 *tf, u64 *t) { u16 st[8] = {tf[0], tf[1], 0, tf[2], tf[3], tf[4], tf[5], tf[6]}; return SystemTimeToFileTime(st, t) ? 1 : 0; }
static s32 NTAPI NtSetSystemTime(u64 *nt, u64 *old) { if (old) GetSystemTimeAsFileTime(old); return ST_OK; }

/* ---- crypto (SHA-1, HMAC-SHA1, RC4 real; the rest accept everything) ---- */
typedef struct { u32 h[5]; u32 lo, hi; u8 buf[64]; } SHA;
static u32 rol(u32 x, int n) { return (x << n) | (x >> (32 - n)); }
static void sha_block(SHA *c, const u8 *p) {
    u32 w[80], a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[i*4] << 24 | (u32)p[i*4+1] << 16 | (u32)p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    for (int i = 0; i < 80; i++) {
        u32 k, g;
        if (i < 20) { g = (b & d) | (~b & e); k = 0x5A827999; } else if (i < 40) { g = b ^ d ^ e; k = 0x6ED9EBA1; }
        else if (i < 60) { g = (b & d) | (b & e) | (d & e); k = 0x8F1BBCDC; } else { g = b ^ d ^ e; k = 0xCA62C1D6; }
        u32 t = rol(a, 5) + g + f + k + w[i]; f = e; e = d; d = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
}
static void NTAPI XcSHAInit(SHA *c) { c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0; c->lo = c->hi = 0; }
static void NTAPI XcSHAUpdate(SHA *c, const u8 *p, u32 n) {
    while (n--) { c->buf[c->lo & 63] = *p++; if ((++c->lo & 63) == 0) sha_block(c, c->buf); if (!c->lo) c->hi++; }
}
static void NTAPI XcSHAFinal(SHA *c, u8 *dig) {
    u64 bits = ((u64)c->hi << 32 | c->lo) * 8; u8 z = 0x80; XcSHAUpdate(c, &z, 1); z = 0;
    while ((c->lo & 63) != 56) XcSHAUpdate(c, &z, 1);
    for (int i = 7; i >= 0; i--) { u8 b = (u8)(bits >> (i * 8)); XcSHAUpdate(c, &b, 1); }
    for (int i = 0; i < 20; i++) dig[i] = (u8)(c->h[i / 4] >> (24 - (i % 4) * 8));
}
static void hmac_hex(const u8 *p, u32 n, u32 mx) { static const char hx[] = "0123456789ABCDEF"; for (u32 i = 0; i < n && i < mx; i++) { char t[4] = {' ', hx[p[i] >> 4], hx[p[i] & 15], 0}; out(t); } }
static void NTAPI XcHMAC(const u8 *key, u32 kl, const u8 *a, u32 al, const u8 *b, u32 bl, u8 *dig) {
    u8 k[64], pad[64], inner[20]; SHA c; memset_(k, 0, 64);
    if (kl > 64) { XcSHAInit(&c); XcSHAUpdate(&c, key, kl); XcSHAFinal(&c, k); } else memcpy_(k, key, kl);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    XcSHAInit(&c); XcSHAUpdate(&c, pad, 64); if (a) XcSHAUpdate(&c, a, al); if (b) XcSHAUpdate(&c, b, bl); XcSHAFinal(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5C;
    XcSHAInit(&c); XcSHAUpdate(&c, pad, 64); XcSHAUpdate(&c, inner, 20); XcSHAFinal(&c, dig);
    {   static u32 nl; if (nl < 400) { nl++;   /* Milestone 106: what the game signs / checks (System Link packets): the first 80 calls */
            out("  [hmac] from "); hex((u32)__builtin_return_address(0)); out(" key "); dec(kl); out(":"); hmac_hex(key, kl, 32);
            out(" | a "); dec(al); out(":"); if (a) hmac_hex(a, al, 96); out(" | b "); dec(bl); out(":"); if (b) hmac_hex(b, bl, 32);
            out(" | ="); hmac_hex(dig, 20, 20); out("\r\n"); } }
}
static void NTAPI XcRC4Key(u8 *s, u32 kl, const u8 *key) {
    for (int i = 0; i < 256; i++) s[i] = (u8)i;
    for (int i = 0, j = 0; i < 256; i++) { j = (j + s[i] + key[i % kl]) & 255; u8 t = s[i]; s[i] = s[j]; s[j] = t; }
    s[256] = s[257] = 0;
}
static void NTAPI XcRC4Crypt(u8 *s, u32 n, u8 *d) {
    u32 i = s[256], j = s[257];
    while (n--) { i = (i + 1) & 255; j = (j + s[i]) & 255; u8 t = s[i]; s[i] = s[j]; s[j] = t; *d++ ^= s[(s[i] + s[j]) & 255]; }
    s[256] = (u8)i; s[257] = (u8)j;
}
/* XcModExp (Milestone 97): a = b ^ c mod d, numbers of n little-endian dwords (at most 64). XNet's System Link key exchange
   (Diffie-Hellman) needs the real result: both consoles must arrive at the same shared key. Plain square-and-multiply with
   bit-serial reduction; a 768-bit exchange takes a fraction of a second, once per connection. */
#define BN 65
static int bn_ge(const u32 *a, const u32 *m, u32 n) { for (int i = (int)n - 1; i >= 0; i--) if (a[i] != m[i]) return a[i] > m[i]; return 1; }
static void bn_sub(u32 *a, const u32 *m, u32 n) { u32 br = 0; for (u32 i = 0; i < n; i++) { u64 t = (u64)a[i] - m[i] - br; a[i] = (u32)t; br = (u32)(t >> 63); } }
static void bn_mod(u32 *r, const u32 *t, u32 tn, const u32 *m, u32 n) {   /* r = t mod m (r has n words; m nonzero) */
    u32 x[BN]; memset_(x, 0, sizeof x);
    for (int w = (int)tn - 1; w >= 0; w--) for (int b = 31; b >= 0; b--) {
        u32 top = x[n - 1] >> 31; for (int i = (int)n - 1; i > 0; i--) x[i] = (x[i] << 1) | (x[i - 1] >> 31); x[0] = (x[0] << 1) | ((t[w] >> b) & 1); x[n] = (x[n] << 1) | top;
        if (x[n] || bn_ge(x, m, n)) { bn_sub(x, m, n); x[n] = 0; } }
    memcpy_(r, x, n * 4);
}
static void bn_mulmod(u32 *r, const u32 *a, const u32 *b, const u32 *m, u32 n) {
    u32 t[2 * BN]; memset_(t, 0, 2 * n * 4);
    for (u32 i = 0; i < n; i++) { u64 c = 0; for (u32 j = 0; j < n; j++) { c += (u64)a[i] * b[j] + t[i + j]; t[i + j] = (u32)c; c >>= 32; } t[i + n] = (u32)c; }
    bn_mod(r, t, 2 * n, m, n);
}
static u32 NTAPI XcModExp(u32 *a, u32 *b, u32 *c, u32 *d, u32 n) {
    if (!n || n > 64) { out("  XcModExp: size "); dec(n); out(" not supported\r\n"); return 0; }
    u32 base[BN], res[BN]; bn_mod(base, b, n, d, n); memset_(res, 0, sizeof res); res[0] = 1;
    if (n == 1 && d[0] == 1) res[0] = 0;
    int top = (int)n * 32 - 1; while (top >= 0 && !((c[top / 32] >> (top % 32)) & 1)) top--;
    for (int i = top; i >= 0; i--) { bn_mulmod(res, res, res, d, n); if ((c[i / 32] >> (i % 32)) & 1) bn_mulmod(res, res, base, d, n); }
    memcpy_(a, res, n * 4);
    static u32 nl; if (nl++ < 4) { out("  XcModExp: "); dec(n * 32); out("-bit\r\n"); }
    return 1;
}
static u8 NTAPI XcVerifyPKCS1Signature(void *hdr, void *pub, void *dig) { return 1; }
static void NTAPI XcDESKeyParity(u8 *k, u32 n) { for (u32 i = 0; i < n; i++) { u8 b = k[i] & 0xFE, p = 0; for (int j = 1; j < 8; j++) p ^= (b >> j) & 1; k[i] = b | (p ^ 1); } }
/* DES / 3DES: no encryption (the data is copied). Every console here runs this loader, so both ends agree. Logged once. */
static void NTAPI XcKeyTable(u32 cipher, void *table, void *key) { static u32 nl; if (!nl++) out("  XcKeyTable (no encryption)\r\n"); }
static void NTAPI XcBlockCryptCBC(u32 cipher, u32 n, void *in, void *o, void *table, u32 op, void *fb) { static u32 nl; if (!nl++) out("  XcBlockCryptCBC (copies only)\r\n"); if (o != in) memcpy_(o, in, n); }

typedef struct { const char *name; void *fn; } KFN;
const KFN g_impl2[] = {
#define F(n) { #n, (void *)n }
    F(NtCreateFile), F(NtOpenFile), F(NtReadFile), F(NtWriteFile), F(NtFlushBuffersFile), F(NtQueryInformationFile),
    F(NtSetInformationFile), F(NtQueryFullAttributesFile), F(NtQueryVolumeInformationFile), F(NtQueryDirectoryFile),
    F(NtDeleteFile), F(NtFsControlFile), F(NtDeviceIoControlFile),
    F(IoCreateSymbolicLink), F(IoDeleteSymbolicLink), F(NtOpenSymbolicLinkObject), F(NtQuerySymbolicLinkObject),
    F(KeWaitForSingleObject), F(KeWaitForMultipleObjects), F(KeSetEvent),
    F(KeInitializeDpc), F(KeInsertQueueDpc), F(KeRemoveQueueDpc),
    F(KeInitializeTimerEx), F(KeCancelTimer), F(KeSetTimer), F(KeSetTimerEx),
    F(KeInitializeInterrupt), F(KeConnectInterrupt), F(KeDisconnectInterrupt), F(HalGetInterruptVector), F(KeSynchronizeExecution),
    F(KeSaveFloatingPointState), F(KeRestoreFloatingPointState), F(HalReadWritePCISpace),
    F(AvSendTVEncoderOption), F(AvSetDisplayMode), F(PhyGetLinkState), F(PhyInitialize),
    F(IoCreateDevice), F(IoInvalidDeviceRequest), F(IofCallDriver), F(IofCompleteRequest), F(IoBuildSynchronousFsdRequest),
    F(IoStartPacket), F(IoStartNextPacket), F(IoMarkIrpMustComplete), F(ObReferenceObjectByName), F(HalInitiateShutdown),
    F(RtlTimeToTimeFields), F(RtlTimeFieldsToTime), F(NtSetSystemTime),
    F(XcSHAInit), F(XcSHAUpdate), F(XcSHAFinal), F(XcHMAC), F(XcRC4Key), F(XcRC4Crypt), F(XcModExp),
    F(XcVerifyPKCS1Signature), F(XcDESKeyParity), F(XcKeyTable), F(XcBlockCryptCBC),
    { 0, 0 }
#undef F
};

/* ---- stand-in for the Xbox hardware registers (Milestone 74) ----
 * 0xFD000000-0xFEFFFFFF (GPU, audio, USB...) becomes plain memory. The GPU "consumes" everything
 * Direct3D puts in its command buffer at once (GET = PUT), so the game runs without a picture. */
u32 gpu_init(void) {
    void *p = VirtualAlloc((void *)0xFD000000, 0x2000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) return 0;
    /* contiguous memory: one 64 MB block seen at 0x80000000 and again at 0xF0000000 (the Xbox's write-combined view) */
    /* read/write only (Milestone 77): mapped executable, the process was paused for seconds at a time */
    { HANDLE s = CreateFileMappingA(INVALID_HANDLE, 0, 0x04 /* PAGE_READWRITE */, 0, 0x4000000, "BruteForcePCLoader_Mem");   /* named: the viewer maps it (Milestone 79) */
      if (!s || !MapViewOfFileEx(s, 0x2 | 0x4 /* read, write */, 0, 0, 0x4000000, (void *)0x80000000)) return 0;
      if (!MapViewOfFileEx(s, 0x2 | 0x4, 0, 0, 0x4000000, (void *)0xF0000000)) out("  (no write-combined view at 0xF0000000)\r\n"); }
    REG(0x000000) = 0x02A000A1;   /* PMC_BOOT_0: NV2A A1 */
    REG(0x100200) = 0x03070103;   /* PFB_CFG0 */
    REG(0x10020C) = 0x04000000;   /* PFB_CSTATUS: 64 MB */
    REG(0x009200) = 1; REG(0x009210) = 1;   /* PTIMER numerator/denominator */
    REG(0x003214) = 0x10;                  /* PFIFO CACHE1_STATUS: empty (reg_rd makes it follow pending NOPs) */
    REG(0x002400) = 0x10;                  /* PFIFO RUNOUT_STATUS: empty */
    { void mmio_protect_all(void); mmio_protect_all(); }
    { extern volatile u32 g_mem_ready; g_mem_ready = 1; }
    return 1;
}
void gpu_tick(void) {
    REG(0x003214) |= 0x10;
    /* Brute Force's Direct3D device (pointer at 0x37DD88) waits for the GPU to report the last "fence"
       it reached; since every command is consumed at once, report the latest one issued. */
    { u32 dev = *(volatile u32 *)0x37DD88;
      (void)dev; }   /* fences are now written by the semaphore releases themselves (gpu.c, method 0x1D70) */
    if (REG(0x003244) != REG(0x003240)) REG(0x003244) = REG(0x003240);   /* PFIFO CACHE1 DMA_GET = DMA_PUT */
}

/* ---- I/O ports (called for the game's in/out instructions) ---- */
static u32 g_portlog;
u32 port_in(u32 port, u32 size) {
    if (g_portlog++ < 40) { out("  port in  "); hex(port); out("\r\n"); }
    return 0;
}
void port_out(u32 port, u32 size, u32 val) {
    if (g_portlog++ < 40) { out("  port out "); hex(port); out(" = "); hex(val); out("\r\n"); }
}


/* ---- GPU registers that do not behave like memory (Milestone 74) ----
 * Their pages are made inaccessible. When the game touches one, the faulting instruction is decoded
 * and carried out here against a shadow copy, applying the register's rules:
 *   interrupt status registers clear the bits written as 1; PMC_INTR_0 summarises the units' interrupts;
 *   the write-combine flush bit is done at once. No page is ever opened, so threads cannot race. */
IMP BOOL WINAPI VirtualProtect(void *, u32, DWORD, DWORD *);
static const u32 g_trap_pages[] = {0x000000, 0x002000, 0x009000, 0x100000, 0x400000, 0x600000, 0x003000, 0x1F00000 /* network chip 0xFEF00000 (Milestone 97, only with netplay.txt) */};
#define NTRAP 8
#define NIC_PAGE 0x1F00000
int net_enabled(void); u32 nic_rd(u32 off); void nic_wr(u32 off, u32 v, u32 mask); u32 net_link(void); void net_mac(u8 *m);
static u32 g_shadow[NTRAP][1024];
static int trap_index(u32 off) { for (int i = 0; i < NTRAP; i++) if (g_trap_pages[i] == (off & ~0xFFFu)) return (g_trap_pages[i] == NIC_PAGE && !net_enabled()) ? -1 : i; return -1; }
volatile u32 *regp(u32 off) { int i = trap_index(off); return i < 0 ? &NV(off) : &g_shadow[i][(off & 0xFFF) / 4]; }
void mmio_protect_all(void) { DWORD o; for (u32 i = 0; i < NTRAP; i++) if (g_trap_pages[i] != NIC_PAGE || net_enabled()) VirtualProtect((void *)(0xFD000000 + g_trap_pages[i]), 0x1000, 0x01 /* PAGE_NOACCESS */, &o); }
static int is_w1c(u32 off) { return off == 0x002100 || off == 0x009100 || off == 0x400100 || off == 0x600100; }
static u32 reg_rd(u32 off) {
    if ((off & ~0xFFFu) == NIC_PAGE) return nic_rd(off & 0xFFF);
    if (off == 0x000100) {   /* PMC_INTR_0 */
        u32 v = 0;
        if (REG(0x002100)) v |= 1 << 8;
        if (REG(0x400100)) v |= 1 << 12;
        if (REG(0x009100)) v |= 1 << 20;
        if (REG(0x600100)) v |= 1 << 24;
        return v;
    }
    if (off == 0x100410) return REG(off) & ~0x10000u;   /* write-combine flush: already done */
    /* PFIFO CACHE1 (Milestone 79): "busy" while a NOP software interrupt is pending, "empty" otherwise. Direct3D's
       interrupt routine only services PGRAPH while CACHE1 is busy, and loops until it reads empty. */
    if (off == 0x003214) return (REG(off) & ~0x10u) | ((REG(0x400100) & 0x00100000) ? 0 : 0x10);
    if (off == 0x003220) return REG(off) | ((REG(0x400100) & 0x00100000) ? 0x1000 : 0);
    if (off == 0x009400) { s64 c; QueryPerformanceCounter(&c); return (u32)(c << 5); }   /* PTIMER time, low */
    if (off == 0x009410) { s64 c; QueryPerformanceCounter(&c); return (u32)(c >> 27); }  /* PTIMER time, high */
    return REG(off);
}
static void reg_wr(u32 off, u32 v, u32 mask) {   /* mask: the bytes actually written */
    if ((off & ~0xFFFu) == NIC_PAGE) { nic_wr(off & 0xFFF, v, mask); return; }
    if (off == 0x000100) return;
    if (is_w1c(off)) { REG(off) &= ~(v & mask); return; }
    REG(off) = (REG(off) & ~mask) | (v & mask);
}
static u32 rd_sz(u32 addr, u32 sz) { u32 off = addr - 0xFD000000, sh = (off & 3) * 8; u32 v = reg_rd(off & ~3u) >> sh; return sz == 4 ? v : v & ((1u << (sz * 8)) - 1); }
static void wr_sz(u32 addr, u32 sz, u32 v) {
    u32 off = addr - 0xFD000000, sh = (off & 3) * 8, m = sz == 4 ? 0xFFFFFFFF : ((1u << (sz * 8)) - 1);
    reg_wr(off & ~3u, (v & m) << sh, m << sh);
}
static const u8 g_ro[8] = {0xB0, 0xAC, 0xA8, 0xA4, 0xC4, 0xB4, 0xA0, 0x9C};   /* CONTEXT offsets of eax..edi */
static u32 greg(u32 *c, int r, u32 sz) {
    if (sz == 1) return r < 4 ? c[g_ro[r] / 4] & 0xFF : (c[g_ro[r - 4] / 4] >> 8) & 0xFF;
    u32 v = c[g_ro[r] / 4]; return sz == 2 ? v & 0xFFFF : v;
}
static void sreg(u32 *c, int r, u32 sz, u32 v) {
    if (sz == 1) { if (r < 4) c[g_ro[r] / 4] = (c[g_ro[r] / 4] & ~0xFFu) | (v & 0xFF); else c[g_ro[r - 4] / 4] = (c[g_ro[r - 4] / 4] & ~0xFF00u) | ((v & 0xFF) << 8); }
    else if (sz == 2) c[g_ro[r] / 4] = (c[g_ro[r] / 4] & ~0xFFFFu) | (v & 0xFFFF);
    else c[g_ro[r] / 4] = v;
}
static void set_flags(u32 *c, u32 res, u32 sz, int cf, int of) {
    u32 m = sz == 4 ? 0xFFFFFFFF : ((1u << (sz * 8)) - 1); res &= m;
    u32 f = c[0xC0 / 4] & ~(0x1u | 0x4 | 0x40 | 0x80 | 0x800);
    u8 b = (u8)res; b ^= b >> 4; b ^= b >> 2; b ^= b >> 1;
    if (!(b & 1)) f |= 0x4; if (!res) f |= 0x40; if (res >> (sz * 8 - 1)) f |= 0x80; if (cf) f |= 1; if (of) f |= 0x800;
    c[0xC0 / 4] = f;
}
/* op: 0 add, 1 or, 4 and, 5 sub, 6 xor, 7 cmp. Returns the result; sets flags. */
static u32 alu(u32 *c, int op, u32 a, u32 b, u32 sz) {
    u32 m = sz == 4 ? 0xFFFFFFFF : ((1u << (sz * 8)) - 1), sb = 1u << (sz * 8 - 1), r;
    a &= m; b &= m;
    switch (op) {
    case 0: r = (a + b) & m; set_flags(c, r, sz, r < a, !((a ^ b) & sb) && ((a ^ r) & sb)); return r;
    case 1: r = a | b; set_flags(c, r, sz, 0, 0); return r;
    case 4: r = a & b; set_flags(c, r, sz, 0, 0); return r;
    case 6: r = a ^ b; set_flags(c, r, sz, 0, 0); return r;
    default: r = (a - b) & m; set_flags(c, r, sz, a < b, ((a ^ b) & sb) && ((a ^ r) & sb)); return r;
    }
}
static u32 g_unknown_ops;
int mmio_fault(u32 addr, u32 *c) {
    if (addr < 0xFD000000 || addr >= 0xFF000000 || trap_index(addr - 0xFD000000) < 0) return 0;
    u8 *ip = (u8 *)c[0xB8 / 4], *p = ip; u32 sz = 4; int op2 = 0;
    if (*p == 0x66) { sz = 2; p++; }
    u8 op = *p++;
    if (op == 0xA1 || op == 0xA3) {   /* mov eax,[moffs] / mov [moffs],eax */
        if (op == 0xA1) sreg(c, 0, sz, rd_sz(addr, sz)); else wr_sz(addr, sz, greg(c, 0, sz));
        c[0xB8 / 4] = (u32)(p + 4); return 1;
    }
    if (op == 0x0F) op2 = *p++;
    u8 modrm = *p++; int mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
    if (mod != 3) {
        if (rm == 4) { u8 sib = *p++; if (mod == 0 && (sib & 7) == 5) p += 4; }
        if (mod == 0 && rm == 5) p += 4; else if (mod == 1) p += 1; else if (mod == 2) p += 4;
    }
    u32 v;
    if (op2) {
        if (op2 == 0xB6) { sreg(c, reg, 4, rd_sz(addr, 1)); goto done; }
        if (op2 == 0xB7) { sreg(c, reg, 4, rd_sz(addr, 2)); goto done; }
        goto unknown;
    }
    switch (op) {
    case 0x8B: sreg(c, reg, sz, rd_sz(addr, sz)); break;
    case 0x8A: sreg(c, reg, 1, rd_sz(addr, 1)); break;
    case 0x89: wr_sz(addr, sz, greg(c, reg, sz)); break;
    case 0x88: wr_sz(addr, 1, greg(c, reg, 1)); break;
    case 0xC7: if (reg) goto unknown; v = sz == 2 ? *(u16 *)p : *(u32 *)p; p += sz; wr_sz(addr, sz, v); break;
    case 0xC6: if (reg) goto unknown; wr_sz(addr, 1, *p++); break;
    case 0x85: alu(c, 4, rd_sz(addr, sz), greg(c, reg, sz), sz); break;
    case 0x84: alu(c, 4, rd_sz(addr, 1), greg(c, reg, 1), 1); break;
    case 0xF7: if (reg) goto unknown; v = sz == 2 ? *(u16 *)p : *(u32 *)p; p += sz; alu(c, 4, rd_sz(addr, sz), v, sz); break;
    case 0xF6: if (reg) goto unknown; alu(c, 4, rd_sz(addr, 1), *p++, 1); break;
    case 0x81: case 0x83: case 0x80: {
        u32 s = op == 0x80 ? 1 : sz;
        if (op == 0x81) { v = sz == 2 ? *(u16 *)p : *(u32 *)p; p += sz; } else v = op == 0x83 ? (u32)(s32)(signed char)*p++ : *p++;
        if (reg == 2 || reg == 3) goto unknown;
        u32 r = alu(c, reg, rd_sz(addr, s), v, s); if (reg != 7) wr_sz(addr, s, r); break; }
    case 0x01: case 0x09: case 0x21: case 0x29: case 0x31: case 0x39: {   /* op [m], r */
        static const int map[8] = {0, 1, -1, -1, 4, 5, 6, 7}; int o = map[op >> 3];
        u32 r = alu(c, o, rd_sz(addr, sz), greg(c, reg, sz), sz); if (o != 7) wr_sz(addr, sz, r); break; }
    case 0x03: case 0x0B: case 0x23: case 0x2B: case 0x33: case 0x3B: {   /* op r, [m] */
        static const int map[8] = {0, 1, -1, -1, 4, 5, 6, 7}; int o = map[op >> 3];
        u32 r = alu(c, o, greg(c, reg, sz), rd_sz(addr, sz), sz); if (o != 7) sreg(c, reg, sz, r); break; }
    default: goto unknown;
    }
done:
    c[0xB8 / 4] = (u32)p; return 1;
unknown:
    if (g_unknown_ops++ < 10) { out("  GPU register access by an instruction not emulated yet at "); hex((u32)ip); out(":"); for (int i = 0; i < 8; i++) { out(" "); hex(ip[i]); } out("\r\n"); }
    return 0;
}
int mmio_step(u32 *c) { return 0; }

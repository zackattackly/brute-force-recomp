/* bflist.exe - the match-list helper (Milestone 98b), started by the loader when netplay.txt has "list <url>".
 *
 * WinHTTP cannot run inside the loader (it reads the program's own header, where the game's Xbox header sits), so this small
 * normal process talks to the match list (a Cloudflare Worker) and to the loader over UDP on this PC:
 *   loader -> helper (127.0.0.1:<game port - 1>):  "BFNH" + hosting (u32)          once a second
 *   helper -> loader (127.0.0.1:<game port>):      "BFNL" + our ip + n + n x (ip, port)   after each list refresh (5 s)
 * While the loader says it hosts, the helper registers the match (/host); when it stops, /stop. It ends with the loader.
 * Command line: bflist.exe <list host> <path prefix or -> <room> <game port> <our ip or 0> <loader pid> */
typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef unsigned char u8; typedef int BOOL; typedef void *HANDLE; typedef u32 DWORD;
#define WINAPI __stdcall
#define K32 __declspec(dllimport)
K32 void WINAPI Sleep(DWORD);
K32 DWORD WINAPI GetTickCount(void);
K32 void *WINAPI LoadLibraryA(const char *);
K32 void *WINAPI GetProcAddress(void *, const char *);
K32 void WINAPI ExitProcess(u32);
K32 char *WINAPI GetCommandLineA(void);
K32 HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD);
K32 DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);

typedef struct { u16 fam, port; u32 addr; u8 zero[8]; } SIN;
static int (WINAPI *p_WSAStartup)(u16, void *); static u32 (WINAPI *p_socket)(int, int, int); static int (WINAPI *p_bind)(u32, const SIN *, int);
static int (WINAPI *p_sendto)(u32, const void *, int, int, const SIN *, int); static int (WINAPI *p_recvfrom)(u32, void *, int, int, SIN *, int *);
static int (WINAPI *p_ioctlsocket)(u32, u32, u32 *);
static void *(WINAPI *w_open)(const u16 *, u32, const u16 *, const u16 *, u32); static void *(WINAPI *w_connect)(void *, const u16 *, u16, u32);
static void *(WINAPI *w_request)(void *, const u16 *, const u16 *, const u16 *, const u16 *, const u16 **, u32);
static BOOL (WINAPI *w_send)(void *, const u16 *, u32, void *, u32, u32, u32); static BOOL (WINAPI *w_receive)(void *, void *);
static BOOL (WINAPI *w_read)(void *, void *, u32, u32 *); static BOOL (WINAPI *w_close)(void *); static BOOL (WINAPI *w_timeouts)(void *, int, int, int, int);
static void *g_ses; static char g_host[128], g_path[128];
static u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
static void wide(u16 *d, const char *s, u32 cap) { u32 k = 0; while (s[k] && k < cap - 1) { d[k] = (u8)s[k]; k++; } d[k] = 0; }
static int get(const char *path, char *o, u32 cap) {
    u16 wh[128], wp[256]; static const u16 GET[] = {'G','E','T',0}; char full[256]; u32 k = 0;
    for (const char *s = g_path; *s && k < 200; s++) full[k++] = *s; for (const char *s = path; *s && k < 250; s++) full[k++] = *s; full[k] = 0;
    wide(wh, g_host, 128); wide(wp, full, 256); int got = -1; void *c = w_connect(g_ses, wh, 443, 0);
    if (c) { void *r = w_request(c, GET, wp, 0, 0, 0, 0x00800000);
        if (r) { if (w_send(r, 0, 0, 0, 0, 0, 0) && w_receive(r, 0)) { u32 n = 0, t = 0; while (t + 1 < cap && w_read(r, o + t, cap - 1 - t, &n) && n) t += n; o[t] = 0; got = (int)t; } w_close(r); }
        w_close(c); }
    return got; }
static int dig(char c) { return c >= '0' && c <= '9'; }
static u32 num(const char **p) { u32 v = 0; while (dig(**p)) v = v * 10 + (u32)(*(*p)++ - '0'); return v; }
static u32 ip4(const char **p) { u32 ip = 0; for (int k = 0; k < 4; k++) { if (!dig(**p)) return 0; u32 v = num(p); if (v > 255) return 0; ip |= v << (k * 8); if (k < 3) { if (**p != '.') return 0; (*p)++; } } return ip; }
static void put(char **w, const char *s) { while (*s) *(*w)++ = *s++; }
static void putn(char **w, u32 v) { char d[12]; int z = 0; do d[z++] = (char)('0' + v % 10); while (v /= 10); while (z) *(*w)++ = d[--z]; }
static void putip(char **w, u32 ip) { for (int k = 0; k < 4; k++) { putn(w, (ip >> (k * 8)) & 255); if (k < 3) *(*w)++ = '.'; } }
static const char *word(const char *c, char *o, u32 cap) { while (*c == ' ') c++; u32 k = 0; int q = *c == '"'; if (q) c++; while (*c && (q ? *c != '"' : *c != ' ') && k < cap - 1) o[k++] = *c++; if (q && *c) c++; o[k] = 0; return c; }

void WINAPI start(void) {
    const char *c = GetCommandLineA(); char a[6][128]; c = word(c, a[0], 128);   /* skip the program name */
    for (int i = 0; i < 6; i++) c = word(c, a[i], 128);
    { u32 k = 0; for (; a[0][k] && k < 127; k++) g_host[k] = a[0][k]; g_host[k] = 0; k = 0; if (a[1][0] != '-') for (; a[1][k] && k < 127; k++) g_path[k] = a[1][k]; g_path[k] = 0; }
    const char *p; p = a[2]; u32 room = num(&p); p = a[3]; u32 port = num(&p); p = a[4]; u32 myip = ip4(&p); p = a[5]; u32 pid = num(&p);
    if (!g_host[0] || !port) ExitProcess(1);
    HANDLE parent = OpenProcess(0x00100000 /* SYNCHRONIZE */, 0, pid);
    void *ws = LoadLibraryA("ws2_32.dll"), *wh = LoadLibraryA("winhttp.dll"); if (!ws || !wh) ExitProcess(2);
    #define GP(m, v, n) *(void **)&v = GetProcAddress(m, n)
    GP(ws, p_WSAStartup, "WSAStartup"); GP(ws, p_socket, "socket"); GP(ws, p_bind, "bind"); GP(ws, p_sendto, "sendto"); GP(ws, p_recvfrom, "recvfrom"); GP(ws, p_ioctlsocket, "ioctlsocket");
    GP(wh, w_open, "WinHttpOpen"); GP(wh, w_connect, "WinHttpConnect"); GP(wh, w_request, "WinHttpOpenRequest"); GP(wh, w_send, "WinHttpSendRequest");
    GP(wh, w_receive, "WinHttpReceiveResponse"); GP(wh, w_read, "WinHttpReadData"); GP(wh, w_close, "WinHttpCloseHandle"); GP(wh, w_timeouts, "WinHttpSetTimeouts");
    #undef GP
    static u8 wsa[512]; p_WSAStartup(0x0202, wsa);
    u32 s = p_socket(2, 2, 17); SIN me = {2, bswap16((u16)(port - 1)), 0x0100007F}; if (s == 0xFFFFFFFF || p_bind(s, &me, sizeof me)) ExitProcess(3);
    u32 one = 1; p_ioctlsocket(s, 0x8004667E, &one);
    SIN loader = {2, bswap16((u16)port), 0x0100007F};
    static const u16 agent[] = {'B','r','u','t','e','F','o','r','c','e','R','e','c','o','m','p',0};
    g_ses = w_open(agent, 0, 0, 0, 0); if (!g_ses) ExitProcess(4); if (w_timeouts) w_timeouts(g_ses, 5000, 5000, 5000, 8000);
    u32 hosting = 0, registered = 0, last = 0, heard = GetTickCount(); static char o[4096];
    if (!myip) {   /* our internet IPv4 address: Cloudflare's 1.1.1.1 is reached over IPv4 only and says which address asked
                      (the list may see us over IPv6, which the game cannot use) */
        char sh[128], sp[128]; for (int k = 0; k < 128; k++) { sh[k] = g_host[k]; sp[k] = g_path[k]; }
        const char *t = "1.1.1.1"; int k = 0; while (t[k]) { g_host[k] = t[k]; k++; } g_host[k] = 0; g_path[0] = 0;
        if (get("/cdn-cgi/trace", o, sizeof o) > 0) { const char *r = o; while (*r) { if (r[0] == 'i' && r[1] == 'p' && r[2] == '=') { r += 3; myip = ip4(&r); break; } while (*r && *r != '\n') r++; if (*r) r++; } }
        for (int j = 0; j < 128; j++) { g_host[j] = sh[j]; g_path[j] = sp[j]; } }
    for (;;) {
        if (parent && WaitForSingleObject(parent, 0) == 0) break;   /* the game ended */
        u8 b[64]; SIN f; int fl = sizeof f; int n;
        while ((n = p_recvfrom(s, b, sizeof b, 0, &f, &fl)) >= 8) if (*(u32 *)b == 0x484E4642 /* BFNH */) { hosting = *(u32 *)(b + 4); heard = GetTickCount(); }
        u32 now = GetTickCount();
        if (now - heard > 20000) break;   /* no word from the loader for 20 s */
        if (now - last >= 5000 || (hosting && !registered)) { last = now; char q[192], *w;
            if (hosting) { w = q; put(&w, "/host?room="); putn(&w, room); put(&w, "&port="); putn(&w, port); if (myip) { put(&w, "&ip="); putip(&w, myip); } *w = 0;
                if (get(q, o, sizeof o) > 3 && o[0] == 'o' && o[1] == 'k') { const char *r = o + 2; while (*r == ' ') r++; u32 ip = ip4(&r); if (ip) myip = ip; registered = 1; } }
            else if (registered) { w = q; put(&w, "/stop?port="); putn(&w, port); if (myip) { put(&w, "&ip="); putip(&w, myip); } *w = 0; get(q, o, sizeof o); registered = 0; }
            w = q; put(&w, "/list?room="); putn(&w, room); *w = 0;
            if (get(q, o, sizeof o) >= 0) {   /* to the loader: BFNL, our ip, count, entries */
                static u32 m[3 + 2 * 32]; u32 k = 0; const char *r = o; m[0] = 0x4C4E4642; m[1] = myip;
                while (*r && k < 32) { u32 ip = ip4(&r); while (*r == ' ') r++; u32 pt = num(&r); if (ip && pt && pt < 65536) { m[3 + 2 * k] = ip; m[4 + 2 * k] = pt; k++; } while (*r && *r != '\n') r++; if (*r) r++; }
                m[2] = k; p_sendto(s, m, (int)(12 + 8 * k), 0, &loader, sizeof loader); } }
        Sleep(200);
    }
    if (registered) { char q[96], *w = q; put(&w, "/stop?port="); putn(&w, port); if (myip) { put(&w, "&ip="); putip(&w, myip); } *w = 0; get(q, o, sizeof o); }
    ExitProcess(0);
}

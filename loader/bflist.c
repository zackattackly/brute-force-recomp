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
static const char *word(const char *c, char *o, u32 cap) { while (*c == ' ') c++; u32 k = 0; int q = *c == '"'; if (q) c++; while (*c && (q ? *c != '"' : *c != ' ')) { if (k < cap - 1) o[k++] = *c; c++; } if (q && *c) c++; o[k] = 0; return c; }   /* (a word longer than cap is cut but skipped whole: a long program path shifted every argument) */

/* ---- Milestone 100: relay mode ----
 * bflist.exe relay <host> <path or -> <room> <game port> <loader pid>
 * The loader runs its "online" mode (BFN2 + room + frame, a keep-alive = no frame) against this helper on 127.0.0.1:<port - 1>;
 * the frames go over one WebSocket to wss://<host><path>/relay?room=R (a Durable Object that switches them between the
 * players of the room), and what comes back goes to the loader as BFN2 packets. An outgoing HTTPS connection: no router setup. */
K32 HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
K32 HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
K32 BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
K32 BOOL WINAPI CloseHandle(HANDLE);
static BOOL (WINAPI *w_option)(void *, u32, void *, u32); static void *(WINAPI *w_upgrade)(void *, u32);
static u32 (WINAPI *w_wsend)(void *, u32, void *, u32); static u32 (WINAPI *w_wrecv)(void *, void *, u32, u32 *, u32 *); static u32 (WINAPI *w_wclose)(void *, u16, void *, u32);
static int (WINAPI *p_select)(int, void *, void *, void *, const s32 *);
static volatile void *g_ws; static volatile u32 g_wlock, g_rx, g_tx, g_room, g_sock, g_txerr, g_ping_t, g_pong, g_pp_t; static SIN g_loader;
static void wl(void) { while (__sync_lock_test_and_set(&g_wlock, 1)) Sleep(0); } static void wu(void) { __sync_lock_release(&g_wlock); }
static void logl(const char *s) {   /* relay_log.txt next to the game: connects and disconnects only */
    HANDLE f = CreateFileA("relay_log.txt", 0x04 /* FILE_APPEND_DATA */, 1, 0, 4 /* OPEN_ALWAYS */, 0x80, 0); if (f == (HANDLE)-1) return;
    char b[160], *w = b; putn(&w, GetTickCount() / 1000); put(&w, " s: "); put(&w, s); put(&w, "\r\n"); DWORD n; WriteFile(f, b, (DWORD)(w - b), &n, 0); CloseHandle(f); }
K32 DWORD WINAPI GetLastError(void);
static char g_fail[96];   /* the step that failed and its Windows error, for the log */
static void fail_at(const char *step) { char *w = g_fail; put(&w, "relay connect failed at "); put(&w, step); put(&w, ", error "); putn(&w, GetLastError()); *w = 0; }
/* a watchdog for the connection steps: a step still running after 15 s (and after 60 s) is written to relay_log.txt, even if
   the call itself never returns (on one PC the helper hung without a trace) */
static volatile const char *g_step; static volatile u32 g_step_t;
static void step(const char *s) { g_step = s; g_step_t = s ? (GetTickCount() | 1) : 0; }
static u32 WINAPI watchdog(void *unused) {
    (void)unused; u32 told = 0, told_t = 0;
    for (;;) { Sleep(1000); u32 t0 = g_step_t; const char *s = (const char *)g_step; if (!t0 || !s) { told = 0; continue; }
        u32 age = GetTickCount() - t0; if (t0 != told_t) { told_t = t0; told = 0; }
        if ((age > 15000 && told == 0) || (age > 60000 && told == 1)) { told++; char b[120], *w = b; put(&w, "still waiting at "); put(&w, s); put(&w, " after "); putn(&w, age / 1000); put(&w, " s"); *w = 0; logl(b); } }
    return 0; }
static void *ws_connect(void) {
    /* every step has a time limit: on one PC the upgrade request was sent and no answer ever came (something between the
       PC and Cloudflare held it); with no receive limit the helper waited forever and never tried again */
    char q[192], *w = q; put(&w, g_path); put(&w, "/relay?room="); putn(&w, g_room); *w = 0;
    u16 wh[128], wp[256]; static const u16 GET[] = {'G','E','T',0}; wide(wh, g_host, 128); wide(wp, q, 256);
    step("connect"); void *c = w_connect(g_ses, wh, 443, 0); if (!c) { step(0); fail_at("connect"); return 0; }
    step("open request"); void *r = w_request(c, GET, wp, 0, 0, 0, 0x00800000 /* SECURE */); if (!r) { step(0); fail_at("open request"); w_close(c); return 0; }
    if (w_timeouts) w_timeouts(r, 8000, 8000, 8000, 10000);   /* resolve, connect, send, receive: the answer within 10 s */
    void *ws = 0;
    if (step("upgrade option"), !w_option(r, 114 /* WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET */, 0, 0)) fail_at("upgrade option");
    else if (step("send"), !w_send(r, 0, 0, 0, 0, 0, 0)) fail_at("send");
    else if (step("receive"), !w_receive(r, 0)) fail_at("receive (no answer from the server)");
    else if (step("upgrade"), !(ws = w_upgrade(r, 0))) fail_at("upgrade (the answer was not a WebSocket)");
    step(0);
    w_close(r); if (!ws) w_close(c);   /* (the connection handle stays open while the WebSocket lives) */
    else { u32 inf = 0; w_option(ws, 6 /* WINHTTP_OPTION_RECEIVE_TIMEOUT */, &inf, 4); }   /* connected: wait for frames as long as it takes */
    return ws; }
static u32 WINAPI rx_thread(void *unused) {   /* relay -> loader */
    (void)unused; static u8 b[2048];
    for (;;) {
        void *ws = (void *)g_ws; if (!ws) { Sleep(50); continue; }
        u32 n = 0, type = 0; u32 e = w_wrecv(ws, b + 8, sizeof b - 8, &n, &type);
        if (e == 12002 /* ERROR_WINHTTP_TIMEOUT: a quiet room, not a lost connection */) continue;
        if (e || type == 4 /* close */) { wl(); if (g_ws == ws) { g_ws = 0; w_close(ws); logl("relay connection lost"); } wu(); continue; }
        if (type == 0 && n == 4 && b[8] == 'P' && b[9] == 'O' && b[10] == 'N' && b[11] == 'G') {   /* the answer to our round-trip test */
            if (!g_pong && g_ping_t) { g_pong = 1; char t[96], *w = t; put(&w, "relay round trip ok ("); putn(&w, GetTickCount() - g_ping_t); put(&w, " ms): data flows both ways"); *w = 0; logl(t); }
            else if (g_pp_t) { u32 m[3] = {0x504E4642 /* "BFNP" */, GetTickCount() - g_pp_t, 1}; g_pp_t = 0; p_sendto(g_sock, m, 12, 0, &g_loader, sizeof g_loader); }   /* the game's performance panel: ping to the relay */
            continue; }
        if (type == 0 /* a whole binary message */ && n >= 14) { *(u32 *)b = 0x324E4642; *(u32 *)(b + 4) = g_room; p_sendto(g_sock, b, (int)n + 8, 0, &g_loader, sizeof g_loader); g_rx++; } }
    return 0; }
static void relay_main(HANDLE parent, u32 room, u32 port) {
    g_room = room;
    u32 s = p_socket(2, 2, 17); SIN me = {2, bswap16((u16)(port - 1)), 0x0100007F};
    if (s == 0xFFFFFFFF || p_bind(s, &me, sizeof me)) { logl("relay helper could not start: its port is in use (an old helper still running? restart the PC)"); ExitProcess(3); }
    { int big = 1 << 20; static int (WINAPI *so)(u32, int, int, const void *, int); void *ws2 = LoadLibraryA("ws2_32.dll"); *(void **)&so = GetProcAddress(ws2, "setsockopt"); if (so) { so(s, 0xFFFF, 0x1002, &big, 4); so(s, 0xFFFF, 0x1001, &big, 4); } }
    { u32 one = 1; p_ioctlsocket(s, 0x8004667E /* FIONBIO */, &one); }   /* non-blocking: the loop below drains, then waits in select */
    g_sock = s; SIN ld = {2, bswap16((u16)port), 0x0100007F}; g_loader = ld;
    u32 tid; CreateThread(0, 0, rx_thread, 0, 0, &tid); CreateThread(0, 0, watchdog, 0, 0, &tid);
    static u8 b[2048]; u32 heard = GetTickCount(), tryat = heard, ka = 0, fails = 0;   /* (tryat = now, not 0: (int)(now - 0) is negative once a PC has been on for 24.8 days - it never tried to connect) */
    logl("relay helper started (version 2026-11-17a)");
    for (;;) {
        if (parent && WaitForSingleObject(parent, 0) == 0) break;   /* the game ended */
        u32 now = GetTickCount();
        if (now - heard > 20000) break;                             /* no word from the loader for 20 s */
        if (!g_ws && (int)(now - tryat) >= 0) { void *ws = ws_connect();
            if (ws) { wl(); g_ws = ws; wu(); fails = 0; logl("relay connected");
                /* round-trip test: the relay answers "PING" with "PONG". A PC whose connection opens but whose data never
                   arrives (something between it and Cloudflare inspecting the traffic) shows no answer here */
                static u8 ping[4] = {'P', 'I', 'N', 'G'}; g_pong = 0; g_ping_t = GetTickCount() | 1; wl(); if (g_ws) w_wsend((void *)g_ws, 0, ping, 4); wu(); }
            else { fails++; tryat = now + (fails < 5 ? 1000 : 5000); if (fails <= 3 || fails % 12 == 0) logl(g_fail[0] ? g_fail : "relay connect failed (retrying)"); } }
        struct { u32 n; u32 s[1]; } rd = {1, {s}}; s32 tv[2] = {0, 50000};
        if (p_select(0, &rd, 0, 0, tv) > 0) for (int k = 0; k < 64; k++) {
            SIN f; int fl = sizeof f; int n = p_recvfrom(s, b, sizeof b, 0, &f, &fl);
            if (n < 8 || *(u32 *)b != 0x324E4642) break;
            heard = GetTickCount();
            if (n >= 8 + 14) { wl(); void *ws = (void *)g_ws; if (ws) { if (w_wsend(ws, 0 /* binary message */, b + 8, (u32)n - 8) == 0) g_tx++; else g_txerr++; } wu(); } }
        if (g_ping_t && !g_pong && (s32)(GetTickCount() - g_ping_t) > 6000) {   /* (the time now: `now` was read before connecting) */ g_ping_t = 0; logl("relay round trip FAILED: no answer in 6 s - connected, but data does not get through (antivirus web protection, a VPN or a firewall?)"); }
        {   static u32 tst, lrx = 0xFFFFFFFF, ltx = 0xFFFFFFFF, lerr; if (now - tst > 30000) { tst = now;   /* every 30 s while the numbers change: game data to and from the relay */
                if (g_rx != lrx || g_tx != ltx || g_txerr != lerr) { lrx = g_rx; ltx = g_tx; lerr = g_txerr; char t[120], *w = t; put(&w, "relay data: sent "); putn(&w, g_tx); put(&w, ", received "); putn(&w, g_rx);
                    if (g_txerr) { put(&w, ", send errors "); putn(&w, g_txerr); } *w = 0; logl(t); } } }
        {   static u32 tp; if (now - tp >= 2000) { tp = now;   /* every 2 s: a ping (answered by the relay) for the game's performance panel; not connected = report that */
                if (g_ws && g_pong) { static u8 ping[4] = {'P', 'I', 'N', 'G'}; g_pp_t = GetTickCount() | 1; wl(); if (g_ws) w_wsend((void *)g_ws, 0, ping, 4); wu(); }
                else { u32 m[3] = {0x504E4642, 0, 0}; p_sendto(g_sock, m, 12, 0, &g_loader, sizeof g_loader); } } }
        if (g_ws && now - ka > 20000) { ka = now; static u8 z[1]; wl(); if (g_ws) w_wsend((void *)g_ws, 0, z, 1); wu(); }   /* keep the connection warm */
    }
    /* no WebSocket close handshake: WinHttpWebSocketClose waits for the server's reply, which the receive thread would take
       (it hung here); ending the process drops the connection and the relay room forgets this player */
    logl("relay helper ended"); (void)w_wclose;
    ExitProcess(0); }

void WINAPI start(void) {
    const char *c = GetCommandLineA(); char a[6][128]; c = word(c, a[0], 128);   /* skip the program name */
    for (int i = 0; i < 6; i++) c = word(c, a[i], 128);
    if (a[0][0] == 'r' && a[0][1] == 'e' && a[0][2] == 'l' && a[0][3] == 'a' && a[0][4] == 'y' && !a[0][5]) {   /* relay <host> <path> <room> <port> <pid> */
        u32 k = 0; for (; a[1][k] && k < 127; k++) g_host[k] = a[1][k]; g_host[k] = 0; k = 0; if (a[2][0] != '-') for (; a[2][k] && k < 127; k++) g_path[k] = a[2][k]; g_path[k] = 0;
        const char *p = a[3]; u32 room = num(&p); p = a[4]; u32 port = num(&p); p = a[5]; u32 pid = num(&p);
        if (!g_host[0] || port < 2) ExitProcess(1);
        HANDLE parent = pid ? OpenProcess(0x00100000, 0, pid) : 0;
        void *ws = LoadLibraryA("ws2_32.dll"), *wh = LoadLibraryA("winhttp.dll"); if (!ws || !wh) ExitProcess(2);
        #define GP(m, v, n) *(void **)&v = GetProcAddress(m, n)
        GP(ws, p_WSAStartup, "WSAStartup"); GP(ws, p_socket, "socket"); GP(ws, p_bind, "bind"); GP(ws, p_sendto, "sendto"); GP(ws, p_recvfrom, "recvfrom"); GP(ws, p_select, "select"); GP(ws, p_ioctlsocket, "ioctlsocket");
        GP(wh, w_open, "WinHttpOpen"); GP(wh, w_connect, "WinHttpConnect"); GP(wh, w_request, "WinHttpOpenRequest"); GP(wh, w_send, "WinHttpSendRequest");
        GP(wh, w_receive, "WinHttpReceiveResponse"); GP(wh, w_close, "WinHttpCloseHandle"); GP(wh, w_timeouts, "WinHttpSetTimeouts"); GP(wh, w_option, "WinHttpSetOption");
        GP(wh, w_upgrade, "WinHttpWebSocketCompleteUpgrade"); GP(wh, w_wsend, "WinHttpWebSocketSend"); GP(wh, w_wrecv, "WinHttpWebSocketReceive"); GP(wh, w_wclose, "WinHttpWebSocketClose");
        #undef GP
        if (!w_upgrade || !w_wsend || !w_wrecv || !p_select) ExitProcess(5);   /* (WebSockets in WinHTTP: Windows 8 and later) */
        static u8 wsa[512]; p_WSAStartup(0x0202, wsa);
        static const u16 agent[] = {'B','r','u','t','e','F','o','r','c','e','R','e','c','o','m','p',0};
        g_ses = w_open(agent, 4 /* AUTOMATIC_PROXY: the system's own proxy settings, like a browser (Windows 8.1+) */, 0, 0, 0); if (!g_ses) g_ses = w_open(agent, 0, 0, 0, 0); if (!g_ses) ExitProcess(4); if (w_timeouts) w_timeouts(g_ses, 8000, 8000, 8000, 10000);   /* (each request sets its own; a connected WebSocket waits without a limit) */
        relay_main(parent, room, port); }
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

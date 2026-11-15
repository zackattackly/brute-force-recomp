/* bfserver.exe - the online server for Brute Force PC recomp (Milestone 98).
 *
 * Every player's loader sends its System Link traffic (Ethernet frames of the console's network chip, net.c) here over UDP. The
 * server keeps players in rooms and passes each frame to everyone else in the same room, like one System Link cable per room:
 * the game's own session list then shows every match hosted in that room (the server browser), and joining works as on a LAN.
 *   room 0          the public room: every match is listed for everyone
 *   room = hash     a private room: only players who typed the same password (netplay.txt "password ...") see its matches
 * Packets: "BFN2" + room (u32) + frame; an empty frame is a keep-alive (players send one a second; 15 s of silence = gone).
 *
 * Run: bfserver.exe [port]   (default 34567). The PC running it needs that UDP port forwarded to it on the router. */
typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef unsigned char u8; typedef int BOOL; typedef void *HANDLE; typedef u32 DWORD;
#define WINAPI __stdcall
#define K32 __declspec(dllimport)
K32 HANDLE WINAPI GetStdHandle(DWORD);
K32 BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
K32 void WINAPI Sleep(DWORD);
K32 DWORD WINAPI GetTickCount(void);
K32 void *WINAPI LoadLibraryA(const char *);
K32 void *WINAPI GetProcAddress(void *, const char *);
K32 void WINAPI ExitProcess(u32);
K32 char *WINAPI GetCommandLineA(void);

static HANDLE g_out;
static void out(const char *s) { DWORD n = 0, w; while (s[n]) n++; WriteFile(g_out, s, n, &w, 0); }
static void dec(u32 v) { char b[12]; int k = 0; do b[k++] = (char)('0' + v % 10); while (v /= 10); char r[12]; for (int i = 0; i < k; i++) r[i] = b[k - 1 - i]; r[k] = 0; out(r); }
static void hexv(u32 v) { char b[11] = "0x"; for (int i = 0; i < 8; i++) b[2 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; b[10] = 0; out(b); }

typedef struct { u16 fam, port; u32 addr; u8 zero[8]; } SIN;
static int (WINAPI *p_WSAStartup)(u16, void *);
static u32 (WINAPI *p_socket)(int, int, int);
static int (WINAPI *p_bind)(u32, const SIN *, int);
static int (WINAPI *p_sendto)(u32, const void *, int, int, const SIN *, int);
static int (WINAPI *p_recvfrom)(u32, void *, int, int, SIN *, int *);
static int (WINAPI *p_setsockopt)(u32, int, int, const void *, int);
static int (WINAPI *p_select)(int, void *, void *, void *, const s32 *);

#define MAGIC 0x324E4642   /* "BFN2" */
#define MAXP 256
static struct { SIN a; u32 room, seen; } g_p[MAXP];
static u32 g_np, g_frames, g_bytes;
static u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
static void addr_out(const SIN *a) { for (int q = 0; q < 4; q++) { dec((a->addr >> (q * 8)) & 255); if (q < 3) out("."); } out(":"); dec(bswap16(a->port)); }
static u32 room_count(u32 room) { u32 n = 0; for (u32 i = 0; i < g_np; i++) if (g_p[i].room == room) n++; return n; }

void WINAPI start(void) {
    g_out = GetStdHandle((DWORD)-11);
    u16 port = 34567; { const char *c = GetCommandLineA(); int q = 0; if (*c == '"') { c++; while (*c && *c != '"') c++; if (*c) c++; } else while (*c && *c != ' ') c++;
        while (*c == ' ') c++; u32 v = 0; while (*c >= '0' && *c <= '9') { v = v * 10 + (u32)(*c - '0'); c++; q = 1; } if (q && v > 0 && v < 65536) port = (u16)v; }
    void *ws = LoadLibraryA("ws2_32.dll"); if (!ws) { out("ws2_32.dll not found\r\n"); ExitProcess(1); }
    #define GP(v, n) *(void **)&v = GetProcAddress(ws, n)
    GP(p_WSAStartup, "WSAStartup"); GP(p_socket, "socket"); GP(p_bind, "bind"); GP(p_sendto, "sendto"); GP(p_recvfrom, "recvfrom"); GP(p_setsockopt, "setsockopt"); GP(p_select, "select");
    #undef GP
    static u8 wsa[512]; if (p_WSAStartup(0x0202, wsa)) { out("WSAStartup failed\r\n"); ExitProcess(1); }
    u32 s = p_socket(2, 2, 17); SIN me = {2, bswap16(port), 0};
    if (s == 0xFFFFFFFF || p_bind(s, &me, sizeof me)) { out("Cannot use UDP port "); dec(port); out(" (is another server running?)\r\n"); ExitProcess(1); }
    { int big = 1 << 22; p_setsockopt(s, 0xFFFF, 0x1002, &big, 4); p_setsockopt(s, 0xFFFF, 0x1001, &big, 4); }
    out("Brute Force online server on UDP port "); dec(port); out(". Forward this port on the router to this PC. Close the window to stop.\r\n");
    static u8 b[2048]; u32 tl = GetTickCount();
    for (;;) {
        struct { u32 n; u32 s[1]; } rd = {1, {s}}; s32 tv[2] = {0, 200000};   /* wait up to 0.2 s, then tidy up */
        if (p_select(0, &rd, 0, 0, tv) > 0) for (int burst = 0; burst < 256; burst++) {
            SIN from; int fl = sizeof from; int n = p_recvfrom(s, b, sizeof b, 0, &from, &fl);
            if (n < 8 || *(u32 *)b != MAGIC) break;
            u32 room = *(u32 *)(b + 4), now = GetTickCount(), i = 0;
            for (; i < g_np && !(g_p[i].a.addr == from.addr && g_p[i].a.port == from.port); i++) ;
            if (i == g_np) { if (g_np == MAXP) continue; g_p[i].a = from; g_np++; g_p[i].room = room;
                out("player joined "); addr_out(&from); out(room ? " (private room " : " (public room"); if (room) hexv(room); out("), players in the room: "); dec(room_count(room)); out("\r\n"); }
            else if (g_p[i].room != room) { g_p[i].room = room; out("player "); addr_out(&from); out(" moved to room "); hexv(room); out("\r\n"); }
            g_p[i].seen = now;
            if (n > 8) { g_frames++; g_bytes += (u32)n; for (u32 j = 0; j < g_np; j++) if (j != i && g_p[j].room == room) p_sendto(s, b, n, 0, &g_p[j].a, sizeof g_p[j].a); }
        }
        u32 now = GetTickCount();
        for (u32 i = 0; i < g_np; ) { if (now - g_p[i].seen > 15000) { out("player left "); addr_out(&g_p[i].a); out("\r\n"); g_p[i] = g_p[--g_np]; } else i++; }
        if (now - tl > 60000) { tl = now; out("players "); dec(g_np); out(", frames relayed in the last minute "); dec(g_frames); out(" ("); dec(g_bytes / 1024); out(" KB)\r\n"); g_frames = g_bytes = 0; }
    }
}

/* System Link over the internet (Milestone 97).
 *
 * The game links the Xbox network library (XNet) into itself; that library drives the console's network chip (nForce
 * "NVNET", registers at 0xFEF00000, interrupt vector 0x34) directly. This file is that chip: the register page is trapped
 * (kernel2.c mmio_fault -> nic_rd / nic_wr), frames the game puts in its transmit ring are sent over UDP, and frames from
 * the other players are written into its receive ring and announced with the interrupt. To the game, every player's
 * console is on one System Link cable: discovery, joining, the security handshake and the game's own netcode are unchanged.
 *
 * netplay.txt next to the loader turns it on (without it the cable is "unplugged", as before):
 *     host            this PC hosts (UDP port 34567; open/forward it on the router for internet play)
 *     host 40000      the same on another port
 *     join 192.168.1.20         join a host (LAN address, internet address or host name; default port 34567)
 *     join example.org:40000
 * The host relays every frame to all other players (a hub), so only the host needs an open port.
 * Each PC gets its own console address (MAC 00:50:F2:xx:xx:xx from the computer name; "mac 1a2b3c" in netplay.txt sets it). */
#include "util.h"

IMP DWORD WINAPI GetFileAttributesA(const char *);
IMP void *WINAPI LoadLibraryA(const char *);
IMP void *WINAPI GetProcAddress(void *, const char *);

#define PHYS(pa) ((u8 *)(0x80000000u + ((pa) & 0x03FFFFFF)))
#define NET_PORT 34567
#define MAGIC 0x314E4642   /* "BFN1" */

/* ---- settings ---- */
static int g_on = -1, g_host; static u16 g_port = NET_PORT; static u32 g_host_ip; static u16 g_host_port = NET_PORT;
/* Milestone 98: online through the server (bfserver.exe): "online <server>[:port]" and optionally "password <text>" (a private
   room: only players with the same password see each other's matches; without it, the public room where every match is listed).
   The password itself is never sent: the room is its hash. */
static int g_online; static u32 g_room;
#define MAGIC2 0x324E4642   /* "BFN2" + room + frame */
/* Milestone 98b: the match list (a Cloudflare Worker, dist\matchlist): "list https://<name>.workers.dev" (+ "password ...").
   No server carries game traffic: a console that answers another's search broadcast is hosting, and while it does a thread
   registers it on the list (every 5 s; it drops off 90 s after the last). Every player connects to every match listed in its
   room, so the game's own session list shows them all (the server browser); the host passes frames between its players.
   external_ip.txt (written by the launcher from the router, UPnP) gives the host's internet address; else the list uses the
   address it sees. "host" in netplay.txt with "list" = always registered. */
static int g_list, g_force_host; static char g_list_host[128], g_list_path[128];
#define MAGIC3 0x334E4642   /* "BFN3" + room + frame: player to player */
static u32 g_ext_ip;          /* our internet address as the list knows it (network byte order) */
static u32 parse_ip(const char **p); static void ip_out(u32 a);
void swap_header(int pe);   /* (CreateThread: win32.h) */
static u8 g_mac[6] = {0x00, 0x50, 0xF2, 0, 0, 0};
static char g_join[128];

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static u32 parse_num(const char **p) { u32 v = 0; while (is_digit(**p)) v = v * 10 + (u32)(*(*p)++ - '0'); return v; }
static int hexv(char c) { return is_digit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; }

static void net_config(void) {
    if (g_on >= 0) return;
    g_on = 0;
    if (GetFileAttributesA("netplay.txt") == 0xFFFFFFFF) return;
    char t[512]; DWORD n = 0; HANDLE f = CreateFileA("netplay.txt", 0x80000000, 1, 0, 3, 0x80, 0);
    if (f == (HANDLE)-1) return; ReadFile(f, t, sizeof t - 1, &n, 0); CloseHandle(f); t[n] = 0;
    {   /* the console address: from the computer name (each PC differs), unless netplay.txt says "mac" */
        char nm[64]; DWORD ln = sizeof nm; nm[0] = 0;
        BOOL (WINAPI *gcn)(char *, DWORD *) = (BOOL (WINAPI *)(char *, DWORD *))GetProcAddress(LoadLibraryA("kernel32.dll"), "GetComputerNameA");
        if (gcn) gcn(nm, &ln);
        u32 h = 2166136261u; for (DWORD i = 0; nm[i]; i++) h = (h ^ (u8)nm[i]) * 16777619u;
        g_mac[3] = (u8)(h >> 16); g_mac[4] = (u8)(h >> 8); g_mac[5] = (u8)h; }
    const char *p = t;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        const char *w = p; while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        u32 wl = (u32)(p - w); while (*p == ' ' || *p == '\t') p++;
        if (wl == 4 && w[0] == 'h' && w[1] == 'o' && w[2] == 's' && w[3] == 't') { g_on = 1; g_host = 1; g_force_host = 1; if (is_digit(*p)) g_port = (u16)parse_num(&p); }
        else if (wl == 4 && w[0] == 'l' && w[1] == 'i' && w[2] == 's' && w[3] == 't') {   /* list https://host[/path] */
            g_on = 1; g_list = 1; const char *u = p; if (u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p' && u[4] == 's' && u[5] == ':' && u[6] == '/' && u[7] == '/') u += 8;
            u32 k = 0; while (*u && *u != '/' && *u != ' ' && *u != '\r' && *u != '\n' && k < sizeof g_list_host - 1) g_list_host[k++] = *u++; g_list_host[k] = 0;
            k = 0; while (*u && *u != ' ' && *u != '\r' && *u != '\n' && k < sizeof g_list_path - 1) g_list_path[k++] = *u++; while (k && g_list_path[k - 1] == '/') k--; g_list_path[k] = 0;
            p = u; }
        else if (wl == 4 && w[0] == 'j' && w[1] == 'o' && w[2] == 'i' && w[3] == 'n') {
            g_on = 1; g_host = 0; g_port = 0; u32 k = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ':' && k < sizeof g_join - 1) g_join[k++] = *p++;
            g_join[k] = 0; if (*p == ':') { p++; g_host_port = (u16)parse_num(&p); } }
        else if (wl == 6 && w[0] == 'o' && w[1] == 'n' && w[2] == 'l' && w[3] == 'i' && w[4] == 'n' && w[5] == 'e') {   /* online <server>[:port] */
            g_on = 1; g_host = 0; g_online = 1; g_port = 0; u32 k = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ':' && k < sizeof g_join - 1) g_join[k++] = *p++;
            g_join[k] = 0; if (*p == ':') { p++; g_host_port = (u16)parse_num(&p); } }
        else if (wl == 8 && w[0] == 'p' && w[1] == 'a' && w[2] == 's' && w[3] == 's' && w[4] == 'w' && w[5] == 'o' && w[6] == 'r' && w[7] == 'd') {   /* password <text>: a private room */
            u32 h = 2166136261u; int any = 0; while (*p && *p != '\r' && *p != '\n') { h = (h ^ (u8)*p++) * 16777619u; any = 1; }
            while (any && (h == 0)) h = 1; g_room = any ? (h | 1) : 0; }
        else if (wl == 3 && w[0] == 'm' && w[1] == 'a' && w[2] == 'c') {
            for (int k = 0; k < 3; k++) { int a = hexv(p[0]), b = a >= 0 ? hexv(p[1]) : -1; if (b < 0) break; g_mac[3 + k] = (u8)(a * 16 + b); p += 2; } }
        while (*p && *p != '\n') p++;   /* the rest of the line */
    }
}
int net_enabled(void) { net_config(); return g_on; }
void net_mac(u8 *m) { net_config(); for (int i = 0; i < 6; i++) m[i] = g_mac[i]; }
u32 net_link(void) { return net_enabled() ? 0x0B : 0; }   /* PhyGetLinkState: link up, 100 Mbit, full duplex */

/* ---- the UDP link (ws2_32, loaded at run time) ---- */
typedef struct { u16 fam, port; u32 addr; u8 zero[8]; } SIN;
static int (WINAPI *p_WSAStartup)(u16, void *);
static u32 (WINAPI *p_socket)(int, int, int);
static int (WINAPI *p_bind)(u32, const SIN *, int);
static int (WINAPI *p_sendto)(u32, const void *, int, int, const SIN *, int);
static int (WINAPI *p_recvfrom)(u32, void *, int, int, SIN *, int *);
static int (WINAPI *p_ioctlsocket)(u32, u32, u32 *);
static int (WINAPI *p_setsockopt)(u32, int, int, const void *, int);
static void *(WINAPI *p_gethostbyname)(const char *);
static int (WINAPI *p_WSAGetLastError)(void);
static u32 g_sock = 0xFFFFFFFF;
static u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
#define NPEER 16
static struct { SIN a; u32 seen, listed; } g_peer[NPEER];   /* host: the other players; client: [0] = the host; list mode: listed matches + players who contacted us */
static u32 g_npeer;
static u32 g_tx_frames, g_rx_frames, g_rx_drop, g_fwd;

static int net_open(void) {
    static int tried; if (tried) return g_sock != 0xFFFFFFFF; tried = 1;
    void *ws = LoadLibraryA("ws2_32.dll"); if (!ws) { out("  [net] ws2_32.dll not found\r\n"); return 0; }
    #define GP(v, n) *(void **)&v = GetProcAddress(ws, n)
    GP(p_WSAStartup, "WSAStartup"); GP(p_socket, "socket"); GP(p_bind, "bind"); GP(p_sendto, "sendto"); GP(p_recvfrom, "recvfrom");
    GP(p_ioctlsocket, "ioctlsocket"); GP(p_setsockopt, "setsockopt"); GP(p_gethostbyname, "gethostbyname"); GP(p_WSAGetLastError, "WSAGetLastError");
    #undef GP
    static u8 wsa[512]; if (!p_WSAStartup || p_WSAStartup(0x0202, wsa)) { out("  [net] WSAStartup failed\r\n"); return 0; }
    u32 s = p_socket(2, 2, 17); if (s == 0xFFFFFFFF) { out("  [net] socket failed\r\n"); return 0; }
    SIN me = {2, bswap16(g_port), 0}; if (p_bind(s, &me, sizeof me)) { out("  [net] cannot use UDP port "); dec(g_port); out(" (another program has it?)\r\n"); return 0; }
    u32 one = 1; p_ioctlsocket(s, 0x8004667E /* FIONBIO */, &one);
    { int big = 1 << 20; p_setsockopt(s, 0xFFFF, 0x1002 /* SO_RCVBUF */, &big, 4); p_setsockopt(s, 0xFFFF, 0x1001 /* SO_SNDBUF */, &big, 4); }
    g_sock = s;
    if (g_list) {   /* the match list: our internet address from the router if the launcher wrote it, then the list thread */
        HANDLE f = CreateFileA("external_ip.txt", 0x80000000, 1, 0, 3, 0x80, 0);
        if (f != (HANDLE)-1) { char t[32] = {0}; DWORD n = 0; ReadFile(f, t, 31, &n, 0); CloseHandle(f); const char *q = t; while (*q == ' ') q++; g_ext_ip = parse_ip(&q); }
        out("  [net] online: match list "); out(g_list_host); out(g_room ? ", private room (password)" : ", public room"); out(", UDP port "); dec(g_port);
        if (g_ext_ip) { out(", internet address "); ip_out(g_ext_ip); } out("\r\n");
        return 1; }   /* (the match list is read by bflist.exe, started by net_prestart) */
    if (!g_host) {   /* the host's address: dotted numbers, or a name */
        const char *q = g_join; u32 ip = 0; int dots = 0, ok = 1;
        for (int k = 0; k < 4; k++) { if (!is_digit(*q)) { ok = 0; break; } u32 v = parse_num(&q); if (v > 255) ok = 0; ip |= v << (k * 8); if (k < 3) { if (*q != '.') { ok = 0; break; } q++; dots++; } }
        if (!ok || *q) { ip = 0; u8 *he = p_gethostbyname ? (u8 *)p_gethostbyname(g_join) : 0; if (he) { u8 **list = *(u8 ***)(he + 12); if (list && list[0]) ip = *(u32 *)list[0]; } }
        g_host_ip = ip;
        if (!ip) { out("  [net] cannot find the host \""); out(g_join); out("\"\r\n"); }
        else { g_peer[0].a.fam = 2; g_peer[0].a.port = bswap16(g_host_port); g_peer[0].a.addr = ip; g_peer[0].seen = GetTickCount(); g_npeer = 1; } }
    out("  [net] System Link over UDP: "); out(g_online ? "online through the server " : g_host ? "hosting on port " : "joining "); if (g_host) dec(g_port); else { out(g_join); out(" port "); dec(g_host_port); }
    if (g_online) out(g_room ? ", private room (password)" : ", public room");
    out(", console address 00:50:F2:"); for (int k = 3; k < 6; k++) { static const char hx[] = "0123456789ABCDEF"; char b[4] = {hx[g_mac[k] >> 4], hx[g_mac[k] & 15], k < 5 ? ':' : 0, 0}; out(b); } out("\r\n");
    return 1;
}
static void udp_send(const SIN *to, const u8 *frame, u32 len) {
    static u8 b[1600]; if (len > 1560) return;
    if (g_online) { *(u32 *)b = MAGIC2; *(u32 *)(b + 4) = g_room; memcpy_(b + 8, frame, len); p_sendto(g_sock, b, (int)len + 8, 0, to, sizeof *to); return; }
    *(u32 *)b = MAGIC; memcpy_(b + 4, frame, len);
    p_sendto(g_sock, b, (int)len + 4, 0, to, sizeof *to);
}
static int same(const SIN *a, const SIN *b) { return a->addr == b->addr && a->port == b->port; }

/* ---- list mode (Milestone 98b) ---- */
static volatile u32 g_plk;   /* the peer table: the ticker, game threads (sending) and the list thread */
static void plock(void) { while (__sync_lock_test_and_set(&g_plk, 1)) ; }
static void punlock(void) { __sync_lock_release(&g_plk); }
static volatile u32 g_host_until;   /* hosting until (tick): our console answered a search broadcast */
static struct { u8 m[6]; u32 t; } g_bc[8]; static u32 g_bci;   /* consoles that broadcast lately (their searches) */
static int peer_add(const SIN *a, int listed) {   /* lock held; returns the index or -1 */
    u32 i = 0; for (; i < g_npeer && !same(&g_peer[i].a, a); i++) ;
    if (i == g_npeer) { if (g_npeer == NPEER) return -1; g_peer[i].a = *a; g_peer[i].seen = GetTickCount(); g_peer[i].listed = 0; g_npeer++; }
    if (listed) g_peer[i].listed = 1; return (int)i; }
static void list_send(const SIN *to, const u8 *frame, u32 len) {
    static u8 b[1600]; if (len > 1560) return; *(u32 *)b = MAGIC3; *(u32 *)(b + 4) = g_room; if (len) memcpy_(b + 8, frame, len);
    p_sendto(g_sock, b, (int)len + 8, 0, to, sizeof *to); }
static int is_hosting(void) { return g_force_host || (int)(g_host_until - GetTickCount()) > 0; }
static void ip_out(u32 a) { for (int q = 0; q < 4; q++) { dec((a >> (q * 8)) & 255); if (q < 3) out("."); } }

static void num_out(char **q, u32 v) { char d[12]; int z = 0; do d[z++] = (char)('0' + v % 10); while (v /= 10); while (z) *(*q)++ = d[--z]; }
/* the match-list helper (bflist.exe): started at boot, before the game runs (creating a process later would need the program
   header, where the game's sits). It reads the list over HTTPS and sends us the matches; we tell it whether we host. */
IMP BOOL WINAPI CreateProcessA(const char *, char *, void *, void *, BOOL, DWORD, void *, const char *, void *, void *);
IMP DWORD WINAPI GetCurrentProcessId(void);
static u32 parse_ip(const char **p);
void net_prestart(void) {
    net_config(); if (!g_list) return;
    { HANDLE f = CreateFileA("external_ip.txt", 0x80000000, 1, 0, 3, 0x80, 0);
      if (f != (HANDLE)-1) { char t[32] = {0}; DWORD n = 0; ReadFile(f, t, 31, &n, 0); CloseHandle(f); const char *q = t; while (*q == ' ') q++; g_ext_ip = parse_ip(&q); } }
    static char cmd[512]; char *w = cmd; const char *s = "bflist.exe "; while (*s) *w++ = *s++;
    for (s = g_list_host; *s; s++) *w++ = *s; *w++ = ' '; if (g_list_path[0]) { for (s = g_list_path; *s; s++) *w++ = *s; } else *w++ = '-'; *w++ = ' ';
    num_out(&w, g_room); *w++ = ' '; num_out(&w, g_port); *w++ = ' ';
    for (int k = 0; k < 4; k++) { num_out(&w, (g_ext_ip >> (k * 8)) & 255); if (k < 3) *w++ = '.'; } *w++ = ' '; num_out(&w, GetCurrentProcessId()); *w = 0;
    u32 si[17] = {68}, pi[4];
    if (CreateProcessA("bflist.exe", cmd, 0, 0, 0, 0x08000000 /* CREATE_NO_WINDOW */, 0, 0, si, pi)) { CloseHandle((HANDLE)pi[0]); CloseHandle((HANDLE)pi[1]); out("  [net] match-list helper started (bflist.exe)\r\n"); }
    else out("  [net] could not start bflist.exe: no match list\r\n"); }
static u32 parse_ip(const char **p) { u32 ip = 0; for (int k = 0; k < 4; k++) { if (!is_digit(**p)) return 0; u32 v = parse_num(p); if (v > 255) return 0; ip |= v << (k * 8); if (k < 3) { if (**p != '.') return 0; (*p)++; } } return ip; }

/* ---- the chip (NVNET registers, as in Linux's forcedeth and xemu's nvnet) ---- */
#define R_IRQ_STATUS 0x000
#define R_IRQ_MASK   0x004
#define R_TX_CTL     0x084
#define R_TX_STATUS  0x088
#define R_RX_CTL     0x094
#define R_RX_STATUS  0x098
#define R_TX_RING    0x100
#define R_RX_RING    0x104
#define R_RING_SIZES 0x108
#define R_TXRX_CTL   0x144
#define R_MII_STATUS 0x180
#define R_MII_CTL    0x190
#define R_MII_DATA   0x194
#define IRQ_RX       0x002
#define IRQ_RX_NOBUF 0x004
#define IRQ_TX_OK    0x010
#define IRQ_LINK     0x040
#define TX_VALID     0x8000
#define TX_LAST      0x0001
#define RX_AVAIL     0x8000
#define RX_VALID     0x0001
#define RX_BIT4      0x0010
static u32 g_r[0x400];                 /* register values (the page is 4 KB) */
static u32 g_tx_i, g_rx_i;             /* the chip's place in each ring */
static volatile u32 g_nlock;           /* chip state: game threads (register access) and the ticker (receive) */
static volatile u32 g_irq_pending;     /* (the interrupt is level-triggered: raised while status & mask) */
static void lock(void) { while (__sync_lock_test_and_set(&g_nlock, 1)) ; }
static void unlock(void) { __sync_lock_release(&g_nlock); }
static u32 tx_size(void) { return (g_r[R_RING_SIZES / 4] & 0xFFFF) + 1; }
static u32 rx_size(void) { return (g_r[R_RING_SIZES / 4] >> 16) + 1; }
static void irq_update(void) { if (g_r[R_IRQ_STATUS / 4] & g_r[R_IRQ_MASK / 4]) g_irq_pending = 1; }
static u32 g_log_n;
static void reg_log(const char *what, u32 off, u32 v) { if (g_log_n < 400) { g_log_n++; out("  [nic] "); out(what); out(" "); hex(off); out(" "); hex(v); out("\r\n"); } }

static void deliver_local(const u8 *f, u32 len);
static void tx_frame(const u8 *f, u32 len) {
    g_tx_frames++;
    if (!net_open()) return;
    if (g_list) {
        if (len >= 14 && !(f[0] & 1)) {   /* a frame to one console that searched lately: we answered a search, so we host */
            u32 now = GetTickCount(); for (int i = 0; i < 8; i++) if (g_bc[i].t && now - g_bc[i].t < 3000) { int m = 1; for (int k = 0; k < 6; k++) if (g_bc[i].m[k] != f[k]) m = 0;
                if (m) { if (!is_hosting()) out("  [net] this console answered a search: hosting\r\n"); g_host_until = now + 120000; break; } } }
        plock(); for (u32 i = 0; i < g_npeer; i++) list_send(&g_peer[i].a, f, len); punlock(); return; }
    if (g_host) { for (u32 i = 0; i < g_npeer; i++) udp_send(&g_peer[i].a, f, len); }
    else if (g_npeer) udp_send(&g_peer[0].a, f, len);
}
static void tx_process(void) {   /* everything the game has marked valid in its transmit ring */
    static u8 fr[2048]; static u32 fl;
    u32 ring = g_r[R_TX_RING / 4], n = tx_size(), sent = 0;
    if (!ring) return;
    for (u32 k = 0; k < n; k++) {
        u8 *d = PHYS(ring + g_tx_i * 8); u16 len = *(u16 *)(d + 4), flags = *(u16 *)(d + 6);
        if (!(flags & TX_VALID)) break;
        u32 bl = (u32)len + 1; if (fl + bl <= sizeof fr) { memcpy_(fr + fl, PHYS(*(u32 *)d), bl); fl += bl; }
        *(u16 *)(d + 6) = (u16)(flags & ~(TX_VALID | 0x7FF8));   /* done, no errors */
        if (flags & TX_LAST) { tx_frame(fr, fl); fl = 0; }
        g_tx_i = (g_tx_i + 1) % n; sent++;
    }
    if (sent) { g_r[R_IRQ_STATUS / 4] |= IRQ_TX_OK; irq_update(); }
}
static void rx_frame(const u8 *f, u32 len) {   /* into the game's receive ring (lock held) */
    u32 ring = g_r[R_RX_RING / 4], n = rx_size();
    if (!ring || !(g_r[R_RX_CTL / 4] & 1)) { g_rx_drop++; return; }
    u8 *d = PHYS(ring + g_rx_i * 8); u16 cap = *(u16 *)(d + 4), flags = *(u16 *)(d + 6);
    if (!(flags & RX_AVAIL)) { g_rx_drop++; g_r[R_IRQ_STATUS / 4] |= IRQ_RX_NOBUF; irq_update(); return; }
    if (len > (u32)cap + 1) len = (u32)cap + 1;
    memcpy_(PHYS(*(u32 *)d), f, len);
    *(u16 *)(d + 4) = (u16)len; *(u16 *)(d + 6) = RX_VALID | RX_BIT4;
    g_rx_i = (g_rx_i + 1) % n; g_rx_frames++;
    g_r[R_IRQ_STATUS / 4] |= IRQ_RX; irq_update();
}
static void deliver_local(const u8 *f, u32 len) {
    if (len < 14) return;
    { int own = 1; for (int k = 0; k < 6; k++) if (f[6 + k] != g_mac[k]) own = 0; if (own) return; }   /* our own frame come back (we can be in our own list) */
    if (f[0] & 1) { u32 i = g_bci++ & 7; for (int k = 0; k < 6; k++) g_bc[i].m[k] = f[6 + k]; g_bc[i].t = GetTickCount() | 1; }   /* someone broadcast (a search) */
    int mine = (f[0] & 1); if (!mine) { mine = 1; for (int k = 0; k < 6; k++) if (f[k] != g_mac[k]) mine = 0; }   /* broadcast / multicast, or to this console */
    if (!mine) return;
    lock(); rx_frame(f, len); unlock();
}

u32 nic_rd(u32 off) {
    u32 v;
    lock();
    if (off == R_MII_CTL) v = g_r[off / 4] & ~0x8000u;           /* never busy */
    else if (off == R_TX_STATUS || off == R_RX_STATUS) v = 0;    /* idle */
    else if (off == R_TXRX_CTL) v = g_r[off / 4] | 0x8;          /* NVREG_TXRXCTL_IDLE (the driver waits for it after writing BIT2) */
    else v = g_r[off / 4];
    unlock();
    if (off != R_IRQ_STATUS) reg_log("rd", off, v);
    return v;
}
void nic_wr(u32 off, u32 v, u32 mask) {
    if (off != R_IRQ_STATUS) reg_log("wr", off, v & mask);
    lock();
    u32 *r = &g_r[off / 4];
    if (off == R_IRQ_STATUS || off == R_MII_STATUS) *r &= ~(v & mask);   /* bits written as 1 are cleared */
    else {
        *r = (*r & ~mask) | (v & mask);
        if (off == R_TX_RING) g_tx_i = 0;
        if (off == R_RX_RING) g_rx_i = 0;
        if (off == R_IRQ_MASK) irq_update();
        if (off == R_MII_CTL && !(v & 0x400)) {   /* a PHY register read: link up, 100 Mbit full duplex */
            u32 reg = v & 0x1F, d = 0;
            if (reg == 0) d = 0x3100; else if (reg == 1) d = 0x782D; else if (reg == 2) d = 0x0043; else if (reg == 3) d = 0x2413;
            else if (reg == 4 || reg == 5) d = 0x01E1;
            g_r[R_MII_DATA / 4] = d; }
        if (off == R_TXRX_CTL) { if (v & 0x10) { g_tx_i = g_rx_i = 0; } if (v & 1) tx_process(); }
    }
    unlock();
}

/* ---- the ticker (about every millisecond): received frames, the interrupt, keep-alives ---- */
void raise_vector(u32 vec);
void nic_service(void) {
    if (!net_enabled() || !net_open()) return;
    static u8 b[2048]; u32 now = GetTickCount();
    for (int k = 0; k < 64; k++) {
        SIN from; int fl = sizeof from; int n = p_recvfrom(g_sock, b, sizeof b, 0, &from, &fl);
        if (g_list) {   /* from another player: "BFN3" + room + frame; from the helper on this PC: "BFNL" + our ip + n + (ip, port) x n */
            if (n >= 12 && *(u32 *)b == 0x4C4E4642 && from.addr == 0x0100007F) {
                u32 *m = (u32 *)b, cnt = m[2]; if (m[1]) g_ext_ip = m[1]; if (cnt > 32 || 12 + 8 * cnt > (u32)n) continue;
                static u32 lastn = 0xFFFFFFFF; u32 ng = 0;
                plock(); for (u32 i = 0; i < g_npeer; i++) g_peer[i].listed = 0;
                for (u32 i = 0; i < cnt; i++) { if (m[3 + 2 * i] == g_ext_ip && m[4 + 2 * i] == g_port) continue;   /* our own match */
                    SIN a = {2, bswap16((u16)m[4 + 2 * i]), m[3 + 2 * i]}; if (peer_add(&a, 1) >= 0) ng++; }
                punlock();
                if (ng != lastn) { lastn = ng; out("  [net] open matches in our room: "); dec(ng); out("\r\n"); }
                continue; }
            if (n < 8 || *(u32 *)b != MAGIC3) break;
            if (*(u32 *)(b + 4) != g_room) continue;
            plock(); int i = peer_add(&from, 0); if (i >= 0) g_peer[i].seen = now;
            if (n > 8 && is_hosting()) for (u32 j = 0; j < g_npeer; j++) if ((int)j != i) { list_send(&g_peer[j].a, b + 8, (u32)n - 8); g_fwd++; }   /* the host passes it to its other players */
            punlock();
            if (n > 8) deliver_local(b + 8, (u32)n - 8);
            continue; }
        if (g_online) {   /* from the server: "BFN2" + room + frame (only our room's) */
            if (n < 8 || *(u32 *)b != MAGIC2 || *(u32 *)(b + 4) != g_room) break;
            if (g_npeer) g_peer[0].seen = now;
            if (n > 8) deliver_local(b + 8, (u32)n - 8);
            continue; }
        if (n < 4 || *(u32 *)b != MAGIC) break;
        u32 len = (u32)n - 4;
        if (g_host) {   /* learn the sender, pass the frame to the others */
            u32 i = 0; for (; i < g_npeer && !same(&g_peer[i].a, &from); i++) ;
            if (i == g_npeer && g_npeer < NPEER) { g_peer[g_npeer].a = from; g_npeer++; out("  [net] a player joined from "); for (int q = 0; q < 4; q++) { dec((from.addr >> (q * 8)) & 255); if (q < 3) out("."); } out("\r\n"); }
            if (i < NPEER) g_peer[i].seen = now;
            if (len) for (u32 j = 0; j < g_npeer; j++) if (j != i) { udp_send(&g_peer[j].a, b + 4, len); g_fwd++; } }
        else if (g_npeer) g_peer[0].seen = now;
        if (len) deliver_local(b + 4, len);
    }
    {   /* keep-alives (the host learns a joining player from them; they also keep the router's port open), dropped players */
        static u32 tk; if (now - tk > 1000) { tk = now;
            if (g_list) { { u32 hm[2] = {0x484E4642 /* BFNH */, (u32)is_hosting()}; SIN hp = {2, bswap16((u16)(g_port - 1)), 0x0100007F}; p_sendto(g_sock, hm, 8, 0, &hp, sizeof hp);   /* to the helper */
                    static int was; int h = is_hosting(); if (h != was) { was = h; out(h ? "  [net] hosting: listing the match\r\n" : "  [net] not hosting any more: the match comes off the list\r\n"); } }
                plock();   /* keep-alives to everyone; players not on the list who went quiet for 15 s are dropped */
                for (u32 i = 0; i < g_npeer; ) { if (!g_peer[i].listed && now - g_peer[i].seen > 15000) { g_peer[i] = g_peer[--g_npeer]; continue; } list_send(&g_peer[i].a, 0, 0); i++; }
                punlock(); }
            else if (g_online && g_npeer) { u32 m[2] = {MAGIC2, g_room}; p_sendto(g_sock, m, 8, 0, &g_peer[0].a, sizeof g_peer[0].a); }   /* to the server: "here, in this room" */
            else if (!g_host && g_npeer) { u32 m = MAGIC; p_sendto(g_sock, &m, 4, 0, &g_peer[0].a, sizeof g_peer[0].a); }
            if (g_host) for (u32 i = 0; i < g_npeer; ) { if (now - g_peer[i].seen > 15000) { out("  [net] a player left (no data for 15 s)\r\n"); g_peer[i] = g_peer[--g_npeer]; } else i++; } } }
    {   static u32 ts; if (now - ts > 10000) { ts = now; out("  [net] frames sent "); dec(g_tx_frames); out(" received "); dec(g_rx_frames); out(" dropped "); dec(g_rx_drop); out(" relayed "); dec(g_fwd); out(" players "); dec(g_npeer); out(g_host ? " (host)\r\n" : " (client)\r\n"); } }
    {   lock(); u32 pend = g_r[R_IRQ_STATUS / 4] & g_r[R_IRQ_MASK / 4]; g_irq_pending = 0; unlock();
        if (pend) raise_vector(0x34); }
}

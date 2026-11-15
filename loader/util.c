#include "util.h"
HANDLE g_log;
void out(const char *s) { DWORD n, l = 0; while (s[l]) l++; WriteFile(g_log, s, l, &n, 0); }
void log_flush(void) { FlushFileBuffers(g_log); }
void hex(u32 v) { char b[11]; int i; b[0]='0'; b[1]='x'; for (i = 0; i < 8; i++) b[2+i] = "0123456789ABCDEF"[(v >> (28 - 4*i)) & 15]; b[10]=0; out(b); }
void dec(u32 v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); out(b + i); }
/* rep movsb / stosb: fast on current CPUs (Milestone 78: the byte loops showed up in the renderer's profile) */
void *memcpy_(void *d, const void *s, u32 n) { void *dd = d; __asm__ volatile("rep movsb" : "+D"(dd), "+S"(s), "+c"(n) : : "memory"); return d; }
void memset_(void *d, int c, u32 n) { __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); }
u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
/* compiler helpers the freestanding build may need */
void *memcpy(void *d, const void *s, u32 n) { return memcpy_(d, s, n); }
void *memset(void *d, int c, u32 n) { memset_(d, c, n); return d; }
int _fltused = 0x9875;   /* the MSVC float marker the compiler references when floats are used */
int memcmp(const void *a, const void *b, u32 n) { const u8 *x = a, *y = b; u32 i = 0;
    for (; i + 4 <= n; i += 4) if (*(const u32 *)(x + i) != *(const u32 *)(y + i)) break;
    for (; i < n; i++) if (x[i] != y[i]) return x[i] - y[i]; return 0; }

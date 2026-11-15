/* bfgl.c - the viewer's GPU renderer (Milestone 79).
 * Reads the command ring the loader fills (bfgl.h) and draws with OpenGL: one frame-buffer object per Xbox
 * surface, textures decoded from the shared Xbox memory, the register combiners in one "uber" fragment shader
 * driven by uniforms. Freestanding C like the rest of the loader: GL functions are loaded by hand. */
#include "bfgl.h"
typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef short s16; typedef unsigned char u8; typedef float f32;
typedef int BOOL; typedef void *HANDLE; typedef u32 DWORD;
#define WINAPI __stdcall
#define IMP __declspec(dllimport)
IMP HANDLE WINAPI OpenFileMappingA(DWORD, BOOL, const char *);
IMP void *WINAPI MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, u32);
IMP void *WINAPI GetModuleHandleA(const char *);
IMP void *WINAPI LoadLibraryA(const char *);
IMP void *WINAPI GetProcAddress(void *, const char *);
IMP HANDLE WINAPI GetProcessHeap(void);
IMP void *WINAPI HeapAlloc(HANDLE, DWORD, u32);
IMP BOOL WINAPI HeapFree(HANDLE, DWORD, void *);
IMP DWORD WINAPI GetTickCount(void);
IMP DWORD WINAPI GetFileAttributesA(const char *);
IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI QueryPerformanceCounter(long long *);
IMP BOOL WINAPI QueryPerformanceFrequency(long long *);
IMP HANDLE WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI CloseHandle(HANDLE);
IMP void *WINAPI GetDC(void *);
IMP BOOL WINAPI GetClientRect(void *, s32 *);
typedef struct { u16 size, ver; u32 flags; u8 type, color, r, rs, g, gs, b, bs, a, as, accum, ar, ag, ab, aa, depth, stencil, aux, layer, res; u32 lmask, vmask, dmask; } PFD;
IMP s32 WINAPI ChoosePixelFormat(void *, const PFD *);
IMP BOOL WINAPI SetPixelFormat(void *, s32, const PFD *);
IMP BOOL WINAPI SwapBuffers(void *);
IMP void *WINAPI wglCreateContext(void *);
IMP BOOL WINAPI wglMakeCurrent(void *, void *);
IMP void *WINAPI wglGetProcAddress(const char *);

/* ---- OpenGL ---- */
#define GLAPI __stdcall
typedef u32 GLenum; typedef s32 GLint; typedef u32 GLuint; typedef s32 GLsizei; typedef f32 GLfloat; typedef char GLchar; typedef long GLsizeiptr; typedef long GLintptr; typedef u8 GLboolean;
IMP void GLAPI glViewport(GLint, GLint, GLsizei, GLsizei);
IMP void GLAPI glScissor(GLint, GLint, GLsizei, GLsizei);
IMP void GLAPI glClear(u32);
IMP void GLAPI glClearColor(f32, f32, f32, f32);
IMP void GLAPI glClearDepth(double);
IMP void GLAPI glClearStencil(GLint);
IMP void GLAPI glEnable(GLenum);
IMP void GLAPI glDisable(GLenum);
IMP void GLAPI glBlendFunc(GLenum, GLenum);
IMP void GLAPI glDepthFunc(GLenum);
IMP void GLAPI glDepthMask(GLboolean);
IMP void GLAPI glColorMask(GLboolean, GLboolean, GLboolean, GLboolean);
IMP void GLAPI glCullFace(GLenum);
IMP void GLAPI glFrontFace(GLenum);
IMP void GLAPI glStencilFunc(GLenum, GLint, GLuint);
IMP void GLAPI glStencilOp(GLenum, GLenum, GLenum);
IMP void GLAPI glStencilMask(GLuint);
IMP void GLAPI glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
IMP void GLAPI glTexParameteri(GLenum, GLenum, GLint);
IMP void GLAPI glTexParameterf(GLenum, GLenum, float);
IMP void GLAPI glBindTexture(GLenum, GLuint);
IMP void GLAPI glGenTextures(GLsizei, GLuint *);
IMP void GLAPI glDeleteTextures(GLsizei, const GLuint *);
IMP void GLAPI glDrawArrays(GLenum, GLint, GLsizei);
IMP void GLAPI glPixelStorei(GLenum, GLint);
IMP void GLAPI glTexParameterfv(GLenum, GLenum, const GLfloat *);
IMP void GLAPI glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
IMP void GLAPI glFlush(void);
IMP const u8 *GLAPI glGetString(GLenum);
IMP GLenum GLAPI glGetError(void);
IMP void GLAPI glPolygonOffset(f32, f32);
IMP void GLAPI glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
#define GL_FN(ret, name, args) static ret (GLAPI *name) args;
GL_FN(void, glGenQueries, (GLsizei, GLuint *)) GL_FN(void, glBeginQuery, (GLenum, GLuint)) GL_FN(void, glEndQuery, (GLenum)) GL_FN(void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint *))
GL_FN(void, glDeleteBuffers, (GLsizei, const GLuint *)) GL_FN(void, glGetBufferSubData, (GLenum, GLsizeiptr, GLsizeiptr, void *)) GL_FN(void, glGenBuffers, (GLsizei, GLuint *)) GL_FN(void, glBindBuffer, (GLenum, GLuint)) GL_FN(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))
GL_FN(void, glGenFramebuffers, (GLsizei, GLuint *)) GL_FN(void, glBindFramebuffer, (GLenum, GLuint)) GL_FN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GL_FN(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) GL_FN(void, glGenRenderbuffers, (GLsizei, GLuint *)) GL_FN(void, glBindRenderbuffer, (GLenum, GLuint))
GL_FN(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) GL_FN(GLenum, glCheckFramebufferStatus, (GLenum)) GL_FN(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, u32, GLenum))
GL_FN(GLuint, glCreateShader, (GLenum)) GL_FN(void, glShaderSource, (GLuint, GLsizei, const GLchar **, const GLint *)) GL_FN(void, glCompileShader, (GLuint))
GL_FN(void, glGetShaderiv, (GLuint, GLenum, GLint *)) GL_FN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) GL_FN(GLuint, glCreateProgram, (void))
GL_FN(void, glAttachShader, (GLuint, GLuint)) GL_FN(void, glLinkProgram, (GLuint)) GL_FN(void, glGetProgramiv, (GLuint, GLenum, GLint *)) GL_FN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GL_FN(void, glUseProgram, (GLuint)) GL_FN(GLint, glGetUniformLocation, (GLuint, const GLchar *)) GL_FN(void, glUniform1i, (GLint, GLint)) GL_FN(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))
GL_FN(void, glUniform4iv, (GLint, GLsizei, const GLint *)) GL_FN(void, glUniform2iv, (GLint, GLsizei, const GLint *)) GL_FN(void, glUniform4f, (GLint, f32, f32, f32, f32)) GL_FN(void, glUniform2f, (GLint, f32, f32))
GL_FN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) GL_FN(void, glEnableVertexAttribArray, (GLuint)) GL_FN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))
GL_FN(void, glBlendEquation, (GLenum)) GL_FN(void, glBlendColor, (f32, f32, f32, f32)) GL_FN(void, glActiveTexture, (GLenum)) GL_FN(void, glGenVertexArrays, (GLsizei, GLuint *)) GL_FN(void, glBindVertexArray, (GLuint))
GL_FN(void, glBindFragDataLocation, (GLuint, GLuint, const GLchar *)) GL_FN(void, glGenerateMipmap, (GLenum))

static void *glp(const char *n) { void *p = wglGetProcAddress(n); return (p == 0 || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) ? 0 : p; }

/* ---- small helpers ---- */
static void cpy(void *d, const void *s, u32 n) { __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); }
static void zero(void *d, u32 n) { u8 *a = d; while (n--) *a++ = 0; }
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void scat(char *d, const char *s) { d += slen(d); while ((*d++ = *s++)) ; }
static void shex(char *d, u32 v) { char b[11]; b[0] = '0'; b[1] = 'x'; for (int i = 0; i < 8; i++) b[2 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; b[10] = 0; scat(d, b); }
static f32 fl(u32 u) { union { u32 u; f32 f; } x; x.u = u; return x.f; }

static RING *g_r; static u32 *g_rb; static u8 *g_mem; static u32 *g_kel;
#define PHYS(a) (g_mem + ((a) & 0x03FFFFFF))
#define K(m) g_kel[(m) / 4]
static void info(const char *s) { if (slen(g_r->info) + slen(s) < 250) scat(g_r->info, s); }

/* ---- textures: decoded from Xbox memory (the formats of render.c) ---- */
static u32 swz(u32 x, u32 y, u32 w, u32 h) { u32 r = 0, bit = 1; for (u32 i = 1; i < w || i < h; i <<= 1) { if (i < w) { if (x & i) r |= bit; bit <<= 1; } if (i < h) { if (y & i) r |= bit; bit <<= 1; } } return r; }
static u32 c565(u32 p) { u32 r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31; return (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2); }
typedef struct { u32 off, cf, w, h, pitch, linear, pal, bpp, comp, fmt, rect; } TI;
static int tex_info(u32 i, TI *t) {
    u32 base = 0x1B00 + i * 0x40, fmt = K(base + 4);
    zero(t, sizeof *t);
    t->off = K(base); t->fmt = fmt; t->cf = (fmt >> 8) & 0xFF; t->pal = K(base + 0x20) & ~0x3Fu; t->rect = K(base + 0x1C);
    switch (t->cf) {
    case 0x00: case 0x01: case 0x0B: case 0x19: t->bpp = 1; break;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x1A: case 0x27: case 0x28: case 0x29: case 0x31: t->bpp = 2; break;
    case 0x06: case 0x07: case 0x3A: case 0x3B: case 0x3C: t->bpp = 4; break;
    case 0x0C: case 0x0E: case 0x0F: t->comp = 1; break;
    case 0x13: case 0x1B: case 0x1F: t->bpp = 1; t->linear = 1; break;
    case 0x10: case 0x11: case 0x1C: case 0x1D: case 0x20: case 0x30: t->bpp = 2; t->linear = 1; break;
    case 0x12: case 0x1E: case 0x2E: case 0x3F: case 0x40: case 0x41: t->bpp = 4; t->linear = 1; break;
    case 0x24: case 0x25: t->bpp = 2; t->linear = 1; break;   /* YUV 4:2:2 (Bink movies) */
    default: return 0;
    }
    if (t->linear) { t->w = t->rect >> 16; t->h = t->rect & 0xFFFF; t->pitch = K(base + 0x10) >> 16; if (!t->w || !t->h || t->w > 4096 || t->h > 4096) return 0; }
    else { t->w = 1u << ((fmt >> 20) & 0xF); t->h = 1u << ((fmt >> 24) & 0xF); if (t->w > 4096 || t->h > 4096) return 0; t->pitch = t->w * t->bpp; }
    return 1;
}
static u32 dxt_texel(TI *t, u32 x, u32 y) {
    u32 bw = (t->w + 3) / 4, bs = t->cf == 0x0C ? 8 : 16;
    u8 *b = PHYS(t->off + ((y / 4) * bw + x / 4) * bs), *cb = bs == 16 ? b + 8 : b;
    u32 c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8, k = ((cb[4] | cb[5] << 8 | cb[6] << 16 | (u32)cb[7] << 24) >> (((y & 3) * 4 + (x & 3)) * 2)) & 3;
    u32 p0 = c565(c0), p1 = c565(c1), a = 255, col;
    u32 r0 = (p0 >> 16) & 255, g0 = (p0 >> 8) & 255, b0 = p0 & 255, r1 = (p1 >> 16) & 255, g1 = (p1 >> 8) & 255, b1 = p1 & 255;
    if (k == 0) col = p0; else if (k == 1) col = p1;
    else if (c0 > c1 || bs == 16) col = k == 2 ? ((2 * r0 + r1) / 3) << 16 | ((2 * g0 + g1) / 3) << 8 | (2 * b0 + b1) / 3 : ((r0 + 2 * r1) / 3) << 16 | ((g0 + 2 * g1) / 3) << 8 | (b0 + 2 * b1) / 3;
    else if (k == 2) col = ((r0 + r1) / 2) << 16 | ((g0 + g1) / 2) << 8 | (b0 + b1) / 2; else { col = 0; a = 0; }
    if (t->cf == 0x0E) { u32 i = (y & 3) * 4 + (x & 3); a = ((b[i / 2] >> ((i & 1) * 4)) & 15) * 17; }
    else if (t->cf == 0x0F) {
        u32 a0 = b[0], a1 = b[1], bits = b[2] | b[3] << 8 | b[4] << 16, bits2 = b[5] | b[6] << 8 | b[7] << 16, i = (y & 3) * 4 + (x & 3);
        u32 ai = i < 8 ? (bits >> (i * 3)) & 7 : (bits2 >> ((i - 8) * 3)) & 7;
        if (ai == 0) a = a0; else if (ai == 1) a = a1; else if (a0 > a1) a = ((8 - ai) * a0 + (ai - 1) * a1) / 7; else a = ai == 6 ? 0 : ai == 7 ? 255 : ((6 - ai) * a0 + (ai - 1) * a1) / 5;
    }
    return a << 24 | col;
}
static u32 g_ab;   /* abtest.txt (Milestone 96, read every second): 2 = no fog, 3 = A8 textures black (the old decoding) */
static u32 texel(TI *t, u32 x, u32 y) {
    if (t->comp) return dxt_texel(t, x, y);
    u8 *p = PHYS(t->off + (t->linear ? y * t->pitch + x * t->bpp : swz(x, y, t->w, t->h) * t->bpp));
    switch (t->cf) {
    case 0x00: case 0x13: { u32 l = p[0]; return 0xFF000000 | l << 16 | l << 8 | l; }
    case 0x01: case 0x1B: { u32 l = p[0]; return l << 24 | l << 16 | l << 8 | l; }
    case 0x19: case 0x1F: return (u32)p[0] << 24 | (g_ab == 3 ? 0 : 0xFFFFFF);   /* A8: NV2A reads it as white with alpha (as xemu's swizzle 1,1,1,A); black made the radar dots black (Milestone 95) */
    case 0x0B: return *(u32 *)PHYS(t->pal + p[0] * 4);
    case 0x1A: case 0x20: { u32 l = p[0], a = p[1]; return a << 24 | l << 16 | l << 8 | l; }
    case 0x02: case 0x10: { u32 v = *(u16 *)p, r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return ((v & 0x8000) ? 0xFF000000 : 0) | (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2); }
    case 0x03: case 0x1C: { u32 v = *(u16 *)p, r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return 0xFF000000 | (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2); }
    case 0x04: case 0x1D: { u32 v = *(u16 *)p; return ((v >> 12) & 15) * 17u << 24 | ((v >> 8) & 15) * 17u << 16 | ((v >> 4) & 15) * 17u << 8 | (v & 15) * 17u; }
    case 0x05: case 0x11: case 0x27: return 0xFF000000 | c565(*(u16 *)p);
    case 0x06: case 0x12: return *(u32 *)p;
    case 0x07: case 0x1E: return 0xFF000000 | *(u32 *)p;
    case 0x3A: case 0x3F: { u32 v = *(u32 *)p; return (v & 0xFF00FF00) | (v & 0xFF) << 16 | ((v >> 16) & 0xFF); }
    case 0x3B: case 0x40: { u32 v = *(u32 *)p; return (v & 0xFF) << 24 | (v >> 8); }
    case 0x3C: case 0x41: { u32 v = *(u32 *)p; return (v >> 24) | (v & 0xFF) << 24 | ((v >> 8) & 0xFF) << 16 | ((v >> 16) & 0xFF) << 8; }
    case 0x28: { u32 v = *(u16 *)p; return 0xFF000000 | (v >> 8) << 8 | (v & 0xFF); }
    case 0x29: { u32 v = *(u16 *)p; return 0xFF000000 | (v >> 8) << 16 | (v & 0xFF); }
    case 0x31: case 0x30: { u32 l = (*(u16 *)p) >> 8; return 0xFF000000 | l << 16 | l << 8 | l; }
    case 0x2E: { u32 l = (*(u32 *)p) >> 24; return 0xFF000000 | l << 16 | l << 8 | l; }
    case 0x24: case 0x25: {   /* 0x24 = Y0 U Y1 V (YUY2), 0x25 = U Y0 V Y1 (UYVY) */
        u8 *q = PHYS(t->off + y * t->pitch + (x & ~1u) * 2); s32 Y, U, V;
        if (t->cf == 0x24) { Y = q[(x & 1) * 2]; U = q[1]; V = q[3]; } else { Y = q[1 + (x & 1) * 2]; U = q[0]; V = q[2]; }
        s32 c = Y - 16, d = U - 128, e = V - 128;
        s32 r = (298 * c + 409 * e + 128) >> 8, g = (298 * c - 100 * d - 208 * e + 128) >> 8, b = (298 * c + 516 * d + 128) >> 8;
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
        return 0xFF000000 | (u32)r << 16 | (u32)g << 8 | (u32)b; }
    }
    return 0xFFFF00FF;
}
static u32 tex_bytes(TI *t) { return t->comp ? ((t->w + 3) / 4) * ((t->h + 3) / 4) * (t->cf == 0x0C ? 8 : 16) : t->linear ? t->pitch * t->h : t->w * t->h * t->bpp; }
static u32 tex_hash(TI *t) {   /* a cheap fingerprint: 64 words spread over the data, and the palette */
    u32 n = tex_bytes(t) / 4, h = 2166136261u; u32 *p = (u32 *)PHYS(t->off);
    if ((u32)(t->off & 0x03FFFFFF) + n * 4 > 0x4000000) return 0;
    for (u32 i = 0; i < 64 && n; i++) h = (h ^ p[(u32)((unsigned long long)i * n / 64)]) * 16777619u;
    if (t->cf == 0x0B) for (u32 i = 0; i < 256; i += 8) h = (h ^ *(u32 *)PHYS(t->pal + i * 4)) * 16777619u;
    return h;
}
typedef struct { u32 off, fmt, rect, pal, hash, frame; GLuint tex; u32 w, h, mips; } TCACHE;
#define NTC 1024
static TCACHE g_tc[NTC]; static u32 *g_tbuf; static u32 g_frame;
static u32 g_last_mips;   /* whether the texture tex_get returned has mipmaps */
static void ab_poll(void) {   /* abtest.txt: the first digit; a change re-decodes every texture */
    static u32 tl; u32 now = GetTickCount(); if (now - tl < 1000) return; tl = now; u32 v = 0;
    HANDLE f = CreateFileA("abtest.txt", 0x80000000, 1, 0, 3, 0x80, 0); if (f != (HANDLE)-1) { char s[4] = {0}; DWORD n = 0; ReadFile(f, s, 3, &n, 0); CloseHandle(f); if (n && s[0] >= 0x30 && s[0] <= 0x39) v = s[0] - 0x30; }
    if (v != g_ab) { g_ab = v; for (u32 i = 0; i < NTC; i++) g_tc[i].hash ^= 0x5A5A5A5A; } }
IMP BOOL WINAPI CreateDirectoryA(const char *, void *);

/* ---- Milestone 99: button prompts show the keys they are bound to ----
   The game draws its button prompts (A B X Y, the triggers, START, BACK, the black and white buttons, the D-pad arrows) as
   glyphs of one font sheet (128x256, A4R4G4B4, two copies of the icons; content hash 0x8AB7F635). When that sheet is decoded it is
   replaced by a 4x sharper copy whose button cells hold key caps with the bound key's name (keybinds.txt, read every second;
   a change rebuilds it). Texture coordinates are normalised, so the bigger copy is a drop-in. nokeyicons.txt = the Xbox icons. */
#define KEYSHEET_HASH 0x8AB7F635u
IMP void *WINAPI CreateCompatibleDC(void *);
IMP void *WINAPI CreateDIBSection(void *, const void *, u32, void **, void *, u32);
IMP void *WINAPI SelectObject(void *, void *);
IMP void *WINAPI CreateFontA(int, int, int, int, int, u32, u32, u32, u32, u32, u32, u32, u32, const char *);
IMP u32 WINAPI SetTextColor(void *, u32);
IMP int WINAPI SetBkMode(void *, int);
IMP BOOL WINAPI GetTextExtentPoint32A(void *, const char *, int, s32 *);
IMP BOOL WINAPI DeleteObject(void *);
IMP BOOL WINAPI DeleteDC(void *);
IMP BOOL WINAPI TextOutA(void *, int, int, const char *, int);
IMP BOOL WINAPI TextOutW(void *, int, int, const u16 *, int);
IMP BOOL WINAPI GetTextExtentPoint32W(void *, const u16 *, int, s32 *);
static u8 g_kbv[21] = {'W', 'S', 'A', 'D', 0xA0, 0x20, 'V', 'F', 'R', 'Z', 'C', 'Q', 'E', 0x1B, 0x08, 0x26, 0x28, 0x25, 0x27, '1', '3'};   /* input.c's defaults */
static u32 g_kb_sig, g_keyicons = 1;
static const char *key_cap(u32 vk, char *b) {   /* a key cap's label: at most 4 characters */
    /* the caps are tiny on screen (~15x19 of 640x480): one big symbol reads far better than three letters.
       \x03-\x07: a drawn mouse (left, right, middle button; wheel up, down); \x10-\x19: symbols (keycap() maps them to Unicode) */
    static const struct { u8 vk; const char *n; } t[] = {{0x01, "\x03"}, {0x02, "\x04"}, {0x04, "\x05"}, {0x05, "M4"}, {0x06, "M5"}, {0x0E, "\x06"}, {0x0F, "\x07"},
        {0x08, "\x14"}, {0x09, "\x13"}, {0x0D, "\x12"}, {0x10, "\x11"}, {0x11, "Ctrl"}, {0x12, "Alt"}, {0x14, "\x15"}, {0x1B, "Esc"}, {0x20, "\x10"},
        {0x21, "PgUp"}, {0x22, "PgDn"}, {0x23, "End"}, {0x24, "Home"}, {0x25, "\x18"}, {0x26, "\x16"}, {0x27, "\x19"}, {0x28, "\x17"}, {0x2D, "Ins"}, {0x2E, "Del"},
        {0xA0, "\x11"}, {0xA1, "\x11"}, {0xA2, "Ctrl"}, {0xA3, "Ctrl"}, {0xA4, "Alt"}, {0xA5, "Alt"},
        {0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, "-"}, {0xBE, "."}, {0xBF, "/"}, {0xC0, "`"}, {0xDB, "["}, {0xDC, "\\"}, {0xDD, "]"}, {0xDE, "'"}};
    for (u32 i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i].vk == vk) return t[i].n;
    if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A)) { b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= 0x60 && vk <= 0x69) { b[0] = 'N'; b[1] = (char)('0' + vk - 0x60); b[2] = 0; return b; }
    if (vk >= 0x70 && vk <= 0x7B) { u32 f = vk - 0x6F; b[0] = 'F'; if (f >= 10) { b[1] = '1'; b[2] = (char)('0' + f - 10); b[3] = 0; } else { b[1] = (char)('0' + f); b[2] = 0; } return b; }
    b[0] = '?'; b[1] = 0; return b; }
static void kb_poll(void) {   /* keybinds.txt (input.c writes it; Pause/START is always Esc) and nokeyicons.txt, once a second */
    static u32 tl; u32 now = GetTickCount(); if (now - tl < 1000) return; tl = now;
    u8 v[21]; for (int i = 0; i < 21; i++) v[i] = g_kbv[i];
    HANDLE f = CreateFileA("keybinds.txt", 0x80000000, 1, 0, 3, 0x80, 0);
    if (f != (HANDLE)-1) { char s[600]; DWORD n = 0; ReadFile(f, s, sizeof s - 1, &n, 0); CloseHandle(f); u32 k = 0, x = 0; int any = 0;
        for (DWORD i = 0; i <= n && k < 21; i++) { char c = i < n ? s[i] : 10; if (c >= '0' && c <= '9') { x = x * 10 + (u32)(c - '0'); any = 1; } else if (c == 10) { if (any && x > 0 && x < 256) v[k] = (u8)x; k++; x = 0; any = 0; } else if (c == '#') { while (i < n && s[i] != 10) i++; i--; } } }
    v[13] = 0x1B;
    u32 on = GetFileAttributesA("nokeyicons.txt") == 0xFFFFFFFF, sig = 2166136261u; for (int i = 0; i < 21; i++) sig = (sig ^ v[i]) * 16777619u; sig ^= on;
    if (sig != g_kb_sig) { g_kb_sig = sig; for (int i = 0; i < 21; i++) g_kbv[i] = v[i]; g_keyicons = on;
        for (u32 i = 0; i < NTC; i++) if (g_tc[i].tex && g_tc[i].w == 128 && g_tc[i].h == 256) g_tc[i].hash ^= 0x13579BDF; } }   /* the sheet is decoded again */
/* the button cells of the sheet (x0, y0, x1, y1 inclusive) and the binding each shows (index into g_kbv) */
static const struct { u8 x0, y0, x1, y1, kb; } g_cells[] = {
    {29, 30, 43, 48, 5}, {46, 30, 60, 48, 6}, {64, 30, 78, 48, 7}, {82, 30, 96, 48, 8},          /* A B X Y */
    {0, 51, 11, 66, 11}, {14, 51, 25, 66, 12}, {29, 51, 52, 66, 13}, {54, 51, 74, 66, 14},       /* L and R triggers, START, BACK */
    {76, 51, 89, 66, 9}, {92, 51, 105, 66, 10},                                                   /* white, black */
    {0, 70, 15, 82, 17}, {34, 68, 47, 83, 16}, {66, 70, 81, 82, 18}, {0, 85, 13, 100, 15},       /* D-pad: left, down, right, up */
    {29, 98, 43, 116, 5}, {46, 98, 60, 116, 6}, {64, 98, 78, 116, 7}, {82, 98, 96, 116, 8},      /* the second copy */
    {0, 119, 11, 134, 11}, {14, 119, 25, 134, 12}, {29, 119, 53, 134, 13}, {55, 119, 75, 134, 14},
    {77, 119, 90, 134, 9}, {93, 119, 106, 134, 10},
    {0, 138, 15, 150, 17}, {34, 136, 47, 151, 16}, {66, 138, 81, 150, 18}, {0, 153, 13, 168, 15} };
#define KS 4   /* the sharper copy's scale */
static u32 g_keysheet[128 * KS * 256 * KS];
static void keycap(u32 *dst, u32 pitch, u32 w, u32 h, const char *label) {   /* a dark rounded key cap with a light rim and the label in white */
    u32 r = (w < h ? w : h) / 4; if (r < 2) r = 2;
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) {
        s32 dx = 0, dy = 0; if (x < r) dx = (s32)(r - x); else if (x >= w - r) dx = (s32)(x - (w - r - 1)); if (y < r) dy = (s32)(r - y); else if (y >= h - r) dy = (s32)(y - (h - r - 1));
        u32 d2 = (u32)(dx * dx + dy * dy), in = d2 <= r * r, rim = in && (x < 2 || y < 2 || x >= w - 2 || y >= h - 2 || d2 > (r - 2) * (r - 2));
        dst[y * pitch + x] = !in ? 0 : rim ? 0xFFE6E6E6 : 0xFF14161A; }
    {   /* a drawn mouse (\x03 left, \x04 right, \x05 middle button, \x06 wheel up, \x07 wheel down): the body outlined, the button filled */
        u8 m = (u8)label[0];
        if (m >= 3 && m <= 7 && !label[1]) {
            u32 bw2 = w * 52 / 100, bh2 = h * 80 / 100, bx = (w - bw2) / 2, by = (h - bh2) / 2, th = (w > 40 ? w / 20 : 2), rr = bw2 / 2, split = by + bh2 * 42 / 100;
            for (u32 y = by; y < by + bh2; y++) for (u32 x = bx; x < bx + bw2; x++) {
                s32 cx = (s32)(x - bx) - (s32)rr, cy = y < by + rr ? (s32)(y - by) - (s32)rr : y >= by + bh2 - rr ? (s32)(y - (by + bh2 - rr)) : 0;
                s32 d2 = cx * cx + cy * cy; if (d2 > (s32)(rr * rr)) continue;   /* outside the rounded body */
                int edge = d2 > (s32)((rr - th) * (rr - th)) || x < bx + th || x >= bx + bw2 - th;
                int top = y < split, left = x < bx + bw2 / 2, line = (top && (x >= bx + bw2 / 2 - th / 2 && x <= bx + bw2 / 2 + th / 2)) || (y >= split - th / 2 && y <= split + th / 2);
                int fill = top && ((m == 3 && left) || (m == 4 && !left));
                u32 wx0 = bx + bw2 / 2 - bw2 / 7, wx1 = bx + bw2 / 2 + bw2 / 7, wy0 = by + bh2 * 10 / 100, wy1 = split - th;   /* the wheel */
                int wheel = x >= wx0 && x <= wx1 && y >= wy0 && y <= wy1;
                if (edge || line || fill || (wheel && m >= 5)) dst[y * pitch + x] = 0xFFFFFFFF; else if (wheel) dst[y * pitch + x] = 0xFF8A8F96; }
            if (m == 6 || m == 7) {   /* the arrow beside the wheel: a filled triangle at the right of the cap */
                u32 ax = bx + bw2 + (w - bx - bw2) / 2, ah = h / 5, ay = m == 6 ? by + bh2 / 4 : by + bh2 * 3 / 4;
                for (u32 k = 0; k < ah; k++) { u32 row = m == 6 ? ay - ah / 2 + k : ay + ah / 2 - k, half = k * (w - bx - bw2) / (2 * ah) + 1; for (u32 x = ax - half; x <= ax + half && x < w; x++) if (row < h) dst[row * pitch + x] = 0xFFFFFFFF; } }
            return; } }
    /* the label: drawn white on black with GDI into a small bitmap, its brightness used as coverage */
    static void *dc; static u32 *bits; static u32 bw, bh; void *bmp = 0;
    if (!dc) dc = CreateCompatibleDC(0);
    struct { u32 size; s32 w, h; u16 planes, bpp; u32 comp, img, xp, yp, used, imp; } bi = {40, (s32)w, -(s32)h, 1, 32, 0, 0, 0, 0, 0, 0};
    bmp = CreateDIBSection(dc, &bi, 0, (void **)&bits, 0, 0); if (!bmp || !bits) return; bw = w; bh = h;
    void *ob = SelectObject(dc, bmp); for (u32 i = 0; i < w * h; i++) bits[i] = 0;
    static const u16 sym[10] = {0x2423, 0x21E7, 0x21B5, 0x21E5, 0x232B, 0x21EA, 0x2191, 0x2193, 0x2190, 0x2192};   /* space, shift, enter, tab, backspace, caps, up, down, left, right */
    int len = 0, issym = 0; u16 wl[8]; while (label[len] && len < 7) { u8 c = (u8)label[len]; if (c >= 0x10 && c <= 0x19) { wl[len] = sym[c - 0x10]; issym = 1; } else wl[len] = c; len++; }
    const char *face = issym ? "Segoe UI Symbol" : len >= 4 ? "Arial Narrow" : "Arial";   /* symbols; four letters in the narrow face */
    /* the label as large as fits (inside the rim), heavy weight */
    int fh = (int)(h * 0.80f), tw = 0; void *font = 0;
    for (; fh >= 6; fh--) { if (font) DeleteObject(font); font = CreateFontA(-fh, 0, 0, 0, 900, 0, 0, 0, 0, 0, 0, 4 /* ANTIALIASED */, 0, face);
        SelectObject(dc, font); s32 sz[2]; GetTextExtentPoint32W(dc, wl, len, sz); tw = sz[0]; if (sz[0] <= (s32)(w * 0.86f) && sz[1] <= (s32)(h * 1.05f)) break; }
    SetBkMode(dc, 1); SetTextColor(dc, 0x00FFFFFF);
    { s32 sz[2]; GetTextExtentPoint32W(dc, wl, len, sz); TextOutW(dc, ((int)w - sz[0]) / 2, ((int)h - sz[1]) / 2, wl, len); }
    /* a dark outline around the letters (their coverage grown by ~1/16 of the cap), then the letters in pure white */
    {   int o = (int)(h / 16); if (o < 2) o = 2;
        for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) { u32 m = 0;
            for (int dy = -o; dy <= o && m < 255; dy++) for (int dx = -o; dx <= o; dx++) { int xx = (int)x + dx, yy = (int)y + dy; if (xx < 0 || yy < 0 || xx >= (int)w || yy >= (int)h) continue; u32 c = bits[yy * w + xx] & 0xFF; if (c > m) m = c; }
            if (m && (dst[y * pitch + x] >> 24)) { u32 *p = &dst[y * pitch + x], q = *p; u32 k = 255 - m * 3 / 4;   /* darken toward black */
                *p = (q & 0xFF000000) | (((q >> 16) & 255) * k / 255) << 16 | (((q >> 8) & 255) * k / 255) << 8 | ((q & 255) * k / 255); } } }
    for (u32 y = 0; y < h; y++) for (u32 x = 0; x < w; x++) { u32 c = bits[y * w + x] & 0xFF; if (!c) continue; u32 *p = &dst[y * pitch + x], o = *p;
        u32 a = c < 128 ? c * 2 : 255;   /* firmer edges: the coverage doubled */
        u32 rb = ((o >> 16) & 255) * (255 - a) / 255 + a, gb = ((o >> 8) & 255) * (255 - a) / 255 + a, bb = (o & 255) * (255 - a) / 255 + a, al = (o >> 24) > a ? (o >> 24) : a;
        *p = al << 24 | rb << 16 | gb << 8 | bb; }
    SelectObject(dc, ob); DeleteObject(bmp); if (font) DeleteObject(font); (void)tw; (void)bw; (void)bh; }
static void keysheet_build(const u32 *src) {   /* src: the decoded 128x256 sheet */
    for (u32 y = 0; y < 256 * KS; y++) for (u32 x = 0; x < 128 * KS; x++) g_keysheet[y * 128 * KS + x] = src[(y / KS) * 128 + x / KS];
    for (u32 i = 0; i < sizeof g_cells / sizeof g_cells[0]; i++) {
        u32 x0 = g_cells[i].x0, y0 = g_cells[i].y0, w = g_cells[i].x1 - x0 + 1, h = g_cells[i].y1 - y0 + 1; char b[8];
        keycap(&g_keysheet[y0 * KS * 128 * KS + x0 * KS], 128 * KS, w * KS, h * KS, key_cap(g_cells[i].kb == 6 ? 0x1B : g_kbv[g_cells[i].kb], b)); } }   /* B (Back) shows Esc: Esc is Back in the menus (input.c) */

static GLuint tex_get(TI *t) {
    TCACHE *e = 0, *free_ = 0, *old = &g_tc[0]; ab_poll(); kb_poll();
    for (u32 i = 0; i < NTC; i++) {
        TCACHE *c = &g_tc[i];
        if (!c->tex) { if (!free_) free_ = c; continue; }
        if (c->off == t->off && c->fmt == t->fmt && c->rect == (t->linear ? t->rect : 0) && c->pal == t->pal) { e = c; break; }
        if (c->frame < old->frame) old = c;
    }
    if (e && e->frame == g_frame) { g_last_mips = e->mips; return e->tex; }
    u32 h = tex_hash(t);
    if (e && e->hash == h) { e->frame = g_frame; g_last_mips = e->mips; return e->tex; }
    if (!e) { e = free_ ? free_ : old; if (!e->tex) glGenTextures(1, &e->tex); }
    e->off = t->off; e->fmt = t->fmt; e->rect = t->linear ? t->rect : 0; e->pal = t->pal; e->hash = h; e->frame = g_frame; e->w = t->w; e->h = t->h;
    for (u32 y = 0; y < t->h; y++) for (u32 x = 0; x < t->w; x++) g_tbuf[y * t->w + x] = texel(t, x, y);
    {   /* probe (texdump.txt, Milestone 99): every small texture once, as texdump\<hash>_<w>x<h>.bmp (to find the button icons) */
        static u32 on = 2, n, seen[512], ns; if (on == 2) on = GetFileAttributesA("texdump.txt") != 0xFFFFFFFF;
        if (on && t->w <= 512 && t->h <= 512 && (t->w > 128 || t->h > 128) && n < 500) { u32 k = 0; while (k < ns && seen[k] != h) k++;
            if (k == ns && ns < 512) { seen[ns++] = h; n++; CreateDirectoryA("texdump", 0);
                char nm[64]; u32 q = 0; const char *pre = "texdump\\"; while (*pre) nm[q++] = *pre++; for (int s = 28; s >= 0; s -= 4) nm[q++] = "0123456789abcdef"[(h >> s) & 15];
                nm[q++] = '_'; { u32 v = t->w; char d[6]; int z = 0; do d[z++] = (char)('0' + v % 10); while (v /= 10); while (z) nm[q++] = d[--z]; } nm[q++] = 'x'; { u32 v = t->h; char d[6]; int z = 0; do d[z++] = (char)('0' + v % 10); while (v /= 10); while (z) nm[q++] = d[--z]; }
                nm[q++] = '.'; nm[q++] = 'b'; nm[q++] = 'm'; nm[q++] = 'p'; nm[q] = 0;
                HANDLE fh = CreateFileA(nm, 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { u8 hd[54]; zero(hd, 54); u32 sz = t->w * t->h * 4; hd[0] = 'B'; hd[1] = 'M'; *(u32 *)(hd + 2) = 54 + sz; *(u32 *)(hd + 10) = 54; *(u32 *)(hd + 14) = 40;
                    *(s32 *)(hd + 18) = (s32)t->w; *(s32 *)(hd + 22) = -(s32)t->h; *(u16 *)(hd + 26) = 1; *(u16 *)(hd + 28) = 32; *(u32 *)(hd + 34) = sz; DWORD wr; WriteFile(fh, hd, 54, &wr, 0); WriteFile(fh, g_tbuf, sz, &wr, 0); CloseHandle(fh); } } } }
    glBindTexture(0x0DE1, e->tex);
    if (g_keyicons && t->w == 128 && t->h == 256 && h == KEYSHEET_HASH) {   /* Milestone 99: the button prompts as key caps */
        keysheet_build(g_tbuf); glTexImage2D(0x0DE1, 0, 0x8058, 128 * KS, 256 * KS, 0, 0x80E1, 0x1401, g_keysheet);
        e->mips = !t->linear && ((t->fmt >> 16) & 0xF) > 1; if (e->mips) glGenerateMipmap(0x0DE1); g_last_mips = e->mips;
        glTexParameterf(0x0DE1, 0x8501 /* TEXTURE_LOD_BIAS */, -1.0f);   /* drawn small: read from the sharper levels (the key caps' letters) */
        return e->tex; }
    glTexImage2D(0x0DE1, 0, 0x8058 /* RGBA8 */, t->w, t->h, 0, 0x80E1 /* BGRA */, 0x1401, g_tbuf);
    /* mipmaps (Milestone 80): generated on the GPU when the Xbox texture has more than one level */
    e->mips = !t->linear && ((t->fmt >> 16) & 0xF) > 1;
    if (e->mips) glGenerateMipmap(0x0DE1);
    g_last_mips = e->mips;
    return e->tex;
}

/* cube maps (Milestone 88): the NV2A stores the 6 faces one after another (+X -X +Y -Y +Z -Z, as Direct3D), each with
   its whole mip chain, padded to 128 bytes. Brute Force lights its characters (and more) with cube maps; texture mode 3
   used to sample black, which left characters dark. */
typedef struct { u32 off, fmt, hash, frame; GLuint tex; int mips; } CCACHE;
static CCACHE g_cc[64];
static GLuint tex_get_cube(TI *t) {
    u32 levels = (t->fmt >> 16) & 0xF; if (!levels) levels = 1;
    u32 face = 0; for (u32 l = 0; l < levels; l++) { TI s = *t; s.w = t->w >> l; s.h = t->h >> l; if (!s.w) s.w = 1; if (!s.h) s.h = 1; face += tex_bytes(&s); }
    face = (face + 127) & ~127u;
    u32 h = 2166136261u; for (u32 f = 0; f < 6; f++) { TI s = *t; s.off = t->off + f * face; h = (h ^ tex_hash(&s)) * 16777619u; }
    CCACHE *e = 0, *old = &g_cc[0];
    for (u32 i = 0; i < 64; i++) { CCACHE *c = &g_cc[i]; if (c->tex && c->off == t->off && c->fmt == t->fmt) { e = c; break; } if (!c->tex) { old = c; break; } if (c->frame < old->frame) old = c; }
    if (e && (e->frame == g_frame || e->hash == h)) { e->frame = g_frame; g_last_mips = e->mips; return e->tex; }
    if (!e) { e = old; if (!e->tex) glGenTextures(1, &e->tex); }
    e->off = t->off; e->fmt = t->fmt; e->hash = h; e->frame = g_frame; e->mips = levels > 1;
    glBindTexture(0x8513 /* CUBE_MAP */, e->tex);
    for (u32 f = 0; f < 6; f++) { TI s = *t; s.off = t->off + f * face;
        for (u32 y = 0; y < t->h; y++) for (u32 x = 0; x < t->w; x++) g_tbuf[y * t->w + x] = texel(&s, x, y);
        glTexImage2D(0x8515 + f, 0, 0x8058, t->w, t->h, 0, 0x80E1, 0x1401, g_tbuf); }
    if (e->mips) glGenerateMipmap(0x8513);
    g_last_mips = e->mips; return e->tex;
}

/* ---- surfaces: one frame-buffer object per (colour address, size) ---- */
typedef struct { u32 coff, zoff, w, h, used, drawn, gpu_frame; GLuint fbo, col, dep; u32 gs; } FB;   /* gs: GPU scale (Milestone 96: internal resolution) */   /* gpu_frame: when it was last drawn on the GPU (ms) */
#define NFB 64
static FB g_fb[NFB];
static u32 g_hs_surf, g_hs_rast, g_hs_tex;
/* depth buffers (Milestone 88): one per (Xbox depth address, size), shared by every colour surface that uses it, as on
   the NV2A. With a private depth buffer per colour surface, the sun's visibility test (drawn with another colour surface
   but the scene's depth buffer) passed everywhere: full glare even behind mountains. */
typedef struct { u32 zoff, w, h, used; GLuint rb, ctex, cfbo; u32 cframe, gs; } DEP;   /* rb is a depth-stencil TEXTURE (Milestone 88: shadow maps read it); gs: its GPU scale */
static DEP g_dep[32];
/* Milestone 96: internal resolution. Every surface (colour and depth) is allocated at g_res x its Xbox size on the GPU; the game's
   own numbers (sizes, texture coordinates, positions) stay as they are: viewports, scissors, read-backs and pixel counts are scaled
   where they meet the GPU. resscale.txt: the factor (1-4, default 2), read once at start. A surface whose scaled size would pass
   4096 stays at 1 (its depth buffer has the same size, so the same scale). */
static u32 g_res;
static u32 res_scale(u32 w, u32 h) {
    if (!g_res) { g_res = 2; HANDLE f = CreateFileA("resscale.txt", 0x80000000, 1, 0, 3, 0x80, 0);
        if (f != (HANDLE)-1) { char t[8] = {0}; DWORD n = 0; ReadFile(f, t, 7, &n, 0); CloseHandle(f); if (n && t[0] >= 0x31 && t[0] <= 0x34) g_res = t[0] - 0x30; } }
    return (w * g_res <= 4096 && h * g_res <= 4096) ? g_res : 1; }
static GLuint dep_get(u32 zoff, u32 w, u32 h, int *fresh) {
    DEP *d = 0; *fresh = 0;
    for (u32 i = 0; i < 32; i++) if (g_dep[i].rb && g_dep[i].zoff == zoff && g_dep[i].w == w && g_dep[i].h == h) { d = &g_dep[i]; break; }
    if (!d) { for (u32 i = 0; i < 32 && !d; i++) if (!g_dep[i].rb) d = &g_dep[i];
        if (!d) { d = &g_dep[0]; for (u32 i = 1; i < 32; i++) if (g_dep[i].used < d->used) d = &g_dep[i]; }
        if (!d->rb) glGenTextures(1, &d->rb);
        d->zoff = zoff; d->w = w; d->h = h; d->gs = res_scale(w, h); glBindTexture(0x0DE1, d->rb);
        glTexImage2D(0x0DE1, 0, 0x88F0 /* DEPTH24_STENCIL8 */, w * d->gs, h * d->gs, 0, 0x84F9 /* DEPTH_STENCIL */, 0x84FA /* UNSIGNED_INT_24_8 */, 0);
        if (d->ctex) { glBindTexture(0x0DE1, d->ctex); glTexImage2D(0x0DE1, 0, 0x8058, w * d->gs, h * d->gs, 0, 0x80E1, 0x1401, 0); glBindTexture(0x0DE1, d->rb); }   /* a reused slot: its colour copy at the new size */
        glTexParameteri(0x0DE1, 0x2801, 0x2601); glTexParameteri(0x0DE1, 0x2800, 0x2601); glTexParameteri(0x0DE1, 0x2802, 0x812F); glTexParameteri(0x0DE1, 0x2803, 0x812F);
        *fresh = 1; }
    d->used = g_frame; return d->rb;
}
static DEP *dep_by_addr(u32 a) { DEP *b = 0; for (u32 i = 0; i < 32; i++) if (g_dep[i].rb && g_dep[i].zoff == a && (!b || g_dep[i].w * g_dep[i].h > b->w * b->h)) b = &g_dep[i]; return b; }   /* the largest one at that address */
static FB *fb_get(u32 coff, u32 zoff, u32 w, u32 h) {
    FB *f = 0;
    for (u32 i = 0; i < NFB; i++) if (g_fb[i].fbo && g_fb[i].coff == coff && g_fb[i].w == w && g_fb[i].h == h) { f = &g_fb[i]; break; }
    if (!f) {
        for (u32 i = 0; i < NFB && !f; i++) if (!g_fb[i].fbo) f = &g_fb[i];
        if (!f) { f = &g_fb[0]; for (u32 i = 1; i < NFB; i++) if (g_fb[i].used < f->used) f = &g_fb[i]; }
        if (!f->fbo) { glGenFramebuffers(1, &f->fbo); glGenTextures(1, &f->col); }
        f->coff = coff; f->w = w; f->h = h; f->dep = 0; f->gs = res_scale(w, h);
        glBindTexture(0x0DE1, f->col); glTexImage2D(0x0DE1, 0, 0x8058, w * f->gs, h * f->gs, 0, 0x80E1, 0x1401, 0);
        glTexParameteri(0x0DE1, 0x2801, 0x2601); glTexParameteri(0x0DE1, 0x2800, 0x2601);
        glBindFramebuffer(0x8D40, f->fbo);
        glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, f->col, 0);
        glClearColor(0, 0, 0, 1); glDisable(0x0C11); glColorMask(1, 1, 1, 1); glClear(0x4000);   /* colour only: the depth buffer may be shared */
        g_hs_surf = g_hs_rast = g_hs_tex = 0;   /* this disturbed the cached GL state */
    }
    {   int fresh; GLuint rb = dep_get(zoff, w, h, &fresh);
        if (f->dep != rb || f->zoff != zoff) { f->dep = rb; f->zoff = zoff; glBindFramebuffer(0x8D40, f->fbo); glFramebufferTexture2D(0x8D40, 0x821A /* DEPTH_STENCIL_ATTACHMENT */, 0x0DE1, rb, 0); g_hs_surf = g_hs_rast = g_hs_tex = 0; }
        if (fresh) { glBindFramebuffer(0x8D40, f->fbo); glDisable(0x0C11); glClearDepth(1); glClearStencil(0); glDepthMask(1); glStencilMask(0xFF); glClear(0x0500); g_hs_surf = g_hs_rast = g_hs_tex = 0; } }
    f->used = g_frame;
    return f;
}
static FB *fb_by_color(u32 coff) { FB *b = 0; for (u32 i = 0; i < NFB; i++) if (g_fb[i].fbo && g_fb[i].coff == coff && (!b || g_fb[i].used > b->used)) b = &g_fb[i]; return b; }

/* the current surface, from the registers */
static FB *surface(u32 *W, u32 *H, u32 *zbits) {
    u32 f = K(0x208), hz = K(0x200), vt = K(0x204);
    u32 w = (hz & 0xFFFF) + (hz >> 16), h = (vt & 0xFFFF) + (vt >> 16);
    if (((f >> 8) & 0xF) == 2) { w = 1u << ((f >> 16) & 0xF); h = 1u << ((f >> 24) & 0xF); }
    if (!w || !h || w > 4096 || h > 4096) return 0;
    *W = w; *H = h; *zbits = ((f >> 4) & 0xF) == 1 ? 16 : 24;
    return fb_get(K(0x210), K(0x214), w, h);
}

/* ---- the uber shader ---- */
static const char *g_vs =
"#version 130\n"
"in vec4 a_pos, a_d0, a_d1, a_t0, a_t1, a_t2, a_t3; in float a_fog;\n"
"uniform vec4 u_vp; uniform vec2 u_hud;\n"
"out vec4 v_d0, v_d1, v_t0, v_t1, v_t2, v_t3; out float v_fogc;\n"
"void main() {\n"
"  float w = a_pos.w; if (w == 0.0) w = 1.0;\n"
"  float px = a_pos.x; if (u_hud.x > 0.0 && abs(w - 1.0) <= 1e-5) px = u_hud.y + (px - u_hud.y) * u_hud.x;\n"   /* Milestone 96: the HUD in widescreen */
"  vec3 n = vec3(px * u_vp.x - 1.0, a_pos.y * u_vp.y - 1.0, a_pos.z * u_vp.z - 1.0);\n"
"  gl_Position = vec4(n * w, w);\n"
"  v_d0 = a_d0; v_d1 = a_d1; v_t0 = a_t0; v_t1 = a_t1; v_t2 = a_t2; v_t3 = a_t3; v_fogc = a_fog;\n"
"}\n";
static const char *g_fs =
"#version 130\n"
"in vec4 v_d0, v_d1, v_t0, v_t1, v_t2, v_t3; in float v_fogc;\n"
"uniform sampler2D s0, s1, s2, s3; uniform samplerCube c0, c1, c2, c3; uniform sampler2DShadow d0, d1, d2, d3; uniform ivec4 u_shadow; uniform vec4 u_zscale, u_sbias; uniform vec4 u_fogp; uniform int u_fogon;\n"
"uniform ivec4 u_tmode; uniform vec4 u_tscale[4];\n"
"uniform int u_nst, u_muxmsb;\n"
"uniform ivec4 u_crg[8], u_crep[8], u_cmap[8], u_arg[8], u_aal[8], u_amap[8], u_cdst[8], u_adst[8];\n"
"uniform ivec2 u_op[8];\n"
"uniform vec4 u_f0[8], u_f1[8], u_fog;\n"
"uniform ivec4 u_fa, u_fb, u_frep, u_finv;\n"
"uniform ivec2 u_atest;\n"
"out vec4 o_col;\n"
"vec4 R[16];\n"
"float mapf(float x, int m) { if (m == 0) return max(x, 0.0); if (m == 1) return 1.0 - clamp(x, 0.0, 1.0); if (m == 2) return 2.0 * max(x, 0.0) - 1.0;\n"
"  if (m == 3) return -2.0 * max(x, 0.0) + 1.0; if (m == 4) return max(x, 0.0) - 0.5; if (m == 5) return -max(x, 0.0) + 0.5; if (m == 6) return x; return -x; }\n"
"float opf(float x, int o) { if (o == 1) x -= 0.5; else if (o == 2) x *= 2.0; else if (o == 3) x = 2.0 * (x - 0.5); else if (o == 4) x *= 4.0; else if (o == 6) x *= 0.5; return clamp(x, -1.0, 1.0); }\n"
"vec4 opv(vec4 v, ivec2 o) { return vec4(opf(v.r, o.x), opf(v.g, o.x), opf(v.b, o.x), opf(v.a, o.y)); }\n"
"vec4 fin(int reg, int rep, int inv) { vec4 c = R[reg]; if (rep != 0) c = vec4(c.a); c = clamp(c, 0.0, 1.0); return inv != 0 ? vec4(1.0) - c : c; }\n"
"vec4 tx(sampler2D s, int m, vec4 t, vec4 sc) {\n"
"  if (m == 4) return clamp(t, 0.0, 1.0);\n"
"  if (m != 1 && m != 2) return vec4(0.0);\n"
"  vec2 c = t.xy; if (t.w != 0.0 && t.w != 1.0) c /= t.w;\n"
"  return texture(s, c * sc.xy + sc.zw); }\n"
"void main() {\n"
"  for (int i = 0; i < 16; i++) R[i] = vec4(0.0);\n"
"  R[3] = u_fog; R[4] = clamp(v_d0, 0.0, 1.0); R[5] = clamp(v_d1, 0.0, 1.0);\n"
"  if (u_fogon != 0) { float d = v_fogc; int m = int(u_fogp.w); if (m >= 2) d = abs(d); float f;\n"
"    if (m == 0 || m == 2) f = u_fogp.x + d * u_fogp.y - 1.0; else if (m == 1 || m == 3) f = exp2(-d * u_fogp.y * 1.442695); else f = exp2(-d * d * u_fogp.y * u_fogp.y * 1.442695);\n"
"    R[3].a = clamp(f, 0.0, 1.0); }\n"
"  R[8] = u_tmode.x == 3 ? texture(c0, v_t0.xyz) : tx(s0, u_tmode.x, v_t0, u_tscale[0]); R[9] = u_tmode.y == 3 ? texture(c1, v_t1.xyz) : tx(s1, u_tmode.y, v_t1, u_tscale[1]);\n"
"  R[10] = u_tmode.z == 3 ? texture(c2, v_t2.xyz) : tx(s2, u_tmode.z, v_t2, u_tscale[2]); R[11] = u_tmode.w == 3 ? texture(c3, v_t3.xyz) : tx(s3, u_tmode.w, v_t3, u_tscale[3]);\n"
"  if (u_shadow.x == 1) R[8] = vec4(texture(d0, vec3(v_t0.xy / v_t0.w * u_tscale[0].xy, v_t0.z / v_t0.w / u_zscale.x + u_sbias.x)));\n"
"  if (u_shadow.y == 1) R[9] = vec4(texture(d1, vec3(v_t1.xy / v_t1.w * u_tscale[1].xy, v_t1.z / v_t1.w / u_zscale.y + u_sbias.y)));\n"
"  if (u_shadow.z == 1) R[10] = vec4(texture(d2, vec3(v_t2.xy / v_t2.w * u_tscale[2].xy, v_t2.z / v_t2.w / u_zscale.z + u_sbias.z)));\n"
"  if (u_shadow.w == 1) R[11] = vec4(texture(d3, vec3(v_t3.xy / v_t3.w * u_tscale[3].xy, v_t3.z / v_t3.w / u_zscale.w + u_sbias.w)));\n"
"  if (u_shadow.x == 2) R[8] = vec4(1.0); if (u_shadow.y == 2) R[9] = vec4(1.0); if (u_shadow.z == 2) R[10] = vec4(1.0); if (u_shadow.w == 2) R[11] = vec4(1.0);\n"
"  if (u_shadow.x == 3) R[8] = vec4(0.0); if (u_shadow.y == 3) R[9] = vec4(0.0); if (u_shadow.z == 3) R[10] = vec4(0.0); if (u_shadow.w == 3) R[11] = vec4(0.0);\n"
"  if (u_shadow.x == 4) R[8] = v_t0.z / v_t0.w / u_zscale.x >= 1.0 || v_t0.w <= 0.0 ? vec4(0.0) : vec4(texture(d0, vec3(v_t0.xy / v_t0.w * u_tscale[0].xy, v_t0.z / v_t0.w / u_zscale.x + u_sbias.x)));\n"
"  if (u_shadow.y == 4) R[9] = v_t1.z / v_t1.w / u_zscale.y >= 1.0 || v_t1.w <= 0.0 ? vec4(0.0) : vec4(texture(d1, vec3(v_t1.xy / v_t1.w * u_tscale[1].xy, v_t1.z / v_t1.w / u_zscale.y + u_sbias.y)));\n"
"  if (u_shadow.z == 4) R[10] = v_t2.z / v_t2.w / u_zscale.z >= 1.0 || v_t2.w <= 0.0 ? vec4(0.0) : vec4(texture(d2, vec3(v_t2.xy / v_t2.w * u_tscale[2].xy, v_t2.z / v_t2.w / u_zscale.z + u_sbias.z)));\n"
"  if (u_shadow.w == 4) R[11] = v_t3.z / v_t3.w / u_zscale.w >= 1.0 || v_t3.w <= 0.0 ? vec4(0.0) : vec4(texture(d3, vec3(v_t3.xy / v_t3.w * u_tscale[3].xy, v_t3.z / v_t3.w / u_zscale.w + u_sbias.w)));\n"
"  R[12].a = R[8].a;\n"
"  for (int s = 0; s < u_nst; s++) {\n"
"    R[1] = u_f0[s]; R[2] = u_f1[s];\n"
"    vec4 I[4];\n"
"    for (int j = 0; j < 4; j++) {\n"
"      vec4 c = R[u_crg[s][j]]; vec3 cc = u_crep[s][j] != 0 ? vec3(c.a) : c.rgb;\n"
"      vec4 a = R[u_arg[s][j]]; float aa = u_aal[s][j] != 0 ? a.a : a.b;\n"
"      int cm = u_cmap[s][j];\n"
"      I[j] = vec4(mapf(cc.r, cm), mapf(cc.g, cm), mapf(cc.b, cm), mapf(aa, u_amap[s][j]));\n"
"    }\n"
"    vec4 AB = I[0] * I[1], CD = I[2] * I[3]; int fl = u_cdst[s].w;\n"
"    if ((fl & 1) != 0) AB.rgb = vec3(dot(I[0].rgb, I[1].rgb));\n"
"    if ((fl & 2) != 0) CD.rgb = vec3(dot(I[2].rgb, I[3].rgb));\n"
"    bool sel = u_muxmsb != 0 ? R[12].a >= 0.5 : (int(clamp(R[12].a, 0.0, 1.0) * 255.0 + 0.5) & 1) != 0;\n"
"    vec4 SUM = AB + CD, MX = sel ? CD : AB;\n"
"    if ((fl & 4) != 0) SUM.rgb = MX.rgb; if (u_adst[s].w != 0) SUM.a = MX.a;\n"
"    AB = opv(AB, u_op[s]); CD = opv(CD, u_op[s]); SUM = opv(SUM, u_op[s]);\n"
"    int d = u_cdst[s].x; if (d != 0) { R[d].rgb = AB.rgb; if ((fl & 8) != 0) R[d].a = AB.b; }\n"
"    d = u_cdst[s].y; if (d != 0) { R[d].rgb = CD.rgb; if ((fl & 16) != 0) R[d].a = CD.b; }\n"
"    d = u_cdst[s].z; if (d != 0) R[d].rgb = SUM.rgb;\n"
"    d = u_adst[s].x; if (d != 0) R[d].a = AB.a;\n"
"    d = u_adst[s].y; if (d != 0) R[d].a = CD.a;\n"
"    d = u_adst[s].z; if (d != 0) R[d].a = SUM.a;\n"
"  }\n"
"  vec4 E = fin(u_fb.x, u_frep.y & 1, u_finv.y & 1), F = fin(u_fb.y, (u_frep.y >> 1) & 1, (u_finv.y >> 1) & 1);\n"
"  R[15] = vec4(E.rgb * F.rgb, 1.0);\n"
"  vec4 v1 = clamp(R[5], 0.0, 1.0), r0 = clamp(R[12], 0.0, 1.0);\n"
"  if ((u_fb.w & 2) != 0) v1 = vec4(1.0) - v1; if ((u_fb.w & 4) != 0) r0 = vec4(1.0) - r0;\n"
"  vec4 vr = v1 + r0; if ((u_fb.w & 1) != 0) vr = min(vr, vec4(1.0)); R[14] = vec4(vr.rgb, 1.0);\n"
"  vec4 A = fin(u_fa.x, u_frep.x & 1, u_finv.x & 1), B = fin(u_fa.y, (u_frep.x >> 1) & 1, (u_finv.x >> 1) & 1);\n"
"  vec4 C = fin(u_fa.z, (u_frep.x >> 2) & 1, (u_finv.x >> 2) & 1), D = fin(u_fa.w, (u_frep.x >> 3) & 1, (u_finv.x >> 3) & 1);\n"
"  vec3 rgb = clamp(A.rgb * B.rgb + (vec3(1.0) - A.rgb) * C.rgb + D.rgb, 0.0, 1.0);\n"
"  vec4 g = R[u_fb.z]; float ga = clamp((u_frep.y & 4) != 0 ? g.a : g.b, 0.0, 1.0); if ((u_finv.y & 4) != 0) ga = 1.0 - ga;\n"
"  if (u_atest.x >= 0) { int a8 = int(ga * 255.0 + 0.5), r = u_atest.y; int f = u_atest.x; bool ok = f == 7 || (f == 1 && a8 < r) || (f == 2 && a8 == r) || (f == 3 && a8 <= r) || (f == 4 && a8 > r) || (f == 5 && a8 != r) || (f == 6 && a8 >= r); if (!ok) discard; }\n"
"  o_col = vec4(rgb, ga);\n"
"}\n";
static GLuint g_prog, g_vbo, g_vao;
static GLint u_vp, u_tmode, u_tscale, u_nst, u_muxmsb, u_crg, u_crep, u_cmap, u_arg, u_aal, u_amap, u_cdst, u_adst, u_op, u_f0, u_f1, u_fog, u_fa, u_fb, u_frep, u_finv, u_atest, u_c, u_fogp, u_fogon, u_shadow, u_zscale, u_sbias, u_hud;
/* one linked program: its uniform locations (copied into the globals above when it is used) */
typedef struct { u32 key, used, cgen, ustate; GLuint id; GLint loc[29]; f32 hud[2]; } PROG;   /* ustate: hash of the state its uniforms were set for */   /* cgen: the constants generation it last got */   /* hud: the u_hud it holds */
#define ULIST(X) X(u_vp) X(u_tmode) X(u_tscale) X(u_nst) X(u_muxmsb) X(u_crg) X(u_crep) X(u_cmap) X(u_arg) X(u_aal) X(u_amap) X(u_cdst) X(u_adst) X(u_op) X(u_f0) X(u_f1) X(u_fog) X(u_fa) X(u_fb) X(u_frep) X(u_finv) X(u_atest) X(u_c) X(u_fogp) X(u_fogon) X(u_shadow) X(u_zscale) X(u_sbias) X(u_hud)
/* Milestone 96: the 2D HUD in a widescreen window. Screen-space vertices (w == 1) are squeezed horizontally by u_hud.x (= 4:3 / window
   aspect) about the x u_hud.y: 0, 320 or 640 by the screen third the draw's centre is in, so each piece keeps its shape and its side of
   the screen, and the pieces of one element (the radar and its dots) stay together. 3D vertices are never touched. nohudfix.txt = off. */
static f32 g_hud_k = 1.0f;
static void prog_locs(PROG *p) { u32 i = 0;
    #define GETL(n) p->loc[i++] = glGetUniformLocation(p->id, #n);
    ULIST(GETL)
    glUseProgram(p->id); { static const char *cn[4] = {"c0", "c1", "c2", "c3"}, *dn[4] = {"d0", "d1", "d2", "d3"}; for (int k = 0; k < 4; k++) { glUniform1i(glGetUniformLocation(p->id, cn[k]), 4 + k); glUniform1i(glGetUniformLocation(p->id, dn[k]), 8 + k); } } glUniform1i(glGetUniformLocation(p->id, "s0"), 0); glUniform1i(glGetUniformLocation(p->id, "s1"), 1); glUniform1i(glGetUniformLocation(p->id, "s2"), 2); glUniform1i(glGetUniformLocation(p->id, "s3"), 3); }
static PROG *g_curp;
static void prog_use(PROG *p) { if (g_curp == p) return; g_curp = p; u32 i = 0;
    #define SETL(n) n = p->loc[i++];
    ULIST(SETL)
    glUseProgram(p->id); }
static void hud_uniform(f32 k, f32 anchor) { if (!g_curp || u_hud < 0) return; if (g_curp->hud[0] != k || g_curp->hud[1] != anchor) { g_curp->hud[0] = k; g_curp->hud[1] = anchor; glUniform2f(u_hud, k, anchor); } }
static PROG g_fixedp;   /* the program for vertices the loader transformed */
static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, 0); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, 0x8B81, &ok);
    if (!ok) { char log[200]; GLsizei n = 0; glGetShaderInfoLog(s, 199, &n, log); log[n < 199 ? n : 199] = 0; info(" shader: "); info(log); return 0; }
    return s;
}

/* ---- state for one draw ---- */
static void unpack4(u32 c, f32 *o) { o[0] = ((c >> 16) & 255) / 255.0f; o[1] = ((c >> 8) & 255) / 255.0f; o[2] = (c & 255) / 255.0f; o[3] = (c >> 24) / 255.0f; }
static FB *g_cur_fb; static int g_skip_draw, g_noshadow;   /* g_noshadow: noshadowmap.txt (A/B) */   /* set by apply_state for the A/B test nortt.txt */
/* Milestone 81: each group of state is only re-applied when the registers it depends on changed (hashes) */
static u32 hrange(u32 a, u32 b) { u32 h = 2166136261u; for (u32 m = a; m < b; m += 4) h = (h ^ K(m)) * 16777619u; return h; }
static f32 g_wide_k = 1.0f;   /* Milestone 96: horizontal squeeze of the 3D scene = (4/3) / window aspect (1 for a 4:3 window); set in present_tex */
static GLint g_tmode_c[4], g_shad_c[4]; static f32 g_tsc_c[16], g_zs_c[4], g_sb_c[4];
static void state_cache_reset(void) { g_hs_surf = g_hs_rast = g_hs_tex = 0; }
/* Milestone 89: a depth buffer read as a colour texture (the glow pass reads the scene's depth-stencil buffer as R8G8B8A8:
   its alpha = stencil = the per-object glow threshold). OpenGL keeps depth/stencil on the GPU, so the stencil is copied into
   the alpha of a colour texture (once per frame per buffer). Without it the threshold read 0 and nearly everything glowed (haze). */
static GLuint g_cvprog, g_cvao; static GLint g_cv_st;
/* Milestone 96: RGB = the 24 depth bits too (R = bits 23-16, G = 15-8, B = 7-0, as the R8G8B8A8 read of a Z24S8 word gives them).
   With RGB 0, an effect that reads depth from this texture (in the Rescue Brutus cutscene) saw "all near" and darkened the screen
   for a moment. The depth and stencil views of one texture can't be sampled together: two passes, depth -> RGB, stencil -> A. */
static GLuint g_cvprog_d; static GLint g_cv_dt;
/* dcollog.txt (Milestone 96, checked every second): every draw that reads a depth buffer as colour -> dcol_log.txt (hex):
   frame, stage, texture address/format, buffer size, target width and colour offset, blend enable/funcs, colour mask,
   combiner registers 0x1E60 0x288 0x28C 0x260 0xAC0 0xA60 0xA80 0x1E40 0x1E44, alpha test; and "SHOT n" lines at F12 */
static u32 g_dcol_on = 1;   /* records are kept in a ring (the last 4096 reads) and written to dcol_log.txt at each F12 */
#define DCOL_N 2048
#define DCOL_W 64
static u32 g_dcol_ring[DCOL_N][DCOL_W], g_dcol_w;
static void dcol_line(const u32 *v, int n, const char *tag) {
    if (tag[0] == 'R') { u32 *r = g_dcol_ring[g_dcol_w++ % DCOL_N]; for (int z = 0; z < DCOL_W; z++) r[z] = z < n ? v[z] : 0; return; }
    if (GetFileAttributesA("dcollog.txt") == 0xFFFFFFFF) return;
    HANDLE fh = CreateFileA("dcol_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh == (HANDLE)-1) return;
    static char tx[DCOL_N * (DCOL_W * 9 + 4) + 200]; u32 q = 0;
    u32 first = g_dcol_w > DCOL_N ? g_dcol_w - DCOL_N : 0;
    for (u32 k = first; k < g_dcol_w; k++) { u32 *r = g_dcol_ring[k % DCOL_N]; tx[q++] = 'R';
        for (int z = 0; z < DCOL_W; z++) { tx[q++] = 0x20; for (int s = 28; s >= 0; s -= 4) tx[q++] = "0123456789ABCDEF"[(r[z] >> s) & 15]; } tx[q++] = 13; tx[q++] = 10; }
    for (const char *s = tag; *s; s++) tx[q++] = *s;
    for (int z = 0; z < n; z++) { tx[q++] = 0x20; for (int s = 28; s >= 0; s -= 4) tx[q++] = "0123456789ABCDEF"[(v[z] >> s) & 15]; } tx[q++] = 13; tx[q++] = 10;
    DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); g_dcol_w = 0; }
static int is_depth_fmt(u32 cf) { return cf >= 0x2A && cf <= 0x31; }
static void dep_color(DEP *d) {
    if (d->cframe == g_frame + 1 && g_ab != 6) return; d->cframe = g_frame + 1;   /* abtest.txt 6: copy at every read (test) */
    if (!g_cvprog) {
        GLuint vs = compile(0x8B31, "#version 130\nvoid main() { gl_Position = vec4(float((gl_VertexID & 1) * 4 - 1), float((gl_VertexID >> 1) * 4 - 1), 0.0, 1.0); }\n");
        GLuint fs = compile(0x8B30, "#version 130\nuniform usampler2D st; out vec4 o;\nvoid main() { uint s = texelFetch(st, ivec2(gl_FragCoord.xy), 0).r; o = vec4(0.0, 0.0, 0.0, float(s) / 255.0); }\n");
        if (!vs || !fs) { g_cvprog = 0xFFFFFFFF; return; }
        g_cvprog = glCreateProgram(); glAttachShader(g_cvprog, vs); glAttachShader(g_cvprog, fs); glBindFragDataLocation(g_cvprog, 0, "o"); glLinkProgram(g_cvprog);
        glGenVertexArrays(1, &g_cvao); g_cv_st = glGetUniformLocation(g_cvprog, "st");
        GLuint fs2 = compile(0x8B30, "#version 130\nuniform sampler2D dt; out vec4 o;\nvoid main() { float d = texelFetch(dt, ivec2(gl_FragCoord.xy), 0).r; uint z = uint(clamp(d, 0.0, 1.0) * 16777215.0 + 0.5);\n"
            "  o = vec4(float((z >> 16u) & 255u) / 255.0, float((z >> 8u) & 255u) / 255.0, float(z & 255u) / 255.0, 0.0); }\n");
        if (fs2) { g_cvprog_d = glCreateProgram(); glAttachShader(g_cvprog_d, vs); glAttachShader(g_cvprog_d, fs2); glBindFragDataLocation(g_cvprog_d, 0, "o"); glLinkProgram(g_cvprog_d); g_cv_dt = glGetUniformLocation(g_cvprog_d, "dt"); } }
    if (g_cvprog == 0xFFFFFFFF) return;
    if (!d->ctex) { glGenTextures(1, &d->ctex); glGenFramebuffers(1, &d->cfbo); }
    glActiveTexture(0x84C0 + 12); glBindTexture(0x0DE1, d->ctex);
    glTexImage2D(0x0DE1, 0, 0x8058, d->w * d->gs, d->h * d->gs, 0, 0x80E1, 0x1401, 0);   /* the same GPU size as the depth buffer it copies */
    glTexParameteri(0x0DE1, 0x2801, 0x2601); glTexParameteri(0x0DE1, 0x2800, 0x2601);
    glBindFramebuffer(0x8D40, d->cfbo); glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, d->ctex, 0);
    glViewport(0, 0, d->w * d->gs, d->h * d->gs); glDisable(0x0C11); glDisable(0x0BE2); glDisable(0x0B71); glDisable(0x0B90); glDisable(0x0B44); glDisable(0x8037);
    glBindTexture(0x0DE1, d->rb); glTexParameteri(0x0DE1, 0x884C, 0);   /* no depth compare (the shadow map code may have left it on) */
    glTexParameteri(0x0DE1, 0x2801, 0x2600); glTexParameteri(0x0DE1, 0x2800, 0x2600);
    if (g_cvprog_d && g_ab != 5) {   /* pass 1: depth -> RGB (abtest.txt 5 = RGB 0 as before) */
        glTexParameteri(0x0DE1, 0x90EA, 0x1902 /* DEPTH_COMPONENT */); glColorMask(1, 1, 1, 0);
        glUseProgram(g_cvprog_d); glUniform1i(g_cv_dt, 12); glBindVertexArray(g_cvao); glDrawArrays(4, 0, 3); glBindVertexArray(g_vao); }
    else { glColorMask(1, 1, 1, 1); glClearColor(0, 0, 0, 0); glClear(0x4000); }
    glTexParameteri(0x0DE1, 0x90EA, 0x1901 /* STENCIL_INDEX */); glColorMask(0, 0, 0, 1);   /* pass 2: stencil -> A */
    glUseProgram(g_cvprog); glUniform1i(g_cv_st, 12); glBindVertexArray(g_cvao); glDrawArrays(4, 0, 3); glBindVertexArray(g_vao);
    glColorMask(1, 1, 1, 1);
    glTexParameteri(0x0DE1, 0x90EA, 0x1902 /* DEPTH_COMPONENT */); glTexParameteri(0x0DE1, 0x2801, 0x2601); glTexParameteri(0x0DE1, 0x2800, 0x2601);
    g_curp = 0; g_hs_surf = g_hs_rast = 0; g_r->pad[48]++;
}
static int apply_state(PROG *P) {
    {   /* depth buffers read as colour textures: convert before any of this draw's state is set */
        static u32 dc = 2; if (dc == 2) dc = GetFileAttributesA("nodepthcolor.txt") == 0xFFFFFFFF;
        u32 sp0 = K(0x1E70);
        if (dc) for (u32 i = 0; i < 4; i++) { TI t; if (((sp0 >> (i * 5)) & 0x1F) && ((K(0x1B0C + i * 0x40) >> 30) & 1) && tex_info(i, &t) && !is_depth_fmt(t.cf)) { DEP *d = dep_by_addr(t.off); if (d && !fb_by_color(t.off)) dep_color(d); } } }
    u32 W, H, zb; FB *f = surface(&W, &H, &zb); if (!f) return 0;
    g_cur_fb = f; f->drawn = 1; f->gpu_frame = GetTickCount();
    u32 hs = hrange(0x200, 0x218) ^ (u32)f;
    if (hs != g_hs_surf) {
        u32 gs = f->gs ? f->gs : 1; glBindFramebuffer(0x8D40, f->fbo); glViewport(0, 0, W * gs, H * gs);   /* Milestone 96: GPU size */
        u32 hz = K(0x200), vt = K(0x204);
        glEnable(0x0C11); glScissor((hz & 0xFFFF) * gs, (vt & 0xFFFF) * gs, (hz >> 16) * gs, (vt >> 16) * gs);
        g_hs_surf = hs;
    }
    f32 zmax = zb == 16 ? 65535.0f : 16777215.0f;
    prog_use(P);
    u32 hr = hrange(0x300, 0x3A4);
    if (hr != g_hs_rast) { g_hs_rast = hr;
    /* blending, depth, stencil, culling: NV2A uses OpenGL's own enum values */
    if (K(0x304)) { glEnable(0x0BE2); glBlendFunc(K(0x344), K(0x348)); glBlendEquation(K(0x350) ? K(0x350) : 0x8006); f32 bc[4]; unpack4(K(0x34C), bc); glBlendColor(bc[0], bc[1], bc[2], bc[3]); } else glDisable(0x0BE2);
    if (K(0x30C)) { glEnable(0x0B71); glDepthFunc(K(0x354)); } else glDisable(0x0B71);
    glDepthMask(K(0x35C) ? 1 : 0);
    u32 cm = K(0x358); glColorMask((cm >> 16) & 1, (cm >> 8) & 1, cm & 1, (cm >> 24) & 1);
    if (K(0x32C)) { glEnable(0x0B90); glStencilFunc(K(0x364), K(0x368) & 0xFF, K(0x36C) & 0xFF); glStencilOp(K(0x370), K(0x374), K(0x378)); glStencilMask(K(0x360) & 0xFF); } else glDisable(0x0B90);
    {   /* Milestone 89: polygon offset (NV2A 0x320 fill enable, 0x384 scale, 0x388 bias as floats); the shadow-map pass uses it, without it the character showed self-shadow lines */
        u32 sf = K(0x384), sb = K(0x388); f32 fs = *(f32 *)&sf, fb = *(f32 *)&sb;
        if (K(0x320)) { glEnable(0x8037); glPolygonOffset(fs, fb); } else glDisable(0x8037); }
    if (K(0x308)) { glEnable(0x0B44); glCullFace(K(0x39C)); glFrontFace(K(0x3A0) == 0x900 ? 0x0901 : 0x0900); } else glDisable(0x0B44);   /* y is mirrored: CW on the Xbox is CCW here */
    }
    /* textures */
    GLint *tmode = g_tmode_c; f32 *tsc = g_tsc_c;
    u32 sp = K(0x1E70), ht = hrange(0x1B00, 0x1C00) ^ sp ^ (g_frame * 2654435761u);
    if (ht != g_hs_tex) { g_hs_tex = ht; g_skip_draw = 0;
    for (u32 i = 0; i < 4; i++) {
        tmode[i] = (sp >> (i * 5)) & 0x1F; g_shad_c[i] = 0; g_zs_c[i] = 1; g_sb_c[i] = 0; tsc[i * 4] = tsc[i * 4 + 1] = 1; tsc[i * 4 + 2] = tsc[i * 4 + 3] = 0;
        {   /* modelog.txt: how often each texture shader mode is used (per stage), written once after 60 s to mode_log.txt */
            static u32 on = 2, cnt[4][32], t0, done; if (on == 2) on = GetFileAttributesA("modelog.txt") != 0xFFFFFFFF;
            if (on && !done) { if (!t0) t0 = GetTickCount(); cnt[i][tmode[i]]++;
                if (GetTickCount() - t0 > 60000) { done = 1; char tx[2000]; u32 q = 0;
                    for (u32 s = 0; s < 4; s++) for (u32 m = 0; m < 32; m++) if (cnt[s][m]) { u32 vv[3] = {s, m, cnt[s][m]}; for (int k = 0; k < 3; k++) { u32 v = vv[k]; char d[12]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q++] = d[--z]; tx[q++] = k < 2 ? 0x3A : 0x20; } }
                    HANDLE fh = CreateFileA("mode_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } }
        glActiveTexture(0x84C0 + i);
        TI t; GLuint tex = 0; int dcol_used = 0;
        if (tmode[i] && ((K(0x1B0C + i * 0x40) >> 30) & 1) && tex_info(i, &t)) {
            FB *rt = fb_by_color(t.off); u32 sub_x = 0, sub_y = 0;
            if (!rt && t.linear && t.bpp == 4 && g_ab != 8) {   /* Milestone 96: a texture that starts inside a drawn surface (the cutscene depth-of-field reads its
                                                                  first blur level at row 34 of the 320x240 glow surface): that surface, shifted. abtest.txt 8 = off */
                u32 a = t.off & 0x03FFFFFF;
                for (u32 k = 0; k < NFB; k++) { FB *f = &g_fb[k]; u32 pitch = f->w * 4;
                    if (f->fbo && f->drawn && a > f->coff && a < f->coff + pitch * f->h && (!rt || f->used > rt->used)) { rt = f; sub_x = (a - f->coff) % pitch / 4; sub_y = (a - f->coff) / pitch; } } }
            g_last_mips = 0;
            DEP *dcol = (!rt && !is_depth_fmt(t.cf)) ? dep_by_addr(t.off) : 0; if (dcol && !dcol->ctex) dcol = 0;
            if (rt && rt->drawn && GetTickCount() - rt->gpu_frame > 100 && g_ab != 4) { rt = 0; g_r->pad[44]++; }   /* abtest.txt 4: keep old surfaces (Milestone 96 test) */   /* Milestone 87: a surface drawn long ago is no longer what that memory holds (textures streamed into it) */
            {   static u32 nr = 2; if (nr == 2) nr = GetFileAttributesA("nortt.txt") != 0xFFFFFFFF;   /* A/B: skip draws that read rendered surfaces */
                if (nr && rt && rt->drawn) g_skip_draw = 1; }
            if (rt && rt->drawn) {   /* rttlog.txt: details of draws that read rendered surfaces */
                static u32 on3 = 2, nl3, t3; if (on3 == 2) on3 = GetFileAttributesA("rttlog.txt") != 0xFFFFFFFF; if (on3 && !t3) t3 = GetTickCount();
                if (on3 && nl3 < 60 && GetTickCount() - t3 > 60000) { nl3++; u32 vals[14] = {i, t.off, rt->w, rt->h, t.w, t.h, t.linear, tmode[i], K(0x304), K(0x344), K(0x348), g_cur_fb ? g_cur_fb->coff : 0, g_cur_fb ? g_cur_fb->w : 0, K(0x1B0C + i * 0x40)};
                    char tx[300]; u32 q = 0; for (int k = 0; k < 14; k++) { u32 v = vals[k]; char d[12]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q++] = d[--z]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
                    HANDLE fh = CreateFileA("rtt_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } }
                {   /* glowlog.txt: combiner registers of draws reading rendered surfaces, once per distinct state -> glow_log.txt (hex) */
                    static u32 on4, seen4[16], ns4, tl4; { u32 now = GetTickCount(); if (now - tl4 > 1000) { tl4 = now; u32 was = on4; on4 = GetFileAttributesA("glowlog.txt") != 0xFFFFFFFF; if (on4 && !was) ns4 = 0; } }
                    u32 hh = hrange(0x260, 0x290) ^ hrange(0xA60, 0xAE0) * 7 ^ hrange(0xAE0, 0xB00) * 3 ^ hrange(0x1E40, 0x1E64) * 13 ^ K(0x288) * 5 ^ K(0x28C) * 11 ^ (g_cur_fb ? g_cur_fb->w : 0) ^ K(0x304) * 17 ^ K(0x344) * 19 ^ K(0x348) * 23;
                    if (on4 && ns4 < 16) { u32 k = 0; while (k < ns4 && seen4[k] != hh) k++; if (k == ns4) { seen4[ns4++] = hh;
                        static const u32 regs[] = {0x1E60, 0x288, 0x28C, 0x304, 0x344, 0x348, 0x350, 0x1E70, 0x358,
                            0xAC0, 0x1E40, 0x260, 0xAA0, 0xA60, 0xA80,  0xAC4, 0x1E44, 0x264, 0xAA4, 0xA64, 0xA84,  0xAC8, 0x1E48, 0x268, 0xAA8, 0xA68, 0xA88,
                            0xACC, 0x1E4C, 0x26C, 0xAAC, 0xA6C, 0xA8C,  0xAD0, 0x1E50, 0x270, 0xAB0, 0xA70, 0xA90,  0xAD4, 0x1E54, 0x274, 0xAB4, 0xA74, 0xA94};
                        char tx[1000]; u32 q = 0; u32 lead[3] = {g_cur_fb ? g_cur_fb->w : 0, rt->w, i};
                        for (int z2 = 0; z2 < 3 + (int)(sizeof regs / 4); z2++) { u32 v = z2 < 3 ? lead[z2] : K(regs[z2 - 3]); for (int s = 28; s >= 0; s -= 4) tx[q++] = "0123456789ABCDEF"[(v >> s) & 15]; tx[q++] = 0x20; }
                        for (u32 s5 = 0; s5 < 4; s5++) { TI t5; u32 v5[6] = {0, 0, 0, 0, 0, 0}; if (((K(0x1B0C + s5 * 0x40) >> 30) & 1) && tex_info(s5, &t5)) { FB *r5 = fb_by_color(t5.off);
                                v5[0] = t5.off; v5[1] = t5.cf; v5[2] = t5.w | (t5.h << 16); v5[3] = r5 ? 0xFFFFFFFF : texel(&t5, 0, 0); v5[4] = r5 ? 0 : texel(&t5, t5.w / 2, t5.h / 2); v5[5] = r5 ? 0 : texel(&t5, t5.w - 1, t5.h - 1); }
                            tx[q++] = 0x7C; for (int z5 = 0; z5 < 6; z5++) { u32 v = v5[z5]; for (int s = 28; s >= 0; s -= 4) tx[q++] = "0123456789ABCDEF"[(v >> s) & 15]; tx[q++] = 0x20; } }
                        tx[q - 1] = 0x20; tx[q++] = 0x0D; tx[q++] = 0x0A;
                        HANDLE fh = CreateFileA("glow_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } }
                {   /* glowmode.txt (read every second, A/B while running): 1 = clear a small render target (w < 640) before its first draw of each frame */
                    static u32 tl, md, cf[8], cfr[8]; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; md = 0; HANDLE fh = CreateFileA("glowmode.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                        if (fh != (HANDLE)-1) { char b[4] = {0}; DWORD n = 0; ReadFile(fh, b, 3, &n, 0); CloseHandle(fh); if (n && b[0] >= 0x30 && b[0] <= 0x39) md = b[0] - 0x30; } }
                    if (md == 1 && g_cur_fb && g_cur_fb->w < 640) { u32 k = 0; while (k < 8 && cf[k] && cf[k] != g_cur_fb->coff) k++;
                        if (k < 8 && (cf[k] != g_cur_fb->coff || cfr[k] != g_frame)) { cf[k] = g_cur_fb->coff; cfr[k] = g_frame;
                            glDisable(0x0C11); glColorMask(1, 1, 1, 1); glClearColor(0, 0, 0, 0); glClear(0x4000);
                            u32 cm = K(0x358); glColorMask((cm >> 16) & 1, (cm >> 8) & 1, cm & 1, (cm >> 24) & 1);
                            u32 hz = K(0x200), vt = K(0x204), gs = g_cur_fb->gs ? g_cur_fb->gs : 1; glEnable(0x0C11); glScissor((hz & 0xFFFF) * gs, (vt & 0xFFFF) * gs, (hz >> 16) * gs, (vt >> 16) * gs); g_r->pad[47]++; } } }
                g_r->pad[40]++; if (rt == g_cur_fb) g_r->pad[41]++; { u32 nsame = 0; for (u32 q = 0; q < NFB; q++) if (g_fb[q].fbo && g_fb[q].coff == rt->coff) nsame++; if (nsame > 1) g_r->pad[42]++; }
                tex = rt->col; if (t.linear) { tsc[i * 4] = 1.0f / rt->w; tsc[i * 4 + 1] = 1.0f / rt->h; } else { tsc[i * 4] = (f32)t.w / rt->w; tsc[i * 4 + 1] = (f32)t.h / rt->h; }
                tsc[i * 4 + 2] = (f32)sub_x / rt->w; tsc[i * 4 + 3] = (f32)sub_y / rt->h; }   /* the shift of a texture inside the surface */
            else if (dcol) { tex = dcol->ctex; dcol_used = 1; tsc[i * 4] = t.linear ? 1.0f / dcol->w : (f32)t.w / dcol->w; tsc[i * 4 + 1] = t.linear ? 1.0f / dcol->h : (f32)t.h / dcol->h; g_r->pad[49]++;
                { u32 v[DCOL_W] = {g_frame, i, t.off, t.cf, dcol->w, g_cur_fb ? g_cur_fb->w : 0, g_cur_fb ? g_cur_fb->coff : 0, K(0x304), K(0x344), K(0x348), K(0x358),
                      K(0x1E60), K(0x288), K(0x28C), K(0x1E70), K(0x300) | K(0x33C) << 8 | K(0x340) << 16};
                  int z = 16; for (u32 s = 0; s < 4; s++) { v[z++] = K(0x1B00 + s * 0x40); v[z++] = K(0x1B04 + s * 0x40); v[z++] = K(0x1B0C + s * 0x40); }   /* 16..27: each stage's address, format, enable */
                  for (u32 c = 0; c < 4; c++) { v[z++] = K(0x260 + c * 4); v[z++] = K(0xAC0 + c * 4); v[z++] = K(0xA60 + c * 4); v[z++] = K(0xA80 + c * 4); }   /* 28..43: combiner stages 0-3: rgb in, rgb out, alpha in, alpha out */
                  for (u32 c = 0; c < 4; c++) v[z++] = K(0xAA0 + c * 4);   /* 44..47 */
                  v[z++] = K(0x1E40); v[z++] = K(0x1E44); v[z++] = K(0x1E48); v[z++] = K(0x1E4C);   /* 48..51: combiner constants */
                  for (u32 s = 1; s < 4; s++) {   /* 52..; per stage 1-3: exact surface (w | h << 16, age ms), and a surface containing the address (its coff, w | h << 16) */
                      u32 a = K(0x1B00 + s * 0x40) & 0x03FFFFFF; FB *ex = fb_by_color(a); FB *in = 0;
                      for (u32 k = 0; k < NFB; k++) { FB *f = &g_fb[k]; if (f->fbo && a > f->coff && a < f->coff + f->w * 4 * f->h && (!in || f->used > in->used)) in = f; }
                      v[z++] = ex ? ex->w | ex->h << 16 : 0; v[z++] = ex ? GetTickCount() - ex->gpu_frame : 0xFFFFFFFF; v[z++] = in ? in->coff : 0; v[z++] = in ? in->w | in->h << 16 : 0; }
                  dcol_line(v, z, "R"); } }
            else if (tmode[i] == 2 && t.cf >= 0x2A && t.cf <= 0x31 && (g_noshadow ? g_noshadow : (g_noshadow = GetFileAttributesA("noshadowmap.txt") != 0xFFFFFFFF ? 1 : 2)) == 2) {
                /* Milestone 89: shadow map. NV2A PROJECT3D on a depth-format texture compares r/q against the stored depth
                   (SET_SHADOW_COMPARE_FUNC 0x1E6C). The map is a depth buffer the game rendered: use that GL depth texture */
                DEP *d = dep_by_addr(t.off);
                if (d) { glActiveTexture(0x84C0 + 8 + i); glBindTexture(0x0DE1, d->rb);
                    glTexParameteri(0x0DE1, 0x884C, 0x884E); { static const u32 sw[8] = {0, 4, 2, 6, 1, 5, 3, 7}; glTexParameteri(0x0DE1, 0x884D, 0x0200 + sw[K(0x1E6C) & 7]); }   /* NV2A compares stored depth OP r; OpenGL r OP stored: swap the direction */
                    glTexParameteri(0x0DE1, 0x2800, 0x2601); glTexParameteri(0x0DE1, 0x2801, 0x2601);
                    g_shad_c[i] = 1; g_zs_c[i] = (t.cf == 0x2C || t.cf == 0x2D || t.cf == 0x30 || t.cf == 0x31) ? 65535.0f : 16777215.0f;
                    {   /* bias the compare toward "lit" (shadowbias.txt = value x 1e-5, default 300): removes self-shadow lines */
                        static f32 eps = -1; if (eps < 0) { eps = 0.003f; HANDLE fh = CreateFileA("shadowbias.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                            if (fh != (HANDLE)-1) { char b[16] = {0}; DWORD n = 0; ReadFile(fh, b, 15, &n, 0); CloseHandle(fh); u32 v = 0; for (DWORD k = 0; k < n && b[k] >= 0x30 && b[k] <= 0x39; k++) v = v * 10 + (b[k] - 0x30); eps = v * 1e-5f; } }
                        u32 fn = K(0x1E6C) & 7; g_sb_c[i] = (fn == 4 || fn == 6) ? -eps : (fn == 1 || fn == 3) ? eps : 0;
                        {   /* shadowmode.txt (read every second, A/B while running): 0 normal, 1 bias sign flipped, 2 result 1, 3 result 0, 4 normal + beyond far/behind = 0, 5 flipped + beyond far = 0 */
                            static u32 tl, md; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; md = 0; HANDLE fh = CreateFileA("shadowmode.txt", 0x80000000, 1, 0, 3, 0x80, 0);
                                if (fh != (HANDLE)-1) { char b[4] = {0}; DWORD n = 0; ReadFile(fh, b, 3, &n, 0); CloseHandle(fh); if (n && b[0] >= 0x30 && b[0] <= 0x35) md = b[0] - 0x30; } }
                            if (md == 1 || md == 5) g_sb_c[i] = -g_sb_c[i]; if (md == 2) g_shad_c[i] = 2; if (md == 3) g_shad_c[i] = 3; if (md >= 4) g_shad_c[i] = 4; }
                        static u32 logged; if (!logged) { logged = 1; u32 vals[5] = {fn, K(0x320), K(0x384), K(0x388), t.cf}; char tx[120]; u32 q = 0;
                            for (int z2 = 0; z2 < 5; z2++) { u32 v = vals[z2]; char dd[12]; int z = 0; do dd[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q++] = dd[--z]; tx[q++] = 0x20; }
                            HANDLE fh = CreateFileA("shadow_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } }
                    tsc[i * 4] = t.linear ? 1.0f / d->w : 1.0f; tsc[i * 4 + 1] = t.linear ? 1.0f / d->h : 1.0f; g_r->pad[45]++; }
                else { g_shad_c[i] = 2; g_r->pad[46]++; }   /* not rendered here: treat as lit */
                glActiveTexture(0x84C0 + i); glBindTexture(0x0DE1, 0); continue; }
            else if (tmode[i] == 3 && !t.linear) {   /* cube map: unit 4 + i (samplerCube c0..c3) */
                GLuint ct = tex_get_cube(&t);
                {   /* cubelog.txt: each distinct cube texture once (address, size, format, levels, matched surface, mean colour of face 0) */
                    static u32 on = 2, seen[64], ns; if (on == 2) on = GetFileAttributesA("cubelog.txt") != 0xFFFFFFFF;
                    static u32 tl; u32 tnow = GetTickCount(); int again = t.cf == 0 && tnow - tl > 10000; if (again) tl = tnow;
                    if (on && ns < 64) { u32 k = 0; for (; k < ns && seen[k] != t.off; k++) ; if (k == ns || again) { if (k == ns) seen[ns++] = t.off;
                        u32 levels2 = (t.fmt >> 16) & 0xF; if (!levels2) levels2 = 1; u32 facesz = 0; for (u32 l = 0; l < levels2; l++) { TI s2 = t; s2.w = t.w >> l; s2.h = t.h >> l; if (!s2.w) s2.w = 1; if (!s2.h) s2.h = 1; facesz += tex_bytes(&s2); } facesz = (facesz + 127) & ~127u;
                        u32 fmax = 0; for (u32 f = 0; f < 6; f++) { TI s3 = t; s3.off = t.off + f * facesz; for (u32 y = 0; y < t.h; y += 4) for (u32 x = 0; x < t.w; x += 4) { u32 c = texel(&s3, x, y) & 255; if (c > fmax) fmax = c; } }
                        u32 sr = 0, sg = 0, sb = 0, nn = 0; for (u32 y = 0; y < t.h; y += t.h / 8 ? t.h / 8 : 1) for (u32 x = 0; x < t.w; x += t.w / 8 ? t.w / 8 : 1) { u32 c = texel(&t, x, y); sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; nn++; }
                        FB *m = fb_by_color(t.off); u32 vals[10] = {t.off, t.w, t.h, t.cf, (t.fmt >> 16) & 0xF, m ? 1u : 0u, nn ? sr / nn : 0, nn ? sg / nn : 0, nn ? sb / nn : 0, fmax};
                        char tx[220]; u32 q = 0; for (int z2 = 0; z2 < 10; z2++) { u32 v = vals[z2]; char d[12]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q++] = d[--z]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
                        HANDLE fh = CreateFileA("cube_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } glActiveTexture(0x84C0 + 4 + i); glBindTexture(0x8513, ct);
                glTexParameteri(0x8513, 0x2802, 0x812F); glTexParameteri(0x8513, 0x2803, 0x812F); glTexParameteri(0x8513, 0x8072, 0x812F);
                glTexParameteri(0x8513, 0x2800, 0x2601); glTexParameteri(0x8513, 0x2801, g_last_mips ? 0x2703 : 0x2601);
                glActiveTexture(0x84C0 + i); glBindTexture(0x0DE1, 0); continue; }
            else { tex = tex_get(&t); if (t.linear) { tsc[i * 4] = 1.0f / t.w; tsc[i * 4 + 1] = 1.0f / t.h; }
                if (tmode[i] == 2) {   /* vol3log.txt: each distinct texture used in mode 2 (3D): stage, address, w, h, depth (log2), format, levels, dims field */
                    static u32 on = 2, seen[32], ns; if (on == 2) on = GetFileAttributesA("vol3log.txt") != 0xFFFFFFFF;
                    if (on && ns < 32) { u32 k = 0; for (; k < ns && seen[k] != t.off; k++) ; if (k == ns) { seen[ns++] = t.off;
                        u32 vals[8] = {i, t.off, t.w, t.h, (t.fmt >> 28) & 0xF, t.cf, (t.fmt >> 16) & 0xF, (t.fmt >> 4) & 0xF};
                        char tx[200]; u32 q = 0; for (int z2 = 0; z2 < 8; z2++) { u32 v = vals[z2]; char d[12]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q++] = d[--z]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
                        HANDLE fh = CreateFileA("vol3_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } }
            glBindTexture(0x0DE1, tex);
            if (dcol_used) {   /* Milestone 96: the copy holds R = depth 23-16, G = 15-8, B = 7-0, A = stencil; each format sees the Z24S8 word
                                  (depth << 8 | stencil) in its own byte order: 0x12/0x06/0x07/0x1E (A8R8G8B8): A = depth hi, R = mid, G = lo,
                                  B = stencil; 0x41/0x3C and the rest: A = stencil, R = lo, G = mid, B = hi (the glow pass, Milestone 89) */
                int argb = t.cf == 0x12 || t.cf == 0x06 || t.cf == 0x07 || t.cf == 0x1E;
                static const GLint sw_argb[4] = {0x1904, 0x1905, 0x1906, 0x1903}, sw_abgr[4] = {0x1905, 0x1904, 0x1903, 0x1906};
                const GLint *sw = (argb && g_ab != 7) ? sw_argb : sw_abgr;   /* abtest.txt 7: the old layout for every format */
                glTexParameteri(0x0DE1, 0x8E42, sw[0]); glTexParameteri(0x0DE1, 0x8E43, sw[1]); glTexParameteri(0x0DE1, 0x8E44, sw[2]); glTexParameteri(0x0DE1, 0x8E45, sw[3]); }
            u32 ad = K(0x1B08 + i * 0x40), fi = K(0x1B14 + i * 0x40);
            static const GLint wrap[8] = {0x2901, 0x2901, 0x8370, 0x812F, 0x812D, 0x812F, 0x2901, 0x2901};
            glTexParameteri(0x0DE1, 0x2802, wrap[ad & 7]); glTexParameteri(0x0DE1, 0x2803, wrap[(ad >> 8) & 7]);
            {   /* border colour (Milestone 88): NV2A SET_TEXTURE_BORDER_COLOR (0x1B24 + i x 0x40, ARGB). Without it OpenGL used
                   black, and the character's projected shadow darkened everything its projector covered (a large dark
                   wedge following the character) */
                u32 bc = K(0x1B24 + i * 0x40); GLfloat col[4] = {((bc >> 16) & 255) / 255.0f, ((bc >> 8) & 255) / 255.0f, (bc & 255) / 255.0f, (bc >> 24) / 255.0f};
                glTexParameterfv(0x0DE1, 0x1004 /* TEXTURE_BORDER_COLOR */, col); }
            GLint mag = ((fi >> 24) & 0xF) == 1 ? 0x2600 : 0x2601, min;
            switch ((fi >> 16) & 0xFF) {   /* NV2A minification: 1 box, 2 tent (level 0); 3..6 with mipmaps */
            case 1: min = 0x2600; break; case 3: min = g_last_mips ? 0x2700 : 0x2600; break; case 4: min = g_last_mips ? 0x2701 : 0x2601; break;
            case 5: min = g_last_mips ? 0x2702 : 0x2600; break; case 6: min = g_last_mips ? 0x2703 : 0x2601; break; default: min = 0x2601; }
            glTexParameteri(0x0DE1, 0x2800, mag); glTexParameteri(0x0DE1, 0x2801, min);
        } else { tmode[i] = 0; glBindTexture(0x0DE1, 0); }
    }
    }
    /* the program's uniforms */
    u32 hu = hs ^ (ht * 31) ^ hrange(0x260, 0x290) ^ (hrange(0xA60, 0xAE0) * 7) ^ (hrange(0x1E40, 0x1E64) * 13) ^ K(0x2A8) ^ (K(0x300) * 3) ^ (K(0x33C) * 5) ^ (K(0x340) * 11) ^ (K(0x29C) * 17) ^ (K(0x2A4) * 19) ^ (hrange(0x9C0, 0x9CC) * 23) ^ (g_ab * 0x9E3779B1u);
    f32 wk = (W * 3 == H * 4) ? g_wide_k : 0.0f;   /* Milestone 96: widescreen squeeze on every 4:3 target (the screen and the copies of it the effects draw at
                                                     320x240, 160x120, 80x60; squeezing only the screen doubled the depth-of-field image), not on square shadow maps */
    hu ^= *(u32 *)&wk * 0x85EBCA6Bu;
    if (P->ustate == hu) return 1;
    P->ustate = hu;
    glUniform4f(u_vp, 2.0f / W, 2.0f / H, 2.0f / zmax, wk);
    glUniform4iv(u_tmode, 1, tmode); glUniform4fv(u_tscale, 4, tsc); glUniform4iv(u_shadow, 1, g_shad_c); glUniform4fv(u_zscale, 1, g_zs_c); glUniform4fv(u_sbias, 1, g_sb_c);
    /* register combiners */
    u32 ctl = K(0x1E60), ns = ctl & 0xFF; if (ns > 8) ns = 8; if (!ns) ns = 1;
    GLint crg[32], crep[32], cmap[32], arg[32], aal[32], amap[32], cdst[32], adst[32], op[16]; f32 f0[32], f1[32];
    for (u32 s = 0; s < 8; s++) {
        u32 ci = K(0xAC0 + s * 4), ai = K(0x260 + s * 4), co = K(0x1E40 + s * 4), ao = K(0xAA0 + s * 4);
        for (u32 j = 0; j < 4; j++) {
            u32 cb = (ci >> (24 - 8 * j)) & 0xFF, ab = (ai >> (24 - 8 * j)) & 0xFF;
            crg[s * 4 + j] = cb & 15; crep[s * 4 + j] = (cb >> 4) & 1; cmap[s * 4 + j] = (cb >> 5) & 7;
            arg[s * 4 + j] = ab & 15; aal[s * 4 + j] = (ab >> 4) & 1; amap[s * 4 + j] = (ab >> 5) & 7;
        }
        cdst[s * 4] = (co >> 4) & 15; cdst[s * 4 + 1] = co & 15; cdst[s * 4 + 2] = (co >> 8) & 15;
        cdst[s * 4 + 3] = ((co >> 13) & 1) | ((co >> 12) & 1) << 1 | ((co >> 14) & 1) << 2 | ((co >> 19) & 1) << 3 | ((co >> 18) & 1) << 4;
        adst[s * 4] = (ao >> 4) & 15; adst[s * 4 + 1] = ao & 15; adst[s * 4 + 2] = (ao >> 8) & 15; adst[s * 4 + 3] = (ao >> 14) & 1;
        op[s * 2] = (co >> 15) & 7; op[s * 2 + 1] = (ao >> 15) & 7;
        unpack4(K(0xA60 + ((ctl & 0x1000) ? s : 0) * 4), &f0[s * 4]); unpack4(K(0xA80 + ((ctl & 0x10000) ? s : 0) * 4), &f1[s * 4]);
    }
    glUniform1i(u_nst, ns); glUniform1i(u_muxmsb, (ctl >> 8) & 1);
    glUniform4iv(u_crg, 8, crg); glUniform4iv(u_crep, 8, crep); glUniform4iv(u_cmap, 8, cmap); glUniform4iv(u_arg, 8, arg); glUniform4iv(u_aal, 8, aal); glUniform4iv(u_amap, 8, amap);
    glUniform4iv(u_cdst, 8, cdst); glUniform4iv(u_adst, 8, adst); glUniform2iv(u_op, 8, op); glUniform4fv(u_f0, 8, f0); glUniform4fv(u_f1, 8, f1);
    f32 fog[4]; unpack4(K(0x2A8), fog); fog[3] = 1; glUniform4fv(u_fog, 1, fog);   /* fog not applied yet */
    u32 c0 = K(0x288), c1 = K(0x28C), b[7] = {c0 >> 24, (c0 >> 16) & 0xFF, (c0 >> 8) & 0xFF, c0 & 0xFF, c1 >> 24, (c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF};
    GLint fa[4] = {b[0] & 15, b[1] & 15, b[2] & 15, b[3] & 15}, fbv[4] = {b[4] & 15, b[5] & 15, b[6] & 15, ((c1 >> 7) & 1) | ((c1 >> 6) & 1) << 1 | ((c1 >> 5) & 1) << 2};
    GLint frep[4] = {0, 0, 0, 0}, finv[4] = {0, 0, 0, 0};
    for (u32 j = 0; j < 4; j++) { frep[0] |= ((b[j] >> 4) & 1) << j; finv[0] |= ((b[j] >> 5) & 1) << j; }
    for (u32 j = 4; j < 7; j++) { frep[1] |= ((b[j] >> 4) & 1) << (j - 4); finv[1] |= ((b[j] >> 5) & 1) << (j - 4); }
    glUniform4iv(u_fa, 1, fa); glUniform4iv(u_fb, 1, fbv); glUniform4iv(u_frep, 1, frep); glUniform4iv(u_finv, 1, finv);
    GLint at[2] = {K(0x300) ? (GLint)(K(0x33C) & 7) : -1, (GLint)(K(0x340) & 0xFF)}; glUniform2iv(u_atest, 1, at);
    {   /* fog (Milestone 82): 0x29C mode, 0x2A4 enable, 0x9C0 parameters */
        u32 fm = K(0x29C); f32 mode = fm == 0x2601 ? 0 : fm == 0x800 ? 1 : fm == 0x804 ? 2 : fm == 0x802 ? 3 : fm == 0x801 || fm == 0x803 ? 4 : 0;
        glUniform4f(u_fogp, fl(K(0x9C0)), fl(K(0x9C4)), fl(K(0x9C8)), mode); glUniform1i(u_fogon, K(0x2A4) && g_ab != 2 ? 1 : 0); }
    return 1;
}

/* ---- vertex programs on the GPU (Milestone 81): NV2A microcode -> GLSL ---- */
static u32 g_vprog[136][4], g_vstart, g_vdirty = 1, g_vcgen = 1; static f32 g_vc[192][4];   /* g_vcgen: bumped when constants change */
static GLuint g_fsobj, g_vao2, g_vbo2, g_ibo;
static char g_src[65536]; static u32 g_srcn;
static void ap(const char *s) { while (*s && g_srcn < sizeof g_src - 1) g_src[g_srcn++] = *s++; g_src[g_srcn] = 0; }
static void apd(u32 v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); ap(b + i); }
#define FB_(w, i, s, l) (((w)[i] >> (s)) & ((1u << (l)) - 1))
static const char *g_mask[16] = {"", ".w", ".z", ".zw", ".y", ".yw", ".yz", ".yzw", ".x", ".xw", ".xz", ".xzw", ".xy", ".xyw", ".xyz", ""};
static void src_op(const u32 *w, int p) {
    u32 mux, reg, neg, sw[4];
    if (p == 0) { mux = FB_(w, 2, 26, 2); reg = FB_(w, 2, 28, 4); neg = FB_(w, 1, 8, 1); sw[0] = FB_(w, 1, 6, 2); sw[1] = FB_(w, 1, 4, 2); sw[2] = FB_(w, 1, 2, 2); sw[3] = FB_(w, 1, 0, 2); }
    else if (p == 1) { mux = FB_(w, 2, 11, 2); reg = FB_(w, 2, 13, 4); neg = FB_(w, 2, 25, 1); sw[0] = FB_(w, 2, 23, 2); sw[1] = FB_(w, 2, 21, 2); sw[2] = FB_(w, 2, 19, 2); sw[3] = FB_(w, 2, 17, 2); }
    else { mux = FB_(w, 3, 28, 2); reg = (FB_(w, 2, 0, 2) << 2) | FB_(w, 3, 30, 2); neg = FB_(w, 2, 10, 1); sw[0] = FB_(w, 2, 8, 2); sw[1] = FB_(w, 2, 6, 2); sw[2] = FB_(w, 2, 4, 2); sw[3] = FB_(w, 2, 2, 2); }
    if (neg) ap("-");
    if (mux == 1) { if (reg == 12) ap("o0"); else { ap("r"); apd(reg & 15); } }
    else if (mux == 2) { ap("v"); apd(FB_(w, 1, 9, 4)); }
    else if (mux == 3) { if (FB_(w, 3, 1, 1)) { ap("u_c[clamp(a0+"); apd(FB_(w, 1, 13, 8)); ap(",0,191)]"); } else { ap("u_c["); apd(FB_(w, 1, 13, 8)); ap("]"); } }
    else ap("vec4(0.0)");
    if (!(sw[0] == 0 && sw[1] == 1 && sw[2] == 2 && sw[3] == 3)) { ap("."); for (int i = 0; i < 4; i++) { char ch[2] = {"xyzw"[sw[i]], 0}; ap(ch); } }
}
static void wr(const char *dst, u32 mask, const char *val) { if (!mask) return; ap(dst); ap(g_mask[mask]); ap(" = "); ap(val); ap(g_mask[mask]); ap(";\n"); }
static int gen_vsh(void) {
    g_srcn = 0;
    ap("#version 130\nin vec4 v0,v1,v2,v3,v4,v5,v6,v7,v8,v9,v10,v11,v12,v13,v14,v15;\nuniform vec4 u_c[192]; uniform vec4 u_vp; uniform vec2 u_hud;\nout vec4 v_d0, v_d1, v_t0, v_t1, v_t2, v_t3; out float v_fogc;\n");
    ap("void main() {\nvec4 r0=vec4(0.0),r1=r0,r2=r0,r3=r0,r4=r0,r5=r0,r6=r0,r7=r0,r8=r0,r9=r0,r10=r0,r11=r0,r12=r0,r13=r0,r14=r0,r15=r0;\n");
    ap("vec4 o0=vec4(0,0,0,1),o1=o0,o2=o0,o3=o0,o4=o0,o5=o0,o6=o0,o7=o0,o8=o0,o9=o0,o10=o0,o11=o0,o12=o0,o13=o0,o14=o0,o15=o0; int a0=0; vec4 A,B,C,m,l; float s;\n");
    static const u8 macneed[16] = {0, 1, 3, 5, 7, 3, 3, 3, 3, 3, 3, 3, 3, 1, 0, 0};
    u32 n = 0;
    for (u32 pc = g_vstart; pc < 136; pc++, n++) {
        const u32 *w = g_vprog[pc];
        u32 mac = FB_(w, 1, 21, 4), ilu = FB_(w, 1, 25, 3), need = macneed[mac] | (ilu ? 4 : 0);
        if (need & 1) { ap("A = "); src_op(w, 0); ap(";\n"); }
        if (need & 2) { ap("B = "); src_op(w, 1); ap(";\n"); }
        if (need & 4) { ap("C = "); src_op(w, 2); ap(";\n"); }
        switch (mac) {
        case 1: ap("m = A;\n"); break; case 2: ap("m = A*B;\n"); break; case 3: ap("m = A+C;\n"); break; case 4: ap("m = A*B+C;\n"); break;
        case 5: ap("m = vec4(dot(A.xyz,B.xyz));\n"); break; case 6: ap("m = vec4(dot(A.xyz,B.xyz)+B.w);\n"); break; case 7: ap("m = vec4(dot(A,B));\n"); break;
        case 8: ap("m = vec4(1.0, A.y*B.y, A.z, B.w);\n"); break; case 9: ap("m = min(A,B);\n"); break; case 10: ap("m = max(A,B);\n"); break;
        case 11: ap("m = vec4(lessThan(A,B));\n"); break; case 12: ap("m = vec4(greaterThanEqual(A,B));\n"); break;
        case 13: ap("a0 = int(floor(A.x));\n"); break;
        }
        switch (ilu) {
        case 1: ap("l = C;\n"); break;
        case 2: ap("s = C.x; l = vec4(s == 0.0 ? 1e30 : 1.0/s);\n"); break;
        case 3: ap("s = C.x; s = s == 0.0 ? 1e30 : 1.0/s; s = sign(s)*clamp(abs(s), 5.42101e-20, 1.884467e19); l = vec4(s);\n"); break;
        case 4: ap("s = abs(C.x); l = vec4(s == 0.0 ? 1e30 : inversesqrt(s));\n"); break;
        case 5: ap("s = C.x; l = vec4(exp2(floor(s)), s - floor(s), exp2(s), 1.0);\n"); break;
        case 6: ap("s = abs(C.x); l = s == 0.0 ? vec4(-1e30, 1.0, -1e30, 1.0) : vec4(floor(log2(s)), s/exp2(floor(log2(s))), log2(s), 1.0);\n"); break;
        case 7: ap("l = vec4(1.0, max(C.x,0.0), (C.x > 0.0 && C.y > 0.0) ? pow(C.y, clamp(C.w,-127.9961,127.9961)) : 0.0, 1.0);\n"); break;
        }
        char d[8];
        if (mac && mac != 13) { d[0] = 'r'; d[1] = 0; ap(""); char t[8]; t[0] = 'r'; u32 k = FB_(w, 3, 20, 4) & 15; if (k >= 10) { t[1] = '1'; t[2] = '0' + k - 10; t[3] = 0; } else { t[1] = '0' + k; t[2] = 0; } wr(t, FB_(w, 3, 24, 4), "m"); }
        if (ilu) { char t[8]; t[0] = 'r'; u32 k = mac ? 1 : (FB_(w, 3, 20, 4) & 15); if (k >= 10) { t[1] = '1'; t[2] = '0' + k - 10; t[3] = 0; } else { t[1] = '0' + k; t[2] = 0; } wr(t, FB_(w, 3, 16, 4), "l"); }
        u32 om = FB_(w, 3, 12, 4);
        if (om && FB_(w, 3, 11, 1)) { char t[8]; t[0] = 'o'; u32 k = FB_(w, 3, 3, 8) & 15; if (k >= 10) { t[1] = '1'; t[2] = '0' + k - 10; t[3] = 0; } else { t[1] = '0' + k; t[2] = 0; } wr(t, om, FB_(w, 3, 2, 1) ? "l" : "m"); }
        (void)d;
        if (FB_(w, 3, 0, 1)) break;
    }
    ap("float w = o0.w; if (w == 0.0) w = 1.0;\n");
    /* Milestone 96 widescreen: u_vp.w > 0 squeezes perspective (3D) vertices horizontally about the centre, so the 4:3 frame shown
       stretched in a wide window has the right shape and a wider view; screen-space draws (w == 1: the HUD, effect quads) are left alone */
    ap("float kx = (u_vp.w > 0.0 && abs(w - 1.0) > 1e-5) ? u_vp.w : 1.0;\n");
    ap("float hx = o0.x; if (u_hud.x > 0.0 && abs(w - 1.0) <= 1e-5) hx = u_hud.y + (hx - u_hud.y) * u_hud.x;\n");   /* Milestone 96: the HUD in widescreen (screen-space vertices only) */
    ap("gl_Position = vec4(vec3((hx*u_vp.x-1.0)*kx, o0.y*u_vp.y-1.0, o0.z*u_vp.z-1.0)*w, w);\n");
    ap("v_d0 = o3; v_d1 = o4; v_t0 = o9; v_t1 = o10; v_t2 = o11; v_t3 = o12; v_fogc = o5.x;\n}\n");
    return n;
}
#define NPROG 256
static PROG g_vp[NPROG];
static PROG *vsh_program(void) {
    u32 h = 2166136261u ^ g_vstart;
    for (u32 pc = g_vstart; pc < 136; pc++) { for (u32 k = 0; k < 4; k++) h = (h ^ g_vprog[pc][k]) * 16777619u; if (FB_(g_vprog[pc], 3, 0, 1)) break; }
    PROG *free_ = 0, *old = &g_vp[0];
    for (u32 i = 0; i < NPROG; i++) { if (g_vp[i].id && g_vp[i].key == h) { g_vp[i].used = g_frame; return &g_vp[i]; } if (!g_vp[i].id && !free_) free_ = &g_vp[i]; if (g_vp[i].used < old->used) old = &g_vp[i]; }
    PROG *p = free_ ? free_ : old;
    gen_vsh();
    GLuint vs = compile(0x8B31, g_src); if (!vs) return 0;
    GLuint id = glCreateProgram(); glAttachShader(id, vs); glAttachShader(id, g_fsobj);
    for (u32 i = 0; i < 16; i++) { char nm[4] = {'v', 0, 0, 0}; if (i >= 10) { nm[1] = '1'; nm[2] = '0' + i - 10; } else nm[1] = '0' + i; glBindAttribLocation(id, i, nm); }
    glBindFragDataLocation(id, 0, "o_col"); glLinkProgram(id);
    GLint ok = 0; glGetProgramiv(id, 0x8B82, &ok); if (!ok) { info(" vsh link failed"); return 0; }
    p->id = id; p->key = h; p->used = g_frame; prog_locs(p); g_curp = 0;
    return p;
}
/* one attribute of vertex `idx` from its array, as 4 floats (the formats of render.c) */
static void fetch(u32 i, u32 idx, f32 *c) {
    u32 f = K(0x1760 + i * 4), type = f & 0xF, size = (f >> 4) & 0xF, stride = (f >> 8) & 0xFF;
    u8 *p = PHYS(K(0x1720 + i * 4) + idx * stride);
    c[0] = c[1] = c[2] = 0; c[3] = 1;
    switch (type) {
    case 0: if (size >= 4) { c[0] = p[2] / 255.0f; c[1] = p[1] / 255.0f; c[2] = p[0] / 255.0f; c[3] = p[3] / 255.0f; } else for (u32 k = 0; k < size; k++) c[k] = p[k] / 255.0f; break;
    case 4: for (u32 k = 0; k < size && k < 4; k++) c[k] = p[k] / 255.0f; break;
    case 1: for (u32 k = 0; k < size && k < 4; k++) c[k] = ((s16 *)p)[k] / 32767.0f; break;
    case 5: for (u32 k = 0; k < size && k < 4; k++) c[k] = ((s16 *)p)[k]; break;
    case 2: for (u32 k = 0; k < size && k < 4; k++) c[k] = ((f32 *)p)[k]; break;
    case 6: { u32 v = *(u32 *)p; c[0] = ((s32)(v << 21) >> 21) / 1023.0f; c[1] = ((s32)(v << 10) >> 21) / 1023.0f; c[2] = ((s32)v >> 22) / 511.0f; break; }
    }
}
static f32 *g_stage; static u32 *g_idxb;
/* vertex data cache (Milestone 81): one GL buffer per Xbox memory range, re-uploaded only when its contents change
   (fingerprint: 128 sampled words and both ends) */
typedef struct { u32 lo, hi, hash, used, checked; GLuint buf; } VCACHE;
static u32 g_draw_seq;   /* bumped at every flip: a range is re-hashed once per game frame */
#define NVC 1024
static VCACHE g_vcache[NVC];
static u32 vhash(u32 lo, u32 hi) {   /* Milestone 87: the whole range (a 128-word sample missed terrain rewrites: holes, floating slabs) */
    u32 n = (hi - lo) / 4; const u32 *p = (const u32 *)PHYS(lo); u32 a = 2166136261u, b = 0x9E3779B9u, c = 0x85EBCA6Bu, d = 0xC2B2AE35u, i = 0;
    for (; i + 4 <= n; i += 4) { a = (a ^ p[i]) * 16777619u; b = (b ^ p[i + 1]) * 2246822519u; c = (c ^ p[i + 2]) * 3266489917u; d = (d ^ p[i + 3]) * 668265263u; }
    for (; i < n; i++) a = (a ^ p[i]) * 16777619u;
    return a ^ (b * 3) ^ (c * 5) ^ (d * 7) ^ n;
}
GL_FN(void, glBufferSubData_, (GLenum, GLintptr, GLsizeiptr, const void *))
static GLuint vbuf_get(u32 lo, u32 hi) {
    VCACHE *e = 0, *old = &g_vcache[0];
    for (u32 i = 0; i < NVC; i++) { VCACHE *c = &g_vcache[i]; if (c->buf && c->lo == lo && c->hi == hi) { e = c; break; } if (!c->buf) { if (!old->buf || 1) { old = c; break; } } if (c->used < old->used) old = c; }
    if (e && e->used == g_frame && e->checked == g_draw_seq) return e->buf;   /* already checked since the last game flip */
    u32 h = vhash(lo, hi);
    if (e && e->hash == h) { e->used = g_frame; e->checked = g_draw_seq; return e->buf; }
    if (!e) { e = old; if (!e->buf) glGenBuffers(1, &e->buf); e->lo = lo; e->hi = hi; }
    e->hash = h; e->used = g_frame; e->checked = g_draw_seq;
    glBindBuffer(0x8892, e->buf); glBufferData(0x8892, hi - lo, PHYS(lo), 0x88E4 /* STATIC_DRAW */);
    return e->buf;
}
GL_FN(void, glVertexAttrib4fv, (GLuint, const GLfloat *)) GL_FN(void, glDisableVertexAttribArray, (GLuint))
IMP void GLAPI glDrawElements(GLenum, GLsizei, GLenum, const void *);
static PROG *g_state_prog;
static u32 g_state_ok;
static int vsh_setup(PROG **pp) {
    PROG *p = vsh_program(); if (!p) return 0;
    if (!g_state_ok || g_state_prog != p) { if (!apply_state(p)) return 0; g_state_ok = 1; g_state_prog = p; }
    if (p->cgen != g_vcgen) { glUniform4fv(u_c, 192, &g_vc[0][0]); p->cgen = g_vcgen; }   /* only when they changed (Milestone 81) */
    *pp = p; return 1;
}
static int g_zon, g_zrun;
static u32 g_zdraws, g_zverts, g_zdep, g_zfunc, g_zcmask, g_zw, g_zh, g_zprim;   /* what was drawn while counting (report probe) */
static void zcount_draw(u32 mode, u32 n) { (void)0; if (g_zrun) { g_zdraws++; g_zverts += n; g_zdep = K(0x30C); g_zfunc = K(0x354); g_zcmask = K(0x358); g_zprim = mode; if (g_cur_fb) { g_zw = g_cur_fb->w; g_zh = g_cur_fb->h; } } }
/* Milestone 95: the Razorback (razorback.stl, binary STL) drawn in place of the MK-ASLT's mesh (954 indices / 399 vertices;
   its small part 28 / 12 is hidden). The model is centred, its longest side laid along the barrel axis Z (1.0 m long like the
   MK-ASLT), the next along Y; razorflip.txt "<z> <y>" (1 = flip) turns it. Positions + normals as floats, the texture
   coordinate fixed at the MK-ASLT's mean one (its colour). */
IMP u32 WINAPI GetFileSize(HANDLE, u32 *); IMP void *WINAPI VirtualAlloc(void *, u32, u32, u32); IMP int WINAPI VirtualFree(void *, u32, u32);
IMP void GLAPI glGetIntegerv(GLenum, GLint *);
static GLuint g_rz_vbo, g_rz_tex; static u32 g_rz_n; static s32 g_rz_fit[3] = {95, 140, -16}; static int g_rz_state; static u32 g_rz_flip; static s32 g_rz_rot[3];
static void razor_load(void) {
    if (g_rz_state) return; g_rz_state = -1; HANDLE f = CreateFileA("razorback.stl", 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return;
    DWORD sz = GetFileSize(f, 0), rd = 0; if (sz < 84 || sz > 64 * 1024 * 1024) { CloseHandle(f); return; }
    u8 *buf = (u8 *)VirtualAlloc(0, sz, 0x3000, 4); if (!buf) { CloseHandle(f); return; } ReadFile(f, buf, sz, &rd, 0); CloseHandle(f);
    u32 nt = *(u32 *)(buf + 80); if (84 + nt * 50 > rd) { VirtualFree(buf, 0, 0x8000); return; }
    f32 lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (u32 t = 0; t < nt; t++) { f32 *p = (f32 *)(buf + 84 + t * 50 + 12); for (int v = 0; v < 3; v++) for (int k = 0; k < 3; k++) { f32 x = p[v * 3 + k]; if (x < lo[k]) lo[k] = x; if (x > hi[k]) hi[k] = x; } }
    int ax[3] = {0, 1, 2}; f32 ext[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
    for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) if (ext[ax[j]] > ext[ax[i]]) { int s = ax[i]; ax[i] = ax[j]; ax[j] = s; }   /* ax[0] longest -> Z, ax[1] -> Y, ax[2] -> X */
    f32 sc = ext[ax[0]] > 0 ? 1.0f / ext[ax[0]] : 1.0f, ctr[3] = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    u32 nv = nt * 3; f32 *vb = (f32 *)VirtualAlloc(0, nv * 32, 0x3000, 4); if (!vb) { VirtualFree(buf, 0, 0x8000); return; }
    f32 fz = (g_rz_flip & 1) ? -1.0f : 1.0f, fy = (g_rz_flip & 2) ? -1.0f : 1.0f;
    for (u32 t = 0; t < nt; t++) { f32 *rec = (f32 *)(buf + 84 + t * 50), *nr = rec, *p = rec + 3;
        f32 n3[3] = {nr[ax[2]], nr[ax[1]] * fy, nr[ax[0]] * fz};
        for (int v = 0; v < 3; v++) { int vv = (fz * fy < 0) ? 2 - v : v; f32 *o = vb + (t * 3 + v) * 8, *q = p + vv * 3;   /* a mirror flips the winding */
            o[0] = (q[ax[2]] - ctr[ax[2]]) * sc; o[1] = (q[ax[1]] - ctr[ax[1]]) * sc * fy; o[2] = (q[ax[0]] - ctr[ax[0]]) * sc * fz; o[3] = n3[0]; o[4] = n3[1]; o[5] = n3[2];
            for (int a = 0; a < 3; a++) if (g_rz_rot[a]) { f32 ang = g_rz_rot[a] * 0.0174533f, c, s; __asm__("fcos" : "=t"(c) : "0"(ang)); __asm__("fsin" : "=t"(s) : "0"(ang)); int i1 = (a + 1) % 3, i2 = (a + 2) % 3;   /* razorrot.txt: about X, then Y, then Z */
                for (int w2 = 0; w2 < 2; w2++) { f32 *u = o + w2 * 3, x1 = u[i1], x2 = u[i2]; u[i1] = x1 * c - x2 * s; u[i2] = x1 * s + x2 * c; } } } }
    {   /* texture coordinates: the gunsmith picture projected from the side (muzzle at -Z on the left, +Y up) */
        f32 zl = 1e30f, zh = -1e30f, yl = 1e30f, yh = -1e30f; for (u32 i = 0; i < nv; i++) { f32 *o = vb + i * 8; if (o[2] < zl) zl = o[2]; if (o[2] > zh) zh = o[2]; if (o[1] < yl) yl = o[1]; if (o[1] > yh) yh = o[1]; }
        for (u32 i = 0; i < nv; i++) { f32 *o = vb + i * 8; o[6] = (o[2] - zl) / (zh - zl); o[7] = (yh - o[1]) / (yh - yl); } }
    {   /* fit in the hands (razorfit.txt "<scale %> <back mm> <up mm>", read every second): scaled about the centre, moved toward the stock (+Z) and up */
        f32 s = g_rz_fit[0] / 100.0f, dz = g_rz_fit[1] / 1000.0f, dy = g_rz_fit[2] / 1000.0f;
        for (u32 i = 0; i < nv; i++) { f32 *o = vb + i * 8; o[0] *= s; o[1] = o[1] * s + dy; o[2] = o[2] * s + dz; } }
    glGenBuffers(1, &g_rz_vbo); glBindBuffer(0x8892, g_rz_vbo); glBufferData(0x8892, nv * 32, vb, 0x88E4 /* STATIC_DRAW */);
    VirtualFree(vb, 0, 0x8000); VirtualFree(buf, 0, 0x8000); g_rz_n = nv; g_rz_state = 1;
    if (!g_rz_tex) {   /* razorback_tex.raw: 'RZTX', width, height, RGBA rows (top first) */
        HANDLE tf = CreateFileA("razorback_tex.raw", 0x80000000, 1, 0, 3, 0x80, 0);
        if (tf != (HANDLE)-1) { DWORD ts = GetFileSize(tf, 0), r2 = 0; u8 *tb = ts > 12 ? (u8 *)VirtualAlloc(0, ts, 0x3000, 4) : 0;
            if (tb) { ReadFile(tf, tb, ts, &r2, 0); u32 tw = *(u32 *)(tb + 4), th = *(u32 *)(tb + 8);
                if (tb[0] == 'R' && tb[1] == 'Z' && 12 + tw * th * 4 <= r2) { glGenTextures(1, &g_rz_tex); glActiveTexture(0x84C0); glBindTexture(0x0DE1, g_rz_tex);
                    glTexImage2D(0x0DE1, 0, 0x8058, tw, th, 0, 0x1908 /* RGBA */, 0x1401, tb + 12); glGenerateMipmap(0x0DE1);
                    glTexParameteri(0x0DE1, 0x2802, 0x812F); glTexParameteri(0x0DE1, 0x2803, 0x812F); glTexParameteri(0x0DE1, 0x2800, 0x2601); glTexParameteri(0x0DE1, 0x2801, 0x2703); glBindTexture(0x0DE1, 0); }
                VirtualFree(tb, 0, 0x8000); }
            CloseHandle(tf); } } }
static int razor_draw(u32 mode, u32 n, u32 nv, u32 mn, u32 mx, f32 *inl) {   /* 1 = handled (drawn or hidden) */
    static u32 off = 2, tl, cur; if (off == 2) off = GetFileAttributesA("norazorback.txt") != 0xFFFFFFFF; if (off) return 0;
    if (n == 28 && nv == 12) return 1;   /* the MK-ASLT's small part */
    if (!(n == 954 && nv == 399)) return 0;
    { u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; u32 fl = 1; HANDLE f = CreateFileA("razorflip.txt", 0x80000000, 1, 0, 3, 0x80, 0);
        if (f != (HANDLE)-1) { char s[16] = {0}; DWORD r = 0; ReadFile(f, s, 15, &r, 0); CloseHandle(f); int k = 0; for (DWORD i = 0; i < r; i++) if (s[i] == 0x30 || s[i] == 0x31) { if (s[i] == 0x31) fl |= 1u << k; k++; } }
        s32 rt[3] = {0, 0, 0}; f = CreateFileA("razorrot.txt", 0x80000000, 1, 0, 3, 0x80, 0);
        if (f != (HANDLE)-1) { char s[32] = {0}; DWORD r = 0; ReadFile(f, s, 31, &r, 0); CloseHandle(f); int k = 0, ng = 0, an = 0; s32 v = 0; for (DWORD i = 0; i <= r && k < 3; i++) { char ch = i < r ? s[i] : 0x20; if (ch == 0x2D) ng = 1; else if (ch >= 0x30 && ch <= 0x39) { v = v * 10 + (ch - 0x30); an = 1; } else if (an) { rt[k++] = ng ? -v : v; v = 0; an = 0; ng = 0; } } }
        s32 ft[3] = {95, 140, -16};   /* tuned with the user: 95%, 14 cm back, 1.6 cm down */ f = CreateFileA("razorfit.txt", 0x80000000, 1, 0, 3, 0x80, 0);
        if (f != (HANDLE)-1) { char s[48] = {0}; DWORD r = 0; ReadFile(f, s, 47, &r, 0); CloseHandle(f); int k = 0, ng = 0, an = 0; s32 v = 0; for (DWORD i = 0; i <= r && k < 3; i++) { char ch = i < r ? s[i] : 0x20; if (ch == 0x2D) ng = 1; else if (ch >= 0x30 && ch <= 0x39) { v = v * 10 + (ch - 0x30); an = 1; } else if (an) { ft[k++] = ng ? -v : v; v = 0; an = 0; ng = 0; } } }
        if (ft[0] != g_rz_fit[0] || ft[1] != g_rz_fit[1] || ft[2] != g_rz_fit[2]) { g_rz_fit[0] = ft[0]; g_rz_fit[1] = ft[1]; g_rz_fit[2] = ft[2]; if (g_rz_vbo) { GLuint b = g_rz_vbo; glDeleteBuffers(1, &b); g_rz_vbo = 0; } g_rz_state = 0; }
        if (fl != g_rz_flip || rt[0] != g_rz_rot[0] || rt[1] != g_rz_rot[1] || rt[2] != g_rz_rot[2]) { g_rz_flip = fl; g_rz_rot[0] = rt[0]; g_rz_rot[1] = rt[1]; g_rz_rot[2] = rt[2]; if (g_rz_vbo) { GLuint b = g_rz_vbo; glDeleteBuffers(1, &b); g_rz_vbo = 0; } g_rz_state = 0; } } }
    razor_load(); if (g_rz_state != 1) return 0;
    /* the MK-ASLT's mean texture coordinate (attribute 9, float2 at its vertex offset) */
    f32 uv[4] = {0, 0, 0, 1}; { u32 f9 = K(0x1760 + 9 * 4), st9 = (f9 >> 8) & 0xFF, a9 = K(0x1720 + 9 * 4) & 0x03FFFFFF; if ((f9 & 0xF) == 2 && st9) { for (u32 v = mn; v <= mx; v++) { f32 *t = (f32 *)(g_mem + a9 + v * st9); uv[0] += t[0]; uv[1] += t[1]; } uv[0] /= (f32)(mx - mn + 1); uv[1] /= (f32)(mx - mn + 1); } }
    (void)mode; (void)cur;
    glBindVertexArray(g_vao2);
    for (u32 i = 0; i < 16; i++) { glDisableVertexAttribArray(i); glVertexAttrib4fv(i, inl + i * 4); }
    static u32 notex = 2; if (notex == 2) notex = GetFileAttributesA("norazortex.txt") != 0xFFFFFFFF;
    int tx = g_rz_tex && !notex; GLint was = 0;
    glBindBuffer(0x8892, g_rz_vbo); glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, 0x1406, 0, 32, (void *)0);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, 0x1406, 0, 32, (void *)12);
    if (tx) { glEnableVertexAttribArray(9); glVertexAttribPointer(9, 2, 0x1406, 0, 32, (void *)24); glActiveTexture(0x84C0); glGetIntegerv(0x8069 /* TEXTURE_BINDING_2D */, &was); glBindTexture(0x0DE1, g_rz_tex); }
    else glVertexAttrib4fv(9, uv);
    glDrawArrays(4 /* TRIANGLES */, 0, g_rz_n);
    if (tx) { glBindTexture(0x0DE1, (GLuint)was); glDisableVertexAttribArray(9); }
    glBindVertexArray(g_vao); g_r->draws++; return 1; }
static void do_draw_idx(u32 mode, u32 n, u32 mn, u32 mx, f32 *inl, u32 *idx) {
    zcount_draw(mode, n);
    PROG *p; if (mode < 1 || mode > 10 || mx < mn || mx - mn >= 262144) { g_r->pad[0]++; return; }
    if (!vsh_setup(&p)) { g_r->pad[1]++; return; }
    if (g_skip_draw) return;
    u32 nv = mx - mn + 1; if (g_cur_fb) g_cur_fb->gpu_frame = GetTickCount();
    hud_uniform(1.0f, 0.0f);   /* Milestone 96: indexed draws are 3D: no HUD squeeze (the uniform may hold a 2D draw's) */
    if (razor_draw(mode, n, nv, mn, mx, inl)) return;
    {   /* Milestone 95: meshlog.txt -> mesh_log.txt (each distinct mesh: vertex address, indices, vertices, texture, draws, over 10 s);
           hidemesh.txt: vertex addresses (decimal, one per line) whose draws are skipped. Both read every second. */
        static u32 tl, on, hide[256], nh; u32 now = GetTickCount(); u32 vaddr = K(0x1720) & 0x03FFFFFF;
        if (now - tl > 1000) { tl = now; on = GetFileAttributesA("meshlog.txt") != 0xFFFFFFFF; nh = 0; HANDLE f = CreateFileA("hidemesh.txt", 0x80000000, 1, 0, 3, 0x80, 0);
            if (f != (HANDLE)-1) { static char s[4096]; DWORD n = 0; ReadFile(f, s, 4095, &n, 0); CloseHandle(f); u32 v = 0; int any = 0; for (DWORD i = 0; i <= n && nh < 256; i++) { char ch = i < n ? s[i] : 0x0A; if (ch >= 0x30 && ch <= 0x39) { v = v * 10 + (ch - 0x30); any = 1; } else { if (any) hide[nh++] = v; v = 0; any = 0; } } } }
        for (u32 k = 0; k < nh; k++) if (hide[k] == vaddr) return;
        if ((n == 954 && nv == 399) || (n == 28 && nv == 12)) {   /* Milestone 95: the MK-ASLT's meshes: log their vertex layout and position bounds once */
            static u32 logged[2]; int wi = n == 28; if (!logged[wi]) { logged[wi] = 1; char tx[1500]; u32 q = 0;
                #define PUTN(v) { u32 vv = (u32)(v); char d[12]; int zz = 0; do d[zz++] = (char)(0x30 + vv % 10); while (vv /= 10); while (zz) tx[q++] = d[--zz]; tx[q++] = 0x20; }
                PUTN(n) PUTN(nv) PUTN(vaddr) tx[q++] = 0x7C;
                for (u32 i = 0; i < 16; i++) { u32 f = K(0x1760 + i * 4); if (!((f >> 4) & 0xF)) continue; PUTN(i) PUTN(f & 0xF) PUTN((f >> 4) & 0xF) PUTN((f >> 8) & 0xFF) PUTN(K(0x1720 + i * 4) & 0x03FFFFFF) tx[q++] = 0x7C; }
                u32 f0 = K(0x1760), st0 = (f0 >> 8) & 0xFF, ao = K(0x1720) & 0x03FFFFFF;
                if ((f0 & 0xF) == 2 && st0) { f32 lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}; for (u32 v = mn; v <= mx; v++) { f32 *p = (f32 *)(g_mem + ao + v * st0); for (int k = 0; k < 3; k++) { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; } }
                    for (int k = 0; k < 3; k++) { PUTN((s32)(lo[k] * 1000) + 100000) PUTN((s32)(hi[k] * 1000) + 100000) } }
                tx[q++] = 0x0D; tx[q++] = 0x0A; HANDLE fh = CreateFileA("razor_mesh_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); }
                #undef PUTN
            } }
        static u32 ma[512], mi[512], mv[512], mt[512], mc[512], nm, t0, written;
        if (on && !written) { if (!t0) t0 = now; u32 k = 0; while (k < nm && ma[k] != vaddr) k++; if (k == nm && nm < 512) { TI t; ma[k] = vaddr; mi[k] = n; mv[k] = nv; mt[k] = ((K(0x1B0C) >> 30) & 1) && tex_info(0, &t) ? t.off : 0; mc[k] = 0; nm++; } if (k < nm) mc[k]++;
            if (now - t0 > 10000) { written = 1; HANDLE f = CreateFileA("mesh_log.txt", 0x40000000, 0, 0, 2, 0x80, 0);
                if (f != (HANDLE)-1) { for (u32 j = 0; j < nm; j++) { u32 vals[5] = {ma[j], mi[j], mv[j], mt[j], mc[j]}; char tx[80]; u32 q = 0;
                        for (int z = 0; z < 5; z++) { u32 vv = vals[z]; char d[12]; int zz = 0; do d[zz++] = (char)(0x30 + vv % 10); while (vv /= 10); while (zz) tx[q++] = d[--zz]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A; DWORD w; WriteFile(f, tx, q, &w, 0); } CloseHandle(f); } } } }
    {   /* hudlog.txt: small array draws (<= 8 vertices) whose raw positions lie near the screen centre -> hud_log.txt */
        static u32 on, tl, seen[64], ns; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; on = GetFileAttributesA("hudlog.txt") != 0xFFFFFFFF; }
        u32 f0 = K(0x1760), ty = f0 & 0xF, sz = (f0 >> 4) & 0xF, st = (f0 >> 8) & 0xFF, ao = K(0x1720) & 0x03FFFFFF;
        if (on && nv <= 8 && ns < 64 && ty == 2 && sz >= 2 && ao + (mx + 1) * st < 0x4000000) { f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (u32 k = mn; k <= mx; k++) { f32 *q = (f32 *)(g_mem + ao + k * st); if (q[0] < x0) x0 = q[0]; if (q[0] > x1) x1 = q[0]; if (q[1] < y0) y0 = q[1]; if (q[1] > y1) y1 = q[1]; }
            f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            if (x1 - x0 < 200 && y1 - y0 < 200 && cx > 200 && cx < 440 && cy > 100 && cy < 300) { TI t; u32 toff = ((K(0x1B0C) >> 30) & 1) && tex_info(0, &t) ? t.off : 0;
                u32 key = toff ^ ((u32)x0 << 20) ^ ((u32)y0 << 10) ^ (u32)(x1 - x0) * 7; u32 k = 0; while (k < ns && seen[k] != key) k++;
                if (k == ns) { seen[ns++] = key; u32 vals[11] = {2000 + mode, nv, (u32)x0, (u32)y0, (u32)x1, (u32)y1, toff, g_cur_fb ? g_cur_fb->w : 0, g_frame, ao, g_vstart}; char tx[220]; u32 q = 0;
                    for (int z = 0; z < 11; z++) { u32 vv = vals[z]; char d[12]; int zz = 0; do d[zz++] = (char)(0x30 + vv % 10); while (vv /= 10); while (zz) tx[q++] = d[--zz]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
                    HANDLE fh = CreateFileA("hud_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } }
    glBindVertexArray(g_vao2); glBindBuffer(0x8892, g_vbo2);
    /* attributes with arrays (Milestone 81): the raw Xbox bytes are uploaded and OpenGL reads the formats itself;
       ranges that overlap or lie close together (interleaved buffers) are uploaded once. CMP normals are converted. */
    u32 lo[16], hi[16], grp[16], ng = 0, glo[16], ghi[16], gpos[16], tot = 0, cmpbase = 0;
    for (u32 i = 0; i < 16; i++) {
        u32 f = K(0x1760 + i * 4), type = f & 0xF, size = (f >> 4) & 0xF, stride = (f >> 8) & 0xFF;
        grp[i] = 0xFFFFFFFF; if (!size || type == 6) continue;
        u32 eb = type == 2 ? size * 4 : (type == 1 || type == 5) ? size * 2 : size;
        lo[i] = (K(0x1720 + i * 4) & 0x03FFFFFF) + mn * stride; hi[i] = lo[i] + (nv - 1) * stride + eb;
        if (hi[i] > 0x4000000) continue;
        u32 gi = 0; for (; gi < ng; gi++) if (lo[i] < ghi[gi] + 65536 && hi[i] + 65536 > glo[gi]) { if (lo[i] < glo[gi]) glo[gi] = lo[i]; if (hi[i] > ghi[gi]) ghi[gi] = hi[i]; break; }
        if (gi == ng) { glo[ng] = lo[i]; ghi[ng] = hi[i]; ng++; }
        grp[i] = gi;
    }
    GLuint gbuf[16]; for (u32 gi = 0; gi < ng; gi++) gbuf[gi] = vbuf_get(glo[gi], ghi[gi]);
    /* CMP normals: converted into the streaming buffer */
    u32 cpos = 0, ncmp = 0; u8 *st = (u8 *)g_stage;
    for (u32 i = 0; i < 16; i++) { u32 f = K(0x1760 + i * 4); if (((f >> 4) & 0xF) && (f & 0xF) == 6) { if (cpos + nv * 16 > 16 * 262144 * 16) break; lo[i] = cpos; for (u32 v = 0; v < nv; v++) fetch(i, mn + v, (f32 *)(st + cpos + v * 16)); cpos += nv * 16; ncmp++; } }
    if (ncmp) { glBindBuffer(0x8892, g_vbo2); glBufferData(0x8892, cpos, st, 0x88E0); }
    for (u32 i = 0; i < 16; i++) {
        u32 f = K(0x1760 + i * 4), type = f & 0xF, size = (f >> 4) & 0xF, stride = (f >> 8) & 0xFF;
        if (size && type == 6) { glBindBuffer(0x8892, g_vbo2); glEnableVertexAttribArray(i); glVertexAttribPointer(i, 4, 0x1406, 0, 16, (void *)lo[i]); continue; }
        if (!size || grp[i] == 0xFFFFFFFF) { glDisableVertexAttribArray(i); glVertexAttrib4fv(i, inl + i * 4); continue; }
        glBindBuffer(0x8892, gbuf[grp[i]]);
        void *o = (void *)(lo[i] - glo[grp[i]]);
        glEnableVertexAttribArray(i);
        switch (type) {
        case 0: if (size == 4) glVertexAttribPointer(i, 0x80E1 /* GL_BGRA */, 0x1401, 1, stride, o); else glVertexAttribPointer(i, size, 0x1401, 1, stride, o); break;
        case 4: glVertexAttribPointer(i, size, 0x1401, 1, stride, o); break;
        case 1: glVertexAttribPointer(i, size, 0x1402 /* SHORT */, 1, stride, o); break;
        case 5: glVertexAttribPointer(i, size, 0x1402, 0, stride, o); break;
        case 2: glVertexAttribPointer(i, size, 0x1406, 0, stride, o); break;
        default: glDisableVertexAttribArray(i); glVertexAttrib4fv(i, inl + i * 4); break;
        }
    }    for (u32 k = 0; k < n; k++) g_idxb[k] = idx[k] - mn;
    glBindBuffer(0x8893, g_ibo); glBufferData(0x8893, n * 4, g_idxb, 0x88E0);
    glDrawElements(mode - 1, n, 0x1405 /* UNSIGNED_INT */, 0);
    glBindVertexArray(g_vao); g_r->draws++;
}
static void hud_move(f32 *v, u32 n, u32 stride, u32 mode);
static u32 g_hud_corner;   /* set by hud_move: this draw is the radar or the health bar */
/* Milestone 96: u_hud for this 2D draw (the program is bound). Narrowed around the screen centre (menus: a line of text comes in several
   draws, and anchoring each to its own screen third tore words apart); the radar and health bar (moved into the left corners) keep the left edge. */
static void hud_squeeze(f32 *v, u32 n, u32 stride) {
    static u32 tl, off; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; off = GetFileAttributesA("nohudfix.txt") != 0xFFFFFFFF; }
    f32 k = 1.0f, anchor = 0;
    if (!off && g_hud_k < 0.999f && n && g_cur_fb && (g_cur_fb->w == 640 || (g_cur_fb->w == 320 && g_cur_fb->h == 240))) {
        f32 W = (f32)g_cur_fb->w, x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (u32 i = 0; i < n; i++) { f32 x = v[i * stride], y = v[i * stride + 1]; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (!(x1 - x0 > W * 0.94f && y1 - y0 > W * 0.7f)) {   /* never a full-screen pass */
            k = g_hud_k; anchor = g_hud_corner ? 0 : W / 2; } }
    hud_uniform(k, anchor); }
static void do_draw_imm(u32 mode, u32 n, f32 *v) {
    if (n == 4 && g_r->pad[54]) {   /* Milestone 90: the reticle (a 64x64 quad centred at 320, 192) is scaled about its centre by pad[54] / 1000 (hip fire large, aiming small) */
        f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (u32 k = 0; k < 4; k++) { f32 *q = v + k * 64; if (q[0] < x0) x0 = q[0]; if (q[0] > x1) x1 = q[0]; if (q[1] < y0) y0 = q[1]; if (q[1] > y1) y1 = q[1]; }
        f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, w = x1 - x0, h = y1 - y0;
        if (w > 16 && w < 160 && h > 16 && h < 160 && (w - h < 6 && h - w < 6) && cx > 312 && cx < 328 && cy > 182 && cy < 202) { f32 s = g_r->pad[54] / 1000.0f;   /* any square quad centred on the reticle: the game changes its size and texture (e.g. after a kill) */
            for (u32 k = 0; k < 4; k++) { f32 *q = v + k * 64; q[0] = cx + (q[0] - cx) * s; q[1] = cy + (q[1] - cy) * s; } } }
    {   /* hudlog.txt: small draws near the centre (attribute 0 = position before the vertex program) -> hud_log.txt */
        static u32 on, tl, seen[48], ns; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; on = GetFileAttributesA("hudlog.txt") != 0xFFFFFFFF; }
        if (on && n && ns < 48) { f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (u32 k = 0; k < n; k++) { f32 *q = v + k * 64; if (q[0] < x0) x0 = q[0]; if (q[0] > x1) x1 = q[0]; if (q[1] < y0) y0 = q[1]; if (q[1] > y1) y1 = q[1]; }
            f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            if (x1 - x0 < 200 && y1 - y0 < 200 && cx > 120 && cx < 520 && cy > 60 && cy < 420) { TI t; u32 toff = ((K(0x1B0C) >> 30) & 1) && tex_info(0, &t) ? t.off : 0;
                u32 key = toff ^ ((u32)x0 << 20) ^ ((u32)y0 << 10) ^ (u32)(x1 - x0) * 7 ^ n * 131; u32 k = 0; while (k < ns && seen[k] != key) k++;
                if (k == ns) { seen[ns++] = key; u32 vals[10] = {1000 + mode, n, (u32)x0, (u32)y0, (u32)x1, (u32)y1, toff, g_cur_fb ? g_cur_fb->w : 0, g_frame, (u32)(v[2] * 1000)}; char tx[200]; u32 q = 0;
                    for (int z = 0; z < 10; z++) { u32 vv = vals[z]; char d[12]; int zz = 0; do d[zz++] = (char)(0x30 + vv % 10); while (vv /= 10); while (zz) tx[q++] = d[--zz]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
                    HANDLE fh = CreateFileA("hud_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } }
    {   /* Milestone 95: radarlog.txt -> radar_log.txt: distinct draws in the radar's corner (x 40..200, y 280..450): mode, count, bbox,
           stage-0 texture (address, format, size, centre texel), shader stage modes, combiner registers, and every vertex attribute of
           vertex 0 that is not 0 0 0 1 (hex floats) */
        static u32 on, tl, seen[40], ns; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; on = GetFileAttributesA("radarlog.txt") != 0xFFFFFFFF; }
        if (on && n && ns < 40) { f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (u32 k = 0; k < n; k++) { f32 *q = v + k * 64; if (q[0] < x0) x0 = q[0]; if (q[0] > x1) x1 = q[0]; if (q[1] < y0) y0 = q[1]; if (q[1] > y1) y1 = q[1]; }
            f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            if (x1 - x0 < 170 && y1 - y0 < 170 && cx > 40 && cx < 200 && cy > 280 && cy < 450) { TI t; int ht = ((K(0x1B0C) >> 30) & 1) && tex_info(0, &t);
                u32 key = (ht ? t.off * 3 + t.cf : 0) ^ n * 131 ^ mode * 7919 ^ hrange(0x260, 0x290) ^ hrange(0xA60, 0xAE0) * 7 ^ (u32)(x1 - x0) * 31; u32 k = 0; while (k < ns && seen[k] != key) k++;
                if (k == ns) { seen[ns++] = key; char tx[1400]; u32 q = 0;
                    #define HX(val_) { u32 vv_ = (val_); for (int s_ = 28; s_ >= 0; s_ -= 4) tx[q++] = "0123456789ABCDEF"[(vv_ >> s_) & 15]; tx[q++] = 0x20; }
                    HX(mode) HX(n) HX((u32)x0) HX((u32)y0) HX((u32)x1) HX((u32)y1) tx[q++] = 0x7C; tx[q++] = 0x20;
                    if (ht) { HX(t.off) HX(t.cf) HX(t.w) HX(t.h) HX(texel(&t, t.w / 2, t.h / 2)) } else { HX(0) }
                    tx[q++] = 0x7C; tx[q++] = 0x20; HX(K(0x1E70)) HX(K(0x1E60)) HX(K(0x288)) HX(K(0x28C)) HX(K(0x260)) HX(K(0xAC0)) HX(K(0xA60)) HX(K(0xA80)) HX(K(0x1E40)) HX(K(0xAA0))
                    tx[q++] = 0x7C;
                    for (u32 a = 0; a < 16; a++) { f32 *at = v + a * 4; if (at[0] == 0 && at[1] == 0 && at[2] == 0 && (at[3] == 1 || at[3] == 0)) continue; tx[q++] = 0x20; tx[q++] = 0x61; tx[q++] = (char)(0x30 + a / 10); tx[q++] = (char)(0x30 + a % 10); tx[q++] = 0x3A;
                        for (int c4 = 0; c4 < 4; c4++) { union { f32 f; u32 u; } cv; cv.f = at[c4]; HX(cv.u) } }
                    #undef HX
                    tx[q++] = 0x0D; tx[q++] = 0x0A;
                    HANDLE fh = CreateFileA("radar_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } }
    hud_move(v, n, 64, mode);
    zcount_draw(mode, n);
    PROG *p; if (mode < 1 || mode > 10 || !vsh_setup(&p)) { g_r->pad[1]++; return; }
    if (g_skip_draw) return;
    hud_squeeze(v, n, 64);
    if (g_cur_fb) g_cur_fb->gpu_frame = GetTickCount();
    glBindVertexArray(g_vao2); glBindBuffer(0x8892, g_vbo2); glBufferData(0x8892, n * 256, v, 0x88E0);
    for (u32 i = 0; i < 16; i++) { glEnableVertexAttribArray(i); glVertexAttribPointer(i, 4, 0x1406, 0, 256, (void *)(i * 16)); }
    glDrawArrays(mode - 1, 0, n);
    glBindVertexArray(g_vao); g_r->draws++;
}
/* Milestone 96: HUD layout. Screen-space draws (both 2D paths) lying wholly inside the radar's area or the health bars' area move
   further into their corners. Only on the 640x480 screen surface. nohudmove.txt = the game's layout. stride: floats per vertex */
static void hud_move(f32 *v, u32 n, u32 stride, u32 mode) {
    g_hud_corner = 0;
    {   static u32 tl, off; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; off = GetFileAttributesA("nohudmove.txt") != 0xFFFFFFFF; }
        /* gameplay (the loader's camera code ran just now): menus have text in these areas. Signed: the stamp can be 1 ms ahead of
           this clock (it is tick | 1), and an unsigned difference then read as "long ago" for that frame (the HUD flickered back). */
        s32 age = (s32)(GetTickCount() - g_r->pad[55]);
        u32 play = g_r->pad[55] && age < 1500;
        if (!off && play && n && g_cur_fb && (g_cur_fb->w == 640 || (g_cur_fb->w == 320 && g_cur_fb->h == 240))) {   /* the screen, and the half-size glow buffer (the HUD's glowing parts are drawn there too) */
            #define HV(k, c) v[(k) * stride + (c)]
            f32 hs = g_cur_fb->w == 320 ? 2.0f : 1.0f;   /* positions on the glow buffer x 2 = screen positions */
            f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (u32 k = 0; k < n; k++) { f32 qx = HV(k, 0) * hs, qy = HV(k, 1) * hs; if (qx < x0) x0 = qx; if (qx > x1) x1 = qx; if (qy < y0) y0 = qy; if (qy > y1) y1 = qy; }
            f32 dx = 0, dy = 0;
            if (x0 >= 40 && x1 <= 205 && y0 >= 280 && y1 <= 452) { dx = -45; dy = 38; }        /* radar (x 55-185, y 300-430): into the bottom-left corner */
            else if (x0 >= 40 && x1 <= 310 && y0 >= 25 && y1 <= 99) { dx = -50; dy = -28; }    /* health / energy bars and their frame (x 63-275, y 38-80; the end cap's quad reaches past x 300): into the top-left corner */
            if (x1 - x0 > 600 && y1 - y0 > 450) dx = dy = 0;   /* never a full-screen pass */
            g_hud_corner = dx != 0 || dy != 0;   /* radar / health: pinned to the left edge by hud_squeeze */
            if (dx != 0 || dy != 0) for (u32 k = 0; k < n; k++) { HV(k, 0) += dx / hs; HV(k, 1) += dy / hs; }
            (void)mode;
            #undef HV
        } } }

/* ---- commands ---- */
static void do_draw(u32 mode, u32 n, u32 *v) {
    zcount_draw(mode, n);
    if (!g_state_ok || g_state_prog != &g_fixedp) { if (!apply_state(&g_fixedp)) return; g_state_ok = 1; g_state_prog = &g_fixedp; }
    if (g_skip_draw) return;
    if (g_cur_fb) g_cur_fb->gpu_frame = GetTickCount();
    if (mode < 1 || mode > 10) return;
    {   /* hudlog.txt (checked every second): small screen-space draws near the centre (the reticle): texture, bbox -> hud_log.txt */
        static u32 on, tl, seen[48], ns; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; on = GetFileAttributesA("hudlog.txt") != 0xFFFFFFFF; }
        if (on && n && ns < 48) { f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (u32 k = 0; k < n; k++) { f32 *q = v + k * VTX_WORDS; if (q[0] < x0) x0 = q[0]; if (q[0] > x1) x1 = q[0]; if (q[1] < y0) y0 = q[1]; if (q[1] > y1) y1 = q[1]; }
            f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
            if (x1 - x0 < 200 && y1 - y0 < 200 && cx > 120 && cx < 520 && cy > 60 && cy < 420) { TI t; u32 toff = ((K(0x1B0C) >> 30) & 1) && tex_info(0, &t) ? t.off : 0;
                u32 key = toff ^ ((u32)x0 << 20) ^ ((u32)y0 << 10) ^ (u32)(x1 - x0) * 7 ^ n * 131; u32 k = 0; while (k < ns && seen[k] != key) k++;
                if (k == ns) { seen[ns++] = key; u32 vals[9] = {mode, n, (u32)x0, (u32)y0, (u32)x1, (u32)y1, toff, g_cur_fb ? g_cur_fb->w : 0, g_frame}; char tx[200]; u32 q = 0;
                    for (int z = 0; z < 9; z++) { u32 vv = vals[z]; char d[12]; int zz = 0; do d[zz++] = (char)(0x30 + vv % 10); while (vv /= 10); while (zz) tx[q++] = d[--zz]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
                    HANDLE fh = CreateFileA("hud_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } }
    hud_move((f32 *)v, n, VTX_WORDS, mode);   /* Milestone 96: the HUD layout on this 2D path too */
    hud_squeeze((f32 *)v, n, VTX_WORDS);
    glBindBuffer(0x8892, g_vbo); glBufferData(0x8892, n * VTX_WORDS * 4, v, 0x88E0 /* STREAM_DRAW */);
    glDrawArrays(mode - 1, 0, n);   /* NV2A's primitive numbers are OpenGL's + 1 */
    g_r->draws++;
}
static void do_clear(u32 flags) {
    u32 W, H, zb; FB *f = surface(&W, &H, &zb); if (!f) return;
    f->drawn = 1; g_state_ok = 0; state_cache_reset();
    u32 gs = f->gs ? f->gs : 1; glBindFramebuffer(0x8D40, f->fbo); glViewport(0, 0, W * gs, H * gs);   /* Milestone 96: GPU size */
    u32 x0 = K(0x1D98) & 0xFFFF, x1 = K(0x1D98) >> 16, y0 = K(0x1D9C) & 0xFFFF, y1 = K(0x1D9C) >> 16;
    glEnable(0x0C11); glScissor(x0 * gs, y0 * gs, (x1 - x0 + 1) * gs, (y1 - y0 + 1) * gs);
    f32 c[4]; unpack4(K(0x1D90), c); glClearColor(c[0], c[1], c[2], c[3]);
    u32 zv = K(0x1D8C); glClearDepth(zb == 16 ? (zv & 0xFFFF) / 65535.0 : (zv >> 8) / 16777215.0); glClearStencil(zv & 0xFF);
    glColorMask((flags >> 4) & 1, (flags >> 5) & 1, (flags >> 6) & 1, (flags >> 7) & 1); glDepthMask(flags & 1); glStencilMask((flags & 2) ? 0xFF : 0);
    glClear(((flags & 0xF0) ? 0x4000 : 0) | ((flags & 1) ? 0x100 : 0) | ((flags & 2) ? 0x400 : 0));
    {   /* clearlog.txt: clears after 60 s (surface address, w, flags, colour, frame) -> clear_log.txt */
        static u32 on = 2, n, t0; if (on == 2) on = GetFileAttributesA("clearlog.txt") != 0xFFFFFFFF; if (on && !t0) t0 = GetTickCount();
        if (on && n < 200 && GetTickCount() - t0 > 60000) { n++; u32 vals[5] = {f->coff, f->w, flags, K(0x1D90), g_frame}; char tx[120]; u32 q = 0;
            for (int k = 0; k < 5; k++) { u32 v = vals[k]; char d[12]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q++] = d[--z]; tx[q++] = 0x20; } tx[q++] = 0x0D; tx[q++] = 0x0A;
            HANDLE fh = CreateFileA("clear_log.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } }
}
static void *g_dc; static void *g_wnd;
static u32 g_last_save;
/* F12 in the window (Milestone 87): the next presented picture is also saved as shotN.bmp */
volatile int g_shot_req; static u32 g_shot_n;
static const char *g_save_name = "frame.bmp";
static void save_frame(FB *f) {
    static u32 px[640 * 480]; static u8 h[54];
    if (f->w > 640 || f->h > 480) return;
    if (f->gs > 1) {   /* Milestone 96: a scaled surface is scaled down to its Xbox size first (into a 640x480 helper) */
        static GLuint sfbo, stex; if (!sfbo) { glGenTextures(1, &stex); glBindTexture(0x0DE1, stex); glTexImage2D(0x0DE1, 0, 0x8058, 640, 480, 0, 0x80E1, 0x1401, 0);
            glTexParameteri(0x0DE1, 0x2801, 0x2601); glTexParameteri(0x0DE1, 0x2800, 0x2601); glGenFramebuffers(1, &sfbo); glBindFramebuffer(0x8D40, sfbo); glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, stex, 0); }
        glBindFramebuffer(0x8CA8, f->fbo); glBindFramebuffer(0x8CA9, sfbo); glDisable(0x0C11);
        glBlitFramebuffer(0, 0, f->w * f->gs, f->h * f->gs, 0, 0, f->w, f->h, 0x4000, 0x2601);
        glBindFramebuffer(0x8CA8, sfbo); glReadPixels(0, 0, f->w, f->h, 0x80E1, 0x1401, px); g_hs_surf = 0; }
    else { glBindFramebuffer(0x8CA8 /* READ */, f->fbo); glReadPixels(0, 0, f->w, f->h, 0x80E1, 0x1401, px); }
    u32 n = f->w * f->h * 4;
    zero(h, 54); h[0] = 'B'; h[1] = 'M'; *(u32 *)(h + 2) = 54 + n; *(u32 *)(h + 10) = 54; *(u32 *)(h + 14) = 40;
    *(s32 *)(h + 18) = f->w; *(s32 *)(h + 22) = -(s32)f->h; *(u16 *)(h + 26) = 1; *(u16 *)(h + 28) = 32; *(u32 *)(h + 34) = n;
    HANDLE fh = CreateFileA(g_save_name, 0x40000000, 1, 0, 2, 0x80, 0); if (fh == (HANDLE)-1) return;
    DWORD w; WriteFile(fh, h, 54, &w, 0); WriteFile(fh, px, n, &w, 0); CloseHandle(fh);
}
/* a surface the CPU wrote (Bink movies): shown from Xbox memory (Milestone 80) */
static GLuint g_memtex, g_memfbo;
/* Milestone 96: the picture goes to the window through a shader pass: the 4:3 image is kept at its shape, centred with black bars
   (a widescreen window used to stretch it), and FXAA smooths the jagged edges (the game has no anti-aliasing at 640x480).
   stretch.txt = fill the window as before; noaa.txt = no FXAA (both read every second). */
static GLuint g_pprog, g_pvao; static GLint g_p_tex, g_p_rcp; static u32 g_present_movie;
static void present_tex(GLuint tex, u32 w, u32 h) {
    static u32 tl, stretch, noaa; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; stretch = GetFileAttributesA("pillarbox.txt") != 0xFFFFFFFF; noaa = GetFileAttributesA("noaa.txt") != 0xFFFFFFFF; }
    if (!g_pprog) {
        GLuint vs = compile(0x8B31, "#version 130\nout vec2 uv;\nvoid main() { vec2 p = vec2(float((gl_VertexID & 1) * 4 - 1), float((gl_VertexID >> 1) * 4 - 1)); uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); gl_Position = vec4(p, 0.0, 1.0); }\n");
        GLuint fs = compile(0x8B30,   /* FXAA (after Timothy Lottes' FXAA 3, the short console form); uv.y runs down: the Xbox's row 0 is at the top */
            "#version 130\nuniform sampler2D t; uniform vec4 rcp; in vec2 uv; out vec4 o;\n"
            "void main() {\n"
            "  vec2 c = uv; vec3 m = texture(t, c).rgb;\n"
            "  if (rcp.z > 0.5) {\n"   /* no FXAA: sharpening only */
            "    vec3 q4 = texture(t, c + vec2(0.0, -rcp.y)).rgb + texture(t, c + vec2(0.0, rcp.y)).rgb + texture(t, c + vec2(-rcp.x, 0.0)).rgb + texture(t, c + vec2(rcp.x, 0.0)).rgb;\n"
            "    o = vec4(clamp(m + (m - q4 * 0.25) * rcp.w, 0.0, 1.0), 1.0); return; }\n"
            "  vec3 L = vec3(0.299, 0.587, 0.114);\n"
            "  float nw = dot(texture(t, c + vec2(-1.0, -1.0) * rcp.xy).rgb, L), ne = dot(texture(t, c + vec2(1.0, -1.0) * rcp.xy).rgb, L);\n"
            "  float sw = dot(texture(t, c + vec2(-1.0, 1.0) * rcp.xy).rgb, L), se = dot(texture(t, c + vec2(1.0, 1.0) * rcp.xy).rgb, L), lm = dot(m, L);\n"
            "  float lmin = min(lm, min(min(nw, ne), min(sw, se))), lmax = max(lm, max(max(nw, ne), max(sw, se)));\n"
            "  vec2 d = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));\n"
            "  float dr = max((nw + ne + sw + se) * 0.25 * 0.125, 1.0 / 128.0), rc = 1.0 / (min(abs(d.x), abs(d.y)) + dr);\n"
            "  d = clamp(d * rc, vec2(-8.0), vec2(8.0)) * rcp.xy;\n"
            "  vec3 a = 0.5 * (texture(t, c + d * (1.0 / 3.0 - 0.5)).rgb + texture(t, c + d * (2.0 / 3.0 - 0.5)).rgb);\n"
            "  vec3 b = a * 0.5 + 0.25 * (texture(t, c - d * 0.5).rgb + texture(t, c + d * 0.5).rgb);\n"
            "  float lb = dot(b, L); vec3 f = (lb < lmin || lb > lmax) ? a : b;\n"
            /* sharpening (contrast-adaptive, after AMD's CAS idea): the 640x480 frame is enlarged 2-3x on a PC screen and looked soft; detail is lifted
               where the local contrast is low, less on strong edges (no halos); rcp.w = strength (sharpen.txt, percent; default 60) */
            "  vec3 n4 = texture(t, c + vec2(0.0, -rcp.y)).rgb + texture(t, c + vec2(0.0, rcp.y)).rgb + texture(t, c + vec2(-rcp.x, 0.0)).rgb + texture(t, c + vec2(rcp.x, 0.0)).rgb;\n"
            "  float amp = clamp(min(lmin, 1.0 - lmax) / max(lmax, 1e-4), 0.0, 1.0);\n"
            "  o = vec4(clamp(f + (f - n4 * 0.25) * rcp.w * sqrt(amp) * 2.0, 0.0, 1.0), 1.0); }\n");
        if (!vs || !fs) { g_pprog = 0xFFFFFFFF; }
        else { g_pprog = glCreateProgram(); glAttachShader(g_pprog, vs); glAttachShader(g_pprog, fs); glBindFragDataLocation(g_pprog, 0, "o"); glLinkProgram(g_pprog);
            g_p_tex = glGetUniformLocation(g_pprog, "t"); g_p_rcp = glGetUniformLocation(g_pprog, "rcp"); glGenVertexArrays(1, &g_pvao); } }
    s32 rc[4]; GetClientRect(g_wnd, rc); s32 cw = rc[2], ch = rc[3]; if (cw <= 0 || ch <= 0) return;
    s32 dw = cw, dh = ch, dx = 0, dy = 0;
    {   /* the squeeze in the shaders is off: it left the game's own 4:3 view filling only the middle of the screen (its culling
           is 4:3). Widescreen is done in the game's camera instead (input.c cam_widen). shaderwide.txt = this squeeze again. */
        static u32 tw, sw; if (now - tw > 1000) { tw = now; sw = GetFileAttributesA("shaderwide.txt") != 0xFFFFFFFF; }
        g_wide_k = (sw && !stretch && cw * 3 > ch * 4) ? (4.0f * ch) / (3.0f * cw) : 1.0f;
        g_hud_k = (!stretch && cw * 3 > ch * 4) ? (4.0f * ch) / (3.0f * cw) : 1.0f; }   /* the HUD squeeze: how much the window stretches the 4:3 frame */
    if (stretch) { if (cw * 3 > ch * 4) { dw = ch * 4 / 3; dx = (cw - dw) / 2; } else { dh = cw * 3 / 4; dy = (ch - dh) / 2; } }   /* pillarbox.txt: 4:3 with bars (the user wants none: widescreen is drawn wider instead) */
    glBindFramebuffer(0x8D40, 0); glDisable(0x0C11); glColorMask(1, 1, 1, 1);
    glViewport(0, 0, cw, ch); glClearColor(0, 0, 0, 1); glClear(0x4000);
    if (g_pprog == 0xFFFFFFFF) {   /* no shader: the old blit, into the 4:3 area */
        glBlitFramebuffer(0, 0, w, h, dx, dy + dh, dx + dw, dy, 0x4000, 0x2601); return; }
    glDisable(0x0B71); glDisable(0x0BE2); glDisable(0x0B44); glDisable(0x0B90); glDisable(0x0BC0); glDisable(0x8037);   /* depth, blend, cull, stencil, alpha test, polygon offset */
    glViewport(dx, dy, dw, dh); glUseProgram(g_pprog);
    glActiveTexture(0x84C0); glBindTexture(0x0DE1, tex); glTexParameteri(0x0DE1, 0x2800, 0x2601); glTexParameteri(0x0DE1, 0x2801, 0x2601);
    glTexParameteri(0x0DE1, 0x2802, 0x812F); glTexParameteri(0x0DE1, 0x2803, 0x812F);
    static f32 sharp = -1; static u32 tsh; if (sharp < 0 || now - tsh > 1000) { tsh = now; sharp = 0.6f; HANDLE fh = CreateFileA("sharpen.txt", 0x80000000, 1, 0, 3, 0x80, 0);   /* sharpen.txt: strength in percent (0 = off) */
        if (fh != (HANDLE)-1) { char sb[8] = {0}; DWORD nr = 0; ReadFile(fh, sb, 7, &nr, 0); CloseHandle(fh); u32 sv = 0; int any = 0; for (DWORD k = 0; k < nr; k++) if (sb[k] >= 0x30 && sb[k] <= 0x39) { sv = sv * 10 + sb[k] - 0x30; any = 1; } if (any) sharp = sv / 100.0f; } }
    glUniform1i(g_p_tex, 0); glUniform4f(g_p_rcp, 1.0f / w, 1.0f / h, (noaa || g_present_movie) ? 1.0f : 0.0f, g_present_movie ? sharp * 0.5f : sharp);   /* movies: no FXAA (it softened them), light sharpening */
    glBindVertexArray(g_pvao); glDrawArrays(4, 0, 3); glBindVertexArray(g_vao); g_curp = 0; }
static void present_memory(u32 addr) {
    u32 pitch = K(0x20C) & 0xFFFF; if (!pitch) pitch = 2560; u32 w = 640, h = 480;
    if ((addr & 0x03FFFFFF) + pitch * h > 0x4000000) return;
    if (!g_memfbo) { glGenTextures(1, &g_memtex); glBindTexture(0x0DE1, g_memtex); glTexImage2D(0x0DE1, 0, 0x8058, w, h, 0, 0x80E1, 0x1401, 0); glTexParameteri(0x0DE1, 0x2801, 0x2601); glTexParameteri(0x0DE1, 0x2800, 0x2601);
        glGenFramebuffers(1, &g_memfbo); glBindFramebuffer(0x8D40, g_memfbo); glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, g_memtex, 0); }
    glBindTexture(0x0DE1, g_memtex); glPixelStorei(0x0CF2 /* UNPACK_ROW_LENGTH */, pitch / 4);
    glTexSubImage2D(0x0DE1, 0, 0, 0, w, h, 0x80E1, 0x1401, PHYS(addr)); glPixelStorei(0x0CF2, 0);
    {   /* present probe (Milestone 86): new picture (N) or same (s) and ms since the last present; 600 entries to present_log.txt */
        static char pl[600 * 8]; static u32 np, pn, lastsum, done; static long long lt, qf2; long long t; if (!qf2) QueryPerformanceFrequency(&qf2); QueryPerformanceCounter(&t);
        u32 sum = 0; const u32 *q = (const u32 *)PHYS(addr); for (u32 i = 0; i < 640 * 480; i += 4801) sum = sum * 31 + q[i];
        if (!done && ++pn > 900) { u32 ms = lt ? (u32)((t - lt) * 1000 / qf2) : 0; if (ms > 99) ms = 99; pl[np++] = sum != lastsum ? 0x4E : 0x73; pl[np++] = (char)(0x30 + ms / 10); pl[np++] = (char)(0x30 + ms % 10); pl[np++] = 0x20;
            if (np >= 600 * 4) { HANDLE fh = CreateFileA("present_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { u32 wr; WriteFile(fh, pl, np, &wr, 0); CloseHandle(fh); } done = 1; } }
        lastsum = sum; lt = t; }
    glBindFramebuffer(0x8CA8, g_memfbo); g_present_movie = 1; present_tex(g_memtex, w, h); g_present_movie = 0;   /* Milestone 96: 4:3 kept, FXAA (the read framebuffer serves the fallback blit) */
    SwapBuffers(g_dc); g_frame++; g_r->frames++; g_state_ok = 0; state_cache_reset();
    { static FB mf; mf.fbo = g_memfbo; mf.w = w; mf.h = h; if (g_shot_req) { { u32 v[2] = {g_shot_n + 1, g_frame}; dcol_line(v, 2, "SHOT"); } static char nm[24]; u32 k = ++g_shot_n, q = 0; const char *s = "shot"; while (*s) nm[q++] = *s++; char d[10]; int z = 0; do d[z++] = (char)(0x30 + k % 10); while (k /= 10); while (z) nm[q++] = d[--z]; s = ".bmp"; while (*s) nm[q++] = *s++; nm[q] = 0; g_save_name = nm; g_shot_req = 0; save_frame(&mf); g_save_name = "frame.bmp"; }
      u32 t = GetTickCount(); if (t - g_last_save > 5000) { g_last_save = t; save_frame(&mf); } }
}
/* depth-pass pixel counts (Milestone 87): an occlusion query runs while the game counts; the total since the last
   clear is written as the NV2A's 16-byte report (timestamp, count, 0) at the physical address it asked for */
/* never 0 (Milestone 87): the game also occlusion-culls objects with these counts, using the previous frame's result,
   so with fast turning objects coming into view counted 0 and popped in late; a minimum of 1 keeps them drawn (the sun
   glare, which scales with the count, is unaffected in practice). loader\zeroreports.txt restores exact counts. */
/* Milestone 96: no waiting. Reading each query's result at once made the viewer wait for the GPU to finish everything before it, up to
   270 times a frame (in-mission cutscenes ran at ~35 frames/s with the viewer 100% busy, worse at 2x resolution). Now each counting
   stretch is its own query (a ring of segments), a report remembers which segments it sums, and its count is written when the GPU
   has them (checked at each report and flip). Until then the report keeps the count its memory held (the game reuses the address
   each frame and uses the previous frame's count anyway). zsync.txt (read at start) = the old waiting reads. */
#define ZSEG 4096
#define ZREC 2048
static GLuint g_zqs[ZSEG]; static u32 g_zval[ZSEG], g_zvalid[ZSEG];   /* segment results (g_zvalid = segment number + 1 when read) */
static u32 g_zseg, g_zbase;                                            /* next segment number; first segment since the last reset */
static struct { u32 addr, s0, s1, gs; } g_zrec[ZREC]; static u32 g_zrh, g_zrt;   /* pending reports (head = oldest) */
static u32 g_zmin = 2, g_zfixed, g_zsync = 2;
static void zq_stop(void) { if (!g_zrun) return; glEndQuery(0x8914 /* SAMPLES_PASSED */); g_zseg++; g_zrun = 0; }
static void zrep_write(u32 addr, u32 cnt);
static int zseg_ready(u32 s) { u32 k = s % ZSEG; if (g_zvalid[k] == s + 1) return 1; GLuint a = 0; glGetQueryObjectuiv(g_zqs[k], 0x8867 /* RESULT_AVAILABLE */, &a); return a != 0; }
static u32 zseg_val(u32 s) { u32 k = s % ZSEG; if (g_zvalid[k] != s + 1) { GLuint n = 0; glGetQueryObjectuiv(g_zqs[k], 0x8866 /* RESULT */, &n); g_zval[k] = n; g_zvalid[k] = s + 1; } return g_zval[k]; }
static void zq_poll(int force) {   /* write every report whose segments are done (queries finish in order: the last one decides) */
    while (g_zrh != g_zrt) { u32 i = g_zrh % ZREC; u32 s0 = g_zrec[i].s0, s1 = g_zrec[i].s1;
        if (s1 > s0 && !force && !zseg_ready(s1 - 1)) break;
        u32 cnt = 0; for (u32 s = s0; s < s1; s++) cnt += zseg_val(s);
        u32 gs = g_zrec[i].gs; if (gs > 1) cnt = (cnt + gs * gs - 1) / (gs * gs);   /* counts in Xbox pixels (gs x gs GPU pixels per Xbox pixel) */
        zrep_write(g_zrec[i].addr, cnt); g_zrh++; } }
static void zq_start(void) { if (g_zrun || !g_zon) return;
    if (g_zrh != g_zrt && g_zseg - g_zrec[g_zrh % ZREC].s0 >= ZSEG - 1) zq_poll(1);   /* a pending report still needs the slot about to be reused */
    if (g_zseg - g_zbase >= ZSEG - 1) g_zbase = g_zseg - (ZSEG / 2);                  /* (counting for very long without a reset: keep the newest half) */
    u32 k = g_zseg % ZSEG; if (!g_zqs[k]) glGenQueries(1, &g_zqs[k]); glBeginQuery(0x8914, g_zqs[k]); g_zrun = 1; }
static void zrep_write(u32 addr, u32 cnt) {
    u32 *r = (u32 *)PHYS(addr);
    /* only while the report is still the game's pending one (word 3 = 0xFFFFFFFF, set by D3D before asking): a count arriving a
       frame late can find that memory freed and reused (a level load crashed reading 0x200 from an object); then it is dropped */
    if (r[3] != 0xFFFFFFFFu) { static u32 nd; if (nd++ < 5) { u32 v[2] = {addr, r[3]}; (void)v; } return; }
    r[0] = 0x44556677; r[1] = 0x00112233; r[2] = g_zfixed ? g_zfixed : (cnt ? cnt : g_zmin);
    __asm__ volatile("" ::: "memory"); r[3] = 0;   /* done last: the game reads the count once this word is no longer 0xFFFFFFFF */
    {   /* big-count probe (bigreports.txt): reports above 50000 (address, count) */
        static u32 on2 = 2, nb; if (on2 == 2) on2 = GetFileAttributesA("bigreports.txt") != 0xFFFFFFFF;
        static u32 tb0; if (on2 && !tb0) tb0 = GetTickCount();
        if (on2 && r[2] > 50000 && nb < 40 && GetTickCount() - tb0 > 60000) { nb++; char tx[200]; u32 q2 = 0; u32 vals[9] = {addr, r[2], g_zdraws, g_zverts, g_zdep, g_zfunc, g_zcmask, g_zprim, g_zw * 10000 + g_zh};
            for (int k = 0; k < 9; k++) { u32 v = vals[k]; char d[12]; int z = 0; do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) tx[q2++] = d[--z]; tx[q2++] = 0x20; } tx[q2++] = 0x0D; tx[q2++] = 0x0A;
            HANDLE fh = CreateFileA("big_reports.txt", 0x04, 1, 0, 4, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, tx, q2, &w, 0); CloseHandle(fh); } } }
    {   /* report log (reportlog.txt): offset and count of 3000 reports after 60 s, to report_log.txt */
        static u32 on = 2, n, t0; static char lg[3000 * 14]; static u32 q; if (on == 2) on = GetFileAttributesA("reportlog.txt") != 0xFFFFFFFF;
        if (on && n < 3000) { u32 t = GetTickCount(); if (!t0) t0 = t; if (t - t0 > 60000) { u32 v = r[2]; char d[12]; int z = 0;
            lg[q++] = (char)(0x30 + (addr >> 12) % 16 + ((addr >> 12) % 16 > 9 ? 7 : 0)); lg[q++] = (char)(0x30 + (addr >> 8) % 16 + ((addr >> 8) % 16 > 9 ? 7 : 0)); lg[q++] = (char)(0x30 + (addr >> 4) % 16 + ((addr >> 4) % 16 > 9 ? 7 : 0)); lg[q++] = 0x3A;
            do d[z++] = (char)(0x30 + v % 10); while (v /= 10); while (z) lg[q++] = d[--z]; lg[q++] = 0x20; n++;
            if (n == 3000) { HANDLE fh = CreateFileA("report_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { DWORD w; WriteFile(fh, lg, q, &w, 0); CloseHandle(fh); } } } } } }
static void do_zpass(u32 op, u32 arg) {
    { static u32 tz; u32 now = GetTickCount(); if (g_zsync == 2 || now - tz > 1000) { tz = now; g_zsync = GetFileAttributesA("zsync.txt") != 0xFFFFFFFF; } }   /* live A/B */
    if (g_zmin == 2) { g_zmin = GetFileAttributesA("minreports.txt") != 0xFFFFFFFF ? 1 : 0;
        HANDLE f = CreateFileA("fixedreports.txt", 0x80000000, 1, 0, 3, 0x80, 0); if (f != (HANDLE)-1) { char t[16]; DWORD n = 0; ReadFile(f, t, 15, &n, 0); CloseHandle(f); u32 v = 0; for (DWORD k = 0; k < n; k++) if (t[k] >= 0x30 && t[k] <= 0x39) v = v * 10 + t[k] - 0x30; g_zfixed = v ? v : 100000; } }
    if (op == 0) { zq_stop(); g_zbase = g_zseg; g_zdraws = g_zverts = 0; zq_start(); }
    else if (op == 1) { if (arg) { g_zon = 1; zq_start(); } else { zq_stop(); g_zon = 0; } }
    else if (op == 2 && arg + 16 <= 0x4000000) { zq_stop(); g_r->pad[43]++;
        /* the game's D3D marks a pending report with word 3 = 0xFFFFFFFF (count 0): it stays so until zrep_write fills in the count */
        if (g_zrt - g_zrh >= ZREC) zq_poll(1);
        u32 i = g_zrt % ZREC; g_zrec[i].addr = arg; g_zrec[i].s0 = g_zbase; g_zrec[i].s1 = g_zseg; g_zrec[i].gs = g_cur_fb && g_cur_fb->gs ? g_cur_fb->gs : 1; g_zrt++;
        zq_poll(g_zsync); zq_start(); }
}
static void do_flip(u32 addr) {
    g_draw_seq++;
    {   /* every pixel count of this frame is written before the flip: one wait per frame, not one per report (counts arriving several
           frames late made objects flicker: the game hides and shows them by these counts). zlag.txt (read every second) = no wait at the flip. */
        static u32 tl, lag; u32 now = GetTickCount(); if (now - tl > 1000) { tl = now; lag = GetFileAttributesA("zlag.txt") != 0xFFFFFFFF; }
        zq_poll(!lag); }
    if (!(addr & 0x03FFFFFF)) {   /* no display address yet (before AvSetDisplayMode): black, not memory at address 0 */
        glBindFramebuffer(0x8D40, 0); glDisable(0x0C11); glClearColor(0, 0, 0, 1); glClear(0x4000); SwapBuffers(g_dc); g_state_ok = 0; state_cache_reset(); return; }
    FB *f = fb_by_color(addr); g_r->pad[4]++;   /* diagnostics (Milestone 86): flips, memory presents, FBO presents, throttled, slowest present (us) */
    static long long qf; if (!qf) QueryPerformanceFrequency(&qf); long long s0; QueryPerformanceCounter(&s0);
    if (!f || !f->drawn || GetTickCount() - f->gpu_frame > 200) {   /* CPU-written screen (movies): shown as the Xbox's video output would, at the vblank */
        static long long last_mem, qf; long long t; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&t);
        if ((t - last_mem) * 1000 >= qf * 8) { last_mem = t; present_memory(addr); g_r->pad[5]++; } else g_r->pad[7]++; return; }
    g_r->pad[6]++;   /* at most every 8 ms (precise timer: GetTickCount steps 15.6 ms) */
    glBindFramebuffer(0x8CA8, f->fbo); present_tex(f->col, f->w * f->gs, f->h * f->gs);   /* the GPU size: FXAA / sharpening steps one GPU pixel */   /* Milestone 96: 4:3 kept, FXAA; upside down as before (the Xbox's row 0 is at the top) */
    {   /* Milestone 90: depth under the reticle (pixel pad[52], pad[53] of a 640x480 screen; default 320, 190), read through
           two pixel-pack buffers (one frame late, no stall) -> pad[50] (float bits, 0..1), pad[51] counts updates. The loader
           unprojects it to aim the player's shots at what the reticle covers. */
        static GLuint pbo[2]; static u32 pi, primed;
        if (!pbo[0]) { glGenBuffers(2, pbo); for (int k = 0; k < 2; k++) { glBindBuffer(0x88EB, pbo[k]); glBufferData(0x88EB, 4, 0, 0x88E1 /* STREAM_READ */); } glBindBuffer(0x88EB, 0); }
        if (f->w >= 320) { u32 rx = g_r->pad[52] ? g_r->pad[52] : 320, ry = g_r->pad[53] ? g_r->pad[53] : 190;
            glBindFramebuffer(0x8CA8, f->fbo); glBindBuffer(0x88EB, pbo[pi]); glReadPixels(rx * f->w / 640 * f->gs + f->gs / 2, ry * f->h / 480 * f->gs + f->gs / 2, 1, 1, 0x1902 /* DEPTH_COMPONENT */, 0x1406, 0);   /* GPU pixel (Milestone 96) */
            if (primed) { f32 dv = 1; glBindBuffer(0x88EB, pbo[pi ^ 1]); glGetBufferSubData(0x88EB, 0, 4, &dv); g_r->pad[50] = *(u32 *)&dv; g_r->pad[51]++; }
            glBindBuffer(0x88EB, 0); pi ^= 1; primed = 1; } }
    {   /* brightness probe (lumaprobe.txt, Milestone 88): mean brightness of each presented frame (8x8 samples of the
           surface), 800 frames from 40 s, to luma_log.txt; flicker shows as jumps between consecutive frames */
        static u32 on = 2, n, t0; static u8 lg[800]; if (on == 2) on = GetFileAttributesA("lumaprobe.txt") != 0xFFFFFFFF;
        if (on && n < 800) { u32 t = GetTickCount(); if (!t0) t0 = t;
            if (t - t0 > 40000) { u8 px[4 * 4]; u32 sum = 0; glBindFramebuffer(0x8CA8, f->fbo);
                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { glReadPixels((x * 2 + 1) * f->w * f->gs / 16, (y * 2 + 1) * f->h * f->gs / 16, 1, 1, 0x1908, 0x1401, px); sum += px[0] + px[1] + px[2]; }
                lg[n++] = (u8)(sum / (64 * 3));
                if (n == 800) { HANDLE fh = CreateFileA("luma_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { char tx[800 * 4]; u32 q = 0; for (u32 k = 0; k < 800; k++) { u32 v = lg[k]; tx[q++] = (char)(0x30 + v / 100); tx[q++] = (char)(0x30 + v / 10 % 10); tx[q++] = (char)(0x30 + v % 10); tx[q++] = 0x20; } DWORD w; WriteFile(fh, tx, q, &w, 0); CloseHandle(fh); } } } } }
    SwapBuffers(g_dc);
    g_frame++; g_r->frames++; g_state_ok = 0; state_cache_reset();
    if (g_shot_req) { { u32 v[2] = {g_shot_n + 1, g_frame}; dcol_line(v, 2, "SHOT"); } static char nm[24]; u32 k = ++g_shot_n, q = 0; const char *s = "shot"; while (*s) nm[q++] = *s++; char d[10]; int z = 0; do d[z++] = (char)(0x30 + k % 10); while (k /= 10); while (z) nm[q++] = d[--z]; s = ".bmp"; while (*s) nm[q++] = *s++; nm[q] = 0; g_save_name = nm; g_shot_req = 0; save_frame(f); g_save_name = "frame.bmp"; }
    u32 t = GetTickCount(); if (t - g_last_save > 5000) { g_last_save = t; save_frame(f); }
}

/* processes ring commands for up to ~10 ms; returns 1 if it did anything */
int gl_pump(void) {
    if (!g_r || !g_r->gl_ok) return 0;
    if (g_zrh != g_zrt) { static u32 fl; if (g_r->rpos == g_r->wpos && fl != g_zrt) { fl = g_zrt; glFlush(); } zq_poll(0); }   /* pixel counts the GPU has finished (the game may be waiting for one) */
    u32 t0 = GetTickCount(), did = 0;
    while (g_r->rpos != g_r->wpos && GetTickCount() - t0 < 10) {
        u32 p = g_r->rpos, h = g_rb[p], type = h >> 24, len = h & 0xFFFFFF;
        if (type == CMD_WRAP) { g_r->rpos = 0; continue; }
        if (!len) { info(" bad command"); g_r->gl_ok = 0; return 0; }
        u32 *a = &g_rb[p + 1];
        switch (type) {
        case CMD_STATE: cpy(g_kel, a, 0x2000); g_state_ok = 0; break;
        case CMD_DRAW: do_draw(a[0], a[1], a + 2); break;
        case CMD_CLEAR: do_clear(a[0]); break;
        case CMD_FLIP: do_flip(a[0]); break;
        case CMD_FENCE: g_r->fence_done = a[0]; break;
        case CMD_VSH: g_vstart = a[0] < 136 ? a[0] : 0; cpy(g_vprog, a + 1, 136 * 16); break;
        case CMD_CONST: if (a[0] < 192 && a[0] + a[1] <= 192) { cpy(&g_vc[a[0]], a + 2, a[1] * 16); g_vcgen++; } break;
        case CMD_DRAW_IDX: do_draw_idx(a[0], a[1], a[2], a[3], (f32 *)(a + 4), a + 4 + 64); break;
        case CMD_DRAW_IMM: do_draw_imm(a[0], a[1], (f32 *)(a + 2)); break;
        case CMD_ZPASS: do_zpass(a[0], a[1]); break;
        }
        __sync_synchronize(); g_r->rpos = p + len; did = 1;
    }
    return did;
}

/* sets up OpenGL on the window and maps the shared memory; on failure the loader draws in software */
int gl_init(void *wnd) {
    HANDLE m = OpenFileMappingA(0x4 /* read */ | 0x2, 0, "BruteForcePCLoader_Ring");
    if (!m || !(g_r = MapViewOfFile(m, 0x6, 0, 0, sizeof(RING) + RING_WORDS * 4)) || g_r->magic != RING_MAGIC) { g_r = 0; return 0; }
    g_rb = (u32 *)(g_r + 1);
    if (g_r->gl_failed) return 0;   /* software.txt */
    HANDLE mm = OpenFileMappingA(0x6, 0, "BruteForcePCLoader_Mem");
    if (!mm || !(g_mem = MapViewOfFile(mm, 0x6 /* read, write: pixel-count reports are written here (Milestone 87) */, 0, 0, 0x4000000))) { info("no shared Xbox memory"); g_r->gl_failed = 1; return 0; }
    g_kel = HeapAlloc(GetProcessHeap(), 8, 0x2000); g_tbuf = HeapAlloc(GetProcessHeap(), 0, 4096 * 4096 * 4);
    if (!g_kel || !g_tbuf) { info("out of memory"); g_r->gl_failed = 1; return 0; }
    g_wnd = wnd; g_dc = GetDC(wnd);
    PFD pfd; zero(&pfd, sizeof pfd); pfd.size = sizeof pfd; pfd.ver = 1; pfd.flags = 0x4 | 0x20 | 0x1; pfd.color = 32; pfd.depth = 24; pfd.stencil = 8;
    s32 pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf || !SetPixelFormat(g_dc, pf, &pfd)) { info("no pixel format"); g_r->gl_failed = 1; return 0; }
    void *rc = wglCreateContext(g_dc);
    if (!rc || !wglMakeCurrent(g_dc, rc)) { info("no OpenGL context"); g_r->gl_failed = 1; return 0; }
    #define LOAD(n) if (!(*(void **)&n = glp(#n))) { info("missing "); info(#n); g_r->gl_failed = 1; return 0; }
    LOAD(glDeleteBuffers) LOAD(glGetBufferSubData) LOAD(glGenBuffers) LOAD(glBindBuffer) LOAD(glBufferData) LOAD(glGenFramebuffers) LOAD(glBindFramebuffer) LOAD(glFramebufferTexture2D) LOAD(glFramebufferRenderbuffer)
    LOAD(glGenRenderbuffers) LOAD(glBindRenderbuffer) LOAD(glRenderbufferStorage) LOAD(glCheckFramebufferStatus) LOAD(glBlitFramebuffer) LOAD(glCreateShader) LOAD(glShaderSource)
    LOAD(glCompileShader) LOAD(glGetShaderiv) LOAD(glGetShaderInfoLog) LOAD(glCreateProgram) LOAD(glAttachShader) LOAD(glLinkProgram) LOAD(glGetProgramiv) LOAD(glGetProgramInfoLog)
    LOAD(glUseProgram) LOAD(glGetUniformLocation) LOAD(glUniform1i) LOAD(glUniform4fv) LOAD(glUniform4iv) LOAD(glUniform2iv) LOAD(glUniform4f) LOAD(glVertexAttribPointer)
    LOAD(glEnableVertexAttribArray) LOAD(glBindAttribLocation) LOAD(glBlendEquation) LOAD(glBlendColor) LOAD(glActiveTexture) LOAD(glGenVertexArrays) LOAD(glBindVertexArray) LOAD(glBindFragDataLocation) LOAD(glGenerateMipmap) LOAD(glGenQueries) LOAD(glBeginQuery) LOAD(glEndQuery) LOAD(glGetQueryObjectuiv)
    GLuint vs = compile(0x8B31, g_vs), fs = compile(0x8B30, g_fs);
    if (!vs || !fs) { g_r->gl_failed = 1; return 0; }
    g_prog = glCreateProgram(); glAttachShader(g_prog, vs); glAttachShader(g_prog, fs);
    static const char *an[8] = {"a_pos", "a_d0", "a_d1", "a_t0", "a_t1", "a_t2", "a_t3", "a_fog"};
    for (u32 i = 0; i < 8; i++) glBindAttribLocation(g_prog, i, an[i]);
    glBindFragDataLocation(g_prog, 0, "o_col");
    glLinkProgram(g_prog);
    GLint ok = 0; glGetProgramiv(g_prog, 0x8B82, &ok);
    if (!ok) { char log[200]; GLsizei n = 0; glGetProgramInfoLog(g_prog, 199, &n, log); log[n < 199 ? n : 199] = 0; info(" link: "); info(log); g_r->gl_failed = 1; return 0; }
    g_fsobj = fs; g_fixedp.id = g_prog; prog_locs(&g_fixedp); g_curp = 0;
    LOAD(glVertexAttrib4fv) LOAD(glDisableVertexAttribArray)
    g_stage = HeapAlloc(GetProcessHeap(), 0, 16 * 262144 * 16); g_idxb = HeapAlloc(GetProcessHeap(), 0, (1u << 20) * 4);
    if (!g_stage || !g_idxb) { info("out of memory"); g_r->gl_failed = 1; return 0; }
    glGenVertexArrays(1, &g_vao2); glGenBuffers(1, &g_vbo2); glGenBuffers(1, &g_ibo);
    glGenVertexArrays(1, &g_vao); glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo); glBindBuffer(0x8892, g_vbo);
    static const u32 offs[7] = {0, 4, 8, 13, 17, 21, 25};   /* words: pos, D0, D1, (fog), T0..T3 */
    for (u32 i = 0; i < 7; i++) { glEnableVertexAttribArray(i); glVertexAttribPointer(i, 4, 0x1406, 0, VTX_WORDS * 4, (void *)(offs[i] * 4)); }
    glEnableVertexAttribArray(7); glVertexAttribPointer(7, 1, 0x1406, 0, VTX_WORDS * 4, (void *)(12 * 4));   /* fog */
    { s32 (GLAPI *swapint)(s32) = glp("wglSwapIntervalEXT"); if (swapint) swapint(GetFileAttributesA("novsyncpresent.txt") != 0xFFFFFFFF ? 0 : 1); }   /* Milestone 87: vsync on (no tearing on fast turns); novsyncpresent.txt turns it off */
    const u8 *ren = glGetString(0x1F01); if (ren) info((const char *)ren);
    __sync_synchronize(); g_r->gl_ok = 1;
    return 1;
}

int _fltused = 0x9875;

/* ---- sound output (Milestone 80): plays the loader's mixed sound from "BruteForcePCLoader_Audio" ---- */
IMP HANDLE WINAPI CreateEventA(void *, BOOL, BOOL, const char *);
IMP DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
IMP void WINAPI Sleep(DWORD);
IMP HANDLE WINAPI CreateThread(void *, u32, DWORD (WINAPI *)(void *), void *, DWORD, DWORD *);
IMP u32 WINAPI waveOutOpen(void **, u32, void *, u32, u32, u32);
IMP u32 WINAPI waveOutPrepareHeader(void *, void *, u32);
IMP u32 WINAPI waveOutWrite(void *, void *, u32);
typedef struct { char *data; u32 len, rec, user, flags, loops; void *next; u32 res; } WHDR;
/* the monitor's vertical blank (Milestone 86): DwmFlush returns once per composed frame, i.e. per real refresh. The
   count goes to the ring (pad[9]); the loader runs the Xbox vblank on it, so game frames line up with screen refreshes
   (an emulated 60.000 Hz beat against the monitor's refresh and made movies judder now and then). */
IMP BOOL WINAPI SetThreadPriority(HANDLE, int);
IMP BOOL WINAPI SetEvent(HANDLE);
IMP HANDLE WINAPI GetCurrentThread(void);
static DWORD WINAPI vsync_thread(void *p) {
    void *d = LoadLibraryA("dwmapi.dll"); long (WINAPI *flush)(void) = d ? (long (WINAPI *)(void))GetProcAddress(d, "DwmFlush") : 0;
    if (!flush) return 0;
    for (;;) { while (!g_r) Sleep(10); if (flush() < 0) { Sleep(16); continue; } g_r->pad[9]++; }
}
/* during a freeze: every loader thread's previous suspend count and eip */
IMP HANDLE WINAPI CreateToolhelp32Snapshot(DWORD, DWORD);
IMP BOOL WINAPI Thread32First(HANDLE, void *);
IMP BOOL WINAPI Thread32Next(HANDLE, void *);
IMP HANDLE WINAPI OpenThread(DWORD, BOOL, DWORD);
IMP DWORD WINAPI SuspendThread(HANDLE);
IMP DWORD WINAPI ResumeThread(HANDLE);
IMP BOOL WINAPI GetThreadContext(HANDLE, void *);
__declspec(dllimport) long __stdcall NtQueryInformationThread(HANDLE, u32, void *, u32, u32 *);
static void put_s(char *b, u32 *n, const char *s) { while (*s && *n < 15900) b[(*n)++] = *s++; }
static void put_h(char *b, u32 *n, u32 v) { for (int i = 28; i >= 0; i -= 4) { u32 d = (v >> i) & 15; if (*n < 15900) b[(*n)++] = (char)(d < 10 ? 0x30 + d : 0x57 + d); } }
static void thread_dump(u32 pid, char *b, u32 *n) {
    HANDLE s = CreateToolhelp32Snapshot(4 /* TH32CS_SNAPTHREAD */, 0); if (s == (HANDLE)-1) return;
    u32 te[7]; te[0] = 28; put_s(b, n, "threads during freeze:\r\n");
    for (BOOL ok = Thread32First(s, te); ok; ok = Thread32Next(s, te)) { if (te[3] != pid) continue;
        HANDLE h = OpenThread(0x0002 | 0x0008 | 0x0040, 0, te[2]); if (!h) continue;
        DWORD prev = SuspendThread(h); static u32 c[0xB4]; for (u32 i = 0; i < 0xB4; i++) c[i] = 0; c[0] = 0x10001; BOOL g = GetThreadContext(h, c); ResumeThread(h); CloseHandle(h);
        put_s(b, n, "  tid "); put_h(b, n, te[2]); put_s(b, n, " suspended "); put_h(b, n, prev); put_s(b, n, " eip "); put_h(b, n, g ? c[0xB8 / 4] : 0); put_s(b, n, " esp "); put_h(b, n, g ? c[0xC4 / 4] : 0);
        { HANDLE h2 = OpenThread(0x0040, 0, te[2]); u32 sa = 0, rl; if (h2) { NtQueryInformationThread(h2, 9, &sa, 4, &rl); CloseHandle(h2); } put_s(b, n, " start "); put_h(b, n, sa); }
        put_s(b, n, "\r\n"); }
    CloseHandle(s);
    HANDLE fh = CreateFileA("freeze_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { u32 wr; WriteFile(fh, b, *n, &wr, 0); CloseHandle(fh); }
}
/* freeze probe (Milestone 86 diagnosis): samples the loader's heartbeats every ms; logs freezes of the canary */
static DWORD WINAPI freeze_probe(void *p) {
    static char fl[16000]; u32 n = 0, nf = 0, sampled = 0, nsamp = 0; long long qf, t0, t; QueryPerformanceFrequency(&qf);
    while (!g_r) Sleep(10);
    u32 c = g_r->pad[20], tk = g_r->pad[21], kc = g_r->pad[22]; QueryPerformanceCounter(&t0);
    for (;;) { Sleep(1); QueryPerformanceCounter(&t);
        if (g_r->pad[20] != c) { u32 ms = (u32)((t - t0) * 1000 / qf);
            if (ms > 200 && nf < 30) { u32 dt = g_r->pad[21] - tk, dk = g_r->pad[22] - kc; nf++;
                const char *s1 = "freeze ms "; while (*s1) fl[n++] = *s1++; char d[12]; int q = 0; u32 v = ms; do d[q++] = (char)(0x30 + v % 10); while (v /= 10); while (q) fl[n++] = d[--q];
                s1 = " ticker loops "; while (*s1) fl[n++] = *s1++; v = dt; do d[q++] = (char)(0x30 + v % 10); while (v /= 10); while (q) fl[n++] = d[--q];
                s1 = " kernel calls "; while (*s1) fl[n++] = *s1++; v = dk; do d[q++] = (char)(0x30 + v % 10); while (v /= 10); while (q) fl[n++] = d[--q];
                fl[n++] = 0x0D; fl[n++] = 0x0A;
                HANDLE fh = CreateFileA("freeze_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { u32 wr; WriteFile(fh, fl, n, &wr, 0); CloseHandle(fh); } }
            c = g_r->pad[20]; tk = g_r->pad[21]; kc = g_r->pad[22]; t0 = t; sampled = 0; }
        else if (!sampled && nsamp < 4 && (t - t0) * 1000 > qf * 400) { sampled = 1; nsamp++; thread_dump(g_r->pad[24], fl, &n); } }
}
/* the PC controller (Milestone 86): polled here, in a normal Windows program, and handed to the loader in the ring */
static DWORD WINAPI pad_thread(void *p) {
    if (GetFileAttributesA("noxinput.txt") != 0xFFFFFFFF) return 0;
    void *x = LoadLibraryA("xinput1_4.dll"); if (!x) x = LoadLibraryA("xinput9_1_0.dll"); if (!x) return 0;
    u32 (WINAPI *get)(u32, void *) = (u32 (WINAPI *)(u32, void *))GetProcAddress(x, "XInputGetState");
    u32 (WINAPI *set)(u32, void *) = (u32 (WINAPI *)(u32, void *))GetProcAddress(x, "XInputSetState");
    if (!get) return 0;
    u32 lastvib = 0;
    for (;;) { while (!g_r) Sleep(10);
        u32 s[4] = {0, 0, 0, 0}; int ok = get(0, s) == 0;   /* XINPUT_STATE: packet, then the 12-byte gamepad */
        if (ok) { g_r->pad[31] = s[1]; g_r->pad[32] = s[2]; g_r->pad[33] = s[3]; }
        g_r->pad[30] = ok;
        if (ok && set && g_r->pad[34] != lastvib) { lastvib = g_r->pad[34]; u16 v[2] = {(u16)lastvib, (u16)(lastvib >> 16)}; set(0, v); }
        Sleep(ok ? 4 : 2000); }
}
static void vsync_start(void) { CreateThread(0, 0, pad_thread, 0, 0, 0); if (GetFileAttributesA("canary.txt") != 0xFFFFFFFF) CreateThread(0, 0, freeze_probe, 0, 0, 0); if (GetFileAttributesA("vsync.txt") != 0xFFFFFFFF) CreateThread(0, 0, vsync_thread, 0, 0, 0); }   /* opt-in: you saw constant stutter with it */
static DWORD WINAPI audio_thread(void *p) {
    AUDIO *a = 0;
    SetThreadPriority(GetCurrentThread(), 15);   /* time critical: sound must not wait */
    while (!a) { HANDLE m = OpenFileMappingA(0x6, 0, "BruteForcePCLoader_Audio"); if (m) a = MapViewOfFile(m, 0x6, 0, 0, sizeof(AUDIO)); if (!a || a->magic != AUDIO_MAGIC) { a = 0; Sleep(1000); } }
    HANDLE ev = CreateEventA(0, 0, 0, 0); void *wo = 0;
    HANDLE mixev = CreateEventA(0, 0, 0, "BruteForcePCLoader_AudioEv");
    struct { u16 tag, ch; u32 rate, avg; u16 align, bits, cb; } f = {1, 2, 48000, 192000, 4, 16, 0};
    if (waveOutOpen(&wo, 0xFFFFFFFF, &f, (u32)ev, 0, 0x50000)) return 0;
    a->playing = 1;
    static short buf[4][960 * 2]; static WHDR h[4];
    for (u32 i = 0; i < 4; i++) { zero(&h[i], sizeof h[i]); h[i].data = (char *)buf[i]; h[i].len = 960 * 4; waveOutPrepareHeader(wo, &h[i], sizeof h[i]); h[i].flags |= 1; }
    for (;;) {
        for (u32 i = 0; i < 4; i++) if (h[i].flags & 1) {
            h[i].flags &= ~1u;
            {   /* refill log: ms since the last refill and the fill (frames), 300 entries after 20 s, to audio_log.txt */
                static char al[300 * 12]; static u32 na, done, cnt; static long long lt, qf; long long t; if (!qf) QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&t);
                if (!done && ++cnt > 1000) { u32 ms = lt ? (u32)((t - lt) * 1000 / qf) : 0, fl = a->wpos - a->rpos; if (ms > 999) ms = 999; if (fl > 99999) fl = 99999;
                    al[na++] = (char)(0x30 + ms / 100); al[na++] = (char)(0x30 + ms / 10 % 10); al[na++] = (char)(0x30 + ms % 10); al[na++] = 0x2F;
                    for (int q = 10000; q; q /= 10) al[na++] = (char)(0x30 + fl / q % 10); al[na++] = (char)(0x30 + i); al[na++] = 0x20;
                    if (na >= 300 * 11) { HANDLE fh = CreateFileA("audio_log.txt", 0x40000000, 0, 0, 2, 0x80, 0); if (fh != (HANDLE)-1) { u32 wr; WriteFile(fh, al, na, &wr, 0); CloseHandle(fh); } done = 1; } }
                lt = t; }
            if (g_r) { u32 fill = a->wpos - a->rpos; g_r->pad[11]++; if (fill < g_r->pad[12] || !g_r->pad[13]) { g_r->pad[12] = fill; g_r->pad[13] = 1; } }   /* buffers refilled, lowest fill seen */
            for (u32 k = 0; k < 960; k++) {
                if (a->wpos != a->rpos) { u32 r = a->rpos % AUDIO_FRAMES; buf[i][k * 2] = a->pcm[r * 2]; buf[i][k * 2 + 1] = a->pcm[r * 2 + 1]; a->rpos++; }
                else { buf[i][k * 2] = 0; buf[i][k * 2 + 1] = 0; if (g_r) g_r->pad[10]++; }   /* underrun (frames of silence) */
            }
            waveOutWrite(wo, &h[i], sizeof h[i]);
            if (mixev) SetEvent(mixev);   /* wake the loader's mixer to refill the ring */
        }
        WaitForSingleObject(ev, 50);
    }
}
void audio_start(void) { CreateThread(0, 0, audio_thread, 0, 0, 0); vsync_start(); }
/* viewer time accounting: pad[2] busy ms, pad[3] idle loops */
void gl_account(u32 busy, int any) { if (g_r) { if (any) g_r->pad[2] += busy; else g_r->pad[3]++; } }

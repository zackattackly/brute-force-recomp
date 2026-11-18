/* NV2A (Xbox GPU) stand-in, part 2 (Milestone 77): a software renderer for the 3D object (Kelvin).
 * It draws into the colour and depth surfaces the game set up in Xbox memory, so the window shows the result.
 * Covered: vertex input (immediate SET_VERTEX_DATA*, inline arrays, vertex arrays with DRAW_ARRAYS / ARRAY_ELEMENT),
 * vertex programs (an interpreter for the NV2A microcode, the same field layout as tools\vshdis.py), triangles,
 * strips, fans, quads and polygons, depth test, textures (swizzled, linear, DXT1/3/5, palette), the register
 * combiners and final combiner, alpha test, blending and the colour mask.
 * Not yet: fixed-function lighting, fog, stencil, points and lines, mipmaps, anti-aliased surfaces. */
#include "util.h"
#include "bfgl.h"

#define PHYS(a) ((u8 *)(0x80000000u + ((a) & 0x03FFFFFF)))
#define K(m) kel[(m) / 4]
extern u32 kel[0x800];
typedef float f32; typedef short s16;
typedef struct { f32 x, y, z, w; } V4;

static f32 fl(u32 u) { union { u32 u; f32 f; } x; x.u = u; return x.f; }
static f32 cl01(f32 x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static f32 clpm(f32 x) { return x < -1 ? -1 : x > 1 ? 1 : x; }
static f32 fabs_(f32 x) { return x < 0 ? -x : x; }
static f32 floor_(f32 x) { if (x > 2e9f || x < -2e9f) return x; s32 i = (s32)x; if ((f32)i > x) i--; return (f32)i; }
static f32 sqrt_(f32 x) { f32 r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static f32 log2_(f32 x) { f32 r; __asm__("fld1\n\tfxch\n\tfyl2x" : "=t"(r) : "0"(x)); return r; }
static f32 exp2_(f32 x) {
    if (x > 126) x = 126; if (x < -126) return 0;
    f32 e = floor_(x), f = x - e, r; __asm__("f2xm1" : "=t"(r) : "0"(f)); r += 1;
    union { u32 u; f32 f; } s; s.u = (u32)((s32)e + 127) << 23; return r * s.f;
}

/* ---------------- vertex programs ---------------- */
static u32 g_prog[136][4];
static u32 g_pload, g_cload, g_pstart;
static V4 g_c[192];
float *render_consts(void) { return (float *)&g_c[0]; }   /* the vertex shader constants as the push buffer set them (Milestone 87 probe) */
static V4 g_attr[16];                       /* current attribute values (immediate mode) */
#define B(w, i, s, l) (((w)[i] >> (s)) & ((1u << (l)) - 1))

static V4 swz_neg(V4 v, u32 sx, u32 sy, u32 sz, u32 sw, u32 neg) {
    f32 c[4] = {v.x, v.y, v.z, v.w}; V4 r = {c[sx], c[sy], c[sz], c[sw]};
    if (neg) { r.x = -r.x; r.y = -r.y; r.z = -r.z; r.w = -r.w; }
    return r;
}
static V4 vsrc(const u32 *w, int p, V4 *r, V4 *in, V4 *o, int a0) {
    u32 mux, reg, sx, sy, sz, sw, neg;
    if (p == 0) { mux = B(w, 2, 26, 2); reg = B(w, 2, 28, 4); neg = B(w, 1, 8, 1); sx = B(w, 1, 6, 2); sy = B(w, 1, 4, 2); sz = B(w, 1, 2, 2); sw = B(w, 1, 0, 2); }
    else if (p == 1) { mux = B(w, 2, 11, 2); reg = B(w, 2, 13, 4); neg = B(w, 2, 25, 1); sx = B(w, 2, 23, 2); sy = B(w, 2, 21, 2); sz = B(w, 2, 19, 2); sw = B(w, 2, 17, 2); }
    else { mux = B(w, 3, 28, 2); reg = (B(w, 2, 0, 2) << 2) | B(w, 3, 30, 2); neg = B(w, 2, 10, 1); sx = B(w, 2, 8, 2); sy = B(w, 2, 6, 2); sz = B(w, 2, 4, 2); sw = B(w, 2, 2, 2); }
    V4 v = {0, 0, 0, 0};
    if (mux == 1) v = reg == 12 ? o[0] : r[reg & 15];
    else if (mux == 2) v = in[B(w, 1, 9, 4)];
    else if (mux == 3) { s32 ci = (s32)B(w, 1, 13, 8) + (B(w, 3, 1, 1) ? a0 : 0); if (ci < 0) ci = 0; if (ci > 191) ci = 191; v = g_c[ci]; }
    return swz_neg(v, sx, sy, sz, sw, neg);
}
static void wmask(V4 *d, V4 s, u32 m) { if (m & 8) d->x = s.x; if (m & 4) d->y = s.y; if (m & 2) d->z = s.z; if (m & 1) d->w = s.w; }
static V4 rep(f32 x) { V4 r = {x, x, x, x}; return r; }
static f32 rcp_(f32 x) { return x == 0 ? 1.0e30f : 1 / x; }

/* runs the program for one vertex: in[16] -> o[16] (0 oPos, 3 oD0, 4 oD1, 5 oFog, 6 oPts, 7/8 back colours, 9..12 oT0..3).
   The program is decoded once into VINS (Milestone 78: the field-by-field interpreter cost ~5000 cycles a vertex). */
typedef struct { u8 mux, reg, neg, ident; u8 sw[4]; } VSRC;
typedef struct { u8 mac, ilu, need, a0x, vin, macmask, outr, ilumask, ilur, omask, orb, omux, final; u8 cidx; u16 oaddr; VSRC s[3]; } VINS;
static VINS g_dec[136]; static u32 g_dec_n, g_dec_dirty = 1, g_vsh_wc;   /* g_vsh_wc: the program writes constants */
static u32 g_vsh_bad;
static void vsh_decode(void) {
    g_dec_n = 0; g_vsh_wc = 0;
    for (u32 pc = g_pstart; pc < 136; pc++) {
        const u32 *w = g_prog[pc]; VINS *d = &g_dec[g_dec_n++];
        d->mac = B(w, 1, 21, 4); d->ilu = B(w, 1, 25, 3); d->cidx = B(w, 1, 13, 8); d->vin = B(w, 1, 9, 4); d->a0x = B(w, 3, 1, 1);
        d->macmask = B(w, 3, 24, 4); d->outr = B(w, 3, 20, 4) & 15; d->ilumask = B(w, 3, 16, 4); d->ilur = d->mac ? 1 : d->outr;
        d->omask = B(w, 3, 12, 4); d->orb = B(w, 3, 11, 1); d->oaddr = B(w, 3, 3, 8); d->omux = B(w, 3, 2, 1); d->final = B(w, 3, 0, 1);
        static const u8 macneed[16] = {0, 1, 3, 5, 7, 3, 3, 3, 3, 3, 3, 3, 3, 1, 0, 0};
        d->need = macneed[d->mac] | (d->ilu ? 4 : 0);
        for (int p = 0; p < 3; p++) {
            VSRC *s = &d->s[p]; u32 sx, sy, sz, sw;
            if (p == 0) { s->mux = B(w, 2, 26, 2); s->reg = B(w, 2, 28, 4); s->neg = B(w, 1, 8, 1); sx = B(w, 1, 6, 2); sy = B(w, 1, 4, 2); sz = B(w, 1, 2, 2); sw = B(w, 1, 0, 2); }
            else if (p == 1) { s->mux = B(w, 2, 11, 2); s->reg = B(w, 2, 13, 4); s->neg = B(w, 2, 25, 1); sx = B(w, 2, 23, 2); sy = B(w, 2, 21, 2); sz = B(w, 2, 19, 2); sw = B(w, 2, 17, 2); }
            else { s->mux = B(w, 3, 28, 2); s->reg = (B(w, 2, 0, 2) << 2) | B(w, 3, 30, 2); s->neg = B(w, 2, 10, 1); sx = B(w, 2, 8, 2); sy = B(w, 2, 6, 2); sz = B(w, 2, 4, 2); sw = B(w, 2, 2, 2); }
            s->sw[0] = sx; s->sw[1] = sy; s->sw[2] = sz; s->sw[3] = sw; s->ident = sx == 0 && sy == 1 && sz == 2 && sw == 3;
        }
        if (d->omask && !d->orb) g_vsh_wc = 1;
        if (d->final) break;
    }
    g_dec_dirty = 0;
}
static inline V4 fsrc(const VINS *d, const VSRC *s, const V4 *r, const V4 *in, const V4 *o, int a0) {
    const V4 *p;
    if (s->mux == 1) p = s->reg == 12 ? &o[0] : &r[s->reg & 15];
    else if (s->mux == 2) p = &in[d->vin];
    else { s32 ci = (s32)d->cidx + (d->a0x ? a0 : 0); if (ci < 0) ci = 0; if (ci > 191) ci = 191; p = &g_c[ci]; }
    V4 v;
    if (s->ident) v = *p; else { const f32 *c = &p->x; v.x = c[s->sw[0]]; v.y = c[s->sw[1]]; v.z = c[s->sw[2]]; v.w = c[s->sw[3]]; }
    if (s->neg) { v.x = -v.x; v.y = -v.y; v.z = -v.z; v.w = -v.w; }
    return v;
}
static inline void wm(V4 *d, V4 s, u32 m) { if (m == 15) { *d = s; return; } if (m & 8) d->x = s.x; if (m & 4) d->y = s.y; if (m & 2) d->z = s.z; if (m & 1) d->w = s.w; }
static f32 sqrt_sse(f32 x) { f32 r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
static void run_vsh(V4 *in, V4 *o) {
    V4 r[16]; int a0 = 0; V4 z1 = {0, 0, 0, 1}, z0 = {0, 0, 0, 0};
    for (u32 i = 0; i < 16; i++) { o[i] = z1; r[i] = z0; }
    for (u32 n = 0; n < g_dec_n; n++) {
        const VINS *d = &g_dec[n];
        V4 A = z0, Bv = z0, C = z0, m = z0, l = z0;
        if (d->need & 1) A = fsrc(d, &d->s[0], r, in, o, a0);
        if (d->need & 2) Bv = fsrc(d, &d->s[1], r, in, o, a0);
        if (d->need & 4) C = fsrc(d, &d->s[2], r, in, o, a0);
        switch (d->mac) {
        case 1: m = A; break;
        case 2: m.x = A.x * Bv.x; m.y = A.y * Bv.y; m.z = A.z * Bv.z; m.w = A.w * Bv.w; break;
        case 3: m.x = A.x + C.x; m.y = A.y + C.y; m.z = A.z + C.z; m.w = A.w + C.w; break;
        case 4: m.x = A.x * Bv.x + C.x; m.y = A.y * Bv.y + C.y; m.z = A.z * Bv.z + C.z; m.w = A.w * Bv.w + C.w; break;
        case 5: m = rep(A.x * Bv.x + A.y * Bv.y + A.z * Bv.z); break;
        case 6: m = rep(A.x * Bv.x + A.y * Bv.y + A.z * Bv.z + Bv.w); break;
        case 7: m = rep(A.x * Bv.x + A.y * Bv.y + A.z * Bv.z + A.w * Bv.w); break;
        case 8: m.x = 1; m.y = A.y * Bv.y; m.z = A.z; m.w = Bv.w; break;
        case 9: m.x = A.x < Bv.x ? A.x : Bv.x; m.y = A.y < Bv.y ? A.y : Bv.y; m.z = A.z < Bv.z ? A.z : Bv.z; m.w = A.w < Bv.w ? A.w : Bv.w; break;
        case 10: m.x = A.x > Bv.x ? A.x : Bv.x; m.y = A.y > Bv.y ? A.y : Bv.y; m.z = A.z > Bv.z ? A.z : Bv.z; m.w = A.w > Bv.w ? A.w : Bv.w; break;
        case 11: m.x = A.x < Bv.x; m.y = A.y < Bv.y; m.z = A.z < Bv.z; m.w = A.w < Bv.w; break;
        case 12: m.x = A.x >= Bv.x; m.y = A.y >= Bv.y; m.z = A.z >= Bv.z; m.w = A.w >= Bv.w; break;
        }
        if (d->ilu) {
            f32 s = C.x;
            switch (d->ilu) {
            case 1: l = C; break;
            case 2: l = rep(rcp_(s)); break;
            case 3: { f32 v = rcp_(s); f32 a = fabs_(v); if (a < 5.42101e-20f) a = 5.42101e-20f; if (a > 1.884467e19f) a = 1.884467e19f; l = rep(v < 0 ? -a : a); break; }
            case 4: l = rep(rcp_(sqrt_sse(fabs_(s)))); break;
            case 5: { f32 e = floor_(s); l.x = exp2_(e); l.y = s - e; l.z = exp2_(s); l.w = 1; break; }
            case 6: { f32 a = fabs_(s); if (a == 0) { l.x = l.z = -1e30f; l.y = 1; } else { f32 lg = log2_(a), e = floor_(lg); l.x = e; l.y = a / exp2_(e); l.z = lg; } l.w = 1; break; }
            case 7: { f32 dd = C.x, sp = C.y, pw = C.w; if (pw < -127.9961f) pw = -127.9961f; if (pw > 127.9961f) pw = 127.9961f;
                l.x = 1; l.y = dd > 0 ? dd : 0; l.z = (dd > 0 && sp > 0) ? exp2_(pw * log2_(sp)) : 0; l.w = 1; break; }
            }
        }
        if (d->mac == 13) a0 = (s32)floor_(A.x);
        else if (d->mac && d->macmask) wm(&r[d->outr], m, d->macmask);
        if (d->ilu && d->ilumask) wm(&r[d->ilur], l, d->ilumask);
        if (d->omask) { V4 v = d->omux ? l : m; if (d->orb) wm(&o[d->oaddr & 15], v, d->omask); else if (d->oaddr < 192) wm(&g_c[d->oaddr], v, d->omask); }
        if (d->final) return;
    }
    if (g_vsh_bad++ < 3) out("  [render] vertex program ran off the end\r\n");
}
/* fixed-function transform (no lighting yet): composite matrix, then the viewport */
static void run_ff(V4 *in, V4 *o) {
    for (u32 i = 0; i < 16; i++) { V4 z = {0, 0, 0, 1}; o[i] = z; }
    const u32 *M = &K(0x680); V4 p = in[0], q;
    q.x = p.x * fl(M[0]) + p.y * fl(M[4]) + p.z * fl(M[8]) + p.w * fl(M[12]);
    q.y = p.x * fl(M[1]) + p.y * fl(M[5]) + p.z * fl(M[9]) + p.w * fl(M[13]);
    q.z = p.x * fl(M[2]) + p.y * fl(M[6]) + p.z * fl(M[10]) + p.w * fl(M[14]);
    q.w = p.x * fl(M[3]) + p.y * fl(M[7]) + p.z * fl(M[11]) + p.w * fl(M[15]);
    f32 iw = rcp_(q.w); o[0].x = q.x * iw; o[0].y = q.y * iw; o[0].z = q.z * iw; o[0].w = q.w;
    o[3] = in[3]; o[4] = in[4];
    for (u32 t = 0; t < 4; t++) o[9 + t] = in[9 + t];
}

/* ---------------- vertices ---------------- */
#define NVAR 25   /* D0 rgba, D1 rgba, fog, T0..T3 strq */
typedef struct { f32 x, y, z, w; f32 v[NVAR]; } OV;
#define MAXV 16384
static OV g_vb[MAXV];
static u32 g_nv, g_prim, g_in_begin;
u32 g_r_tris, g_r_pix, g_r_ff, g_r_prims[16];

static void draw_tri(OV *a, OV *b, OV *c);
static void assemble(int final);
u64 g_vs_cyc; u32 g_r_verts;   /* time in vertex programs (TSC cycles) and vertex count */
static void transform(V4 *in, OV *v) {
    V4 o[16];
    if ((K(0x1E94) & 3) == 2) run_vsh(in, o); else { run_ff(in, o); g_r_ff++; }
    v->x = o[0].x; v->y = o[0].y; v->z = o[0].z; v->w = o[0].w;
    v->v[0] = cl01(o[3].x); v->v[1] = cl01(o[3].y); v->v[2] = cl01(o[3].z); v->v[3] = cl01(o[3].w);
    v->v[4] = cl01(o[4].x); v->v[5] = cl01(o[4].y); v->v[6] = cl01(o[4].z); v->v[7] = cl01(o[4].w);
    v->v[8] = o[5].x;
    for (u32 t = 0; t < 4; t++) { v->v[9 + t * 4] = o[9 + t].x; v->v[10 + t * 4] = o[9 + t].y; v->v[11 + t * 4] = o[9 + t].z; v->v[12 + t * 4] = o[9 + t].w; }
}
static void flush_pending(void);
static int g_gvsh; static f32 g_gimm[]; static u32 g_ngm;
static void emit_vertex(V4 *in) {
    if (g_gvsh) { if (g_ngm < 16384) { memcpy_(&g_gimm[g_ngm * 64], in, 256); g_ngm++; } return; }
    flush_pending(); if (g_dec_dirty) vsh_decode();
    u64 t0 = __builtin_ia32_rdtsc(); transform(in, &g_vb[g_nv]); g_vs_cyc += __builtin_ia32_rdtsc() - t0; g_r_verts++;
    if (++g_nv == MAXV) assemble(0);
}

/* reads attribute i of vertex index idx from its vertex array (format at 0x1760, offset at 0x1720) */
static int fetch_attr(u32 i, u32 idx, V4 *r) {
    u32 f = K(0x1760 + i * 4), type = f & 0xF, size = (f >> 4) & 0xF, stride = (f >> 8) & 0xFF;
    if (!size) return 0;
    u8 *p = PHYS(K(0x1720 + i * 4) + idx * stride);
    f32 c[4] = {0, 0, 0, 1};
    switch (type) {
    case 0: if (size >= 4) { c[0] = p[2] / 255.0f; c[1] = p[1] / 255.0f; c[2] = p[0] / 255.0f; c[3] = p[3] / 255.0f; } else for (u32 k = 0; k < size; k++) c[k] = p[k] / 255.0f; break;   /* UB_D3D (BGRA) */
    case 4: for (u32 k = 0; k < size && k < 4; k++) c[k] = p[k] / 255.0f; break;                  /* UB_OGL */
    case 1: for (u32 k = 0; k < size && k < 4; k++) c[k] = ((s16 *)p)[k] / 32767.0f; break;        /* S1 */
    case 5: for (u32 k = 0; k < size && k < 4; k++) c[k] = ((s16 *)p)[k]; break;                   /* S32K */
    case 2: for (u32 k = 0; k < size && k < 4; k++) c[k] = ((f32 *)p)[k]; break;                   /* F */
    case 6: { u32 v = *(u32 *)p; c[0] = ((s32)(v << 21) >> 21) / 1023.0f; c[1] = ((s32)(v << 10) >> 21) / 1023.0f; c[2] = ((s32)v >> 22) / 511.0f; break; }   /* CMP */
    default: return 0;
    }
    r->x = c[0]; r->y = c[1]; r->z = c[2]; r->w = c[3];
    return 1;
}
/* Indexed and array draws (Milestone 78): the indices are queued; at the end of the draw (or when the buffer is
   full) each distinct index is transformed once, on the worker threads when there are many, and the results are
   copied into the vertex buffer in draw order. */
static u32 g_pidx[MAXV], g_npidx, g_uidx[MAXV], g_pslot[MAXV], g_nu;
static OV g_uout[MAXV];
#define HSZ 32768
static u32 g_hkey[HSZ], g_hval[HSZ], g_hgen[HSZ], g_hg;
static void vs_part(u32 k, u32 step) {
    for (u32 u = k; u < g_nu; u += step) { V4 in[16]; u32 idx = g_uidx[u];
        for (u32 i = 0; i < 16; i++) if (!fetch_attr(i, idx, &in[i])) in[i] = g_attr[i];
        transform(in, &g_uout[u]); }
}
static void par_run(u32 kind);
u32 g_vb_par, g_vb_ser; u64 g_vb_parc, g_vb_serc, g_vb_copyc, g_vb_hashc;
static void flush_pending(void) {
    if (!g_npidx) return;
    if (g_dec_dirty) vsh_decode();
    u64 t0 = __builtin_ia32_rdtsc();
    g_hg++; g_nu = 0;
    for (u32 i = 0; i < g_npidx; i++) {
        u32 idx = g_pidx[i], h = (idx * 2654435761u) >> 17;
        while (g_hgen[h] == g_hg && g_hkey[h] != idx) h = (h + 1) & (HSZ - 1);
        if (g_hgen[h] != g_hg) { g_hgen[h] = g_hg; g_hkey[h] = idx; g_hval[h] = g_nu; g_uidx[g_nu++] = idx; }
        g_pslot[i] = g_hval[h];
    }
    u64 t1 = __builtin_ia32_rdtsc();
    if (g_nu >= 48 && !g_vsh_wc && (K(0x1E94) & 3) == 2) { par_run(1); g_vb_par++; g_vb_parc += __builtin_ia32_rdtsc() - t1; } else { vs_part(0, 1); g_vb_ser++; g_vb_serc += __builtin_ia32_rdtsc() - t1; }
    u64 t2 = __builtin_ia32_rdtsc();
    for (u32 i = 0; i < g_npidx; i++) g_vb[g_nv++] = g_uout[g_pslot[i]];
    g_vb_copyc += __builtin_ia32_rdtsc() - t2; g_vb_hashc += t1 - t0;
    g_r_verts += g_nu; g_npidx = 0;
    g_vs_cyc += __builtin_ia32_rdtsc() - t0;
    if (g_nv >= MAXV) assemble(0);
}
static int g_gvsh; static u32 g_gidx[], g_ngi;
static void emit_index(u32 idx) {
    if (g_gvsh) { if (g_ngi < (1u << 20)) g_gidx[g_ngi++] = idx; return; }
    if (g_nv + g_npidx >= MAXV) flush_pending();
    g_pidx[g_npidx++] = idx;
}

/* inline arrays: the words of each vertex in attribute order */
static u32 g_ia[64], g_ian;
static u32 attr_words(u32 f) { u32 type = f & 0xF, size = (f >> 4) & 0xF; if (!size) return 0; u32 b = type == 2 ? size * 4 : (type == 1 || type == 5) ? size * 2 : type == 6 ? 4 : size; return (b + 3) / 4; }
static void inline_word(u32 v) {
    u32 need = 0; for (u32 i = 0; i < 16; i++) need += attr_words(K(0x1760 + i * 4));
    if (!need || need > 64) return;
    g_ia[g_ian++] = v;
    if (g_ian < need) return;
    g_ian = 0;
    V4 in[16]; u32 at = 0;
    for (u32 i = 0; i < 16; i++) {
        u32 f = K(0x1760 + i * 4), nw = attr_words(f);
        if (!nw) { in[i] = g_attr[i]; continue; }
        u32 save_off = K(0x1720 + i * 4), save_f = f;
        /* reuse the array reader on the inline words (stride 0, data at g_ia + at) */
        u32 type = f & 0xF, size = (f >> 4) & 0xF; u8 *p = (u8 *)&g_ia[at]; f32 c[4] = {0, 0, 0, 1};
        if (type == 2) for (u32 k = 0; k < size && k < 4; k++) c[k] = ((f32 *)p)[k];
        else if (type == 0 && size >= 4) { c[0] = p[2] / 255.0f; c[1] = p[1] / 255.0f; c[2] = p[0] / 255.0f; c[3] = p[3] / 255.0f; }
        else if (type == 0 || type == 4) for (u32 k = 0; k < size && k < 4; k++) c[k] = p[k] / 255.0f;
        else if (type == 1) for (u32 k = 0; k < size && k < 4; k++) c[k] = ((s16 *)p)[k] / 32767.0f;
        else if (type == 5) for (u32 k = 0; k < size && k < 4; k++) c[k] = ((s16 *)p)[k];
        else if (type == 6) { u32 w = *(u32 *)p; c[0] = ((s32)(w << 21) >> 21) / 1023.0f; c[1] = ((s32)(w << 10) >> 21) / 1023.0f; c[2] = ((s32)w >> 22) / 511.0f; }
        (void)save_off; (void)save_f;
        in[i].x = c[0]; in[i].y = c[1]; in[i].z = c[2]; in[i].w = c[3];
        at += nw;
    }
    emit_vertex(in);
}

/* ---- the GPU path (Milestone 79): draws go to the viewer through the shared ring (bfgl.h) ---- */
RING *g_ring; static u32 *g_rbuf; int g_gl; static u32 g_last_kel[0x800], g_kel_sent;
u32 g_gl_draws, g_gl_words;
static void gl_dead(const char *why) { if (g_gl) { out("  [gl] the viewer stopped reading the ring ("); out(why); out(") - drawing in software from now on\r\n"); } g_gl = 0; }
/* n contiguous words in the ring (waits for the viewer; a CMD_WRAP is written when the end is reached) */
u64 g_ring_wait;   /* cycles spent waiting for ring space */
static u32 *ring_alloc(u32 n) {
    u32 w = g_ring->wpos, t0 = GetTickCount(); u64 c0 = __builtin_ia32_rdtsc();
    for (;;) {
        u32 r = g_ring->rpos;
        if (r <= w) {
            if (w + n < RING_WORDS) { g_ring_wait += __builtin_ia32_rdtsc() - c0; return &g_rbuf[w]; }
            if (r > n) { g_rbuf[w] = CMD_WRAP << 24 | 1; __sync_synchronize(); g_ring->wpos = 0; w = 0; continue; }
        } else if (w + n < r) { g_ring_wait += __builtin_ia32_rdtsc() - c0; return &g_rbuf[w]; }
        if (GetTickCount() - t0 > 10000) { gl_dead("ring full for 10 s"); return 0; }
        Sleep(0);
    }
}
static void ring_commit(u32 *p, u32 n) { __sync_synchronize(); g_ring->wpos = (u32)(p - g_rbuf) + n; g_gl_words += n; }
IMP DWORD WINAPI GetFileAttributesA(const char *);
extern u32 g_kdirty[64], g_kdlist[0x800], g_kdn;
static void gl_state(int force) {
    /* Performance: the whole 8 KB register block went to the viewer before ~46,000 draws a second (~370 MB/s copied on both
       sides); now only the registers that changed (gpu.c keeps the list) go, as (index, value) pairs. fullstate.txt = old way. */
    static int full = -1; if (full < 0) full = GetFileAttributesA("fullstate.txt") != 0xFFFFFFFF;
    if (force || !g_kel_sent || full || g_kdn > 0x300) {
        if (!force && g_kel_sent && full && !__builtin_memcmp(kel, g_last_kel, sizeof g_last_kel)) goto clear;
        u32 *p = ring_alloc(1 + 0x800); if (!p) return;
        p[0] = CMD_STATE << 24 | (1 + 0x800); memcpy_(p + 1, kel, 0x2000); ring_commit(p, 1 + 0x800);
        memcpy_(g_last_kel, kel, sizeof g_last_kel); g_kel_sent = 1; goto clear; }
    {   if (!g_kdn) return;
        u32 cnt = 0; for (u32 k = 0; k < g_kdn; k++) { u32 i = g_kdlist[k]; if (kel[i] != g_last_kel[i]) cnt++; }
        if (cnt) { u32 *p = ring_alloc(1 + cnt * 2); if (!p) return; u32 q = 1;
            for (u32 k = 0; k < g_kdn; k++) { u32 i = g_kdlist[k]; if (kel[i] != g_last_kel[i]) { p[q++] = i; p[q++] = kel[i]; g_last_kel[i] = kel[i]; } }
            p[0] = CMD_STATE_DELTA << 24 | (1 + cnt * 2); ring_commit(p, 1 + cnt * 2); } }
clear:
    for (u32 k = 0; k < g_kdn; k++) { u32 i = g_kdlist[k]; g_kdirty[i >> 5] &= ~(1u << (i & 31)); } g_kdn = 0;
}
static void gl_draw(u32 mode, OV *v, u32 n) {
    if (!n) return;
    {   static u8 seen[256]; u32 sp = K(0x1E70);   /* log each texture format the first time it is drawn with (GPU path) */
        for (u32 i = 0; i < 4; i++) if (((sp >> (i * 5)) & 0x1F) && ((K(0x1B0C + i * 0x40) >> 30) & 1)) { u32 cf = (K(0x1B04 + i * 0x40) >> 8) & 0xFF;
            if (!seen[cf]) { seen[cf] = 1; out("  [gl] texture format "); hex(cf); out(" first used, stage "); dec(i); out(", size "); hex(K(0x1B1C + i * 0x40)); out(", shader mode "); dec((sp >> (i * 5)) & 0x1F); out("\r\n"); } } }
    gl_state(0); if (!g_gl) return;
    while (n) {
        u32 m = n > 8192 ? 8192 : n;
        if (m < n && (mode == 5)) m -= m % 3;
        u32 *p = ring_alloc(3 + m * VTX_WORDS); if (!p) return;
        p[0] = CMD_DRAW << 24 | (3 + m * VTX_WORDS); p[1] = mode; p[2] = m; memcpy_(p + 3, v, m * VTX_WORDS * 4); ring_commit(p, 3 + m * VTX_WORDS);
        v += m; n -= m; g_gl_draws++;
    }
}
static void gl_cmd1(u32 type, u32 a) { u32 *p = ring_alloc(2); if (!p) return; p[0] = type << 24 | 2; p[1] = a; ring_commit(p, 2); }
/* ---- vertex programs on the GPU (Milestone 81): the viewer runs them; here only indices or raw attributes are sent ---- */
static int g_gvsh;                       /* this draw (Begin..End) goes through the GPU vertex path */
static int g_cpuvertex;                  /* loader\cpuvertex.txt: run vertex programs on the CPU (checked once) */
static u32 g_prog_dirty = 1, g_cmin = 0, g_cmax = 191;   /* program changed; constants changed (range) */
#define MAXGI (1u << 20)
static u32 g_gidx[MAXGI], g_ngi; static f32 g_gimm[16384 * 64]; static u32 g_ngm;
static void gl_send_prog(void) {
    if (g_prog_dirty) { u32 *p = ring_alloc(2 + 136 * 4); if (!p) return; p[0] = CMD_VSH << 24 | (2 + 136 * 4); p[1] = g_pstart; memcpy_(p + 2, g_prog, 136 * 16); ring_commit(p, 2 + 136 * 4); g_prog_dirty = 0; }
    if (g_cmin <= g_cmax) { u32 n = g_cmax - g_cmin + 1; u32 *p = ring_alloc(3 + n * 4); if (!p) return; p[0] = CMD_CONST << 24 | (3 + n * 4); p[1] = g_cmin; p[2] = n; memcpy_(p + 3, &g_c[g_cmin], n * 16); ring_commit(p, 3 + n * 4); g_cmin = 192; g_cmax = 0; }
}
static void gl_flush_idx(void) {
    if (!g_ngi) return;
    gl_state(0); gl_send_prog(); if (!g_gl) return;
    u32 mn = 0xFFFFFFFF, mx = 0; for (u32 i = 0; i < g_ngi; i++) { u32 v = g_gidx[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
    u32 *p = ring_alloc(5 + 64 + g_ngi); if (!p) return;
    p[0] = CMD_DRAW_IDX << 24 | (5 + 64 + g_ngi); p[1] = g_prim; p[2] = g_ngi; p[3] = mn; p[4] = mx; memcpy_(p + 5, g_attr, 256); memcpy_(p + 5 + 64, g_gidx, g_ngi * 4);
    ring_commit(p, 5 + 64 + g_ngi); g_gl_draws++; g_r_verts += mx - mn + 1;
    /* keep what a strip or fan needs to continue (only when the buffer filled up in the middle of a draw) */
    g_ngi = 0;
}
static void gl_flush_imm(void) {
    if (!g_ngm) return;
    gl_state(0); gl_send_prog(); if (!g_gl) return;
    u32 *p = ring_alloc(3 + g_ngm * 64); if (!p) return;
    p[0] = CMD_DRAW_IMM << 24 | (3 + g_ngm * 64); p[1] = g_prim; p[2] = g_ngm; memcpy_(p + 3, g_gimm, g_ngm * 256); ring_commit(p, 3 + g_ngm * 64); g_gl_draws++;
    g_ngm = 0;
}
void render_gl_fence(u32 v) { if (g_gl) gl_cmd1(CMD_FENCE, v); }
void render_gl_flip(u32 a) { if (g_gl) gl_cmd1(CMD_FLIP, a); }
/* depth-pass pixel counts (Milestone 87): counted by the viewer with an occlusion query, in order with the draws */
void render_gl_zpass(u32 op, u32 arg) { if (!g_gl) return; u32 *p = ring_alloc(3); if (!p) return; p[0] = CMD_ZPASS << 24 | 3; p[1] = op; p[2] = arg; ring_commit(p, 3); }
/* called once the viewer has answered: the GPU path is used unless loader\software.txt exists */
IMP DWORD WINAPI GetFileAttributesA(const char *);
void render_gl_init(RING *r) {
    g_ring = r; g_rbuf = (u32 *)(r + 1);
    if (!r || !r->gl_ok) { out("  [gl] the viewer has no GPU renderer: drawing in software\r\n"); return; }
    if (GetFileAttributesA("software.txt") != 0xFFFFFFFF) { out("  [gl] software.txt found: drawing in software\r\n"); return; }
    g_gl = 1; out("  [gl] drawing with the GPU: "); out(r->info); out("\r\n");
    g_cpuvertex = GetFileAttributesA("cpuvertex.txt") != 0xFFFFFFFF; if (g_cpuvertex) out("  [gl] cpuvertex.txt: vertex programs run on the CPU\r\n");
}

/* turns the buffered vertices into triangles; final = at END (else the buffer is full: keep what strips/fans need) */
static void assemble(int final) {
    u32 n = g_nv, p = g_prim, keep = 0;
    if (p >= 1 && p < 16) g_r_prims[p]++;
    if (g_gl) { gl_draw(p, g_vb, n); p = p == 5 || p == 8 || p == 6 || p == 9 || p == 7 || p == 10 ? p : 0; if (!g_gl) goto sw; }
    if (g_gl) switch (p) { case 5: keep = n % 3; break; case 8: keep = n % 4; break; case 6: case 9: keep = 2; break; case 7: case 10: keep = 1; break; }
    else sw: switch (p) {
    case 5: for (u32 i = 0; i + 2 < n; i += 3) draw_tri(&g_vb[i], &g_vb[i + 1], &g_vb[i + 2]); keep = n % 3; break;
    case 6: for (u32 i = 0; i + 2 < n; i++) { if (i & 1) draw_tri(&g_vb[i + 1], &g_vb[i], &g_vb[i + 2]); else draw_tri(&g_vb[i], &g_vb[i + 1], &g_vb[i + 2]); } keep = 2; break;
    case 7: case 10: for (u32 i = 1; i + 1 < n; i++) draw_tri(&g_vb[0], &g_vb[i], &g_vb[i + 1]); keep = 1; break;
    case 8: for (u32 i = 0; i + 3 < n; i += 4) { draw_tri(&g_vb[i], &g_vb[i + 1], &g_vb[i + 2]); draw_tri(&g_vb[i], &g_vb[i + 2], &g_vb[i + 3]); } keep = n % 4; break;
    case 9: for (u32 i = 0; i + 3 < n; i += 2) { draw_tri(&g_vb[i], &g_vb[i + 1], &g_vb[i + 3]); draw_tri(&g_vb[i], &g_vb[i + 3], &g_vb[i + 2]); } keep = 2; break;
    default: keep = 0; break;   /* points and lines: not drawn yet */
    }
    if (final || n < keep) { g_nv = 0; return; }
    if (p == 7 || p == 10) { g_vb[1] = g_vb[n - 1]; g_nv = 2; }        /* fan: first vertex + last */
    else if (p == 6) { if (n & 1) { g_vb[0] = g_vb[n - 2]; g_vb[1] = g_vb[n - 1]; g_nv = 2; } else { g_vb[0] = g_vb[n - 2]; g_vb[1] = g_vb[n - 1]; g_nv = 2; } }
    else { for (u32 k = 0; k < keep; k++) g_vb[k] = g_vb[n - keep + k]; g_nv = keep; }
}

/* ---------------- textures ---------------- */
static u32 swz(u32 x, u32 y, u32 w, u32 h) {
    u32 r = 0, bit = 1;
    for (u32 i = 1; i < w || i < h; i <<= 1) {
        if (i < w) { if (x & i) r |= bit; bit <<= 1; }
        if (i < h) { if (y & i) r |= bit; bit <<= 1; }
    }
    return r;
}
typedef struct { u32 off, cf, w, h, pitch, linear, pal, addr, filt, ok, bpp, comp; const u32 *sxt, *syt; } TEXU;   /* sxt/syt: swizzle tables */
typedef struct { f32 r, g, b, a; } RGBA;
typedef struct { u32 coff, zoff, cpitch, zpitch, cfmt, zfmt, swz, w, h, cx0, cy0, cx1, cy1, cbpp, zbpp; const u32 *sxt, *syt; } SURF;
/* register combiners, decoded per draw call into 4-lane vectors (r, g, b = the colour portion, a = the alpha portion) */
typedef float v4 __attribute__((vector_size(16), aligned(4)));
typedef int v4i __attribute__((vector_size(16), aligned(4)));
typedef struct { u8 rc, crep, ra, aalpha; v4 lo, hi, sc, bi; } CIN;          /* one input: mapping = sc * clamp(x, lo, hi) + bi */
typedef struct { CIN in[4]; v4 os, ob; v4i mux; u8 dotab, dotcd, cab, ccd, csum, aab, acd, asum, b2a_ab, b2a_cd; } CSTAGE;
typedef struct { u8 reg, rep, inv; } FIN;
typedef struct { FIN in[7]; u8 clampsum, invv1, invr0; } FCOMB;   /* A B C D E F G */
/* Everything one draw call's pixels depend on. It is snapshotted per draw call, so the triangles of many draw calls
   can be queued and drawn together by all the threads (Milestone 78). Code reaches it through "rs". */
typedef struct {
    TEXU tex[4]; SURF s;
    u32 alpha_en, alpha_func, alpha_ref, blend, sf, df, beq, bcol, ztest, zfunc, zwrite, cmask, cull, cullface, front, flat, nstages, ctl, fcw0, fcw1, tmode[4], cicw[8], aicw[8], cocw[8], aocw[8];
    f32 zmax; RGBA f0[8], f1[8], fogc;
    CSTAGE cs[8]; FCOMB fc;
} RS;
static RS g_cur;   /* the state being set up (main thread) */
#define g_tex (rs->tex)
#define g_s (rs->s)
#define g_alpha_en (rs->alpha_en)
#define g_alpha_func (rs->alpha_func)
#define g_alpha_ref (rs->alpha_ref)
#define g_blend (rs->blend)
#define g_sf (rs->sf)
#define g_df (rs->df)
#define g_beq (rs->beq)
#define g_bcol (rs->bcol)
#define g_ztest (rs->ztest)
#define g_zfunc (rs->zfunc)
#define g_zwrite (rs->zwrite)
#define g_cmask (rs->cmask)
#define g_cull (rs->cull)
#define g_cullface (rs->cullface)
#define g_front (rs->front)
#define g_flat (rs->flat)
#define g_nstages (rs->nstages)
#define g_ctl (rs->ctl)
#define g_fcw0 (rs->fcw0)
#define g_fcw1 (rs->fcw1)
#define g_tmode (rs->tmode)
#define g_cicw (rs->cicw)
#define g_aicw (rs->aicw)
#define g_cocw (rs->cocw)
#define g_aocw (rs->aocw)
#define g_zmax (rs->zmax)
#define g_f0 (rs->f0)
#define g_f1 (rs->f1)
#define g_fogc (rs->fogc)
static u32 g_unk_fmt[4];
/* swizzle offset tables (Milestone 78): swz(x, y) = sx[x] | sy[y], one pair per texture size, built once */
static u32 *g_swzt[13][13][2];
static u32 log2u(u32 v) { u32 l = 0; while ((1u << l) < v && l < 12) l++; return l; }
static void swz_tables(u32 w, u32 h, const u32 **sx, const u32 **sy) {
    u32 lw = log2u(w), lh = log2u(h);
    if (!g_swzt[lw][lh][0]) {
        u32 *a = HeapAlloc(GetProcessHeap(), 0, (w + h) * 4); if (!a) { *sx = *sy = 0; return; }
        for (u32 x = 0; x < w; x++) a[x] = swz(x, 0, w, h);
        for (u32 y = 0; y < h; y++) a[w + y] = swz(0, y, w, h);
        g_swzt[lw][lh][1] = a + w; __sync_synchronize(); g_swzt[lw][lh][0] = a;
    }
    *sx = g_swzt[lw][lh][0]; *sy = g_swzt[lw][lh][1];
}
static void tex_setup(u32 i) { RS *rs = &g_cur;
    TEXU *t = &g_tex[i]; u32 base = 0x1B00 + i * 0x40, fmt = K(base + 4);
    t->ok = (K(base + 0xC) >> 30) & 1;
    t->off = K(base); t->cf = (fmt >> 8) & 0xFF; t->addr = K(base + 8); t->filt = K(base + 0x14); t->pal = K(base + 0x20) & ~0x3Fu;
    t->comp = 0; t->linear = 0;
    switch (t->cf) {
    case 0x00: case 0x01: case 0x0B: case 0x19: t->bpp = 1; break;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x1A: case 0x27: case 0x28: case 0x29: case 0x31: t->bpp = 2; break;
    case 0x06: case 0x07: case 0x3A: case 0x3B: case 0x3C: t->bpp = 4; break;
    case 0x0C: case 0x0E: case 0x0F: t->comp = 1; t->bpp = 0; break;
    case 0x13: case 0x1B: case 0x1F: t->bpp = 1; t->linear = 1; break;
    case 0x10: case 0x11: case 0x1C: case 0x1D: case 0x20: case 0x30: t->bpp = 2; t->linear = 1; break;
    case 0x12: case 0x1E: case 0x2E: case 0x3F: case 0x40: case 0x41: t->bpp = 4; t->linear = 1; break;
    default: if (t->ok) { for (u32 k = 0; k < 4; k++) if (g_unk_fmt[k] == t->cf + 1) break; else if (!g_unk_fmt[k]) { g_unk_fmt[k] = t->cf + 1; out("  [render] texture format not handled: "); hex(t->cf); out("\r\n"); break; } } t->ok = 0; return;
    }
    if (t->linear) { u32 r = K(base + 0x1C); t->w = r >> 16; t->h = r & 0xFFFF; t->pitch = K(base + 0x10) >> 16; if (!t->w || !t->h) t->ok = 0; }
    else { t->w = 1u << ((fmt >> 20) & 0xF); t->h = 1u << ((fmt >> 24) & 0xF); t->pitch = t->w * t->bpp; if (t->w > 4096) t->w = 4096; if (t->h > 4096) t->h = 4096; swz_tables(t->w, t->h, &t->sxt, &t->syt); if (!t->sxt) t->ok = 0; }
}
static u32 c565(u32 p) { u32 r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31; return (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2); }
/* the last DXT block decoded (per sample call): its address and its 4 colours (Milestone 78) */
typedef struct { u8 *b; u32 pal[4]; } DXTC;
static u32 dxt_texel_c(TEXU *t, u32 x, u32 y, DXTC *dc) {
    u32 bw = (t->w + 3) / 4, bs = t->cf == 0x0C ? 8 : 16;
    u8 *b = PHYS(t->off + ((y / 4) * bw + x / 4) * bs);
    u8 *cb = bs == 16 ? b + 8 : b;
    if (dc->b != b) {
        dc->b = b;
        u32 c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8, p0 = c565(c0), p1 = c565(c1);
        u32 r0 = (p0 >> 16) & 255, g0 = (p0 >> 8) & 255, b0 = p0 & 255, r1 = (p1 >> 16) & 255, g1 = (p1 >> 8) & 255, b1 = p1 & 255;
        dc->pal[0] = 0xFF000000 | p0; dc->pal[1] = 0xFF000000 | p1;
        if (c0 > c1 || bs == 16) { dc->pal[2] = 0xFF000000 | ((2 * r0 + r1) / 3) << 16 | ((2 * g0 + g1) / 3) << 8 | (2 * b0 + b1) / 3; dc->pal[3] = 0xFF000000 | ((r0 + 2 * r1) / 3) << 16 | ((g0 + 2 * g1) / 3) << 8 | (b0 + 2 * b1) / 3; }
        else { dc->pal[2] = 0xFF000000 | ((r0 + r1) / 2) << 16 | ((g0 + g1) / 2) << 8 | (b0 + b1) / 2; dc->pal[3] = 0; }
    }
    u32 k = (rd32(cb + 4) >> (((y & 3) * 4 + (x & 3)) * 2)) & 3;
    u32 v = dc->pal[k], a = v >> 24, col = v & 0xFFFFFF;
    if (t->cf == 0x0E) { u32 i = (y & 3) * 4 + (x & 3); a = ((b[i / 2] >> ((i & 1) * 4)) & 15) * 17; }
    else if (t->cf == 0x0F) {
        u32 a0 = b[0], a1 = b[1]; u32 bits = (b[2] | b[3] << 8 | b[4] << 16) , bits2 = (b[5] | b[6] << 8 | b[7] << 16);
        u32 i = (y & 3) * 4 + (x & 3), ai = i < 8 ? (bits >> (i * 3)) & 7 : (bits2 >> ((i - 8) * 3)) & 7;
        if (ai == 0) a = a0; else if (ai == 1) a = a1;
        else if (a0 > a1) a = ((8 - ai) * a0 + (ai - 1) * a1) / 7;
        else a = ai == 6 ? 0 : ai == 7 ? 255 : ((6 - ai) * a0 + (ai - 1) * a1) / 5;
    }
    return a << 24 | col;
}
static u32 texel_c(TEXU *t, u32 x, u32 y, DXTC *dc);
static u32 texel(TEXU *t, u32 x, u32 y) { DXTC dc = {0}; return texel_c(t, x, y, &dc); }
static u32 texel_c(TEXU *t, u32 x, u32 y, DXTC *dc) {
    if (t->comp) return dxt_texel_c(t, x, y, dc);
    u32 o = t->linear ? y * t->pitch + x * t->bpp : (t->sxt[x] | t->syt[y]) * t->bpp;
    u8 *p = PHYS(t->off + o);
    switch (t->cf) {
    case 0x00: case 0x13: { u32 l = p[0]; return 0xFF000000 | l << 16 | l << 8 | l; }
    case 0x01: case 0x1B: { u32 l = p[0]; return l << 24 | l << 16 | l << 8 | l; }
    case 0x19: case 0x1F: return (u32)p[0] << 24;
    case 0x0B: return *(u32 *)PHYS(t->pal + p[0] * 4);
    case 0x1A: case 0x20: { u32 l = p[0], a = p[1]; return a << 24 | l << 16 | l << 8 | l; }
    case 0x02: case 0x10: { u32 v = *(u16 *)p, r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return ((v & 0x8000) ? 0xFF000000 : 0) | (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2); }
    case 0x03: case 0x1C: { u32 v = *(u16 *)p, r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return 0xFF000000 | (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2); }
    case 0x04: case 0x1D: { u32 v = *(u16 *)p; return ((v >> 12) & 15) * 17u << 24 | ((v >> 8) & 15) * 17u << 16 | ((v >> 4) & 15) * 17u << 8 | (v & 15) * 17u; }
    case 0x05: case 0x11: return 0xFF000000 | c565(*(u16 *)p);
    case 0x06: case 0x12: return *(u32 *)p;
    case 0x07: case 0x1E: return 0xFF000000 | *(u32 *)p;
    case 0x3A: case 0x3F: { u32 v = *(u32 *)p; return (v & 0xFF00FF00) | (v & 0xFF) << 16 | (v >> 16) & 0xFF; }   /* A8B8G8R8 */
    case 0x3B: case 0x40: { u32 v = *(u32 *)p; return (v & 0xFF) << 24 | (v >> 8); }                                /* B8G8R8A8 */
    case 0x3C: case 0x41: { u32 v = *(u32 *)p; return (v >> 24) | (v & 0xFF) << 24 | ((v >> 8) & 0xFF) << 16 | ((v >> 16) & 0xFF) << 8; }   /* R8G8B8A8 */
    case 0x28: { u32 v = *(u16 *)p; return 0xFF000000 | (v >> 8) << 8 | (v & 0xFF); }   /* G8B8 */
    case 0x29: { u32 v = *(u16 *)p; return 0xFF000000 | (v >> 8) << 16 | (v & 0xFF); }  /* R8B8 */
    case 0x27: { u32 v = *(u16 *)p; return 0xFF000000 | c565(v); }
    case 0x31: case 0x30: { u32 l = (*(u16 *)p) >> 8; return 0xFF000000 | l << 16 | l << 8 | l; }
    case 0x2E: { u32 l = (*(u32 *)p) >> 24; return 0xFF000000 | l << 16 | l << 8 | l; }
    }
    return 0xFFFF00FF;
}
static s32 wrap_coord(s32 c, u32 n, u32 mode) {
    if (mode == 1 && !(n & (n - 1))) return c & (s32)(n - 1);
    switch (mode) {
    case 1: default: c %= (s32)n; if (c < 0) c += n; return c;
    case 2: { s32 p = 2 * (s32)n; c %= p; if (c < 0) c += p; return c >= (s32)n ? p - 1 - c : c; }
    case 3: case 4: case 5: return c < 0 ? 0 : c >= (s32)n ? (s32)n - 1 : c;
    }
}
static void sample(RS *rs, u32 i, f32 s, f32 tt, f32 *o) {
    TEXU *t = &g_tex[i];
    if (!t->linear) { s *= t->w; tt *= t->h; }
    u32 um = t->addr & 0xF, vm = (t->addr >> 8) & 0xF;
    u32 c;
    if (((t->filt >> 24) & 0xF) == 2) {   /* bilinear */
        f32 fs = s - 0.5f, ft = tt - 0.5f; f32 xs = floor_(fs), ys = floor_(ft); f32 ax = fs - xs, ay = ft - ys;
        if (xs < -1e6f) xs = -1e6f; if (xs > 1e6f) xs = 1e6f; if (ys < -1e6f) ys = -1e6f; if (ys > 1e6f) ys = 1e6f;
        s32 x0 = (s32)xs, y0 = (s32)ys;
        u32 X0 = wrap_coord(x0, t->w, um), X1 = wrap_coord(x0 + 1, t->w, um), Y0 = wrap_coord(y0, t->h, vm), Y1 = wrap_coord(y0 + 1, t->h, vm);
        DXTC dc = {0};
        u32 c00 = texel_c(t, X0, Y0, &dc), c10 = texel_c(t, X1, Y0, &dc), c01 = texel_c(t, X0, Y1, &dc), c11 = texel_c(t, X1, Y1, &dc);
        #define UNPK(c) ((v4){(f32)(((c) >> 16) & 255), (f32)(((c) >> 8) & 255), (f32)((c) & 255), (f32)((c) >> 24)})
        v4 top = UNPK(c00) + (UNPK(c10) - UNPK(c00)) * ax, bot = UNPK(c01) + (UNPK(c11) - UNPK(c01)) * ax;
        v4 r = (top + (bot - top) * ay) * (1 / 255.0f);
        o[0] = r[0]; o[1] = r[1]; o[2] = r[2]; o[3] = r[3];
        return;
    }
    f32 xs = floor_(s), ys = floor_(tt);
    if (xs < -1e6f) xs = -1e6f; if (xs > 1e6f) xs = 1e6f; if (ys < -1e6f) ys = -1e6f; if (ys > 1e6f) ys = 1e6f;
    c = texel(t, wrap_coord((s32)xs, t->w, um), wrap_coord((s32)ys, t->h, vm));
    o[0] = ((c >> 16) & 255) * (1 / 255.0f); o[1] = ((c >> 8) & 255) * (1 / 255.0f); o[2] = (c & 255) * (1 / 255.0f); o[3] = (c >> 24) * (1 / 255.0f);
}

/* ---------------- register combiners ---------------- */
static RGBA unpack(u32 c) { RGBA r = {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, (c >> 24) / 255.0f}; return r; }
static f32 map_in(f32 x, u32 m) {
    switch (m) {
    case 0: return x < 0 ? 0 : x;
    case 1: return 1 - cl01(x);
    case 2: return 2 * (x < 0 ? 0 : x) - 1;
    case 3: return -2 * (x < 0 ? 0 : x) + 1;
    case 4: return (x < 0 ? 0 : x) - 0.5f;
    case 5: return -(x < 0 ? 0 : x) + 0.5f;
    case 6: return x;
    default: return -x;
    }
}
static void in_rgb(RGBA *R, u32 b, f32 *o) { RGBA *s = &R[b & 15]; u32 m = (b >> 5) & 7; if (b & 0x10) o[0] = o[1] = o[2] = map_in(s->a, m); else { o[0] = map_in(s->r, m); o[1] = map_in(s->g, m); o[2] = map_in(s->b, m); } }
static f32 in_a(RGBA *R, u32 b) { RGBA *s = &R[b & 15]; return map_in((b & 0x10) ? s->a : s->b, (b >> 5) & 7); }
static f32 out_op(f32 x, u32 op) { switch (op) { case 1: x -= 0.5f; break; case 2: x *= 2; break; case 3: x = 2 * (x - 0.5f); break; case 4: x *= 4; break; case 6: x *= 0.5f; break; } return clpm(x); }

/* per-draw cache of the combiner setup */

static inline v4 ld4(const RGBA *p) { return *(const v4 *)p; }
static inline v4 vmin(v4 a, v4 b) { return __builtin_ia32_minps(a, b); }
static inline v4 vmax(v4 a, v4 b) { return __builtin_ia32_maxps(a, b); }
static inline v4 splat(f32 x) { v4 r = {x, x, x, x}; return r; }
/* decodes the combiner setup of the current draw (called from state_setup) */
static void comb_decode(RS *rs) {
    static const f32 mlo[8] = {0, 0, 0, 0, 0, 0, -1e30f, -1e30f}, mhi[8] = {1e30f, 1, 1e30f, 1e30f, 1e30f, 1e30f, 1e30f, 1e30f};
    static const f32 msc[8] = {1, -1, 2, -2, 1, -1, 1, -1}, mbi[8] = {0, 1, -1, 1, -0.5f, 0.5f, 0, 0};
    static const f32 oos[8] = {1, 1, 2, 2, 4, 1, 0.5f, 1}, oob[8] = {0, -0.5f, 0, -0.5f, 0, 0, 0, 0};
    for (u32 s = 0; s < g_nstages; s++) {
        CSTAGE *t = &rs->cs[s]; u32 ci = g_cicw[s], ai = g_aicw[s], co = g_cocw[s], ao = g_aocw[s];
        for (u32 j = 0; j < 4; j++) {
            u32 cb = (ci >> (24 - 8 * j)) & 0xFF, ab = (ai >> (24 - 8 * j)) & 0xFF; CIN *n = &t->in[j];
            u32 cm = (cb >> 5) & 7, am = (ab >> 5) & 7;
            n->rc = cb & 15; n->crep = (cb >> 4) & 1; n->ra = ab & 15; n->aalpha = (ab >> 4) & 1;
            n->lo = (v4){mlo[cm], mlo[cm], mlo[cm], mlo[am]}; n->hi = (v4){mhi[cm], mhi[cm], mhi[cm], mhi[am]};
            n->sc = (v4){msc[cm], msc[cm], msc[cm], msc[am]}; n->bi = (v4){mbi[cm], mbi[cm], mbi[cm], mbi[am]};
        }
        u32 cop = (co >> 15) & 7, aop = (ao >> 15) & 7;
        t->os = (v4){oos[cop], oos[cop], oos[cop], oos[aop]}; t->ob = (v4){oob[cop], oob[cop], oob[cop], oob[aop]};
        int cmx = (co & 0x4000) ? -1 : 0, amx = (ao & 0x4000) ? -1 : 0; t->mux = (v4i){cmx, cmx, cmx, amx};
        t->dotab = (co >> 13) & 1; t->dotcd = (co >> 12) & 1;
        t->cab = (co >> 4) & 15; t->ccd = co & 15; t->csum = (co >> 8) & 15;
        t->aab = (ao >> 4) & 15; t->acd = ao & 15; t->asum = (ao >> 8) & 15;
        t->b2a_ab = (co >> 19) & 1; t->b2a_cd = (co >> 18) & 1;
    }
    u32 c0 = g_fcw0, c1 = g_fcw1, b[7] = {c0 >> 24, (c0 >> 16) & 0xFF, (c0 >> 8) & 0xFF, c0 & 0xFF, c1 >> 24, (c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF};
    for (u32 j = 0; j < 7; j++) { rs->fc.in[j].reg = b[j] & 15; rs->fc.in[j].rep = (b[j] >> 4) & 1; rs->fc.in[j].inv = (b[j] >> 5) & 1; }
    rs->fc.clampsum = (c1 >> 7) & 1; rs->fc.invv1 = (c1 >> 6) & 1; rs->fc.invr0 = (c1 >> 5) & 1;
}
static inline v4 cin(const CIN *n, const RGBA *R) {
    v4 c = ld4(&R[n->rc]); if (n->crep) c = splat(c[3]);
    const RGBA *a = &R[n->ra]; c[3] = n->aalpha ? a->a : a->b;
    return vmin(vmax(c, n->lo), n->hi) * n->sc + n->bi;
}
static inline v4 fin(const FIN *n, const RGBA *R) {
    v4 c = ld4(&R[n->reg]); if (n->rep) c = splat(c[3]);
    c = vmin(vmax(c, splat(0)), splat(1)); return n->inv ? splat(1) - c : c;
}
static void combine(RS *rs, RGBA *R, f32 *outc) {
    const v4 m1 = splat(-1), p1 = splat(1);
    for (u32 s = 0; s < g_nstages; s++) {
        const CSTAGE *t = &rs->cs[s];
        R[1] = g_f0[s]; R[2] = g_f1[s];
        v4 A = cin(&t->in[0], R), Bv = cin(&t->in[1], R), C = cin(&t->in[2], R), D = cin(&t->in[3], R);
        v4 AB = A * Bv, CD = C * D;
        if (t->dotab) { f32 d = AB[0] + AB[1] + AB[2]; AB[0] = AB[1] = AB[2] = d; }
        if (t->dotcd) { f32 d = CD[0] + CD[1] + CD[2]; CD[0] = CD[1] = CD[2] = d; }
        int sel = (g_ctl & 0x100) ? R[12].a >= 0.5f : ((u32)(cl01(R[12].a) * 255 + 0.5f) & 1);
        v4 SUM = AB + CD, MX = sel ? CD : AB;
        SUM = (v4)(((v4i)MX & t->mux) | ((v4i)SUM & ~t->mux));
        AB = vmin(vmax((AB + t->ob) * t->os, m1), p1); CD = vmin(vmax((CD + t->ob) * t->os, m1), p1); SUM = vmin(vmax((SUM + t->ob) * t->os, m1), p1);
        u32 d;
        if ((d = t->cab)) { R[d].r = AB[0]; R[d].g = AB[1]; R[d].b = AB[2]; if (t->b2a_ab) R[d].a = AB[2]; }
        if ((d = t->ccd)) { R[d].r = CD[0]; R[d].g = CD[1]; R[d].b = CD[2]; if (t->b2a_cd) R[d].a = CD[2]; }
        if ((d = t->csum)) { R[d].r = SUM[0]; R[d].g = SUM[1]; R[d].b = SUM[2]; }
        if ((d = t->aab)) R[d].a = AB[3];
        if ((d = t->acd)) R[d].a = CD[3];
        if ((d = t->asum)) R[d].a = SUM[3];
    }
    /* final combiner: rgb = A*B + (1-A)*C + D, alpha = G; E*F and V1+R0 are registers 15 and 14 */
    const FCOMB *f = &rs->fc;
    {   v4 E = fin(&f->in[4], R), F = fin(&f->in[5], R), ef = E * F; ef[3] = 1;
        v4 v1 = vmin(vmax(ld4(&R[5]), splat(0)), p1), r0 = vmin(vmax(ld4(&R[12]), splat(0)), p1);
        if (f->invv1) v1 = p1 - v1; if (f->invr0) r0 = p1 - r0;
        v4 vr = v1 + r0; if (f->clampsum) vr = vmin(vr, p1); vr[3] = 1;
        *(v4 *)&R[14] = vr; *(v4 *)&R[15] = ef; }
    v4 A = fin(&f->in[0], R), Bv = fin(&f->in[1], R), C = fin(&f->in[2], R), D = fin(&f->in[3], R);
    v4 o = vmin(vmax(A * Bv + (p1 - A) * C + D, splat(0)), p1);
    outc[0] = o[0]; outc[1] = o[1]; outc[2] = o[2];
    { const FIN *g = &f->in[6]; const RGBA *s = &R[g->reg]; f32 v = cl01(g->rep ? s->a : s->b); outc[3] = g->inv ? 1 - v : v; }
}
/* ---------------- the surface ---------------- */
static void surf_setup(void) { RS *rs = &g_cur;
    u32 f = K(0x208);
    g_s.cfmt = f & 0xF; g_s.zfmt = (f >> 4) & 0xF; g_s.swz = ((f >> 8) & 0xF) == 2;
    g_s.coff = K(0x210); g_s.zoff = K(0x214); g_s.cpitch = K(0x20C) & 0xFFFF; g_s.zpitch = K(0x20C) >> 16;
    g_s.cbpp = (g_s.cfmt == 1 || g_s.cfmt == 2 || g_s.cfmt == 3) ? 2 : (g_s.cfmt == 9) ? 1 : (g_s.cfmt == 0xA) ? 2 : 4;
    g_s.zbpp = g_s.zfmt == 1 ? 2 : 4;
    u32 hz = K(0x200), vt = K(0x204);
    g_s.cx0 = hz & 0xFFFF; g_s.cx1 = g_s.cx0 + (hz >> 16); g_s.cy0 = vt & 0xFFFF; g_s.cy1 = g_s.cy0 + (vt >> 16);
    if (g_s.swz) { g_s.w = 1u << ((f >> 16) & 0xF); g_s.h = 1u << ((f >> 24) & 0xF); if (g_s.w > 4096) g_s.w = 4096; if (g_s.h > 4096) g_s.h = 4096; swz_tables(g_s.w, g_s.h, &g_s.sxt, &g_s.syt); if (g_s.cx1 > g_s.w) g_s.cx1 = g_s.w; if (g_s.cy1 > g_s.h) g_s.cy1 = g_s.h; }
    if (g_s.cx1 > 4096) g_s.cx1 = 4096; if (g_s.cy1 > 4096) g_s.cy1 = 4096;
}
static u8 *cptr(RS *rs, u32 x, u32 y) { return PHYS(g_s.coff + (g_s.swz ? (g_s.sxt[x] | g_s.syt[y]) * g_s.cbpp : y * g_s.cpitch + x * g_s.cbpp)); }
static u8 *zptr(RS *rs, u32 x, u32 y) { return PHYS(g_s.zoff + (g_s.swz ? (g_s.sxt[x] | g_s.syt[y]) * g_s.zbpp : y * g_s.zpitch + x * g_s.zbpp)); }
static int ok_ptr(u8 *p, u32 n) { return (u32)p >= 0x80000000u && (u32)p + n <= 0x83F00000u; }

static void flush_tris(void);
void render_clear(u32 flags) {
    if (g_gl) { gl_state(0); gl_cmd1(CMD_CLEAR, flags); if (g_gl) return; }
    flush_tris(); surf_setup(); RS *rs = &g_cur;
    u32 x0 = K(0x1D98) & 0xFFFF, x1 = K(0x1D98) >> 16, y0 = K(0x1D9C) & 0xFFFF, y1 = K(0x1D9C) >> 16;
    if (x1 >= 4096 || y1 >= 4096) return;
    u32 c = K(0x1D90), zv = K(0x1D8C);
    for (u32 y = y0; y <= y1; y++) for (u32 x = x0; x <= x1; x++) {
        if (flags & 0xF0) { u8 *p = cptr(rs, x, y); if (!ok_ptr(p, 4)) continue;
            if (g_s.cbpp == 4) { u32 m = ((flags & 0x80) ? 0xFF000000 : 0) | ((flags & 0x10) ? 0xFF0000 : 0) | ((flags & 0x20) ? 0xFF00 : 0) | ((flags & 0x40) ? 0xFF : 0);   /* bits: 4 R, 5 G, 6 B, 7 A */ *(u32 *)p = (*(u32 *)p & ~m) | (c & m); }
            else if (g_s.cbpp == 2) *(u16 *)p = (u16)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F)); }
        if (flags & 3) { u8 *p = zptr(rs, x, y); if (!ok_ptr(p, 4)) continue;
            if (g_s.zbpp == 4) { u32 m = ((flags & 1) ? 0xFFFFFF00 : 0) | ((flags & 2) ? 0xFF : 0); *(u32 *)p = (*(u32 *)p & ~m) | (zv & m); }
            else if (flags & 1) *(u16 *)p = (u16)zv; }
    }
}

/* ---------------- per-draw state ---------------- */
static int g_state_ok;
static void state_setup(void) { RS *rs = &g_cur;
    surf_setup();
    g_alpha_en = K(0x300); g_alpha_func = K(0x33C); g_alpha_ref = K(0x340) & 0xFF;
    g_blend = K(0x304); g_sf = K(0x344); g_df = K(0x348); g_bcol = K(0x34C); g_beq = K(0x350);
    g_ztest = K(0x30C) && g_s.zfmt; g_zfunc = K(0x354); g_zwrite = K(0x35C) && g_s.zfmt;
    g_cmask = K(0x358); g_cull = K(0x308); g_cullface = K(0x39C); g_front = K(0x3A0); g_flat = K(0x37C) == 0x1D00;
    g_zmax = g_s.zfmt == 1 ? 65535.0f : 16777215.0f;
    u32 sp = K(0x1E70);
    for (u32 i = 0; i < 4; i++) { g_tmode[i] = (sp >> (i * 5)) & 0x1F; tex_setup(i); if (g_tmode[i] == 0) g_tex[i].ok = 0; }
    g_ctl = K(0x1E60); g_nstages = g_ctl & 0xFF; if (g_nstages > 8) g_nstages = 8; if (!g_nstages) g_nstages = 1;
    for (u32 s = 0; s < 8; s++) {
        g_cicw[s] = K(0xAC0 + s * 4); g_aicw[s] = K(0x260 + s * 4); g_cocw[s] = K(0x1E40 + s * 4); g_aocw[s] = K(0xAA0 + s * 4);
        g_f0[s] = unpack(K(0xA60 + ((g_ctl & 0x1000) ? s : 0) * 4)); g_f1[s] = unpack(K(0xA80 + ((g_ctl & 0x10000) ? s : 0) * 4));
    }
    g_fcw0 = K(0x288); g_fcw1 = K(0x28C); comb_decode(rs); g_fogc = unpack(K(0x2A8)); g_fogc.a = 1;   /* fog not applied yet: factor 1 */
    g_state_ok = 1; { void state_changed(void); state_changed(); }
}
static int cmpf(u32 f, f32 a, f32 b) {
    switch (f) { case 0x200: return 0; case 0x201: return a < b; case 0x202: return a == b; case 0x203: return a <= b; case 0x204: return a > b; case 0x205: return a != b; case 0x206: return a >= b; default: return 1; }
}
static v4 bfac_v(u32 f, v4 s, v4 d, v4 bc) {
    const v4 one = splat(1);
    switch (f) {
    case 0: return splat(0); case 1: return one;
    case 0x300: return s; case 0x301: return one - s; case 0x302: return splat(s[3]); case 0x303: return one - splat(s[3]);
    case 0x304: return splat(d[3]); case 0x305: return one - splat(d[3]); case 0x306: return d; case 0x307: return one - d;
    case 0x308: { f32 m = 1 - d[3]; if (s[3] < m) m = s[3]; v4 r = {m, m, m, 1}; return r; }
    case 0x8001: return bc; case 0x8002: return one - bc; case 0x8003: return splat(bc[3]); case 0x8004: return one - splat(bc[3]);
    }
    return one;
}
static f32 bfac(u32 f, f32 *s, f32 *d, RGBA *bc, u32 k) {
    f32 cc = k == 0 ? bc->r : k == 1 ? bc->g : k == 2 ? bc->b : bc->a;
    switch (f) {
    case 0: return 0; case 1: return 1;
    case 0x300: return s[k]; case 0x301: return 1 - s[k]; case 0x302: return s[3]; case 0x303: return 1 - s[3];
    case 0x304: return d[3]; case 0x305: return 1 - d[3]; case 0x306: return d[k]; case 0x307: return 1 - d[k];
    case 0x308: { f32 m = 1 - d[3]; if (s[3] < m) m = s[3]; return k == 3 ? 1 : m; }
    case 0x8001: return cc; case 0x8002: return 1 - cc; case 0x8003: return bc->a; case 0x8004: return 1 - bc->a;
    }
    return 1;
}

/* ---------------- triangles ---------------- */
static u32 g_dbg;   /* > 0: dump this many pixels (debug) */
static void dumpf(f32 x) { s32 i = (s32)(x * 1000); if (i < 0) { out("-"); i = -i; } dec((u32)i / 1000); out("."); u32 f = (u32)i % 1000; out(f < 100 ? (f < 10 ? "00" : "0") : ""); dec(f); }
static void dump_ov(OV *v) { out("    v "); dumpf(v->x); out(","); dumpf(v->y); out(" z "); dumpf(v->z); out(" w "); dumpf(v->w); out(" d0 "); for (u32 k = 0; k < 4; k++) { dumpf(v->v[k]); out(" "); } out("t0 "); dumpf(v->v[9]); out(" "); dumpf(v->v[10]); out("\r\n"); }
u32 g_r_us;   /* microseconds spent rasterizing */
/* Triangles are queued and drawn by NW threads at the end of each draw call (state only changes between draw
   calls). Thread k draws the rows y % NW == k, so every pixel still sees the triangles in order. */
#define NW 12
#define MAXQ 8192
#define MAXS 1024
typedef struct { OV a, b, c; u32 si; } TRI;
static RS g_rs[MAXS]; static u32 g_nrs, g_snap_dirty = 1;
static TRI g_tq[MAXQ]; static u32 g_ntq; static f32 g_qarea;
static volatile u32 g_gen, g_done[NW]; static HANDLE g_wev[NW];
static u32 g_pix_k[NW * 16];   /* one cache line each */
static volatile u32 g_kind;     /* the job: 0 = rasterize the triangle queue, 1 = transform vertices */
static void raster(RS *rs, OV *a, OV *b, OV *c, u32 k, u32 step);
/* a new draw call's state: take a new snapshot; a new render target: draw what is queued first */
void state_changed(void) {
    g_snap_dirty = 1;
    if (g_ntq && g_nrs && (g_rs[g_nrs - 1].s.coff != g_cur.s.coff || g_rs[g_nrs - 1].s.zoff != g_cur.s.zoff)) flush_tris();
}
u64 g_wk_cyc[NW * 8];   /* rasterizing time per thread (TSC), one cache line each */
static void run_queue(u32 k) { u64 t0 = __builtin_ia32_rdtsc(); for (u32 i = 0; i < g_ntq; i++) raster(&g_rs[g_tq[i].si], &g_tq[i].a, &g_tq[i].b, &g_tq[i].c, k, NW); g_wk_cyc[k * 8] += __builtin_ia32_rdtsc() - t0; }
static DWORD __stdcall worker(void *p) {
    u32 k = (u32)p, seen = 0;
    for (;;) {
        WaitForSingleObject(g_wev[k], 0xFFFFFFFF);
        for (;;) {   /* after a job, stay awake briefly: the next big draw usually follows at once */
            u32 g = g_gen;
            if (g != seen) { seen = g; if (g_kind) vs_part(k, NW); else run_queue(k); __sync_synchronize(); g_done[k] = g; continue; }
            u32 i = 0; while (i < 20000 && g_gen == seen) { __builtin_ia32_pause(); i++; }
            if (g_gen == seen) break;
        }
    }
}
u32 g_fl_par, g_fl_ser, g_fl_tris;   /* flushes (parallel, serial) and the triangles they drew */
static void flush_tris(void) {
    if (!g_ntq) return;
    g_fl_tris += g_ntq; if (g_qarea < 3000 || !g_wev[1]) g_fl_ser++; else g_fl_par++;
    s64 t0, t1, f; QueryPerformanceCounter(&t0);
    if (g_qarea < 3000 || !g_wev[1]) { for (u32 i = 0; i < g_ntq; i++) raster(&g_rs[g_tq[i].si], &g_tq[i].a, &g_tq[i].b, &g_tq[i].c, 0, 1); }   /* small: not worth waking the workers */
    else par_run(0);
    g_ntq = 0; g_qarea = 0; g_nrs = 0; g_snap_dirty = 1;
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); g_r_us += (u32)((t1 - t0) * 1000000 / f);
    u32 s = 0; for (u32 k = 0; k < NW; k++) s += g_pix_k[k * 16]; g_r_pix = s;
}
static void par_run(u32 kind) {
    if (!g_wev[1]) { if (kind) vs_part(0, 1); else for (u32 k = 0; k < NW; k++) run_queue(k); return; }
    g_kind = kind; u32 g = ++g_gen; __sync_synchronize();
    for (u32 k = 1; k < NW; k++) if (g_wev[k]) SetEvent(g_wev[k]);
    if (kind) vs_part(0, NW); else run_queue(0);
    for (u32 k = 1; k < NW; k++) if (g_wev[k]) while (g_done[k] != g) __builtin_ia32_pause();
}
void render_start_workers(void) {
    for (u32 k = 1; k < NW; k++) { g_wev[k] = CreateEventA(0, 0, 0, 0); if (!CreateThread(0, 0x10000, worker, (void *)k, 0, 0)) g_wev[k] = 0; }
}
static void draw_tri(OV *a, OV *b, OV *c) {
    if (!g_state_ok) state_setup();
    RS *rs = &g_cur;
    if (!(a->w > 0 && b->w > 0 && c->w > 0)) return;
    {   f32 area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);   /* zero area and culling, before queuing */
        if (!(fabs_(area) > 1e-8f)) return;
        if (g_cull) { int front = g_front == 0x900 ? area > 0 : area < 0;
            if (g_cullface == 0x408 || (g_cullface == 0x405 && !front) || (g_cullface == 0x404 && front)) return; } }
    g_r_tris++;
    if (g_r_tris == 30000 || g_r_tris == 60000) {
        out("  [render] triangle "); dec(g_r_tris); out(" tex0 ok "); dec(g_tex[0].ok); out(" fmt "); hex(g_tex[0].cf); out(" "); dec(g_tex[0].w); out("x"); dec(g_tex[0].h); out(" off "); hex(g_tex[0].off);
        out(" stages "); dec(g_nstages); out(" blend "); dec(g_blend); out(" alphatest "); dec(g_alpha_en); out(" ztest "); dec(g_ztest); out(" cull "); dec(g_cull); out(" surf "); hex(g_s.coff); out("\r\n");
        dump_ov(a); dump_ov(b); dump_ov(c); g_dbg = 1;
    }
    {   f32 mnx = a->x, mxx = a->x, mny = a->y, mxy = a->y;
        if (b->x < mnx) mnx = b->x; if (c->x < mnx) mnx = c->x; if (b->x > mxx) mxx = b->x; if (c->x > mxx) mxx = c->x;
        if (b->y < mny) mny = b->y; if (c->y < mny) mny = c->y; if (b->y > mxy) mxy = b->y; if (c->y > mxy) mxy = c->y;
        f32 ar = (mxx - mnx) * (mxy - mny); g_qarea += ar < 0 ? 0 : ar > 1e7f ? 1e7f : ar; }
    if (g_snap_dirty || !g_nrs) { if (g_nrs == MAXS) flush_tris(); g_rs[g_nrs++] = g_cur; g_snap_dirty = 0; }
    TRI *t = &g_tq[g_ntq++]; t->a = *a; t->b = *b; t->c = *c; t->si = g_nrs - 1;
    if (g_ntq == MAXQ) flush_tris();
}
static void raster(RS *rs, OV *a, OV *b, OV *c, u32 kk, u32 step) {
    if (step > 1) {   /* thread kk draws the 8-row bands b with b % NW == kk: skip triangles outside all of them */
        f32 mny = a->y, mxy = a->y; if (b->y < mny) mny = b->y; if (c->y < mny) mny = c->y; if (b->y > mxy) mxy = b->y; if (c->y > mxy) mxy = c->y;
        if (mxy < 0 || mny > 8192) return;
        s32 b0 = mny < 0 ? 0 : (s32)mny >> 3, b1 = (s32)mxy >> 3;
        if (b1 - b0 + 1 < NW) { s32 bb = b0; for (; bb <= b1; bb++) if ((u32)bb % NW == kk) break; if (bb > b1) return; }
    }

    f32 area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (!(fabs_(area) > 1e-8f)) return;
    if (g_cull) { int front = g_front == 0x900 ? area > 0 : area < 0;   /* 0x900 = CW */
        if (g_cullface == 0x408 || (g_cullface == 0x405 && !front) || (g_cullface == 0x404 && front)) return; }
    f32 lim = 8192;
    if (!(fabs_(a->x) < lim && fabs_(a->y) < lim && fabs_(b->x) < lim && fabs_(b->y) < lim && fabs_(c->x) < lim && fabs_(c->y) < lim)) return;
    f32 mnx = a->x, mxx = a->x, mny = a->y, mxy = a->y;
    if (b->x < mnx) mnx = b->x; if (c->x < mnx) mnx = c->x; if (b->x > mxx) mxx = b->x; if (c->x > mxx) mxx = c->x;
    if (b->y < mny) mny = b->y; if (c->y < mny) mny = c->y; if (b->y > mxy) mxy = b->y; if (c->y > mxy) mxy = c->y;
    s32 x0 = (s32)floor_(mnx), x1 = (s32)floor_(mxx) + 1, y0 = (s32)floor_(mny), y1 = (s32)floor_(mxy) + 1;
    if (x0 < (s32)g_s.cx0) x0 = g_s.cx0; if (y0 < (s32)g_s.cy0) y0 = g_s.cy0; if (x1 > (s32)g_s.cx1) x1 = g_s.cx1; if (y1 > (s32)g_s.cy1) y1 = g_s.cy1;
    if (x0 >= x1 || y0 >= y1) return;
    f32 ia = 1 / area;
    f32 iwa = 1 / a->w, iwb = 1 / b->w, iwc = 1 / c->w;
    /* plane equations (Milestone 78): l0 = l0c + l0x*px + l0y*py, the same for l1; any per-vertex value Q is then
       Qc + (Qa-Qc)*l0 + (Qb-Qc)*l1, i.e. linear in px and py */
    f32 l0x = (b->y - c->y) * ia, l0y = (c->x - b->x) * ia, l0c = (b->x * c->y - c->x * b->y) * ia;
    f32 l1x = (c->y - a->y) * ia, l1y = (a->x - c->x) * ia, l1c = (c->x * a->y - a->x * c->y) * ia;
    #define PLANE(qa, qb, qc, Q0, QX, QY) { QX = ((qa) - (qc)) * l0x + ((qb) - (qc)) * l1x; QY = ((qa) - (qc)) * l0y + ((qb) - (qc)) * l1y; Q0 = (qc) + ((qa) - (qc)) * l0c + ((qb) - (qc)) * l1c; }
    f32 z0, zx, zy, w0, wx, wy; PLANE(a->z, b->z, c->z, z0, zx, zy); PLANE(iwa, iwb, iwc, w0, wx, wy);
    /* the varyings as 4-lane vectors, divided by w: 0 = D0, 1 = D1, 2..5 = T0..T3 */
    v4 q0[6], qx[6], qy[6]; u32 nq = 0, qidx[6], tq[4] = {0, 0, 0, 0};
    for (u32 j = 0; j < 6; j++) {
        u32 base = j == 0 ? 0 : j == 1 ? 4 : 9 + (j - 2) * 4;
        if (j >= 2 && !g_tex[j - 2].ok) continue;
        OV *fa = g_flat && j < 2 ? c : a, *fb = g_flat && j < 2 ? c : b;
        v4 va = {fa->v[base], fa->v[base + 1], fa->v[base + 2], fa->v[base + 3]}, vb = {fb->v[base], fb->v[base + 1], fb->v[base + 2], fb->v[base + 3]}, vc = {c->v[base], c->v[base + 1], c->v[base + 2], c->v[base + 3]};
        va = va * iwa; vb = vb * iwb; vc = vc * iwc;
        PLANE(va, vb, vc, q0[nq], qx[nq], qy[nq]);
        if (j >= 2) tq[j - 2] = nq;
        qidx[nq++] = j;
    }
    v4 bcv = {((g_bcol >> 16) & 255) / 255.0f, ((g_bcol >> 8) & 255) / 255.0f, (g_bcol & 255) / 255.0f, (g_bcol >> 24) / 255.0f};
    u32 cmask32 = ((g_cmask & 0x01000000) ? 0xFF000000 : 0) | ((g_cmask & 0x010000) ? 0xFF0000 : 0) | ((g_cmask & 0x0100) ? 0xFF00 : 0) | ((g_cmask & 1) ? 0xFF : 0);
    for (s32 y = y0; y < y1; y++) {
        if (step > 1 && ((u32)y >> 3) % NW != kk) { y |= 7; continue; }
        f32 py = y + 0.5f;
        /* the row's span: each edge function is linear in x, so solve for where it crosses 0 */
        f32 sx0 = (f32)x0, sx1 = (f32)x1;
        f32 e0r = l0c + l0y * py, e1r = l1c + l1y * py;
        {   f32 er[3] = {e0r, e1r, 1 - e0r - e1r}, ex[3] = {l0x, l1x, -l0x - l1x};
            for (u32 k = 0; k < 3; k++) {   /* l_k(px) = er + ex*px >= 0 */
                if (ex[k] > 1e-12f) { f32 xs = -er[k] / ex[k] - 0.5f; if (xs > sx0) sx0 = xs; }
                else if (ex[k] < -1e-12f) { f32 xe = -er[k] / ex[k] - 0.5f + 1; if (xe < sx1) sx1 = xe; }
                else if (er[k] < 0) sx1 = sx0;
            }
        }
        s32 rx0 = (s32)floor_(sx0), rx1 = (s32)floor_(sx1) + 1; if (rx0 < x0) rx0 = x0; if (rx1 > x1) rx1 = x1;
        f32 zr = z0 + zy * py, wr = w0 + wy * py; v4 qr[6]; for (u32 j = 0; j < nq; j++) qr[j] = q0[j] + qy[j] * py;
        for (s32 x = rx0; x < rx1; x++) {
            f32 px = x + 0.5f;
            f32 l0 = e0r + l0x * px, l1 = e1r + l1x * px;
            if (l0 < 0 || l1 < 0 || l0 + l1 > 1) continue;
            f32 z = zr + zx * px;
            if (z < 0) z = 0; if (z > g_zmax) z = g_zmax;
            u8 *zp = 0; u32 zi = (u32)z;
            if (g_ztest || g_zwrite) {
                zp = zptr(rs, x, y); if (!ok_ptr(zp, 4)) continue;
                if (g_ztest) { u32 old = g_s.zbpp == 4 ? *(u32 *)zp >> 8 : *(u16 *)zp; if (!cmpf(g_zfunc, (f32)zi, (f32)old)) continue; }
            }
            f32 w = 1 / (wr + wx * px);
            RGBA R[16]; { RGBA zz = {0, 0, 0, 0}; R[0] = zz; R[6] = zz; R[7] = zz; R[8] = zz; R[9] = zz; R[10] = zz; R[11] = zz; R[12] = zz; R[13] = zz; }
            *(v4 *)&R[4] = (qr[0] + qx[0] * px) * w; *(v4 *)&R[5] = (qr[1] + qx[1] * px) * w;
            R[3] = g_fogc;
            for (u32 i = 0; i < 4; i++) if (g_tex[i].ok) {
                v4 T = (qr[tq[i]] + qx[tq[i]] * px) * w; f32 s = T[0], t = T[1], q = T[3], o[4];
                if (g_tmode[i] == 4) { R[8 + i].r = cl01(s); R[8 + i].g = cl01(t); R[8 + i].b = cl01(T[2]); R[8 + i].a = cl01(q); continue; }
                if (q != 0 && q != 1) { f32 iq = 1 / q; s *= iq; t *= iq; }
                sample(rs, i, s, t, o);
                R[8 + i].r = o[0]; R[8 + i].g = o[1]; R[8 + i].b = o[2]; R[8 + i].a = o[3];
            }
            R[12].a = R[8].a;   /* R0.a starts as T0.a */
            f32 col[4]; combine(rs, R, col);
            if (g_alpha_en && !cmpf(g_alpha_func, (f32)(u32)(col[3] * 255 + 0.5f), (f32)g_alpha_ref)) continue;
            u8 *cp = cptr(rs, x, y); if (!ok_ptr(cp, 4)) continue;
            v4 sv = {col[0], col[1], col[2], col[3]}, dv4;
            u32 dv = g_s.cbpp == 4 ? *(u32 *)cp : 0;
            if (g_s.cbpp == 4) dv4 = (v4){(f32)((dv >> 16) & 255), (f32)((dv >> 8) & 255), (f32)(dv & 255), (f32)(dv >> 24)} * (1 / 255.0f);
            else { u32 p = *(u16 *)cp; dv4 = (v4){((p >> 11) & 31) / 31.0f, ((p >> 5) & 63) / 63.0f, (p & 31) / 31.0f, 1}; }
            if (g_blend) {
                v4 fs = bfac_v(g_sf, sv, dv4, bcv), fd = bfac_v(g_df, sv, dv4, bcv), r;
                switch (g_beq) { case 0x800A: r = sv * fs - dv4 * fd; break; case 0x800B: r = dv4 * fd - sv * fs; break; case 0x8007: r = vmin(sv, dv4); break; case 0x8008: r = vmax(sv, dv4); break; default: r = sv * fs + dv4 * fd; }
                sv = vmin(vmax(r, splat(0)), splat(1));
            }
            if (g_s.cbpp == 4) {
                v4 o = sv * 255.0f + 0.5f;
                u32 nv = (u32)o[3] << 24 | (u32)o[0] << 16 | (u32)o[1] << 8 | (u32)o[2];
                *(u32 *)cp = (dv & ~cmask32) | (nv & cmask32);
            } else if (g_cmask & 0x00010101) *(u16 *)cp = (u16)((u32)(sv[0] * 31 + 0.5f) << 11 | (u32)(sv[1] * 63 + 0.5f) << 5 | (u32)(sv[2] * 31 + 0.5f));
            if (g_zwrite && zp) { if (g_s.zbpp == 4) *(u32 *)zp = (zi << 8) | (*(u32 *)zp & 0xFF); else *(u16 *)zp = (u16)zi; }
            g_pix_k[kk * 16]++;
        }
    }
    (void)qidx;
}
/* ---------------- methods ---------------- */
static void set_attr(u32 i, f32 x, f32 y, f32 z, f32 w) { g_attr[i].x = x; g_attr[i].y = y; g_attr[i].z = z; g_attr[i].w = w; }
static void emit_inline(void) { V4 in[16]; for (u32 i = 0; i < 16; i++) in[i] = g_attr[i]; emit_vertex(in); }
static f32 g_tmp[4];
/* called for every 3D-object method after kel[] has the new value; m = method address */
void render_method(u32 m, u32 v) {
    if (m >= 0x0B00 && m < 0x0B80) { u32 k = (m - 0x0B00) / 4; g_dec_dirty = 1; g_prog_dirty = 1; if (g_pload < 136) g_prog[g_pload][k & 3] = v; if ((k & 3) == 3) g_pload++; return; }
    if (m >= 0x0B80 && m < 0x0C00) { u32 k = (m - 0x0B80) / 4; if (g_cload < 192) { ((u32 *)&g_c[g_cload])[k & 3] = v;
        if ((k & 3) == 3) { extern u32 g_eye_swap; extern float g_eye_follow[3], g_eye_follow_prev[3], g_eye_ours[3]; if (g_eye_swap) { f32 *q = (f32 *)&g_c[g_cload];
            f32 dx = q[0] - g_eye_follow[0], dy = q[1] - g_eye_follow[1], dz = q[2] - g_eye_follow[2], ex = q[0] - g_eye_follow_prev[0], ey = q[1] - g_eye_follow_prev[1], ez = q[2] - g_eye_follow_prev[2];
            if (dx * dx + dy * dy + dz * dz < 0.0025f || ex * ex + ey * ey + ez * ez < 0.0025f) { q[0] = g_eye_ours[0]; q[1] = g_eye_ours[1]; q[2] = g_eye_ours[2]; } } } if (g_cload < g_cmin) g_cmin = g_cload; if (g_cload > g_cmax) g_cmax = g_cload;
        if ((k & 3) == 3 && g_cload >= 3) { extern void vp_watch(const float *m); vp_watch((const float *)&g_c[g_cload - 3]); } }   /* Milestone 97: catch the camera's view-projection as it is uploaded (input.c) */
        if ((k & 3) == 3) g_cload++; return; }
    switch (m) {
    case 0x1E9C: g_pload = v; return;
    case 0x1EA0: g_pstart = v; g_dec_dirty = 1; g_prog_dirty = 1; return;
    case 0x1EA4: g_cload = v; return;
    case 0x17FC:   /* SET_BEGIN_END */
        if (v) { g_prim = v; g_nv = 0; g_npidx = 0; g_in_begin = 1; g_ian = 0; state_setup();
            if (g_dec_dirty) vsh_decode();
            g_gvsh = g_gl && (K(0x1E94) & 3) == 2 && !g_vsh_wc && !g_cpuvertex; g_ngi = 0; g_ngm = 0; }
        else { if (g_in_begin) { if (g_gvsh) { gl_flush_idx(); gl_flush_imm(); } else { flush_pending(); assemble(1); } } g_in_begin = 0; g_state_ok = 0; }
        return;
    case 0x1810: { u32 s = v & 0xFFFFFF, n = (v >> 24) + 1; for (u32 i = 0; i < n; i++) emit_index(s + i); return; }   /* DRAW_ARRAYS */
    case 0x1800: emit_index(v & 0xFFFF); emit_index(v >> 16); return;                                                    /* ARRAY_ELEMENT16 */
    case 0x1808: emit_index(v); return;                                                                                   /* ARRAY_ELEMENT32 */
    case 0x1818: inline_word(v); return;                                                                                  /* INLINE_ARRAY */
    case 0x1500: case 0x1504: g_tmp[(m - 0x1500) / 4] = fl(v); return;
    case 0x1508: set_attr(0, g_tmp[0], g_tmp[1], fl(v), 1); emit_inline(); return;                                      /* SET_VERTEX3F */
    case 0x1518: case 0x151C: case 0x1520: g_tmp[(m - 0x1518) / 4] = fl(v); return;
    case 0x1524: set_attr(0, g_tmp[0], g_tmp[1], g_tmp[2], fl(v)); emit_inline(); return;                               /* SET_VERTEX4F */
    }
    if (m >= 0x1880 && m < 0x1900) { u32 i = (m - 0x1880) / 8; if (m & 4) { set_attr(i, fl(K(m - 4)), fl(v), 0, 1); if (!i) emit_inline(); } return; }     /* DATA2F */
    if (m >= 0x1900 && m < 0x1940) { u32 i = (m - 0x1900) / 4; set_attr(i, (s16)(v & 0xFFFF), (s16)(v >> 16), 0, 1); if (!i) emit_inline(); return; }      /* DATA2S */
    if (m >= 0x1940 && m < 0x1980) { u32 i = (m - 0x1940) / 4; set_attr(i, (v & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, (v >> 24) / 255.0f); if (!i) emit_inline(); return; }   /* DATA4UB */
    if (m >= 0x1980 && m < 0x1A00) { u32 i = (m - 0x1980) / 8; if (m & 4) { u32 a = K(m - 4); set_attr(i, (s16)(a & 0xFFFF), (s16)(a >> 16), (s16)(v & 0xFFFF), (s16)(v >> 16)); if (!i) emit_inline(); } return; }   /* DATA4S */
    if (m >= 0x1A00 && m < 0x1B00) { u32 i = (m - 0x1A00) / 16; if ((m & 0xC) == 0xC) { set_attr(i, fl(K(m - 12)), fl(K(m - 8)), fl(K(m - 4)), fl(v)); if (!i) emit_inline(); } return; }   /* DATA4F */
}
void render_init(void) { for (u32 i = 0; i < 16; i++) set_attr(i, 0, 0, 0, 1); set_attr(3, 1, 1, 1, 1); }

/* benchmark (Milestone 77): with loader\bench.txt present, 40 full-screen triangles with the title screen's state
   (DXT3 texture, 2 combiner stages, alpha test, blending) are drawn into spare memory on one thread and timed */
IMP DWORD WINAPI GetFileAttributesA(const char *);
void render_bench(void) { RS *rs = &g_cur;
    if (GetFileAttributesA("bench.txt") == 0xFFFFFFFF) return;
    static const u32 st[][2] = {{0x200, 0x02800000}, {0x204, 0x01E00000}, {0x208, 0x128}, {0x20C, 0x0A000A00}, {0x210, 0x01000000}, {0x214, 0x01200000},
        {0x260, 0xD4D81010}, {0xAC0, 0xC4C80000}, {0xAC4, 0x2CC120CC}, {0xAA0, 0xC0}, {0x1E40, 0xC0}, {0x1E44, 0xC00}, {0x1E60, 0x11102}, {0x288, 0xC}, {0x28C, 0x1C80},
        {0x300, 1}, {0x33C, 0x206}, {0x340, 8}, {0x304, 1}, {0x344, 0x302}, {0x348, 0x303}, {0x350, 0x8006}, {0x358, 0x01010101}, {0x30C, 1}, {0x354, 0x207}, {0x35C, 1},
        {0x1E70, 1}, {0x1B00, 0x01400000}, {0x1B04, 0x09810E2A}, {0x1B08, 0x00030303}, {0x1B0C, 0x40000000}, {0x1B14, 0x02022000}};
    u32 save[64]; for (u32 i = 0; i < sizeof st / sizeof st[0]; i++) { save[i] = K(st[i][0]); K(st[i][0]) = st[i][1]; }
    for (u32 i = 0; i < 0x40000; i++) ((u32 *)PHYS(0x01400000))[i] = i * 2654435761u;
    state_setup();
    OV v[4]; memset_(v, 0, sizeof v);
    f32 xy[4][2] = {{0, 0}, {640, 0}, {640, 480}, {0, 480}};
    for (u32 i = 0; i < 4; i++) { v[i].x = xy[i][0]; v[i].y = xy[i][1]; v[i].z = 100; v[i].w = 1; v[i].v[0] = v[i].v[1] = v[i].v[2] = 1; v[i].v[3] = 0.8f; v[i].v[9] = xy[i][0] / 640; v[i].v[10] = xy[i][1] / 480; v[i].v[12] = 1; }
    s64 t0, t1, f; QueryPerformanceCounter(&t0);
    u32 p0 = g_pix_k[0];
    for (u32 r = 0; r < 20; r++) { raster(rs, &v[0], &v[1], &v[2], 0, 1); raster(rs, &v[0], &v[2], &v[3], 0, 1); }
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f);
    u32 us = (u32)((t1 - t0) * 1000000 / f), px = g_pix_k[0] - p0;
    out("  [bench] "); dec(px); out(" pixels in "); dec(us / 1000); out(" ms = "); dec(us ? (u32)((u64)px * 1000 / us) : 0); out(" thousand pixels/s (one thread)\r\n");
    {   volatile f32 sink; f32 x = 1; s64 a0, a1; QueryPerformanceCounter(&a0);
        for (u32 i = 0; i < 50000000; i++) x = x * 0.9999999f + 0.5f;
        sink = x; QueryPerformanceCounter(&a1); out("  [bench] 50M dependent float ops: "); dec((u32)((a1 - a0) * 1000 / f)); out(" ms\r\n");
        volatile u32 *m = (volatile u32 *)PHYS(0x01000000); QueryPerformanceCounter(&a0);
        for (u32 r = 0; r < 20; r++) for (u32 i = 0; i < 0x40000; i++) m[i] = m[i] + 1;
        QueryPerformanceCounter(&a1); out("  [bench] 20 x 1 MB read-modify-write of contiguous memory: "); dec((u32)((a1 - a0) * 1000 / f)); out(" ms\r\n");
        u32 *hm = HeapAlloc(GetProcessHeap(), 0, 0x100000); volatile u32 *h = hm; QueryPerformanceCounter(&a0);
        for (u32 r = 0; r < 20; r++) for (u32 i = 0; i < 0x40000; i++) h[i] = h[i] + 1;
        QueryPerformanceCounter(&a1); out("  [bench] the same on heap memory: "); dec((u32)((a1 - a0) * 1000 / f)); out(" ms\r\n"); HeapFree(GetProcessHeap(), 0, hm); }
    for (u32 i = 0; i < sizeof st / sizeof st[0]; i++) K(st[i][0]) = save[i];
    g_pix_k[0] = p0; g_state_ok = 0;
}
IMP BOOL WINAPI QueryThreadCycleTime(HANDLE, u64 *); IMP HANDLE WINAPI GetCurrentThread(void);
/* start-up speed probe (Milestone 77): times 20M dependent float ops; used to find what slowed the process down */
void speed_probe(const char *where) {
    if (GetFileAttributesA("bench.txt") == 0xFFFFFFFF) return;
    volatile f32 sink; f32 x = 1; s64 a0, a1, f; u64 c0, c1; QueryPerformanceFrequency(&f);
    QueryThreadCycleTime(GetCurrentThread(), &c0); QueryPerformanceCounter(&a0);
    for (u32 i = 0; i < 20000000; i++) x = x * 0.9999999f + 0.5f;
    sink = x; QueryPerformanceCounter(&a1); QueryThreadCycleTime(GetCurrentThread(), &c1);
    out("  [speed] "); out(where); out(": "); dec((u32)((a1 - a0) * 1000 / f)); out(" ms, "); dec((u32)((c1 - c0) / 1000000)); out(" M cycles on this thread (about 40 ms / 150 M is normal)");
    {   s64 s0, prev, now, mx = 0; u32 ngap = 0; QueryPerformanceCounter(&s0); prev = s0;   /* 300 ms of timer reads: the longest gap shows whether the thread was paused */
        do { QueryPerformanceCounter(&now); s64 d = now - prev; if (d > mx) mx = d; if (d * 1000 > f) ngap++; prev = now; } while ((now - s0) * 1000 < 300 * f);
        out(", longest gap in 300 ms: "); dec((u32)(mx * 1000000 / f)); out(" us, gaps over 1 ms: "); dec(ngap); out("\r\n"); }
}
/* draws everything queued (gpu.c calls this at fences, NOP interrupts and the end of each push-buffer pass) */
void render_flush(void) { flush_tris(); }
void render_worker_report(void) { out("  [render] vertex batches: parallel "); dec(g_vb_par); out(" ("); dec((u32)(g_vb_parc / 3800000)); out(" ms), serial "); dec(g_vb_ser); out(" ("); dec((u32)(g_vb_serc / 3800000)); out(" ms), hashing "); dec((u32)(g_vb_hashc / 3800000)); out(" ms, copying "); dec((u32)(g_vb_copyc / 3800000)); out(" ms\r\n"); out("  [render] busy ms per thread:"); for (u32 k = 0; k < NW; k++) { out(" "); dec((u32)(g_wk_cyc[k * 8] / 3800000)); } out("\r\n"); }

/* Sound (Milestone 80): the game's DirectSound calls are replaced (high-level emulation). The wrappers
 * (IDirectSoundBuffer_Play etc., addresses from platform_boundary.csv) jump here; buffers and streams become
 * voices of a software mixer that plays through the PC's waveOut at 48 kHz stereo.
 * Covered: PCM 8/16-bit and Xbox ADPCM, volume, pitch/frequency, play regions, looping, pause, simple 3D
 * (distance falloff and pan from the listener), streams (XMediaObject Process, packet completion).
 * Not yet: envelopes, mix bins and effects (reverb), WMA music. */
#include "util.h"

#define NTAPI __stdcall
typedef float f32; typedef short s16;
IMP void WINAPI ExitThread(DWORD);
typedef struct { u16 tag, ch; u32 rate, avg; u16 align, bits, cb; } WFX;
typedef struct { u32 size, flags, bytes; WFX *fmt; void *mixbins; u32 inbin; } DSBDESC;
typedef struct { u32 flags, maxpackets; WFX *fmt; void (__stdcall *cb)(void *, void *, u32); void *ctx; void *mixbins; } DSSDESC;
typedef struct { void *buf; u32 max; u32 *done; u32 *status; void *ev_or_ctx; void *ts; } XPKT;

/* trace of the last calls (name, caller), dumped when the game jumps to 0 */
static const char *g_trn[32]; static void *g_trr[32]; static volatile u32 g_trc;
#define TR(n) { u32 i_ = __sync_fetch_and_add(&g_trc, 1) & 31; g_trn[i_] = n; g_trr[i_] = __builtin_return_address(0); }
void dsound_trace_dump(void) { out("  [sound] last DirectSound calls:"); for (u32 i = 0; i < 32; i++) { u32 j = (g_trc + i) & 31; if (g_trn[j]) { out(" "); out(g_trn[j]); out("@"); hex((u32)g_trr[j]); } } out("\r\n"); }
static volatile u32 g_lock;
static void lock(void) { while (__sync_val_compare_and_swap(&g_lock, 0, 1)) __builtin_ia32_pause(); }
static void unlock(void) { __sync_lock_release(&g_lock); }

/* ---- voices ---- */
#define MAXV 256
#define MAXPK 64
typedef struct VOICE {
    u32 magic, used, stream, refs;
    WFX fmt; u8 *data; u32 bytes, rstart, rlen;          /* buffer data, play region (bytes) */
    u32 playing, looping, paused;
    double pos;                                           /* position in frames, from the region start */
    f32 vol, rate; s32 pitch; int haspitch;
    u32 mode3d; f32 px, py, pz, mind, maxd;
    /* streams */
    XPKT pk[MAXPK]; u32 npk, pkpos, maxpk; void (__stdcall *cb)(void *, void *, u32); void *ctx;
    void *vtbl;                                           /* streams: XMediaObject methods (the object starts here for the game) */
    s16 dec[130]; u32 decblk; u8 *decsrc;
    u8 *own;                                              /* memory the buffer was created with */
    u32 ngp, nlock, nplay;                                /* diagnostics: GetCurrentPosition, Lock, Play calls */                 /* ADPCM: the decoded block */
} VOICE;
static VOICE g_v[MAXV];
static f32 g_lx, g_ly, g_lz, g_fx = 0, g_fy = 0, g_fz = 1, g_tx = 0, g_ty = 1, g_tz = 0;
static u32 g_dsdev[16];
u32 g_snd_buffers, g_snd_plays, g_snd_streams, g_snd_packets, g_snd_peak;
void dsound_report(void) { u32 nv = 0, np = 0; for (u32 i = 0; i < MAXV; i++) if (g_v[i].used) { nv++; if (g_v[i].playing) np++; }
    out("  [sound] buffers "); dec(g_snd_buffers); out(", plays "); dec(g_snd_plays); out(", streams "); dec(g_snd_streams); out(", packets done "); dec(g_snd_packets); out(", voices "); dec(nv); out(" ("); dec(np); out(" playing), peak "); dec(g_snd_peak);
    { extern u32 g_flips_total; static u32 lf; out(", flips +"); dec(g_flips_total - lf); lf = g_flips_total; }
    { extern u64 g_mix_cyc; extern u32 g_mix_blocks; static u32 lb; static u64 lc; out(", mixed blocks +"); dec(g_mix_blocks - lb); out(" mix ms "); dec((u32)((g_mix_cyc - lc) / 3800000)); { extern u64 g_mix_max, g_sleep_max; out(" longest mix ms "); dec((u32)(g_mix_max / 3800000)); out(" longest sleep ms "); dec((u32)(g_sleep_max / 3800000)); g_mix_max = g_sleep_max = 0;
      extern u64 g_can_max[2]; out(" canary spin/sleep ms "); dec((u32)(g_can_max[0] / 3800000)); out("/"); dec((u32)(g_can_max[1] / 3800000)); g_can_max[0] = g_can_max[1] = 0; } lb = g_mix_blocks; lc = g_mix_cyc;
      { u32 audio_fill(void); out(" ring "); dec(audio_fill()); } }
    for (u32 i = 0; i < MAXV; i++) { VOICE *v = &g_v[i]; if (v->used && !v->stream && v->playing && v->nlock) { out(" | v"); dec(i); out(" r"); dec(v->fmt.rate); out("/"); dec((u32)v->rate); out(" c"); dec(v->fmt.ch); out(" t"); hex(v->fmt.tag); out(" b"); dec(v->bytes); out(" p"); dec((u32)v->pos); out(" gp"); dec(v->ngp); out(" lk"); dec(v->nlock); out(" pl"); dec(v->nplay); out(v->looping ? " L" : " 1"); v->ngp = v->nlock = v->nplay = 0; } }
    out("\r\n"); g_snd_peak = 0; }

static VOICE *vo(void *p) { VOICE *v = p; return (v >= g_v && v < g_v + MAXV && v->magic == 0x56534642) ? v : 0; }
static VOICE *vs(void *p) { return vo((u8 *)p - __builtin_offsetof(VOICE, vtbl)); }   /* streams are handed out as &v->vtbl */
static VOICE *new_voice(void) {
    lock();
    for (u32 i = 0; i < MAXV; i++) if (!g_v[i].used) { VOICE *v = &g_v[i]; memset_(v, 0, sizeof *v); v->magic = 0x56534642; v->used = 1; v->refs = 1; v->vol = 1; v->mind = 1; v->maxd = 1e9f; v->decblk = 0xFFFFFFFF; unlock(); return v; }
    unlock(); return 0;
}
static f32 db(s32 mb) { if (mb <= -10000) return 0; f32 x = mb / 2000.0f; f32 r = 1, b = 10; /* 10^x */
    f32 e = x * 3.321928f; s32 ip = (s32)e; if ((f32)ip > e) ip--; f32 fr = e - ip; f32 p2 = 1 + fr * (0.6931472f + fr * (0.2402265f + fr * (0.0555041f + fr * 0.0096181f)));
    union { u32 u; f32 f; } s; s.u = (u32)(ip + 127) << 23; (void)r; (void)b; return p2 * s.f; }
static f32 exp2f_(f32 e) { s32 ip = (s32)e; if ((f32)ip > e) ip--; f32 fr = e - ip; f32 p2 = 1 + fr * (0.6931472f + fr * (0.2402265f + fr * (0.0555041f + fr * 0.0096181f))); union { u32 u; f32 f; } s; s.u = (u32)(ip + 127) << 23; return p2 * s.f; }

/* ---- Xbox ADPCM (IMA, 36 bytes per channel per 64+1 samples) ---- */
static const s32 g_step[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const s32 g_idx[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static void adpcm_block(const u8 *b, u32 ch, s16 *out) {   /* decodes one block of `ch` channels into 65 interleaved frames */
    s32 pred[2], idx[2];
    for (u32 c = 0; c < ch; c++) { pred[c] = (s16)(b[c * 4] | b[c * 4 + 1] << 8); idx[c] = b[c * 4 + 2]; if (idx[c] > 88) idx[c] = 88; out[c] = (s16)pred[c]; }
    const u8 *d = b + 4 * ch;
    for (u32 g = 0; g < 8; g++)            /* 8 groups of 8 samples per channel */
        for (u32 c = 0; c < ch; c++) {
            const u8 *q = d + (g * ch + c) * 4;
            for (u32 k = 0; k < 8; k++) {
                u32 n = (q[k / 2] >> ((k & 1) * 4)) & 15; s32 st = g_step[idx[c]], diff = st >> 3;
                if (n & 1) diff += st >> 2; if (n & 2) diff += st >> 1; if (n & 4) diff += st; if (n & 8) diff = -diff;
                pred[c] += diff; if (pred[c] > 32767) pred[c] = 32767; if (pred[c] < -32768) pred[c] = -32768;
                idx[c] += g_idx[n]; if (idx[c] < 0) idx[c] = 0; if (idx[c] > 88) idx[c] = 88;
                out[(1 + g * 8 + k) * ch + c] = (s16)pred[c];
            }
        }
}
/* one frame of a voice's data at frame f (0..frames-1); returns 0 past the end */
static u32 frames_of(VOICE *v, u32 bytes) {
    if (v->fmt.tag == 0x69) { u32 ch = v->fmt.ch ? v->fmt.ch : 1; return bytes / (36 * ch) * 65; }
    u32 fb = (v->fmt.bits / 8) * (v->fmt.ch ? v->fmt.ch : 1); return fb ? bytes / fb : 0;
}
static void sample_at(VOICE *v, u8 *base, u32 f, f32 *l, f32 *r) {
    u32 ch = v->fmt.ch ? v->fmt.ch : 1;
    if (v->fmt.tag == 0x69) {
        u32 blk = f / 65, i = f % 65; u8 *src = base + blk * 36 * ch;
        if (v->decblk != blk || v->decsrc != base) { adpcm_block(src, ch > 2 ? 2 : ch, v->dec); v->decblk = blk; v->decsrc = base; }
        *l = v->dec[i * (ch > 2 ? 2 : ch)] / 32768.0f; *r = ch >= 2 ? v->dec[i * 2 + 1] / 32768.0f : *l; return;
    }
    if (v->fmt.bits == 16) { s16 *p = (s16 *)base + f * ch; *l = p[0] / 32768.0f; *r = ch >= 2 ? p[1] / 32768.0f : *l; }
    else { u8 *p = base + f * ch; *l = (p[0] - 128) / 128.0f; *r = ch >= 2 ? (p[1] - 128) / 128.0f : *l; }
}
static f32 sqrt_(f32 x) { f32 r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }

/* ---- stream packet completion: finished packets are handed to the ticker thread (game code needs its KPCR) ---- */
typedef struct { void (__stdcall *cb)(void *, void *, u32); void *ctx, *pctx; } DONE;
static DONE g_done[1024]; static volatile u32 g_dw, g_dr;
static void complete_packet(VOICE *v, XPKT *p, u32 status) {
    if (p->done) *p->done = p->max;
    if (p->status) *p->status = status;
    if (v->cb) { u32 w = g_dw; if (w - g_dr < 1024) { g_done[w & 1023].cb = v->cb; g_done[w & 1023].ctx = v->ctx; g_done[w & 1023].pctx = p->ev_or_ctx; __sync_synchronize(); g_dw = w + 1; } }
    else if (p->ev_or_ctx) SetEvent(p->ev_or_ctx);
    g_snd_packets++;
}
void dsound_tick(void) {   /* called by the ticker */
    while (g_dr != g_dw) { DONE d = g_done[g_dr & 1023]; g_dr++; d.cb(d.ctx, d.pctx, 0); }
}

/* ---- the mixer ---- */
#define RATE 48000
#define BLOCK 960   /* 20 ms */
static void mix(s16 *out, u32 n) {
    static f32 acc[BLOCK * 2];
    memset_(acc, 0, n * 8);
    f32 rx = g_ty * g_fz - g_tz * g_fy, ry = g_tz * g_fx - g_tx * g_fz, rz = g_tx * g_fy - g_ty * g_fx;   /* listener right = top x front */
    /* no lock while mixing (Milestone 80: holding it made every DirectSound call of the game wait for a whole block);
       only stream packet queues are locked, briefly */
    for (u32 i = 0; i < MAXV; i++) {
        VOICE *v = &g_v[i];
        if (!v->used || !v->playing || v->paused) continue;
        f32 gl = v->vol, gr = v->vol;
        if (v->mode3d != 2) {
            f32 dx = v->px, dy = v->py, dz = v->pz; if (v->mode3d == 0) { dx -= g_lx; dy -= g_ly; dz -= g_lz; }
            f32 d = sqrt_(dx * dx + dy * dy + dz * dz), g = d <= v->mind ? 1 : v->mind / (d < v->maxd ? d : v->maxd);
            f32 pan = d > 1e-3f ? (dx * rx + dy * ry + dz * rz) / d : 0; if (pan > 1) pan = 1; if (pan < -1) pan = -1;
            gl *= g * sqrt_(0.5f * (1 - pan)) * 1.4142f; gr *= g * sqrt_(0.5f * (1 + pan)) * 1.4142f;
        }
        f32 rate = v->rate > 0 ? v->rate : (f32)v->fmt.rate; f32 step = rate / RATE;
        for (u32 k = 0; k < n; k++) {
            u8 *base; u32 frames;
            if (v->stream) {
                if (!v->npk) { break; }
                XPKT *p = &v->pk[0]; base = p->buf; frames = frames_of(v, p->max);
                if ((u32)v->pos >= frames) {
                    lock();
                    if (v->npk) { complete_packet(v, &v->pk[0], 0); for (u32 j = 1; j < v->npk; j++) v->pk[j - 1] = v->pk[j]; v->npk--; }
                    unlock();
                    v->pos -= frames; v->decblk = 0xFFFFFFFF;
                    k--; continue;
                }
            } else {
                if (!v->data) { v->playing = 0; break; }
                base = v->data + v->rstart; frames = frames_of(v, v->rlen ? v->rlen : v->bytes - v->rstart);
                if ((u32)v->pos >= frames) { if (v->looping && frames) { while ((u32)v->pos >= frames) v->pos -= frames; } else { v->playing = 0; break; } }
            }
            f32 l, r; sample_at(v, base, (u32)v->pos, &l, &r);
            acc[k * 2] += l * gl; acc[k * 2 + 1] += r * gr;
            v->pos += step;
        }
    }
    /* soft limiter (Milestone 85): Bink movies play 4 speaker tracks at full volume, which summed into stereo used to
       clip hard; above 0.75 the level bends smoothly toward 1.0 instead */
    for (u32 k = 0; k < n * 2; k++) { f32 x = acc[k] * 0.7f, ax = x < 0 ? -x : x;
        if (ax > 0.75f) { f32 u = (ax - 0.75f) * 4.0f; ax = 0.75f + 0.25f * u / (1.0f + u); x = x < 0 ? -ax : ax; }
        f32 s = x * 32767.0f; if (s > 32767) s = 32767; if (s < -32768) s = -32768; out[k] = (s16)s; u32 a = s < 0 ? (u32)-s : (u32)s; if (a > g_snd_peak) g_snd_peak = a; }
}
#include "bfgl.h"
IMP HANDLE WINAPI CreateFileMappingA(HANDLE, void *, DWORD, DWORD, DWORD, const char *);
IMP void *WINAPI MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, u32);
static AUDIO *g_au;
u64 g_mix_cyc, g_mix_max, g_sleep_max; u32 g_mix_blocks;   /* diagnostics: mixer time (TSC) and 20 ms blocks mixed */
u32 audio_fill(void) { return g_au ? (g_au->wpos - g_au->rpos) | (g_au->playing ? 0x80000000u : 0) : 0; }
/* the mixer keeps ~100 ms of mixed sound in the shared ring; bfview.exe plays it (a waveOut device opened inside
   this process failed: Windows' audio stack does not like the XBE in place of the main image) */
IMP BOOL WINAPI SetThreadPriority(HANDLE, int);
IMP HANDLE WINAPI GetCurrentThread(void);
IMP HANDLE WINAPI CreateEventA(void *, BOOL, BOOL, const char *);
IMP DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
/* Milestone 86: the viewer signals this event whenever it takes sound from the ring, so the mixer refills at once.
   (The 1.5 s sound gaps first blamed on this wait were the whole process being suspended by PC XInput checks.) */
static HANDLE g_au_ev;
/* canaries (Milestone 86 diagnosis): do nothing but measure their own longest gap */
u64 g_can_max[2];
static DWORD __stdcall canary(void *p) { u32 k = (u32)p; if (k == 0) SetThreadPriority(GetCurrentThread(), 15);
    IMP BOOL WINAPI SwitchToThread(void); u64 l = __builtin_ia32_rdtsc();
    for (;;) { if (k == 0) { for (u32 q = 0; q < 64; q++) __builtin_ia32_pause(); SwitchToThread(); { extern u32 *gpu_pad(void); u32 *pd = gpu_pad(); if (pd) pd[20]++; } } else Sleep(1);
        u64 c = __builtin_ia32_rdtsc(); if (c - l > g_can_max[k]) g_can_max[k] = c - l; l = c; } }
static DWORD __stdcall mixer(void *p) {
    static s16 blk[BLOCK * 2];
    SetThreadPriority(GetCurrentThread(), 15);   /* Milestone 86: at normal priority its Sleep(5) sometimes lasted ~1.5 s: 1.5 s of silence every few seconds */
    for (;;) {
        while (g_au->wpos - g_au->rpos < 4800) {
            u64 c0 = __builtin_ia32_rdtsc(); mix(blk, BLOCK); u64 dc = __builtin_ia32_rdtsc() - c0; g_mix_cyc += dc; g_mix_blocks++; if (dc > g_mix_max) g_mix_max = dc;
            for (u32 i = 0; i < BLOCK; i++) { u32 w = (g_au->wpos + i) % AUDIO_FRAMES; g_au->pcm[w * 2] = blk[i * 2]; g_au->pcm[w * 2 + 1] = blk[i * 2 + 1]; }
            __sync_synchronize(); g_au->wpos += BLOCK;
        }
        { u64 s0 = __builtin_ia32_rdtsc(); u32 wr = WaitForSingleObject(g_au_ev, 5);   /* woken by the viewer when it takes sound */ u64 ds = __builtin_ia32_rdtsc() - s0; if (ds > g_sleep_max) g_sleep_max = ds;
          if (ds > 380000000ull) { static u32 nl; if (nl++ < 20) { out("  [sound] mixer stalled "); dec((u32)(ds / 3800000)); out(" ms, wait result "); hex(wr); out(", ending at tick "); dec(GetTickCount()); out("\r\n"); } } }
        /* no sound device in the viewer: consume in real time anyway, so streams keep completing their packets */
        { static u32 t0; u32 t = GetTickCount(); if (!g_au->playing) { if (t - t0 >= 20) { t0 = t; if (g_au->wpos - g_au->rpos >= BLOCK) g_au->rpos += BLOCK; } } }
    }
}
/* ---- DirectSound (device) ---- */
static s32 NTAPI DirectSoundCreate_(void *guid, void **pp, void *unk) { TR("DirectSoundCreate_"); *pp = g_dsdev; return 0; }
static s32 NTAPI DS_CommitDeferredSettings(void *ds) { return 0; }
static u32 NTAPI DS_Release(void *ds) { TR("DS_Release"); return 1; }
static s32 NTAPI DS_GetCaps(void *ds, u32 *caps) { TR("DS_GetCaps"); caps[0] = 192; caps[1] = 64; caps[2] = 2047; caps[3] = 0; return 0;   /* Xbox DSCAPS: free 2D voices, free 3D voices, free buffer SGEs, memory allocated (16 bytes only!) */ }
static s32 NTAPI DS_DownloadEffectsImage(void *ds, void *img, u32 size, void *loc, void **desc) { TR("DS_DownloadEffectsImage"); static u32 zero[16]; if (desc) *desc = zero; return 0; }
static void NTAPI DirectSoundDoWork_(void) { TR("DirectSoundDoWork_"); }
static void NTAPI DirectSoundUseFullHRTF_(void) { TR("DirectSoundUseFullHRTF_"); }
static s32 NTAPI DS_SetPosition(void *ds, f32 x, f32 y, f32 z, u32 apply) { TR("DS_SetPosition"); g_lx = x; g_ly = y; g_lz = z; return 0; }
static s32 NTAPI DS_SetOrientation(void *ds, f32 fx, f32 fy, f32 fz, f32 tx, f32 ty, f32 tz, u32 apply) { TR("DS_SetOrientation"); g_fx = fx; g_fy = fy; g_fz = fz; g_tx = tx; g_ty = ty; g_tz = tz; return 0; }
static s32 NTAPI DS_SetDistanceFactor(void *ds, f32 f, u32 apply) { TR("DS_SetDistanceFactor"); return 0; }
static s32 NTAPI DS_SetI3DL2Listener(void *ds, void *p, u32 apply) { TR("DS_SetI3DL2Listener"); return 0; }

/* ---- buffers ---- */
static s32 NTAPI DirectSoundCreateBuffer_(DSBDESC *d, void **pp) { TR("DirectSoundCreateBuffer_");
    VOICE *v = new_voice(); if (!v) { *pp = 0; return 0x8007000E; }
    if (d && d->fmt) v->fmt = *d->fmt; v->bytes = d ? d->bytes : 0;
    if (v->bytes) { v->own = HeapAlloc(GetProcessHeap(), 8, v->bytes); v->data = v->own; }   /* its own memory, unless SetBufferData points it elsewhere */
    v->mode3d = d && (d->flags & 0x10 /* DSBCAPS_CTRL3D */) ? 0 : 2;
    g_snd_buffers++; *pp = v; return 0;
}
static s32 NTAPI DS_CreateSoundBuffer(void *ds, DSBDESC *d, void **pp, void *unk) { TR("DS_CreateSoundBuffer"); return DirectSoundCreateBuffer_(d, pp); }   /* Bink uses this one */
static u32 NTAPI B_Release(void *b) { TR("B_Release"); VOICE *v = vo(b); if (!v) return 0; lock(); u32 r = --v->refs; if (!r) { if (v->own) HeapFree(GetProcessHeap(), 0, v->own); v->used = 0; v->magic = 0; } unlock(); return r; }
static s32 NTAPI B_SetBufferData(void *b, void *data, u32 bytes) { TR("B_SetBufferData"); VOICE *v = vo(b); if (v) { v->playing = 0; v->data = data; v->bytes = bytes; v->rstart = 0; v->rlen = 0; v->pos = 0; v->decblk = 0xFFFFFFFF; } return 0; }
static s32 NTAPI B_SetFormat(void *b, WFX *f) { TR("B_SetFormat"); VOICE *v = vo(b); if (v && f) { v->fmt = *f; v->decblk = 0xFFFFFFFF; } return 0; }
static s32 NTAPI B_SetPlayRegion(void *b, u32 start, u32 len) { TR("B_SetPlayRegion"); VOICE *v = vo(b); if (v) { v->rstart = start; v->rlen = len; } return 0; }
static s32 NTAPI B_SetFrequency(void *b, u32 f) { TR("B_SetFrequency"); VOICE *v = vo(b); if (v) v->rate = (f32)(f ? f : v->fmt.rate); return 0; }
static s32 NTAPI B_SetPitch(void *b, s32 p) { TR("B_SetPitch"); VOICE *v = vo(b); if (v) v->rate = 48000.0f * exp2f_(p / 4096.0f); return 0; }
static s32 NTAPI B_SetVolume(void *b, s32 vol) { TR("B_SetVolume"); VOICE *v = vo(b); if (v) v->vol = db(vol); return 0; }
/* Milestone 95: a ring of recent plays, dumped by input.c when the player's MK-ASLT fires or reloads (to find its sounds) */
typedef struct { u32 t, data, rstart, rlen, bytes, rate, ch, tag, bits, ra, vi; } PLAYREC;
static PLAYREC g_pr[64]; static u32 g_prn;
void snd_dump(const char *what, u32 back_ms) { u32 now = GetTickCount(); out("  [sndid] "); out(what); out("\r\n");
    for (u32 i = 0; i < 64; i++) { PLAYREC *r = &g_pr[(g_prn + i) % 64]; if (!r->t || now - r->t > back_ms) continue;
        out("    -"); dec(now - r->t); out("ms v"); dec(r->vi); out(" data "); hex(r->data); out(" region "); hex(r->rstart); out("+"); hex(r->rlen); out(" bytes "); hex(r->bytes);
        out(" fmt "); hex(r->tag); out(" "); dec(r->rate); out("Hz c"); dec(r->ch); out(" b"); dec(r->bits); out(" from "); hex(r->ra); out("\r\n"); } }
/* Milestone 95: the Razorback's sounds. The MK-ASLT's shots are two ADPCM regions of the weapon bank (0xBC8B0+0x1A28,
   0x82848+0x1680); when one plays, a single shot cut from razor_shots.wav plays instead, on one of 8 extra voices (with
   the game voice's volume and 3D position). The reload: razor_reload.wav when input.c sees the MK-ASLT start reloading;
   the game's reload sounds (0x12FC0+0xF0C, 0x83F10+0x1680) are muted for 3.5 s after that. norazorsound.txt = original. */
IMP DWORD WINAPI GetFileAttributesA(const char *);
static s16 *g_rz_shot, *g_rz_rel; static u32 g_rz_shotn, g_rz_reln, g_rz_snd = 2, g_rz_mute; static VOICE *g_rzv[8];
static s16 *wav_load(const char *name, u32 *frames) {   /* 16-bit stereo 48 kHz PCM; returns the samples */
    *frames = 0; HANDLE f = CreateFileA(name, 0x80000000, 1, 0, 3, 0x80, 0); if (f == (HANDLE)-1) return 0;
    static u8 hdr[256]; DWORD rd = 0; ReadFile(f, hdr, 256, &rd, 0); u32 at = 12, len = 0, ok = 0;
    while (at + 8 <= rd) { u32 cl = *(u32 *)(hdr + at + 4); if (hdr[at] == 'f' && hdr[at + 1] == 'm' && hdr[at + 2] == 't') { WFX *w = (WFX *)(hdr + at + 8); ok = w->tag == 1 && w->ch == 2 && w->rate == 48000 && w->bits == 16; }
        if (hdr[at] == 'd' && hdr[at + 1] == 'a' && hdr[at + 2] == 't' && hdr[at + 3] == 'a') { len = cl; at += 8; break; } at += 8 + cl; }
    if (!ok || !len) { CloseHandle(f); return 0; }
    s16 *p = HeapAlloc(GetProcessHeap(), 8, len); if (!p) { CloseHandle(f); return 0; }
    u32 have = rd > at ? rd - at : 0; if (have > len) have = len; for (u32 i = 0; i < have; i++) ((u8 *)p)[i] = hdr[at + i];
    if (have < len) { DWORD r2 = 0; ReadFile(f, (u8 *)p + have, len - have, &r2, 0); }
    CloseHandle(f); *frames = len / 4; return p; }
static void razor_snd_load(void) {
    if (g_rz_snd != 2) return; g_rz_snd = GetFileAttributesA("norazorsound.txt") == 0xFFFFFFFF; if (!g_rz_snd) return;
    u32 n = 0; s16 *s = wav_load("razor_shots.wav", &n);
    if (s && n > 48000) {   /* one shot: 0.370 s .. 0.670 s, 4 ms fade-in, fading out over the last 0.18 s, peak 0.9 */
        u32 a = 17760, len = 14400; if (a + len > n) len = n - a; g_rz_shot = HeapAlloc(GetProcessHeap(), 8, len * 4); s32 pk = 1;
        for (u32 i = 0; i < len * 2; i++) { s32 x = s[a * 2 + i]; if (x < 0) x = -x; if (x > pk) pk = x; }
        if (g_rz_shot) { for (u32 i = 0; i < len; i++) { f32 g = 0.9f * 32767.0f / pk; if (i < 192) g *= i / 192.0f; if (i > len - 8640) g *= (len - i) / 8640.0f;
            for (u32 c = 0; c < 2; c++) g_rz_shot[i * 2 + c] = (s16)(s[(a + i) * 2 + c] * g); } g_rz_shotn = len; }
        HeapFree(GetProcessHeap(), 0, s); }
    g_rz_rel = wav_load("razor_reload.wav", &g_rz_reln);
    for (int i = 0; i < 8; i++) { VOICE *v = new_voice(); if (!v) break; v->fmt.tag = 1; v->fmt.ch = 2; v->fmt.rate = 48000; v->fmt.bits = 16; v->fmt.align = 4; v->mode3d = 2; g_rzv[i] = v; }
    out("  [razor] sounds: shot frames "); dec(g_rz_shotn); out(", reload frames "); dec(g_rz_reln); out("\r\n"); }
static void razor_play(s16 *pcm, u32 frames, VOICE *like) {
    if (!pcm || !frames) return; VOICE *o = 0;
    for (int i = 0; i < 8; i++) if (g_rzv[i] && !g_rzv[i]->playing) { o = g_rzv[i]; break; }
    if (!o) { static u32 rr; o = g_rzv[rr++ & 7]; if (!o) return; }
    o->playing = 0; o->data = (u8 *)pcm; o->bytes = frames * 4; o->rstart = 0; o->rlen = 0; o->pos = 0; o->rate = 48000; o->looping = 0; o->paused = 0;
    if (like) { o->vol = like->vol; o->mode3d = like->mode3d; o->px = like->px; o->py = like->py; o->pz = like->pz; o->mind = like->mind; o->maxd = like->maxd; }
    else { o->vol = 1; o->mode3d = 2; }
    __sync_synchronize(); o->playing = 1; }
/* Milestone 101: the hit marker - hitmarker.wav (16-bit stereo 48 kHz) when the player's shot hits a character (input.c);
   its own 2 voices, not positioned (heard as the player's own). nohitsound.txt = off. */
static s16 *g_hm; static u32 g_hmn, g_hm_state = 2; static VOICE *g_hmv[2];
void snd_hitmarker(void) {
    if (g_hm_state == 2) { g_hm_state = GetFileAttributesA("nohitsound.txt") == 0xFFFFFFFF;
        if (g_hm_state) { g_hm = wav_load("hitmarker.wav", &g_hmn); if (!g_hm) g_hm_state = 0;
            for (int i = 0; i < 2 && g_hm_state; i++) { VOICE *v = new_voice(); if (!v) break; v->fmt.tag = 1; v->fmt.ch = 2; v->fmt.rate = 48000; v->fmt.bits = 16; v->fmt.align = 4; v->mode3d = 2; g_hmv[i] = v; } }
        out("  [hit] hit marker sound: "); if (g_hm_state) { dec(g_hmn); out(" frames\r\n"); } else out("off (no hitmarker.wav or nohitsound.txt)\r\n"); }
    if (!g_hm_state) return;
    static u32 rr; VOICE *o = g_hmv[rr++ & 1]; if (!o) o = g_hmv[0]; if (!o) return;
    o->playing = 0; o->data = (u8 *)g_hm; o->bytes = g_hmn * 4; o->rstart = 0; o->rlen = 0; o->pos = 0; o->rate = 48000; o->looping = 0; o->paused = 0; o->vol = 1; o->mode3d = 2;
    __sync_synchronize(); o->playing = 1; }
void snd_razor_reload(void) { razor_snd_load(); if (!g_rz_snd) return; g_rz_mute = GetTickCount() + 3500; razor_play(g_rz_rel, g_rz_reln, 0); }
static s32 NTAPI B_Play(void *b, u32 r1, u32 r2, u32 flags) { TR("B_Play"); VOICE *v = vo(b);
    if (v && v->fmt.tag == 0x69 && !(flags & 1)) { razor_snd_load();
        if (g_rz_snd && ((v->rstart == 0xBC8B0 && v->rlen == 0x1A28) || (v->rstart == 0x82848 && v->rlen == 0x1680) || (v->rstart == 0xFDE60 && v->rlen == 0x15F0))) { razor_play(g_rz_shot, g_rz_shotn, v); v->playing = 0; v->pos = 0; return 0; }
        if (g_rz_snd && (s32)(g_rz_mute - GetTickCount()) > 0 && ((v->rstart == 0x12FC0 && v->rlen == 0xF0C) || (v->rstart == 0x83F10 && v->rlen == 0x1680))) { v->playing = 0; v->pos = 0; return 0; } }
    if (v) { PLAYREC *r = &g_pr[g_prn++ % 64]; r->t = GetTickCount(); r->data = (u32)v->data; r->rstart = v->rstart; r->rlen = v->rlen; r->bytes = v->bytes; r->rate = v->fmt.rate; r->ch = v->fmt.ch; r->tag = v->fmt.tag; r->bits = v->fmt.bits; r->ra = (u32)__builtin_return_address(0); r->vi = (u32)(v - g_v); }
    if (v) { v->nplay++; if (!v->playing) v->pos = 0; v->looping = flags & 1; v->paused = 0; v->playing = 1; g_snd_plays++; } return 0; }
static s32 NTAPI B_Stop(void *b) { TR("B_Stop"); VOICE *v = vo(b); if (v) { v->playing = 0; v->pos = 0; } return 0; }
static s32 NTAPI B_StopEx(void *b, u32 t0, u32 t1, u32 flags) { TR("B_StopEx"); VOICE *v = vo(b); if (v) { v->playing = 0; v->pos = 0; } return 0; }
static s32 NTAPI B_Pause(void *b, u32 p) { TR("B_Pause"); VOICE *v = vo(b); if (v) v->paused = p == 1; return 0; }
static s32 NTAPI B_GetStatus(void *b, u32 *st) { TR("B_GetStatus"); VOICE *v = vo(b); *st = v && v->playing ? (1 | (v->looping ? 4 : 0)) : 0; return 0; }
static s32 NTAPI B_GetCurrentPosition(void *b, u32 *play, u32 *write) { TR("B_GetCurrentPosition");
    VOICE *v = vo(b); u32 p = 0;
    if (v) { v->ngp++; u32 ch = v->fmt.ch ? v->fmt.ch : 1; p = v->fmt.tag == 0x69 ? (u32)v->pos / 65 * 36 * ch : (u32)v->pos * (v->fmt.bits / 8) * ch; p += v->rstart; }
    if (play) *play = p; if (write) *write = p; return 0;
}
static s32 NTAPI B_SetMode(void *b, u32 mode, u32 apply) { TR("B_SetMode"); VOICE *v = vo(b); if (v) v->mode3d = mode; return 0; }
static s32 NTAPI B_SetPos(void *b, f32 x, f32 y, f32 z, u32 apply) { TR("B_SetPos"); VOICE *v = vo(b); if (v) { v->px = x; v->py = y; v->pz = z; } return 0; }
static s32 NTAPI B_SetVel(void *b, f32 x, f32 y, f32 z, u32 apply) { TR("B_SetVel"); return 0; }
static s32 NTAPI B_SetMinD(void *b, f32 d, u32 apply) { TR("B_SetMinD"); VOICE *v = vo(b); if (v) v->mind = d > 0 ? d : 1; return 0; }
static s32 NTAPI B_SetMaxD(void *b, f32 d, u32 apply) { TR("B_SetMaxD"); VOICE *v = vo(b); if (v) v->maxd = d > 0 ? d : 1e9f; return 0; }
static s32 NTAPI B_SetAll(void *b, u32 *p, u32 apply) { TR("B_SetAll");   /* DS3DBUFFER: size, position, velocity, cone in/out, orientation, outside volume, min, max, mode */
    VOICE *v = vo(b); if (v && p) { v->px = *(f32 *)&p[1]; v->py = *(f32 *)&p[2]; v->pz = *(f32 *)&p[3]; v->mind = *(f32 *)&p[12]; v->maxd = *(f32 *)&p[13]; v->mode3d = p[14]; if (v->mind <= 0) v->mind = 1; } return 0;
}
static s32 NTAPI B_Lock(void *b, u32 off, u32 bytes, void **p1, u32 *n1, void **p2, u32 *n2, u32 flags) { TR("B_Lock");
    VOICE *v = vo(b); if (v) v->nlock++; if (!v || !v->data || !v->bytes) { if (p1) *p1 = 0; if (n1) *n1 = 0; if (p2) *p2 = 0; if (n2) *n2 = 0; return 0x80004005; }
    if (flags & 2) { off = 0; bytes = v->bytes; }
    if (flags & 1) { u32 w = 0; B_GetCurrentPosition(b, 0, &w); off = w; }
    off %= v->bytes; if (bytes > v->bytes) bytes = v->bytes;
    u32 a = v->bytes - off < bytes ? v->bytes - off : bytes;
    if (p1) *p1 = v->data + off; if (n1) *n1 = a;
    if (p2) *p2 = bytes > a ? v->data : 0; if (n2) *n2 = bytes > a ? bytes - a : 0;
    return 0;
}
static s32 NTAPI B_SetCurrentPosition(void *b, u32 pos) { TR("B_SetCurrentPosition");
    VOICE *v = vo(b); if (v) { u32 ch = v->fmt.ch ? v->fmt.ch : 1; v->pos = v->fmt.tag == 0x69 ? (pos / (36 * ch)) * 65.0 : (f32)(pos / ((v->fmt.bits / 8 ? v->fmt.bits / 8 : 1) * ch)); } return 0;
}
static s32 NTAPI B_Ignore2(void *b, u32 a) { TR("B_Ignore2"); return 0; }
static s32 NTAPI B_Ignore4(void *b, u32 a, u32 c, u32 d) { TR("B_Ignore4"); return 0; }

/* ---- streams: the game gets &v->vtbl, an XMediaObject ---- */
static u32 NTAPI S_AddRef(void *s) { TR("S_AddRef"); VOICE *v = vs(s); return v ? ++v->refs : 0; }
static u32 NTAPI S_Release(void *s) { TR("S_Release"); VOICE *v = vs(s); if (!v) return 0; lock(); u32 r = --v->refs; if (!r) { for (u32 i = 0; i < v->npk; i++) complete_packet(v, &v->pk[i], 0x80004004 /* flushed */); v->npk = 0; v->used = 0; v->magic = 0; } unlock(); return r; }
static s32 NTAPI S_GetInfo(void *s, u32 *info) { TR("S_GetInfo"); VOICE *v = vs(s); if (info) { info[0] = 0; info[1] = v ? (v->fmt.align ? v->fmt.align : 4) : 4; info[2] = 0; info[3] = 0; } return 0; }
static s32 NTAPI S_GetStatus(void *s, u32 *st) { TR("S_GetStatus"); VOICE *v = vs(s); u32 r = 0; if (v) { if (v->npk < v->maxpk) r |= 1; if (v->playing && !v->paused) r |= 0x10000; if (v->paused) r |= 0x20000; if (!v->npk) r |= 0x40000; } *st = r; return 0; }
static s32 NTAPI S_Process(void *s, XPKT *in, XPKT *outp) { TR("S_Process");
    VOICE *v = vs(s); if (!v || !in) return 0x80070057;
    if (in->status) *in->status = 0x8000000A;   /* pending */
    if (in->done) *in->done = 0;
    lock();
    if (v->npk < MAXPK) { v->pk[v->npk++] = *in; v->playing = 1; } else { unlock(); complete_packet(v, in, 0x80004005); return 0x80004005; }
    unlock(); return 0;
}
static s32 NTAPI S_Discontinuity(void *s) { TR("S_Discontinuity"); return 0; }
static s32 NTAPI S_Flush(void *s) { TR("S_Flush"); VOICE *v = vs(s); if (v) { lock(); for (u32 i = 0; i < v->npk; i++) complete_packet(v, &v->pk[i], 0x80004004); v->npk = 0; v->pos = 0; unlock(); } return 0; }
static void *g_svt[7] = {S_AddRef, S_Release, S_GetInfo, S_GetStatus, S_Process, S_Discontinuity, S_Flush};
static s32 NTAPI DirectSoundCreateStream_(DSSDESC *d, void **pp) { TR("DirectSoundCreateStream_");
    VOICE *v = new_voice(); if (!v) { *pp = 0; return 0x8007000E; }
    v->stream = 1; v->vtbl = g_svt; v->mode3d = 2; v->maxpk = d && d->maxpackets ? (d->maxpackets < MAXPK ? d->maxpackets : MAXPK) : 8;
    if (d && d->fmt) v->fmt = *d->fmt; if (d) { v->cb = d->cb; v->ctx = d->ctx; }
    g_snd_streams++; *pp = &v->vtbl; return 0;
}
static s32 NTAPI ST_FlushEx(void *s, u32 t0, u32 t1, u32 flags) { TR("ST_FlushEx"); return S_Flush(s); }
static s32 NTAPI ST_Pause(void *s, u32 p) { TR("ST_Pause"); VOICE *v = vs(s); if (v) v->paused = p == 1; return 0; }
static s32 NTAPI ST_SetVolume(void *s, s32 vol) { TR("ST_SetVolume"); VOICE *v = vs(s); if (v) v->vol = db(vol); return 0; }
static s32 NTAPI ST_SetPitch(void *s, s32 p) { TR("ST_SetPitch"); VOICE *v = vs(s); if (v) v->rate = 48000.0f * exp2f_(p / 4096.0f); return 0; }
static s32 NTAPI ST_SetMode(void *s, u32 mode, u32 apply) { TR("ST_SetMode"); VOICE *v = vs(s); if (v) v->mode3d = mode; return 0; }
static s32 NTAPI ST_SetPos(void *s, f32 x, f32 y, f32 z, u32 apply) { TR("ST_SetPos"); VOICE *v = vs(s); if (v) { v->px = x; v->py = y; v->pz = z; } return 0; }
static s32 NTAPI ST_SetVel(void *s, f32 x, f32 y, f32 z, u32 apply) { TR("ST_SetVel"); return 0; }
static s32 NTAPI ST_SetMinD(void *s, f32 d, u32 apply) { TR("ST_SetMinD"); VOICE *v = vs(s); if (v) v->mind = d > 0 ? d : 1; return 0; }
static s32 NTAPI ST_SetMaxD(void *s, f32 d, u32 apply) { TR("ST_SetMaxD"); VOICE *v = vs(s); if (v) v->maxd = d > 0 ? d : 1e9f; return 0; }
static s32 NTAPI ST_SetAll(void *s, u32 *p, u32 apply) { TR("ST_SetAll"); VOICE *v = vs(s); if (v && p) { v->px = *(f32 *)&p[1]; v->py = *(f32 *)&p[2]; v->pz = *(f32 *)&p[3]; v->mind = *(f32 *)&p[12]; v->maxd = *(f32 *)&p[13]; v->mode3d = p[14]; if (v->mind <= 0) v->mind = 1; } return 0; }
static s32 NTAPI ST_Ignore2(void *s, u32 a) { TR("ST_Ignore2"); return 0; }
static s32 NTAPI ST_Ignore4(void *s, u32 a, u32 b, u32 c) { TR("ST_Ignore4"); return 0; }

/* ---- XFileCreateMediaObjectAsync (Milestone 80): a file media object that reads synchronously. The game streams
   speech from its wave banks with it: Seek to the wave, then Process packets into a DirectSound stream. ---- */
IMP BOOL WINAPI SetFilePointerEx(HANDLE, s64, s64 *, DWORD);
IMP BOOL WINAPI GetFileSizeEx(HANDLE, s64 *);
typedef struct { void *vtbl; HANDLE h; u32 refs; } XFMO;
static u32 NTAPI F_AddRef(XFMO *f) { return ++f->refs; }
static u32 NTAPI F_Release(XFMO *f) { u32 r = --f->refs; if (!r) HeapFree(GetProcessHeap(), 0, f); return r; }
static s32 NTAPI F_GetInfo(XFMO *f, u32 *info) { if (info) { info[0] = 0; info[1] = 0; info[2] = 1; info[3] = 0; } return 0; }
static s32 NTAPI F_GetStatus(XFMO *f, u32 *st) { if (st) *st = 1; return 0; }
static s32 NTAPI F_Process(XFMO *f, XPKT *in, XPKT *o) {
    if (!o) return 0;
    DWORD n = 0; ReadFile(f->h, o->buf, o->max, &n, 0);
    if (o->done) *o->done = n; if (o->status) *o->status = 0;
    if (o->ev_or_ctx) SetEvent(o->ev_or_ctx);
    return 0;
}
static s32 NTAPI F_Discontinuity(XFMO *f) { return 0; }
static s32 NTAPI F_Flush(XFMO *f) { return 0; }
static s32 NTAPI F_Seek(XFMO *f, s32 off, u32 origin, u32 *abs) { s64 np = 0; SetFilePointerEx(f->h, off, &np, origin); if (abs) *abs = (u32)np; return 0; }
static s32 NTAPI F_GetLength(XFMO *f, u32 *len) { s64 sz = 0; GetFileSizeEx(f->h, &sz); if (len) *len = (u32)sz; return 0; }
static s32 NTAPI F_DoWork(XFMO *f) { return 0; }   /* the reads are synchronous: nothing to pump */
static void *g_fvt[10] = {F_AddRef, F_Release, F_GetInfo, F_GetStatus, F_Process, F_Discontinuity, F_Flush, F_Seek, F_GetLength, F_DoWork};
static s32 NTAPI XFileCreateMediaObjectAsync_(HANDLE h, u32 maxpk, void **pp) { TR("XFileCreateMediaObjectAsync");
    XFMO *f = HeapAlloc(GetProcessHeap(), 8, sizeof *f); if (!f) { *pp = 0; return 0x8007000E; }
    f->vtbl = g_fvt; f->h = h; f->refs = 1; *pp = f; return 0;
}

/* ---- patching ---- */
static int jmp_to(u32 at, void *fn) {
    u8 *p = (u8 *)at;
    if (p[0] == 0xCC || p[0] == 0) { out("  sound patch skipped at "); hex(at); out("\r\n"); return 0; }
    p[0] = 0xE9; *(u32 *)(p + 1) = (u32)fn - (at + 5); return 1;
}
IMP DWORD WINAPI GetFileAttributesA(const char *);
void swap_header(int pe);
void dsound_patch(void) {
    if (GetFileAttributesA("nosound.txt") != 0xFFFFFFFF) { out("Sound: off (nosound.txt)\r\n"); return; }
    static const struct { u32 at; void *fn; } t[] = {
        {0x314714, DirectSoundCreate_}, {0x311A3D, DS_Release}, {0x3129FE, DS_GetCaps}, {0x312A1A, DS_DownloadEffectsImage}, {0x312C23, DirectSoundDoWork_},
        {0x311A6E, DirectSoundUseFullHRTF_}, {0x3141DF, DS_SetPosition}, {0x314195, DS_SetOrientation}, {0x314171, DS_SetDistanceFactor}, {0x314214, DS_SetI3DL2Listener},
        {0x31475B, DirectSoundCreateBuffer_}, {0x311A58, B_Release}, {0x314234, B_SetBufferData}, {0x313882, B_SetFormat}, {0x3139D0, B_SetPlayRegion},
        {0x31389E, B_SetFrequency}, {0x312A5D, B_SetPitch}, {0x312A41, B_SetVolume}, {0x312AE9, B_Play}, {0x312B0D, B_Stop}, {0x312B25, B_StopEx},
        {0x312B49, B_Pause}, {0x312B65, B_GetStatus}, {0x312B81, B_GetCurrentPosition}, {0x313922, B_SetMode}, {0x313942, B_SetPos}, {0x313977, B_SetVel},
        {0x3138FE, B_SetMinD}, {0x3138DA, B_SetMaxD}, {0x3138BA, B_SetAll}, {0x312A79, B_Ignore2}, {0x312AB1, B_Ignore2}, {0x312ACD, B_Ignore2}, {0x3139AC, B_Ignore4},
        {0x3147B2, DirectSoundCreateStream_}, {0x312C0B, ST_FlushEx}, {0x312C06, ST_Pause}, {0x312BED, ST_SetVolume}, {0x312BF2, ST_SetPitch}, {0x313A25, ST_SetMode},
        {0x313A2A, ST_SetPos}, {0x313A53, ST_SetVel}, {0x313A0D, ST_SetMinD}, {0x3139F5, ST_SetMaxD}, {0x3139F0, ST_SetAll}, {0x312BF7, ST_Ignore2},
        {0x312BFC, ST_Ignore2}, {0x312C01, ST_Ignore2}, {0x313A7C, ST_Ignore4},
        {0x312C4C, XFileCreateMediaObjectAsync_}, {0x31454A, DS_CreateSoundBuffer}, {0x31386A, DS_CommitDeferredSettings}, {0x312BBD, B_Lock}, {0x312BA1, B_SetCurrentPosition}, {0x312A95, B_Ignore2},   /* used by the Bink movie player */
    };
    u32 n = 0; for (u32 i = 0; i < sizeof t / sizeof t[0]; i++) n += jmp_to(t[i].at, t[i].fn);
    out("Sound functions patched: "); dec(n); out(" of "); dec(sizeof t / sizeof t[0]); out("\r\n");
    HANDLE m = CreateFileMappingA((HANDLE)-1, 0, 0x04, 0, sizeof(AUDIO), "BruteForcePCLoader_Audio");
    g_au = m ? MapViewOfFile(m, 0x6, 0, 0, sizeof(AUDIO)) : 0;
    if (!g_au) { out("  [sound] no shared audio memory\r\n"); return; }
    g_au->magic = AUDIO_MAGIC;
    g_au_ev = CreateEventA(0, 0, 0, "BruteForcePCLoader_AudioEv");
    if (GetFileAttributesA("canary.txt") != 0xFFFFFFFF) { swap_header(1); CreateThread(0, 0x10000, canary, (void *)0, 0, 0); CreateThread(0, 0x10000, canary, (void *)1, 0, 0); swap_header(0); }
    swap_header(1); CreateThread(0, 0x10000, mixer, 0, 0, 0); swap_header(0);
}

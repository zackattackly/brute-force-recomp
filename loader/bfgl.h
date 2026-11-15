/* Brute Force PC loader: the protocol between the loader and the viewer's GPU renderer (Milestone 79).
 *
 * Shared memory:
 *   "BruteForcePCLoader_Mem"   the Xbox's 64 MB of memory (physical address = offset)
 *   "BruteForcePCLoader_Ring"  RING header + command ring
 *
 * The loader runs the push buffer and the vertex programs; for each draw it sends the GPU registers that matter
 * (all of the 3D object's methods, kel[0x800]) when they changed, then the transformed vertices. The viewer draws
 * them with OpenGL into one frame-buffer object per Xbox surface, and shows the surface the CRTC points at.
 * Commands are u32 words: a header (type << 24 | length in words, header included) then the payload. */
#pragma once

#define RING_MAGIC 0x52474642u   /* "BFGR" */
#define RING_WORDS (8u << 20)    /* 32 MB of commands */

enum {
    CMD_WRAP = 1,    /* the rest of the ring is unused: continue at 0 */
    CMD_STATE,       /* kel[0x800] follows: the 3D object's registers */
    CMD_DRAW,        /* u32 mode (NV2A primitive 1..10), u32 count, then count vertices of VTX_WORDS words */
    CMD_CLEAR,       /* u32 flags (CLEAR_SURFACE), then the registers that matter are in the last CMD_STATE */
    CMD_FLIP,        /* u32 physical address the CRTC shows */
    CMD_FENCE,       /* u32 value: the viewer puts it in fence_done once everything before it is drawn */
    /* vertex programs on the GPU (Milestone 81) */
    CMD_VSH,         /* u32 start slot, then 136 x 4 program words */
    CMD_CONST,       /* u32 first, u32 count, then count x 4 floats */
    CMD_DRAW_IDX,    /* u32 mode, count, min, max, 16 x 4 floats (attributes without arrays), then count u32 indices */
    CMD_DRAW_IMM,    /* u32 mode, count, then count x 16 x 4 floats (all attributes of each vertex) */
    CMD_ZPASS,       /* u32 op (0 clear, 1 enable = arg, 2 write the report to physical address arg), u32 arg (Milestone 87) */
};
#define VTX_WORDS 29     /* x y z w (Xbox window coordinates, clip w), D0 rgba, D1 rgba, fog, T0..T3 strq */

/* sound (Milestone 80): the loader mixes, the viewer plays: 48 kHz stereo 16-bit frames in "BruteForcePCLoader_Audio" */
#define AUDIO_FRAMES 32768u   /* ring size in frames (~0.68 s) */
typedef struct { unsigned magic; volatile unsigned wpos, rpos; volatile unsigned playing; unsigned pad[12]; short pcm[AUDIO_FRAMES * 2]; } AUDIO;
#define AUDIO_MAGIC 0x41474642u

typedef struct {
    unsigned magic, gl_ok, gl_failed;            /* set by the viewer */
    volatile unsigned wpos, rpos;                /* in words; the loader writes wpos, the viewer rpos */
    volatile unsigned fence_done;                /* the last CMD_FENCE value drawn */
    volatile unsigned frames, draws;             /* viewer statistics */
    char info[256];                              /* the viewer's GL renderer string or error */
    unsigned pad[64];                            /* [56]: the tick of the player's last hit (hit marker, Milestone 101) */
} RING;

/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: core types. A drum machine of eight sounds (tracks), each an LXR-style synth voice with a sample layer
 * (the original LXR transient samples, build/gen/lxr_transients.h), sixteen patterns of up to 64 steps a track
 * (four bars, each track its own length). One voice per track, as on a drum machine. Fixed point (no FPU). */
#include <stdint.h>

#define NTRK 8
#define NSTEP 64
#define NPAT 16
#define HALF_FRAMES 128          /* I2S half buffer (audio.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* the panel's printed labels (panel.c maps them to the key matrix) */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

/* the sounds: each track one voice kind (its synth layer) */
enum { K_DRUM, K_SNARE, K_CLAP, K_HATC, K_HATO, K_CYM, K_N };
static const uint8_t TRK_KIND[NTRK] = {K_DRUM, K_SNARE, K_CLAP, K_HATC, K_HATO, K_DRUM, K_DRUM, K_CYM};
static const char *const TRK_NAME[NTRK] = {"KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "TOM", "RIM", "CYMBAL"};
static const char *const TRK_SHORT[NTRK] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CY"};
/* the sample layer: off, the LXR's SNAP (a pitch snap of the synth layer), then its twelve transient samples */
enum { TR_OFF, TR_SNAP, TR_SMP0 };
#define TR_N (TR_SMP0 + 12)

typedef struct {                 /* a sound; values 0..127 unless noted */
    uint8_t p[8];                /* the synth layer: eight parameters by kind (lx_synth.c LX_LABEL) */
    uint8_t smp, slvl, spitch, sdec;   /* the sample layer: TR_*, level, pitch (64 = as recorded), decay (127 = whole) */
    uint8_t syn;                 /* the synth layer's level */
    uint8_t level, dly, rev;
    int8_t pan;                  /* -64..63 */
    uint8_t lfo_rate, lfo_depth, lfo_dest, lfo_wave;   /* LD_*, LW_* */
    uint8_t preset;              /* PRESETS: the variation it was rolled from (0 = the factory sound) */
} sound_t;
enum { LD_OFF, LD_PITCH, LD_FILTER, LD_LEVEL, LD_PAN, LD_N };
enum { LW_SINE, LW_TRI, LW_SAW, LW_SQR, LW_SH, LW_N };

/* a step: 0 = rest, else bits 0..6 the velocity (1..127), bit 7 the accent */
#define S_ACC 0x80u
#define S_VEL(s) ((s) & 0x7Fu)
typedef struct {
    uint8_t s[NTRK][NSTEP];
    uint8_t len[NTRK];           /* steps 1..64 (16 a bar) */
} pattern_t;

typedef struct {
    uint8_t playing, sel;
    uint8_t bpm;                 /* 60..200 */
    uint8_t swing;               /* 0..50 % */
    uint8_t pat, pat_next;       /* the pattern playing; the one cued (0xFF: none) */
    uint8_t chain;               /* patterns chained from chain0 (1 = the pattern loops) */
    uint8_t chain0;
    uint8_t rec, erase;          /* REC armed; SHIFT + REC: the armed mode is ERASE */
    uint8_t click;               /* the metronome */
    uint8_t midi_ch;             /* MIDI IN: 0 every channel, 1..16 */
    uint8_t theme;
    int8_t tune;                 /* the kit's tune, -24..24 semitones */
    uint8_t ddec;                /* the kit's decay, 64 = as the sounds have it */
    uint8_t mute, solo;          /* bit t: track t */
    uint8_t dly_div, dly_fb, rev_size, rev_damp;   /* the global FX */
    uint16_t master_q12;
    uint32_t step, pos;          /* the global 16th, samples into it */
    uint32_t pstep;              /* 16ths since the pattern began */
    uint32_t cpu_q8;
} song_t;

/* note repeat (ARP): the pads (white keys 1..8 while it is on) repeat at RATE */
enum { NR_OFF, NR_4, NR_8, NR_8T, NR_16, NR_16T, NR_32, NR_32T, NR_N };
static const uint8_t NR_SIXTHS[NR_N] = {0, 24, 12, 8, 6, 4, 3, 2};   /* the period in sixths of a 16th */
static const char *const NR_NAME[NR_N] = {"SINGLE", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
enum { NV_FIXED, NV_UP, NV_DOWN, NV_RANDOM, NV_N };

typedef struct {
    uint8_t active, vel, acc, choke;
    uint32_t ph[3], noise, t;    /* t: samples since the hit */
    uint32_t tph, tinc;          /* the sample layer: its phase (Q20 samples), step */
    int32_t amp, penv, click, cfade, nenv, tone, snap, tenv;   /* Q15 */
    int32_t lp, bp;              /* the filter */
} voice_t;

static pattern_t pat[NPAT];
static sound_t snd[NTRK];
static song_t song;
static voice_t voice[NTRK];

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER5 ISR) */
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update (usb.c) */
#endif
#define PAT (&pat[song.pat % NPAT])
static int trk_audible(uint32_t t)
{
    return !((song.mute >> t) & 1u) && (!song.solo || ((song.solo >> t) & 1u));
}
#ifndef OC_HOST
/* boot-loop guard (oc_main.c): two boots in a row that die in their first 30 s -> UBOOT, where an installer reaches it */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
#endif

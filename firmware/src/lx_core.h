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
static const uint8_t GM_NOTE[NTRK] = {36, 38, 39, 42, 46, 45, 37, 49};   /* MIDI OUT: each sound's note at first */
/* the sample layer: off, the LXR's SNAP (a pitch snap of the synth layer), the LXR's twelve transient samples, sixteen
 * of the sound's own (tools/gen_own_transients.py), sixteen after the classic drum machines (gen_machine_transients.py:
 * 808 909 606 707 DMX SDS, at 22.05 kHz) */
enum { TR_OFF, TR_SNAP, TR_SMP0 };
#define TR_OWN0 (TR_SMP0 + 12)
#define TR_MACH0 (TR_OWN0 + 16)
#define TR_N (TR_MACH0 + 16)
typedef struct { uint8_t dest, wave, rate; int8_t depth; } lfo_t;   /* LD_*, LW_*, LR_* (with the tempo), -64..63 */

typedef struct {                 /* a sound; values 0..127 unless noted */
    uint8_t p[8];                /* the synth layer: eight parameters by kind (lx_synth.c LX_LABEL) */
    uint8_t smp, slvl, spitch, sdec;   /* the sample layer: TR_*, level, pitch (64 = as recorded), decay (127 = whole) */
    uint8_t sstart;              /* .. where it starts (0 = its beginning) */
    int8_t stone, sbend;         /* .. its filter (- low-pass, + high-pass), its pitch bend (- up into, + down from) */
    uint8_t syn;                 /* the synth layer's level */
    uint8_t level, dly, rev, drv;   /* the sends: delay, reverb, drive */
    int8_t pan;                  /* -64..63 */
    lfo_t lfo[2];
    uint8_t preset;              /* the variation it was rolled from (0 = the factory sound) */
    uint8_t och, onote;          /* MIDI OUT: its channel (0 off, 1..16), its note (LXP7) */
    uint8_t atk, rel, gate;      /* ENV (LXP8): attack (0 = at once), release (after the gate), gate (0 off, k quarters of a
                                  * step: then the release takes over from the decay) */
} sound_t;
enum { LD_OFF, LD_PITCH, LD_FILTER, LD_DECAY, LD_LEVEL, LD_PAN, LD_SAMPLE, LD_N };
enum { LW_SINE, LW_TRI, LW_SAW, LW_SQR, LW_SH, LW_N };
enum { LR_16, LR_8, LR_4, LR_2, LR_1BAR, LR_2BAR, LR_4BAR, LR_8BAR, LR_N };   /* the LFO's cycle, with the tempo */
static const uint8_t LR_STEPS[LR_N] = {1, 2, 4, 8, 16, 32, 64, 128};         /* (16ths) */
enum { DM_SOFT, DM_HARD, DM_FOLD, DM_N };                                    /* the drive bus */
enum { DL_MONO, DL_STEREO, DL_PINGPONG, DL_N };                              /* the delay */
enum { Q_16, Q_8, Q_4, Q_N };                                                /* recording: the grid */
static const uint8_t Q_STEPS[Q_N] = {1, 2, 4};
enum { CLICK_OFF, CLICK_REC, CLICK_ON, CLICK_N };

/* a step: 0 = rest, else bits 0..6 the velocity (1..127), bit 7 the accent; its extras (STEP EDIT): when it plays
 * (bits 3..7: 0 always, 1..15 k/16 of the passes, 16..29 the conditions 1:2 2:2 1:4 .. 4:4 1:8 .. 8:8 - the A-th of every
 * B passes, 30 FILL: only while FILL is held, 31 NOT FILL) and its pan (bits 0..2: 0 the sound's, k (k - 4) * 16);
 * r: bits 0..2 the substeps, bits 3..7 its timing (NUDGE: -15..+15 32nds of the step, two's complement in 5 bits) */
#define S_ACC 0x80u
#define S_VEL(s) ((s) & 0x7Fu)
#define X_COND(x) ((x) >> 3)
#define X_PAN(x) ((x) & 7u)
#define X_PANV(x) (((int32_t)X_PAN(x) - 4) * 16)
#define C_PROB_N 16u                     /* (codes 1..15: a probability) */
#define C_FILL 30u
#define C_NFILL 31u
#define C_N 32u
#define R_SUB(r) ((r) & 7u)
#define R_MICRO(r) ((int32_t)(((r) >> 3) & 31u) - (((r) & 0x80u) ? 32 : 0))   /* -16..15 (used: -15..15) */
#define R_MAKE(sub, mic) ((uint8_t)(((sub) & 7u) | (((uint32_t)(mic) & 31u) << 3)))
static const uint8_t COND_A[14] = {1, 2, 1, 2, 3, 4, 1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t COND_B[14] = {2, 2, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8};
typedef struct {
    uint8_t s[NTRK][NSTEP];
    uint8_t x[NTRK][NSTEP];      /* the steps' extras */
    uint8_t r[NTRK][NSTEP];      /* the steps' substeps: 0 one hit, k k + 1 hits over the step (2..6) */
    uint8_t len[NTRK];           /* steps 1..64 (16 a bar) */
    int8_t spd[NTRK];            /* the track's speed: SPD_* (0 = as the tempo) */
} pattern_t;
/* a track's speed, in 16ths a step: 1/8x .. 2x (a step every d / n 16ths) */
#define SPD_MIN (-4)
#define SPD_MAX 2
static const uint8_t SPD_N[7] = {1, 1, 1, 3, 1, 3, 2}, SPD_D[7] = {8, 4, 2, 4, 1, 2, 1};
#define SPD_I(v) ((uint32_t)((int32_t)(v) - SPD_MIN) % 7u)
/* parameter locks: a pool a pattern, each a step of a track (t << 6 | step), the parameter (lx_ui.c P_*, 0 = free) and
 * its value (as the parameter stores it: int8 values cast) */
#define NLOCK 128u
typedef struct { uint16_t key; uint8_t pid, val; } lock_t;
#define NLK_V 10u                        /* the locks a hit carries */
/* the song: rows of a pattern and its repeats */
#define NSONG 32u
typedef struct { uint8_t pat, rep; } srow_t;

typedef struct {
    uint8_t playing, sel;
    uint8_t bpm;                 /* 60..200 */
    uint8_t swing;               /* 0..50 % */
    uint8_t pat, pat_next;       /* the pattern playing; the one cued (0xFF: none) */
    uint8_t chain;               /* patterns chained from chain0 (1 = the pattern loops) */
    uint8_t chain0;
    uint8_t rec, erase;          /* REC armed; SHIFT + REC: the armed mode is ERASE */
    uint8_t click;               /* the metronome: CLICK_* */
    uint8_t qgrid, qround;       /* recording: the grid (Q_*), to the nearest point (0) or the one before (1) */
    uint8_t midi_ch;             /* MIDI IN: 0 every channel, 1..16 */
    uint8_t theme;
    int8_t tune;                 /* the kit's tune, -24..24 semitones */
    uint8_t ddec;                /* the kit's decay, 64 = as the sounds have it */
    uint8_t mute, solo;          /* bit t: track t */
    /* the global FX (stored with the kit) */
    uint8_t drv_mode, drv_amt, drv_tone, drv_lvl;  /* the drive bus: DM_*, gain, tone, return */
    uint8_t dly_div, dly_fb, dly_tone, dly_lvl;    /* the delay: time (16ths), feedback, tone, return */
    uint8_t dly_mode, dly_div2;                    /* .. DM_MONO / STEREO / PING PONG, the right side's time (STEREO) */
    uint8_t rev_size, rev_damp, rev_lvl;           /* the reverb */
    uint8_t rev_pre;                               /* .. its pre-delay (0 .. 127: 0 .. 180 ms; LXP9) */
    uint16_t master_q12;
    uint32_t step, pos;          /* the global 16th, samples into it */
    uint32_t pstep;              /* 16ths since the pattern began */
    uint32_t loops;              /* the patterns played through since PLAY (the conditions' passes) */
    uint32_t cpu_q8;
    /* LXP7 */
    uint8_t fill;                /* FILL held */
    uint8_t clk_src, clk_out;    /* MIDI clock: CK_INT / CK_EXT; sent (24 a quarter, start / stop) */
    uint8_t smode, slen, srow, srep, send;   /* SONG: on, its rows, the row and the repeat playing, at its end loop / stop */
    int8_t mflt;                 /* the master filter: - low-pass, + high-pass, 0 off */
    uint8_t mres;
    uint8_t cthr, crat, catk, crel, cmak, clim;   /* the compressor: threshold -dB, ratio, attack, release, makeup +dB; the
                                                 * limiter on (LXPA, the kit) */
} song_t;
enum { CK_INT, CK_EXT, CK_N };

/* note repeat (ARP): the pads (white keys 1..8 while it is on) repeat at RATE */
enum { NR_OFF, NR_4, NR_8, NR_8T, NR_16, NR_16T, NR_32, NR_32T, NR_N };
static const uint8_t NR_SIXTHS[NR_N] = {0, 24, 12, 8, 6, 4, 3, 2};   /* the period in sixths of a 16th */
static const char *const NR_NAME[NR_N] = {"SINGLE", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
enum { NV_FIXED, NV_UP, NV_DOWN, NV_RANDOM, NV_N };

typedef struct {
    uint8_t active, vel, acc, choke;
    uint8_t nlk, lk[NLK_V][2];   /* the hit's locks (pid, value) */
    int32_t att;                 /* ENV: the attack's ramp (Q15) */
    uint32_t gate_n;             /* .. samples to the release (0: none) */
    int8_t span;                 /* the hit's own pan (a step's), -64..63; 0x7F: the sound's */
    int8_t semi;                 /* the hit's transpose in semitones (a note on the sound's own MIDI channel) */
    uint32_t ph[3], noise, t;    /* t: samples since the hit */
    uint32_t tph, tinc;          /* the sample layer: its phase (Q20 samples), step */
    int32_t tlp, benv;           /* .. its filter's state, its bend's envelope (Q15) */
    int32_t amp, penv, click, cfade, nenv, tone, snap, tenv;   /* Q15 */
    int32_t lp, bp;              /* the filter */
} voice_t;

static pattern_t pat[NPAT];
static lock_t plk[NPAT][NLOCK] __attribute__((section(".pool")));   /* each pattern's locks */
static srow_t srows[NSONG];
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

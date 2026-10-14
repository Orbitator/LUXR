/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: core types. Four tracks with fixed roles (bass, chords, lead, drums), each a sound (patch_t) and a
 * 64-step pattern; a pool of NVOICE voices shared by the three melodic tracks, one voice per lane for the drums. Everything is fixed point (no FPU on the FM-1). */
#include <stdint.h>

#define NTRK 4
#define NSTEP 64
#define NVOICE 8                 /* synth voices (bass, chords, lead); the drums have one voice per lane (dvoice) */
#define NLANE 8
#define HALF_FRAMES 128          /* I2S half buffer (audio.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* the panel's printed labels (panel.c maps them to the key matrix) */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

enum { ROLE_BASS, ROLE_CHORD, ROLE_LEAD, ROLE_DRUM };
enum { E_SUB, E_ACID, E_FM, E_SAW, E_ADD, E_DRUM, E_N };
static const char *const ENGINE_NAME[E_N] = {"SUBTRACT", "ACID", "FM", "SUPERSAW", "ADDITIVE", "DRUMS"};
static const char *const ROLE_NAME[NTRK] = {"BASS", "CHORDS", "LEAD", "DRUMS"};
enum { LN_KICK, LN_SNARE, LN_CLAP, LN_HATC, LN_HATO, LN_TOM, LN_RIM, LN_BELL };

enum { ST_REST, ST_NOTE, ST_TIE };
#define SF_ACC 1u
#define SF_SLIDE 2u
typedef struct {
    uint8_t note[4];
    uint8_t n;                   /* notes in use */
    uint8_t time;                /* ST_REST / ST_NOTE / ST_TIE */
    uint8_t flags;               /* SF_ACC | SF_SLIDE */
    uint8_t vel;
    uint8_t hit, acc;            /* drums: bit l = lane l hits / is accented */
} step_t;

enum { W_SAW, W_SQR, W_TRI, W_SINE };
#define ADD_N 8
typedef struct {                 /* a sound; values 0..127 unless noted */
    uint8_t engine, mono, glide;
    uint8_t wave1, wave2;        /* W_* (SUB: both; ACID: wave1 SAW / SQR) */
    int8_t detune, oct2;         /* SUB / SAW: 1/16 semitones; osc 2 octave -1..+1 */
    uint8_t mix2, sub, noise;
    uint8_t cutoff, res, fenv, fdec, keytrk;   /* filter: cutoff 0..127, env amount, env decay (time index) */
    uint8_t atk, dec, sus, rel;  /* amp: time indices (tools/gen_tables.py), sustain level */
    uint8_t fm_ratio[3];         /* FM: Q4 ratios of op 1 (carrier) 2 3; 16 = 1.0 */
    uint8_t fm_index, fm_idec, fm_fb, fm_algo;
    uint8_t saw_n, saw_spread;   /* SUPERSAW: saws (1..7), spread */
    uint8_t add_n;               /* ADDITIVE: partials (1..ADD_N) */
    uint16_t add_ratio[ADD_N];   /* Q8, 256 = the fundamental */
    uint8_t add_amp[ADD_N], add_tilt;
    uint8_t kit;                 /* DRUMS: 0..3 tuning / decay character */
    uint8_t level, dly, rev;
    int8_t pan;                  /* -64..63 */
} patch_t;

typedef struct {
    uint8_t active, trk, note, vel, gate, stage, lane, acc;   /* stage 1 attack 2 decay/sustain 3 release */
    int32_t pitch16, pitch_to, gstep;        /* 1/16 semitones, glide target and step per block */
    uint32_t ph[8];
    int32_t env;                 /* Q24 */
    int32_t amp;                 /* Q15, last block */
    int32_t fenv;                /* Q24 */
    int32_t ienv;                /* Q15: FM index / drum pitch envelope */
    int32_t lp, bp, z1, fb1, fb2;
    int32_t pa[ADD_N];           /* additive partial levels Q15; drums: their own state */
    uint32_t age, noise, t;      /* t: samples since the note began */
} voice_t;

typedef struct {
    patch_t s;
    step_t step[NSTEP];
    uint8_t len, gate;           /* LEN 1..64 (16ths), GATE % 1..100 */
    uint8_t mute;
    int8_t k_cut, k_res, k_dec;  /* live knob offsets (non-destructive, One Click's CUTOFF slider) */
    uint8_t k_lvl;
    /* sequencer runtime (audio ISR) */
    uint8_t snd[4], nsnd;        /* sounding sequenced notes */
    uint8_t hold;                /* slide / tie: keep them into the next step */
    uint32_t off_at;             /* sample (into the step) of their note-off, 0 = none */
} track_t;

typedef struct {
    uint8_t playing, sel;
    uint8_t bpm;                 /* 60..200 */
    uint8_t swing;               /* 0..50 % */
    uint8_t root, scale;         /* the song's key (scale: SCALES[]) */
    uint8_t prog[4];             /* the chord of each bar: scale degree 0..6 */
    int8_t octave;               /* the keys */
    uint16_t master_q12;
    uint32_t step, pos;          /* the global 16th, samples into it */
    uint32_t cpu_q8;
} song_t;

static track_t trk[NTRK];
static song_t song;
static voice_t voice[NVOICE];
static voice_t dvoice[NLANE];   /* the drum track's: one per lane, as a drum machine (no stealing) */
#define TSEL (&trk[song.sel % NTRK])

/* scales the generator uses: 7-note, semitones above the root */
enum { SC_MIN, SC_DOR, SC_MAJ, SC_MIX, SC_HARM, SC_PHRY, SC_N };
static const char *const SCALE_NAME[SC_N] = {"MINOR", "DORIAN", "MAJOR", "MIXOLYD", "HARM MIN", "PHRYGIAN"};
static const uint8_t SCALES[SC_N][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11},
    {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10},
};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER5 ISR) */
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update (usb.c) */
#endif
/* boot-loop guard (oc_main.c): two boots in a row that die in their first 30 s -> UBOOT, where an installer reaches it */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));

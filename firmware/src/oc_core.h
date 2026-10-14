/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: core types. Four tracks, each an instrument TYPE (bass, chords, pad, lead, drums, ...: its role in
 * the music and how its sounds are made), a sound (patch_t) and a 64-step pattern; a pool of NVOICE voices shared by
 * the melodic tracks, one voice per lane on drum tracks. Everything is fixed point (no FPU on the FM-1). */
#include <stdint.h>

#define NTRK 6                   /* 1..4 the generator's parts, 5 the kick, 6 the loop (as ONE CLICK SYNTH's X-Generator) */
#define NSTEP 64
#define NVOICE 8                 /* synth voices (bass, chords, lead); the drums have one voice per lane (dvoice) */
#define NLANE 8
#define HALF_FRAMES 128          /* I2S half buffer (audio.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* the panel's printed labels (panel.c maps them to the key matrix) */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

/* the roles in the music (how a part is made) and the instrument types (what the track is: its role and its sounds) */
enum { ROLE_BASS, ROLE_CHORD, ROLE_LEAD, ROLE_DRUM, ROLE_LOOP, ROLE_N };
enum { TY_BASS, TY_CHORDS, TY_PAD, TY_ATMO, TY_PIANO, TY_GUITAR, TY_LEAD, TY_PLUCK, TY_BRASS, TY_FLUTE,
       TY_DRUMS, TY_KICK, TY_SNARE, TY_PERC, TY_LOOP, TY_N };
static const char *const TYPE_NAME[TY_N] = {"BASS", "CHORDS", "PAD", "ATMO", "PIANO", "GUITAR", "LEAD", "PLUCK",
                                            "BRASS", "FLUTE", "DRUMS", "KICK", "SNARE", "PERC", "LOOP"};
static const uint8_t TYPE_ROLE[TY_N] = {ROLE_BASS, ROLE_CHORD, ROLE_CHORD, ROLE_CHORD, ROLE_CHORD, ROLE_CHORD,
                                        ROLE_LEAD, ROLE_LEAD, ROLE_LEAD, ROLE_LEAD,
                                        ROLE_DRUM, ROLE_DRUM, ROLE_DRUM, ROLE_DRUM, ROLE_LOOP};
static const uint8_t TYPE_DEFAULT[NTRK] = {TY_BASS, TY_CHORDS, TY_LEAD, TY_DRUMS, TY_KICK, TY_LOOP};   /* the tracks at power-on */
enum { E_SUB, E_ACID, E_FM, E_SAW, E_ADD, E_NOISE, E_DRUM, E_SAMPLE, E_LXR, E_N };
static const char *const ENGINE_NAME[E_N] = {"SUBTRACT", "ACID", "FM", "SUPERSAW", "ADDITIVE", "NOISE", "DRUMS", "SAMPLE", "LXR"};
enum { LN_KICK, LN_SNARE, LN_CLAP, LN_HATC, LN_HATO, LN_TOM, LN_RIM, LN_BELL };
/* LXR (a drum synth in the manner of Sonic Potions' LXR, written anew in fixed point): six voices, each lane plays one */
enum { LX_DRUM1, LX_DRUM2, LX_DRUM3, LX_SNARE, LX_CYMBAL, LX_HIHAT, LX_N };
enum { LK_DRUM, LK_SNARE, LK_CYMBAL, LK_HIHAT };
static const uint8_t LX_KIND[LX_N] = {LK_DRUM, LK_DRUM, LK_DRUM, LK_SNARE, LK_CYMBAL, LK_HIHAT};
static const uint8_t LX_OF_LANE[8] = {LX_DRUM1, LX_SNARE, LX_SNARE, LX_HIHAT, LX_HIHAT, LX_DRUM2, LX_DRUM3, LX_CYMBAL};

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
    uint8_t lfo_rate, lfo_depth, lfo_dest, lfo_wave;   /* LFO: rate (0.05..20 Hz), depth, LD_*, LW_* */
    int8_t dtune;                /* DRUMS: tune, -24..24 */
    uint8_t ddec;                /* DRUMS: decay, 64 = as the kit */
    uint8_t fatt, fsus, frel;    /* the filter envelope's attack, sustain, release (its decay: fdec) */
    int8_t tune, fine;           /* pitch: semitones -24..24, cents -100..100 */
    uint8_t dual, dmix;          /* DUAL: a second voice per note (DU_*), its level */
    uint8_t smp, sstart;         /* SAMPLE (the kick and loop tracks): the slot (smp_dir), the start (0..127 of its length) */
    uint8_t lx[6][8];            /* LXR: six voices x eight parameters (oc_synth.c LXR_*) */
} patch_t;
enum { DU_OFF, DU_UNISON, DU_OCTUP, DU_FIFTH, DU_OCTDN, DU_N };   /* the second voice: detuned, +12, +7, -12 */
enum { LD_OFF, LD_CUT, LD_PITCH, LD_AMP, LD_PAN, LD_N };
enum { LW_SINE, LW_TRI, LW_SAW, LW_SQR, LW_SH, LW_N };

typedef struct {
    uint8_t active, trk, note, vel, gate, stage, lane, acc;   /* stage 1 attack 2 decay/sustain 3 release */
    int32_t pitch16, pitch_to, gstep;        /* 1/16 semitones, glide target and step per block */
    int16_t poff;                /* DUAL's second voice: its offset, 1/16 semitones */
    uint8_t fstage;              /* the filter envelope: 1 attack, 2 decay / sustain, 3 release */
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
    uint8_t type;                /* TY_* */
    uint8_t seq_off;             /* SEQ OFF: the pattern does not play (the keys and MIDI still do) */
    uint8_t master;              /* MASTER: knob changes on this track go to the other tracks too */
    int8_t octave;               /* the track's octave, -2..+2 (played, not stored in the notes) */
    uint8_t gen_mut;             /* MUTATE: how much a partial new (sound, notes, rhythm) changes, 0..127 */
    uint8_t eu_hits, eu_rot, eu_lane;   /* EUCLID: the hits over LEN, the rotation, the drum lane it writes */
    int8_t k_cut, k_res, k_dec;  /* live knob offsets (non-destructive, One Click's CUTOFF slider) */
    uint8_t k_lvl;
    uint8_t gen_dens, gen_var, gen_style;   /* the generator's settings for this track (ARP page): density, variation,
                                             * style (0 AUTO, else the role's style + 1) */
    int8_t gen_range;            /* the part's register, octaves -1..+1 */
    uint32_t lfo_ph;             /* LFO runtime (audio ISR): phase, value Q15 */
    int32_t lfo_v, lfo_sh;
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
    uint8_t prog_i;              /* which of the generator's progressions (oc_gen.c PROGS) */
    int8_t octave;               /* the keys */
    int8_t transpose;            /* the song's, -24..+12 (played, not stored in the notes) */
    uint8_t rec, rec_mode;       /* REC armed (the selected track records); REC_* */
    uint8_t dly_div, dly_fb, rev_size, rev_damp;   /* the global FX: delay time (DLY_DIVS), feedback, reverb */
    uint8_t click;               /* the metronome: CLICK_* */
    uint8_t midi_in;             /* MIDI IN: MI_* */
    uint8_t theme;               /* the screen's palette (gfx.c UI_PALETTES) */
    uint16_t master_q12;
    uint32_t step, pos;          /* the global 16th, samples into it */
    uint32_t cpu_q8;
} song_t;

static track_t trk[NTRK];
static song_t song;
static voice_t voice[NVOICE];
static voice_t dvoice[NTRK][NLANE];   /* drum tracks: one voice per lane, as a drum machine (no stealing) */
enum { REC_OVERDUB, REC_REPLACE, REC_N };
enum { CLICK_OFF, CLICK_REC, CLICK_ON, CLICK_N };
enum { MI_CH14, MI_SEL, MI_N };                /* MIDI IN: channels 1..4 play tracks 1..4 / every channel the selected */
static uint32_t trk_role(uint32_t t) { return TYPE_ROLE[trk[t % NTRK].type % TY_N]; }
#define TSEL (&trk[song.sel % NTRK])
#define IS_DRUM(t) (trk_role(t) == ROLE_DRUM)
#define IS_LOOP(t) (trk_role(t) == ROLE_LOOP)
#define NO_NOTES(t) (trk_role(t) >= ROLE_DRUM)   /* drums (lanes) and the loop (slices): no pitches to transpose or key */
#define USES_DV(t) (trk_role(t) >= ROLE_DRUM)    /* their voices: dvoice[t], not the pool */

/* samples (oc_smp.c): NSMP slots in the store, 8-bit mu-law mono; one-shots for the kick track (tuned by TUNE),
 * loops cut into 16 slices a bar for the loop track */
#define NSMP 8
enum { SK_ONESHOT, SK_LOOP };
typedef struct {
    uint32_t off, len;           /* in the sample area: start, samples */
    uint16_t rate;               /* Hz */
    uint8_t used, kind, root, bars, pad[2];   /* root: the MIDI note it sounds at; bars: a loop's length */
    char name[12];
} smp_t;
static smp_t smp_dir[NSMP];
static const uint8_t *smp_base;  /* the sample area as memory (XIP on the FM-1, RAM in host / emulator), 0 = none */
static uint32_t smp_slices(uint32_t k) { return smp_dir[k % NSMP].kind == SK_LOOP ? 16u * (smp_dir[k % NSMP].bars ? smp_dir[k % NSMP].bars : 1u) : 1u; }

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

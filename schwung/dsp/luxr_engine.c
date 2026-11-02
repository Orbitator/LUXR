/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): one engine - LUXR's eight drum voices (the synth layer after the Sonic Potions LXR
 * and the sample layer), its kit FX (drive, delay, reverb), master filter, compressor and limiter, all of it the
 * firmware's own code (firmware/src/lx_synth.c), unchanged. LUXR's sequencer and panel stay behind: Move plays the
 * sounds over MIDI and edits them through the parameters below.
 *
 * Built once for each slot: cc -DLXE_SLOT=k (luxr_engine.h). Everything here is static, so each copy is a machine.
 *
 * Notes in (NOTE MAP):  MOVE   36..43 the sounds 1..8 (a Move drum rack's first two pad rows), 44..51 the same with
 *                              accent; a velocity over 110 accents as well
 *                       GM     General MIDI drums (as LUXR's MIDI IN: 36 kick, 38 snare, 39 clap, 42 hat ...)
 *                       TONAL  every note plays the sound TONAL SND, C3 (60) its own pitch, a semitone a key
 * Parameters: k_* the kit (FX, master), sN_* sound N (1..8); kit / kit_count / kit_name the preset browser; state. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "luxr_engine.h"

#ifndef LXE_SLOT
#error "build with -DLXE_SLOT=k"
#endif
#define LXE_CAT2(a, b) a##b
#define LXE_CAT(a, b) LXE_CAT2(a, b)

#define OC_HOST 1
#define section(x) aligned(4)                    /* (the firmware's linker sections: none here) */
#include "oc_tables.h"

/* the sample layer's transients: once in the plugin (luxr_tables.c) */
#define LXR_TRANS_N 12
#define LXR_TRANS_LEN 2205
#define OWN_TRANS_N 16
#define OWN_TRANS_LEN 2205
#define MACH_TRANS_N 16
#define MACH_TRANS_LEN 1024
#define HID __attribute__((visibility("hidden")))
extern HID const int8_t lxe_LXR_TRANS[LXR_TRANS_N][LXR_TRANS_LEN];
extern HID const char *const lxe_LXR_TRANS_NAME[LXR_TRANS_N];
extern HID const int8_t lxe_OWN_TRANS[8][OWN_TRANS_N][OWN_TRANS_LEN];
extern HID const int8_t lxe_MACH_TRANS[8][MACH_TRANS_N][MACH_TRANS_LEN];
extern HID const char *const lxe_MACH_NAME[MACH_TRANS_N];
#define LXR_TRANS lxe_LXR_TRANS
#define OWN_TRANS lxe_OWN_TRANS
#define MACH_TRANS lxe_MACH_TRANS

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
#define RING_PUBLISH() ((void)0)
#include "lx_core.h"
#define LX_FILE_LAYER 1                          /* (the sample layer may be a WAV: lx_synth.c TR_FILE) */
#include "lx_synth.c"
#include "luxr_wav.h"
#include "luxr_user.h"

/* the parts of the firmware left behind: no sequencer (Move's), no parameter locks */
static void lock_apply(sound_t *s, uint32_t pid, uint32_t val) { (void)s; (void)pid; (void)val; }
static void seq_block(uint32_t n) { (void)n; }

/* ----------------------------------------------------------------- kits --- */
typedef struct {
    const char *name;
    sound_t s[NTRK];
    int8_t tune;
    uint8_t ddec, drv_mode, drv_amt, drv_tone, drv_lvl, dly_div, dly_fb, dly_tone, dly_lvl, dly_mode, dly_div2;
    uint8_t rev_size, rev_damp, rev_lvl, rev_pre, cthr, crat, catk, crel, cmak, clim;
    int8_t lceil;
    uint8_t lrel;
    int8_t mflt;
    uint8_t mres, pclip, alvl;
    int8_t atgt;
} lxk_kit_t;
#include "luxr_kits.h"

static void kit_apply(const lxk_kit_t *k)
{
    memcpy(snd, k->s, sizeof snd);
    song.tune = k->tune; song.ddec = k->ddec;
    song.drv_mode = k->drv_mode; song.drv_amt = k->drv_amt; song.drv_tone = k->drv_tone; song.drv_lvl = k->drv_lvl;
    song.dly_div = k->dly_div; song.dly_fb = k->dly_fb; song.dly_tone = k->dly_tone; song.dly_lvl = k->dly_lvl;
    song.dly_mode = k->dly_mode; song.dly_div2 = k->dly_div2;
    song.rev_size = k->rev_size; song.rev_damp = k->rev_damp; song.rev_lvl = k->rev_lvl; song.rev_pre = k->rev_pre;
    song.cthr = k->cthr; song.crat = k->crat; song.catk = k->catk; song.crel = k->crel; song.cmak = k->cmak;
    song.clim = k->clim; song.lceil = k->lceil; song.lrel = k->lrel;
    song.mflt = k->mflt; song.mres = k->mres; song.pclip = k->pclip; song.alvl = k->alvl; song.atgt = k->atgt;
}

/* ----------------------------------------------------------- parameters --- */
static uint8_t e_kit, e_vol = 100, e_map, e_tsnd;  /* the kit loaded, VOLUME, NOTE MAP, TONAL SND */
static uint8_t e_mch;                            /* MIDI CH: 0 every channel the slot passes on, else only that one */
/* (e_focus: the sound being edited - ui_voice: a pad hit or PERFORM; the grid follows) */
enum { PT_INT, PT_ENUM };
typedef struct {
    const char *key, *name;
    uint8_t type;
    int16_t min, max;
    const char *const *opt;                      /* PT_ENUM: max + 1 of them */
    const char *unit;
} pdef_t;

static const char *const O_DMODE[] = {"Soft", "Hard", "Fold"};
static const char *const O_DIV[] = {"1/8", "3/16", "1/4", "3/8", "1/2", "3/4"};
static const char *const O_DLMODE[] = {"Mono", "Stereo", "Ping"};
static const char *const O_RATIO[] = {"1:1", "1.5:1", "2:1", "3:1", "4:1", "6:1", "10:1", "Limit"};
static const char *const O_OFFON[] = {"Off", "On"};
static const char *const O_OGAIN[] = {"0 dB", "+3 dB", "+6 dB"};
static const char *const O_MAP[] = {"Move", "GM", "Tonal"};
static const char *const O_MCH[] = {"All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                    "16"};
static const char *const O_SND[] = {"Kick", "Snare", "Clap", "Cl Hat", "Op Hat", "Tom", "Rim", "Cymbal"};
static const char *const O_LDEST[] = {"Off", "Pitch", "Filter", "Decay", "Level", "Pan", "Sample"};
static const char *const O_LWAVE[] = {"Sine", "Tri", "Saw", "Square", "S&H"};
static const char *const O_LRATE[] = {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"};
static char o_smp_names[TR_N + 1][12];
static const char *o_smp[TR_N + 1];              /* the sample layer: OFF, SNAP, the LXR's, the sound's own, the machines',
                                                  * WAV (the sound's file) */
static const char *const O_SLEN[] = {"50 ms", "Whole"};
/* the WAV of each sound: its path, LENGTH (0: its first 50 ms, a transient; 1: the whole of it), the buffer playing */
static char f_path[NTRK][LXW_PATH];
static uint8_t f_whole[NTRK];
static const lxw_buf_t *f_cur[NTRK];
static lxw_loader_t *f_ld;

enum {                                           /* the kit */
    G_VOL, G_TUNE, G_DDEC, G_MAP, G_TSND, G_OGAIN,
    G_DRVM, G_DRVA, G_DRVT, G_DRVL, G_DLYD, G_DLYD2, G_DLYM, G_DLYF, G_DLYT, G_DLYL,
    G_REVS, G_REVD, G_REVP, G_REVL, G_MFLT, G_MRES, G_CTHR, G_CRAT, G_CATK, G_CREL, G_CMAK, G_PCLIP,
    G_CLIM, G_LCEIL, G_LREL, G_ALVL, G_ATGT, G_MCH, G_N
};
static const pdef_t GDEF[G_N] = {
    {"k_vol", "Volume", PT_INT, 0, 127, 0, 0},
    {"k_tune", "Tune", PT_INT, -24, 24, 0, "st"},
    {"k_decay", "Decay", PT_INT, 8, 127, 0, 0},
    {"k_map", "Note Map", PT_ENUM, 0, 2, O_MAP, 0},
    {"k_tsnd", "Tonal Snd", PT_ENUM, 0, 7, O_SND, 0},
    {"k_ogain", "Output", PT_ENUM, 0, 2, O_OGAIN, 0},
    {"k_drv_mode", "Drv Mode", PT_ENUM, 0, DM_N - 1, O_DMODE, 0},
    {"k_drv_amt", "Drv Gain", PT_INT, 0, 127, 0, 0},
    {"k_drv_tone", "Drv Tone", PT_INT, 0, 127, 0, 0},
    {"k_drv_lvl", "Drv Ret", PT_INT, 0, 127, 0, 0},
    {"k_dly_div", "Dly Time", PT_ENUM, 0, 5, O_DIV, 0},
    {"k_dly_div2", "Dly R", PT_ENUM, 0, 5, O_DIV, 0},
    {"k_dly_mode", "Dly Mode", PT_ENUM, 0, DL_N - 1, O_DLMODE, 0},
    {"k_dly_fb", "Feedback", PT_INT, 0, 127, 0, 0},
    {"k_dly_tone", "Dly Tone", PT_INT, 0, 127, 0, 0},
    {"k_dly_lvl", "Dly Ret", PT_INT, 0, 127, 0, 0},
    {"k_rev_size", "Size", PT_INT, 0, 127, 0, 0},
    {"k_rev_damp", "Damp", PT_INT, 0, 127, 0, 0},
    {"k_rev_pre", "Pre Dly", PT_INT, 0, 127, 0, 0},
    {"k_rev_lvl", "Rev Ret", PT_INT, 0, 127, 0, 0},
    {"k_mflt", "Filter", PT_INT, -64, 63, 0, 0},
    {"k_mres", "Reso", PT_INT, 0, 127, 0, 0},
    {"k_cthr", "Thresh", PT_INT, 0, 40, 0, 0},
    {"k_crat", "Ratio", PT_ENUM, 0, 7, O_RATIO, 0},
    {"k_catk", "Attack", PT_INT, 0, 127, 0, 0},
    {"k_crel", "Release", PT_INT, 0, 127, 0, 0},
    {"k_cmak", "Makeup", PT_INT, 0, 24, 0, "dB"},
    {"k_pclip", "Pre Clip", PT_INT, 0, 6, 0, "dB"},
    {"k_clim", "Limiter", PT_ENUM, 0, 1, O_OFFON, 0},
    {"k_lceil", "Ceiling", PT_INT, -12, 0, 0, "dB"},
    {"k_lrel", "Lim Rel", PT_INT, 0, 127, 0, 0},
    {"k_alvl", "Auto Lvl", PT_ENUM, 0, 1, O_OFFON, 0},
    {"k_atgt", "Target", PT_INT, -12, 0, 0, "dB"},
    {"k_mch", "MIDI Ch", PT_ENUM, 0, 16, O_MCH, 0},
};
static uint8_t *const GU8[G_N] = {           /* (the song's fields; 0: kept here, or signed) */
    0, 0, &song.ddec, 0, 0, &song.ogain,
    &song.drv_mode, &song.drv_amt, &song.drv_tone, &song.drv_lvl, &song.dly_div, &song.dly_div2, &song.dly_mode,
    &song.dly_fb, &song.dly_tone, &song.dly_lvl, &song.rev_size, &song.rev_damp, &song.rev_pre, &song.rev_lvl,
    0, &song.mres, &song.cthr, &song.crat, &song.catk, &song.crel, &song.cmak, &song.pclip,
    &song.clim, 0, &song.lrel, &song.alvl, 0, 0,
};
static int32_t g_get(uint32_t i)
{
    switch (i) {
    case G_VOL: return e_vol;
    case G_TUNE: return song.tune;
    case G_MAP: return e_map;
    case G_TSND: return e_tsnd;
    case G_MCH: return e_mch;
    case G_MFLT: return song.mflt;
    case G_LCEIL: return song.lceil;
    case G_ATGT: return song.atgt;
    }
    return GU8[i] ? *GU8[i] : 0;
}
static void master_set(void) { song.master_q12 = (uint16_t)((uint32_t)e_vol * e_vol * 2048u / 10000u); }
static void g_set(uint32_t i, int32_t v)
{
    v = clamp(v, GDEF[i].min, GDEF[i].max);
    switch (i) {
    case G_VOL: e_vol = (uint8_t)v; master_set(); return;
    case G_TUNE: song.tune = (int8_t)v; return;
    case G_MAP: e_map = (uint8_t)v; return;
    case G_TSND: e_tsnd = (uint8_t)v; return;
    case G_MCH: e_mch = (uint8_t)v; return;
    case G_MFLT: song.mflt = (int8_t)v; return;
    case G_LCEIL: song.lceil = (int8_t)v; return;
    case G_ATGT: song.atgt = (int8_t)v; return;
    }
    if (GU8[i])
        *GU8[i] = (uint8_t)v;
}

enum {                                           /* a sound */
    S_P1, S_P2, S_P3, S_P4, S_P5, S_P6, S_P7, S_P8,
    S_SMP, S_SLVL, S_SPITCH, S_SDEC, S_SSTART, S_STONE, S_SBEND, S_SYN,
    S_LEVEL, S_PAN, S_DLY, S_REV, S_DRV, S_ATK, S_GATE, S_REL,
    S_L1D, S_L1W, S_L1R, S_L1A, S_L2D, S_L2W, S_L2R, S_L2A,
    S_VAR, S_SLEN, S_N
};
static const pdef_t SDEF[S_N] = {
    {"p1", 0, PT_INT, 0, 127, 0, 0}, {"p2", 0, PT_INT, 0, 127, 0, 0}, {"p3", 0, PT_INT, 0, 127, 0, 0},
    {"p4", 0, PT_INT, 0, 127, 0, 0}, {"p5", 0, PT_INT, 0, 127, 0, 0}, {"p6", 0, PT_INT, 0, 127, 0, 0},
    {"p7", 0, PT_INT, 0, 127, 0, 0}, {"p8", 0, PT_INT, 0, 127, 0, 0},
    {"smp", "Sample", PT_ENUM, 0, TR_FILE, 0, 0},
    {"slvl", "Smp Lvl", PT_INT, 0, 127, 0, 0},
    {"spitch", "Smp Pitch", PT_INT, 0, 127, 0, 0},
    {"sdec", "Smp Decay", PT_INT, 0, 127, 0, 0},
    {"sstart", "Start", PT_INT, 0, 127, 0, 0},
    {"stone", "Smp Tone", PT_INT, -64, 63, 0, 0},
    {"sbend", "Bend", PT_INT, -64, 63, 0, 0},
    {"syn", "Synth Lvl", PT_INT, 0, 127, 0, 0},
    {"level", "Level", PT_INT, 0, 127, 0, 0},
    {"pan", "Pan", PT_INT, -64, 63, 0, 0},
    {"dly", "Delay", PT_INT, 0, 127, 0, 0},
    {"rev", "Reverb", PT_INT, 0, 127, 0, 0},
    {"drv", "Drive", PT_INT, 0, 127, 0, 0},
    {"atk", "Attack", PT_INT, 0, 127, 0, 0},
    {"gate", "Gate", PT_INT, 0, 64, 0, 0},
    {"rel", "Release", PT_INT, 0, 127, 0, 0},
    {"l1dest", "LFO1 Dest", PT_ENUM, 0, LD_N - 1, O_LDEST, 0},
    {"l1wave", "LFO1 Wave", PT_ENUM, 0, LW_N - 1, O_LWAVE, 0},
    {"l1rate", "LFO1 Rate", PT_ENUM, 0, LR_N - 1, O_LRATE, 0},
    {"l1amt", "LFO1 Amt", PT_INT, -64, 63, 0, 0},
    {"l2dest", "LFO2 Dest", PT_ENUM, 0, LD_N - 1, O_LDEST, 0},
    {"l2wave", "LFO2 Wave", PT_ENUM, 0, LW_N - 1, O_LWAVE, 0},
    {"l2rate", "LFO2 Rate", PT_ENUM, 0, LR_N - 1, O_LRATE, 0},
    {"l2amt", "LFO2 Amt", PT_INT, -64, 63, 0, 0},
    {"var", "Variation", PT_INT, 0, 255, 0, 0},
    {"slen", "Length", PT_ENUM, 0, 1, O_SLEN, 0},
};
static int32_t s_get(uint32_t t, uint32_t i)
{
    const sound_t *s = &snd[t % NTRK];
    if (i <= S_P8)
        return s->p[i];
    switch (i) {
    case S_SMP: return s->smp;
    case S_SLVL: return s->slvl;
    case S_SPITCH: return s->spitch;
    case S_SDEC: return s->sdec;
    case S_SSTART: return s->sstart;
    case S_STONE: return s->stone;
    case S_SBEND: return s->sbend;
    case S_SYN: return s->syn;
    case S_LEVEL: return s->level;
    case S_PAN: return s->pan;
    case S_DLY: return s->dly;
    case S_REV: return s->rev;
    case S_DRV: return s->drv;
    case S_ATK: return s->atk;
    case S_GATE: return s->gate;
    case S_REL: return s->rel;
    case S_L1D: case S_L2D: return s->lfo[i >= S_L2D].dest;
    case S_L1W: case S_L2W: return s->lfo[i >= S_L2D].wave;
    case S_L1R: case S_L2R: return s->lfo[i >= S_L2D].rate;
    case S_L1A: case S_L2A: return s->lfo[i >= S_L2D].depth;
    case S_VAR: return s->preset;
    case S_SLEN: return f_whole[t % NTRK];
    }
    return 0;
}
static uint8_t e_restoring;                      /* (state: VARIATION's number only, not the sound rolled again) */
static void s_set(uint32_t t, uint32_t i, int32_t v)
{
    sound_t *s = &snd[t % NTRK];
    v = clamp(v, SDEF[i].min, SDEF[i].max);
    if (i <= S_P8) {
        s->p[i] = (uint8_t)v;
        return;
    }
    switch (i) {
    case S_SMP: s->smp = (uint8_t)v; break;
    case S_SLVL: s->slvl = (uint8_t)v; break;
    case S_SPITCH: s->spitch = (uint8_t)v; break;
    case S_SDEC: s->sdec = (uint8_t)v; break;
    case S_SSTART: s->sstart = (uint8_t)v; break;
    case S_STONE: s->stone = (int8_t)v; break;
    case S_SBEND: s->sbend = (int8_t)v; break;
    case S_SYN: s->syn = (uint8_t)v; break;
    case S_LEVEL: s->level = (uint8_t)v; break;
    case S_PAN: s->pan = (int8_t)v; break;
    case S_DLY: s->dly = (uint8_t)v; break;
    case S_REV: s->rev = (uint8_t)v; break;
    case S_DRV: s->drv = (uint8_t)v; break;
    case S_ATK: s->atk = (uint8_t)v; break;
    case S_GATE: s->gate = (uint8_t)v; break;
    case S_REL: s->rel = (uint8_t)v; break;
    case S_L1D: case S_L2D: s->lfo[i >= S_L2D].dest = (uint8_t)v; break;
    case S_L1W: case S_L2W: s->lfo[i >= S_L2D].wave = (uint8_t)v; break;
    case S_L1R: case S_L2R: s->lfo[i >= S_L2D].rate = (uint8_t)v; break;
    case S_L1A: case S_L2A: s->lfo[i >= S_L2D].depth = (int8_t)v; break;
    case S_SLEN:                                 /* LENGTH: the WAV's first 50 ms (a transient) or the whole of it */
        f_whole[t % NTRK] = (uint8_t)v;
        lx_file[t % NTRK].whole = (uint8_t)v;
        break;
    case S_VAR:                                  /* VARIATION: the sound rolled again (0: the factory one), as PRESETS */
        if (e_restoring)
            s->preset = (uint8_t)v;
        else if (v != s->preset)
            sound_preset(t, (uint32_t)v);
        break;
    }
}
static const char *s_name(uint32_t t, uint32_t i)   /* the synth layer's eight: by the sound's kind */
{
    static char nm[NTRK][8][12];
    if (i > S_P8)
        return SDEF[i].name;
    if (!nm[t][i][0]) {
        const char *l = LX_LABEL[TRK_KIND[t]][i];
        uint32_t k = 0, up = 1;
        for (; l[k] && k + 1u < sizeof nm[t][i]; k++) {   /* "P.DEC" -> "P.Dec" (the grid's labels read better) */
            char c = l[k];
            nm[t][i][k] = up ? c : (c >= 'A' && c <= 'Z' ? (char)(c + 32) : c);
            up = c == '.' || c == ' ';
        }
        nm[t][i][k] = 0;
    }
    return nm[t][i];
}

/* a key: the kit's (G_*) or a sound's (sN_ + S_*); -1 none */
static uint8_t e_focus;
static int key_find(const char *key, uint32_t *t, uint32_t *i)   /* sN_<key>: sound N; <key> alone: the sound in focus */
{
    uint32_t k;
    if (key[0] == 's' && key[1] >= '1' && key[1] <= '8' && key[2] == '_') {
        for (k = 0; k < S_N; k++)
            if (!strcmp(key + 3, SDEF[k].key)) {
                *t = (uint32_t)(key[1] - '1');
                *i = k;
                return 1;
            }
        return -1;
    }
    for (k = 0; k < G_N; k++)
        if (!strcmp(key, GDEF[k].key)) {
            *i = k;
            return 0;
        }
    for (k = 0; k < S_N; k++)
        if (!strcmp(key, SDEF[k].key)) {
            *t = e_focus;
            *i = k;
            return 1;
        }
    return -1;
}
static int32_t val_parse(const pdef_t *d, const char *v, int *ok)   /* a number, or an enum's option by name */
{
    char *end;
    double x = strtod(v, &end);
    *ok = 1;
    if (end != v)
        return (int32_t)(x < 0 ? x - 0.5 : x + 0.5);
    if (d->type == PT_ENUM && d->opt) {
        int32_t k;
        for (k = 0; k <= d->max; k++)
            if (!strcmp(v, d->opt[k]))
                return k;
    }
    *ok = 0;
    return 0;
}

/* ------------------------------------------------------------------ json --- */
typedef struct { char *b; int n, cap; } jb_t;
static void jb_put(jb_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void jb_put(jb_t *j, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (j->n >= j->cap)
        return;
    va_start(ap, fmt);
    r = vsnprintf(j->b + j->n, (size_t)(j->cap - j->n), fmt, ap);
    va_end(ap);
    j->n = r < 0 ? j->cap : j->n + r > j->cap ? j->cap : j->n + r;
}
static void jb_param(jb_t *j, const char *key, const char *name, const pdef_t *d)
{
    jb_put(j, "{\"key\":\"%s\",\"name\":\"%s\",", key, name);
    if (d->type == PT_ENUM) {
        int32_t k;
        jb_put(j, "\"type\":\"enum\",\"options\":[");
        for (k = 0; k <= d->max; k++)
            jb_put(j, "%s\"%s\"", k ? "," : "", d->opt ? d->opt[k] : o_smp[k]);
        jb_put(j, "]}");
        return;
    }
    jb_put(j, "\"type\":\"int\",\"min\":%d,\"max\":%d,\"step\":1", d->min, d->max);
    if (d->unit)
        jb_put(j, ",\"unit\":\"%s\"", d->unit);
    jb_put(j, "}");
}
/* The pages and the eight knobs. ROOT: the kit's performance macros (knob 1 VOLUME .. 8 REVERB RETURN); then SOUND - one
 * level for all eight sounds (a child level: the grid shows the sound in focus only, and a pad hit brings you to it);
 * page 1 its synth layer in LUXR's EDIT order (knobs 1-4 LUXR's row A, 5-8 the row B LUXR has under SHIFT), page 2 the
 * sample layer, page 3 mix and envelope (LEVEL PAN DELAY REVERB | DRIVE ATTACK GATE RELEASE), page 4 the LFOs (1 on
 * knobs 1-4, 2 on 5-8), page 5 the WAV, LENGTH, VARIATION. Then the kit's pages. PERFORM (perform.js) puts two of these
 * pages on the knobs at once: the first as they are, the second with SHIFT held. Both documents are fixed: built once,
 * when an instance is made, and served as they are (a get_param runs on the audio thread). */
static void jb_str(jb_t *j, const char *v)       /* a JSON string, escaped */
{
    jb_put(j, "\"");
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        if (c == '"' || c == '\\')
            jb_put(j, "\\%c", c);
        else if (c < 0x20u)
            jb_put(j, "\\u%04x", c);
        else
            jb_put(j, "%c", c);
    }
    jb_put(j, "\"");
}
static void jb_file(jb_t *j, const char *key)    /* a sound's WAV: Schwung's file browser, from Move's samples */
{
    jb_put(j, "{\"key\":\"%s\",\"name\":\"WAV File\",\"type\":\"filepath\",\"root\":\"/data/UserData\","
              "\"start_path\":\"/data/UserData/UserLibrary/Samples\",\"filter\":[\".wav\",\".WAV\"]}", key);
}
static const uint8_t KIT_ROOT[8] = {G_VOL, G_TUNE, G_DDEC, G_MFLT, G_MRES, G_DRVA, G_DLYL, G_REVL};
static const uint8_t KIT_PAGE[][8] = {           /* (G_*; 0xFF a gap) */
    {G_DRVM, G_DRVA, G_DRVT, G_DRVL, G_REVS, G_REVD, G_REVP, G_REVL},
    {G_DLYM, G_DLYD, G_DLYD2, G_DLYF, G_DLYT, G_DLYL, 0xFF, 0xFF},
    {G_CTHR, G_CRAT, G_CATK, G_CREL, G_CMAK, G_CLIM, G_LCEIL, G_LREL},
    {G_OGAIN, G_MAP, G_TSND, G_MCH, G_PCLIP, G_ALVL, G_ATGT, 0xFF},
};
static const char *const KIT_PAGE_NAME[] = {"Drive & Reverb", "Delay", "Comp & Limiter", "Setup"};
static const char *const KIT_PAGE_ID[] = {"fx", "delay", "dyn", "setup"};
/* the synth layer's eight on the SOUND pages: one name for every sound's (the kinds differ; PERFORM names them all) */
static const char *const SYN_NAME[8] = {"Pitch", "Decay", "Syn 3", "Syn 4", "Syn 5", "Syn 6", "Syn 7", "Drive"};
#define PERFORM_JSON "{\"key\":\"perform\",\"name\":\"Perform\",\"type\":\"canvas\",\"canvas_script\":\"perform.js\"," \
                     "\"enterable\":true,\"show_footer\":false,\"show_value\":false,\"extra_keys\":[\"k_cpu\"]," \
                     "\"fullscreen_live_ms\":500}"
#define CPU_JSON "{\"key\":\"k_cpu\",\"name\":\"CPU\",\"type\":\"string\",\"access\":\"read\"}"
#define SAVE_JSON "{\"key\":\"k_save\",\"name\":\"Save Kit\",\"type\":\"string\"}," \
                  "{\"key\":\"k_delete\",\"name\":\"Delete Kit\",\"type\":\"enum\",\"options\":[\"-\",\"Delete\"]," \
                  "\"access\":\"write\"}"
static void knobs_put(jb_t *j, const uint8_t *g, uint32_t n)   /* a knob list of G_* (0xFF: null), trailing gaps dropped */
{
    uint32_t i, last = 0;
    for (i = 0; i < n; i++)
        if (g[i] != 0xFF)
            last = i;
    jb_put(j, "\"knobs\":[");
    for (i = 0; i <= last; i++) {
        if (i)
            jb_put(j, ",");
        if (g[i] == 0xFF)
            jb_put(j, "null");
        else
            jb_put(j, "\"%s\"", GDEF[g[i]].key);
    }
    jb_put(j, "]");
}
static void gparams_put(jb_t *j, const uint8_t *g, uint32_t n)
{
    uint32_t i, k = 0;
    for (i = 0; i < n; i++)
        if (g[i] != 0xFF) {
            if (k++)
                jb_put(j, ",");
            jb_param(j, GDEF[g[i]].key, GDEF[g[i]].name, &GDEF[g[i]]);
        }
}
static void hierarchy(jb_t *j)
{
    static const char *const ROLE[NTRK] = {"kick", "snare", "clap", "hat", "hat", "tom", "rim", "cymbal"};
    uint32_t t, k, i;
    jb_put(j, "{\"pad_layout\":\"drums\",\"focus_param\":\"ui_voice\",\"focus_press_param\":\"ui_press\",\"levels\":{"
              "\"root\":{\"name\":\"LUXR\",\"list_param\":\"kit\",\"count_param\":\"kit_count\",\"name_param\":\"kit_name\","
              "\"params\":[" PERFORM_JSON ",");
    gparams_put(j, KIT_ROOT, 8);
    jb_put(j, ",{\"level\":\"sound\",\"label\":\"Sounds\"}");
    for (k = 0; k < NELEM(KIT_PAGE); k++)
        jb_put(j, ",{\"level\":\"%s\",\"label\":\"%s\"}", KIT_PAGE_ID[k], KIT_PAGE_NAME[k]);
    jb_put(j, "],");
    knobs_put(j, KIT_ROOT, 8);
    /* the sounds: one level, an instance a sound (s1_ .. s8_), the notes of the MOVE map; the focus is ours (ui_voice:
     * a pad hit - ui_press and its note together - or PERFORM), so no child_index_param here */
    jb_put(j, "},\"sound\":{\"name\":\"Sound\",\"child_count\":%u,\"child_label\":\"Sound\",\"child_key_template\":"
              "\"s{index}_{key}\",\"child_index_base\":1,\"child_press_param\":\"ui_press\",\"child_notes\":[", NTRK);
    for (t = 0; t < NTRK; t++)
        jb_put(j, "%s%u", t ? "," : "", 36u + t);
    jb_put(j, "],\"child_names\":[");
    for (t = 0; t < NTRK; t++)
        jb_put(j, "%s\"%s\"", t ? "," : "", O_SND[t]);
    jb_put(j, "],\"child_roles\":[");
    for (t = 0; t < NTRK; t++)
        jb_put(j, "%s\"%s\"", t ? "," : "", ROLE[t]);
    jb_put(j, "],\"params\":[");
    for (i = 0; i < S_N; i++) {
        if (i)
            jb_put(j, ",");
        jb_param(j, SDEF[i].key, i <= S_P8 ? SYN_NAME[i] : SDEF[i].name, &SDEF[i]);
    }
    jb_put(j, ",");
    jb_file(j, "file");
    jb_put(j, "],\"knobs\":[");
    for (i = 0; i < S_VAR; i++)
        jb_put(j, "%s\"%s\"", i ? "," : "", SDEF[i].key);
    jb_put(j, ",\"file\",\"slen\",\"var\"]}");      /* (page 5: the WAV, LENGTH, VARIATION) */
    for (k = 0; k < NELEM(KIT_PAGE); k++) {
        jb_put(j, ",\"%s\":{\"name\":\"%s\",\"params\":[", KIT_PAGE_ID[k], KIT_PAGE_NAME[k]);
        gparams_put(j, KIT_PAGE[k], 8);
        if (!strcmp(KIT_PAGE_ID[k], "setup"))
            jb_put(j, "," SAVE_JSON "," CPU_JSON);
        jb_put(j, "],");
        knobs_put(j, KIT_PAGE[k], 8);
        jb_put(j, "}");
    }
    jb_put(j, "}}");
}
/* chain_params: EVERY key the host may look up - each of the kit's, each of a sound's once (bare: the host maps
 * s1_ .. s8_ onto them by the sounds' template, which module.json repeats for it; bare, they address the sound in focus),
 * the WAV's browser, PERFORM and SAVE / DELETE. A key missing here costs the host a full re-read of the list every
 * 250 ms (with 1 MB of parsing, on the audio thread) for as long as it is automated, modulated or locked. */
static void chain_params(jb_t *j)
{
    uint32_t i;
    jb_put(j, "[" PERFORM_JSON "," SAVE_JSON "," CPU_JSON);
    for (i = 0; i < G_N; i++) {
        jb_put(j, ",");
        jb_param(j, GDEF[i].key, GDEF[i].name, &GDEF[i]);
    }
    for (i = 0; i < S_N; i++) {
        jb_put(j, ",");
        jb_param(j, SDEF[i].key, i <= S_P8 ? SYN_NAME[i] : SDEF[i].name, &SDEF[i]);
    }
    jb_put(j, ",");
    jb_file(j, "file");
    jb_put(j, "]");
}
static char doc_hier[48 * 1024], doc_cp[64 * 1024];   /* (built once: e_init) */
static int doc_hier_n, doc_cp_n;
static void docs_build(void)
{
    jb_t j1 = {doc_hier, 0, (int)sizeof doc_hier - 1}, j2 = {doc_cp, 0, (int)sizeof doc_cp - 1};
    hierarchy(&j1);
    chain_params(&j2);
    doc_hier_n = j1.n < j1.cap ? j1.n : -1;
    doc_cp_n = j2.n < j2.cap ? j2.n : -1;
    if (doc_hier_n >= 0) doc_hier[doc_hier_n] = 0;
    if (doc_cp_n >= 0) doc_cp[doc_cp_n] = 0;
}

/* state: the kit's number and every value, as "key":number (the host stores it with the set and the presets) */
static void jb_kv(jb_t *j, char snd, const char *key, int32_t v)   /* ,"[sN_]key":v - by hand: the host reads the
                                                                     * state often (autosave, presets), on the audio thread */
{
    char b[48], *p = b, d[12];
    uint32_t u = (uint32_t)(v < 0 ? -v : v), n = 0;
    *p++ = ',';
    *p++ = '"';
    if (snd) {
        *p++ = 's';
        *p++ = snd;
        *p++ = '_';
    }
    while (*key && p < b + 36)
        *p++ = *key++;
    *p++ = '"';
    *p++ = ':';
    if (v < 0)
        *p++ = '-';
    do d[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < sizeof d);
    while (n)
        *p++ = d[--n];
    if (j->n + (p - b) >= j->cap) {
        j->n = j->cap;
        return;
    }
    memcpy(j->b + j->n, b, (size_t)(p - b));
    j->n += (int)(p - b);
}
static void state_get(jb_t *j)
{
    uint32_t t, i;
    jb_put(j, "{\"v\":1,\"kit\":%u", e_kit);
    for (i = 0; i < G_N; i++)
        jb_kv(j, 0, GDEF[i].key, g_get(i));
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < S_N; i++)
            jb_kv(j, (char)('1' + t), SDEF[i].key, s_get(t, i));
    for (t = 0; t < NTRK; t++)
        if (f_path[t][0]) {
            jb_put(j, ",\"s%u_file\":", t + 1u);
            jb_str(j, f_path[t]);
        }
    jb_put(j, "}");
}
static void set_value(const char *key, const char *val);
static void file_set(uint32_t t, const char *path);
static void state_set(const char *js)            /* a flat object: numbers, and the WAVs' paths (strings) */
{
    const char *p = js;
    char key[24], val[LXW_PATH];
    uint32_t t;
    for (t = 0; t < NTRK; t++)                   /* (a state without a WAV for a sound: none) */
        if (f_path[t][0] && !strstr(js, "_file\""))
            file_set(t, "");
    while ((p = strchr(p, '"'))) {
        const char *q = strchr(p + 1, '"');
        size_t n;
        if (!q)
            return;
        n = (size_t)(q - p - 1);
        if (n >= sizeof key) {
            p = q + 1;
            continue;
        }
        memcpy(key, p + 1, n);
        key[n] = 0;
        p = q + 1;
        while (*p == ' ' || *p == ':')
            p++;
        if (*p == '"') {                         /* a string: unescaped */
            for (p++, n = 0; *p && *p != '"'; p++) {
                char c = *p;
                if (c == '\\' && p[1]) {
                    c = *++p;
                    if (c == 'u' && p[1] && p[2] && p[3] && p[4]) {
                        char hx[5] = {p[1], p[2], p[3], p[4], 0};
                        c = (char)strtol(hx, 0, 16);
                        p += 4;
                    }
                }
                if (n + 1u < sizeof val)
                    val[n++] = c;
            }
            val[n] = 0;
            if (*p)
                p++;
            if (key[0] == 's' && key[1] >= '1' && key[1] <= '8' && !strcmp(key + 2, "_file")) {
                e_restoring = 1;                 /* (the file only: SAMPLE stays as the state has it) */
                file_set((uint32_t)(key[1] - '1'), val);
                e_restoring = 0;
            }
            continue;
        }
        if (!((*p >= '0' && *p <= '9') || *p == '-'))
            continue;
        for (n = 0; n + 1u < sizeof val && ((p[n] >= '0' && p[n] <= '9') || p[n] == '-' || p[n] == '.'); n++)
            val[n] = p[n];
        val[n] = 0;
        p += n;
        if (!strcmp(key, "kit")) {               /* (the number only: the values follow) */
            e_kit = (uint8_t)clamp(atoi(val), 0, LXK_NKITS - 1);
            continue;
        }
        e_restoring = 1;
        set_value(key, val);
        e_restoring = 0;
    }
}

/* ------------------------------------------------------------- the ops --- */
/* the kits: LUXR's own (LXK_KITS: the preset patches, Factory last) and then yours (luxr_user.c, files) */
static lxu_t *f_kits;
static uint8_t k_guard;                          /* a state just restored: the host's "kit" that follows it (the
                                                  * browser's selection, re-applied) must not overwrite the sound */
static uint8_t k_pend;                           /* a kit of yours being read: its number + 1 in the list (0 none) */
static char k_saving[LXU_NAME];                  /* the kit of yours being saved (selected once it is on disk) */
static char k_text[LXU_TEXT];                    /* (a state for the file: built here, copied by lxu_save) */
static uint32_t kit_count(void) { return LXK_NKITS + lxu_count(f_kits); }
static const char *kit_name(uint32_t k) { return k < LXK_NKITS ? LXK_KITS[k].name : lxu_name(f_kits, k - LXK_NKITS); }
static void kit_load(uint32_t k)
{
    uint32_t t;
    if (k >= LXK_NKITS) {                        /* yours: read by the worker, set when it is there (e_render) */
        if (!lxu_load(f_kits, k - LXK_NKITS))
            k_pend = (uint8_t)(k + 1u);
        return;
    }
    e_kit = (uint8_t)k;
    kit_apply(&LXK_KITS[e_kit]);
    for (t = 0; t < NTRK; t++)
        voice[t].active = ghost[t].active = 0;
}
static void kit_save(const char *name)           /* SAVE KIT: the state as a file of yours (the worker writes it) */
{
    jb_t j = {k_text, 0, (int)sizeof k_text - 1};
    state_get(&j);
    if (j.n >= j.cap || lxu_save(f_kits, name, k_text, (uint32_t)j.n))
        return;
    snprintf(k_saving, sizeof k_saving, "%s", name);
}
/* the sound in focus: a pad HIT (Schwung's press - ui_press - and the note it played, within 100 ms of each other) or
 * PERFORM; served as "<count>:<sound>" so that hitting the same pad again brings the pages back to it. A press takes
 * the first note after it; only when none comes within the window, the last note before it (a sequenced note just
 * before a finger must not win over the finger's own) */
#define PRESS_WIN 4410u
static uint32_t e_clk;                           /* samples rendered */
static uint32_t e_quiet;                         /* blocks in a row without a sound (QUIET: e_render) */
/* the CPU meter: render time a block, over each second - its average and peak (CPU, the log) */
static uint32_t cpu_sum, cpu_max, cpu_n, cpu_busy, cpu_vmax, cpu_avg_last, cpu_peak_last;
static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static uint32_t pr_at, nt_at, focus_n;
static uint8_t pr_on, nt_on, nt_snd;
static void focus_to(uint32_t t)
{
    e_focus = (uint8_t)(t % NTRK);
    focus_n++;
}
static void press_note(uint32_t t)               /* a note played now */
{
    if (pr_on && e_clk - pr_at <= PRESS_WIN) {   /* (after the press: its note) */
        focus_to(t);
        pr_on = nt_on = 0;
        return;
    }
    nt_on = 1;
    nt_at = e_clk;
    nt_snd = (uint8_t)t;
}
static void press_tick(void)                     /* (a block's start) a press with no note after it: the one before */
{
    if (pr_on && e_clk - pr_at > PRESS_WIN) {
        if (nt_on && pr_at - nt_at <= PRESS_WIN && (int32_t)(pr_at - nt_at) >= 0)
            focus_to(nt_snd);
        pr_on = nt_on = 0;
    }
}
static void file_set(uint32_t t, const char *path)   /* the WAV asked for (the loader reads it; no I/O here) */
{
    size_t n = strlen(path);
    if (n >= LXW_PATH)
        return;
    memcpy(f_path[t], path, n + 1u);
    lxw_request(f_ld, t, path);
    if (n && !e_restoring)                       /* (a file chosen: the sample layer plays it) */
        snd[t].smp = TR_FILE;
}
static void e_fini(void)
{
    uint32_t t;
    lxw_stop(f_ld);                              /* (the worker gone first: it serves the kits too) */
    f_ld = 0;
    lxu_close(f_kits);
    f_kits = 0;
    for (t = 0; t < NTRK; t++) {
        lxw_free(f_cur[t]);
        f_cur[t] = 0;
        memset(&lx_file[t], 0, sizeof lx_file[t]);
    }
}
static void e_init(uint32_t seed)
{
    uint32_t i;
    memset(voice, 0, sizeof voice);
    memset(ghost, 0, sizeof ghost);
    memset(&song, 0, sizeof song);
    rng_state = seed | 1u;
    song.bpm = 120;
    song.chain = 1;
    song.ogain = 1;                              /* (+3 dB: LUXR's headroom is for a headphone out) */
    e_vol = 100;
    e_map = 0;
    e_tsnd = 0;
    e_mch = 0;
    e_focus = 0;
    e_clk = pr_at = nt_at = focus_n = e_quiet = 0;
    cpu_sum = cpu_max = cpu_n = cpu_busy = cpu_vmax = cpu_avg_last = cpu_peak_last = 0;
    pr_on = nt_on = 0;
    k_pend = k_guard = 0;
    k_saving[0] = 0;
    fx_still = 0;
    master_set();
    if (!f_kits)
        f_kits = lxu_open();
    if (!f_ld)
        f_ld = lxw_start(NTRK, lxu_poll, f_kits);
    memset(f_path, 0, sizeof f_path);
    memset(f_whole, 0, sizeof f_whole);
    rv_init();
    snprintf(o_smp_names[TR_FILE], sizeof o_smp_names[TR_FILE], "WAV");
    o_smp[TR_FILE] = o_smp_names[TR_FILE];
    for (i = 0; i < TR_N; i++) {                 /* the sample layer's names */
        char *n = o_smp_names[i];
        if (i == TR_OFF) snprintf(n, sizeof o_smp_names[i], "Off");
        else if (i == TR_SNAP) snprintf(n, sizeof o_smp_names[i], "Snap");
        else if (i < TR_OWN0) snprintf(n, sizeof o_smp_names[i], "LXR %s", lxe_LXR_TRANS_NAME[i - TR_SMP0]);
        else if (i < TR_MACH0) snprintf(n, sizeof o_smp_names[i], "Own %u", i - TR_OWN0 + 1u);
        else snprintf(n, sizeof o_smp_names[i], "%s", lxe_MACH_NAME[i - TR_MACH0]);
        o_smp[i] = n;
    }
    if (!doc_hier_n)                             /* (once: the documents are the same for every instance) */
        docs_build();
    kit_load(0);                                 /* the first preset (Patch 1), as LUXR at power-on */
}
static void hit(uint32_t t, uint32_t vel, uint32_t acc, int32_t semi)
{
    hit_semi = (int8_t)clamp(semi, -36, 36);
    voice_hit(t % NTRK, vel, acc, PAN_SOUND);
    hit_semi = 0;
}
static int32_t midi_track_gm(uint32_t n)         /* General MIDI drums (as lx_seq.c midi_track) */
{
    switch (n) {
    case 35: case 36: return 0;
    case 38: case 40: return 1;
    case 39: return 2;
    case 42: case 44: return 3;
    case 46: return 4;
    case 41: case 43: case 45: case 47: case 48: case 50: return 5;
    case 37: return 6;
    case 49: case 51: case 52: case 55: case 57: case 59: return 7;
    }
    return -1;
}
static void e_midi(const uint8_t *m, int len)
{
    uint32_t st, n, v;
    int32_t t;
    if (len < 3)
        return;
    st = m[0] & 0xF0u;
    n = m[1] & 0x7Fu;
    v = m[2] & 0x7Fu;
    if (st != 0x90u || !v)                       /* (drums: a note off means nothing; GATE times the release) */
        return;
    if (e_mch && (uint32_t)(m[0] & 0x0Fu) + 1u != e_mch)   /* MIDI CH: notes of other channels are not LUXR's */
        return;
    switch (e_map) {
    case 1:                                      /* GM */
        if ((t = midi_track_gm(n)) >= 0)
            hit((uint32_t)t, v, v > 110u, 0);
        break;
    case 2:                                      /* TONAL: the sound chosen, C3 its own pitch */
        hit((uint32_t)(t = e_tsnd), v, v > 110u, (int32_t)n - 60);
        break;
    default:                                     /* MOVE: the pads' first row 1..8, the second the same accented */
        t = n >= 36u && n < 52u ? (int32_t)((n - 36u) % 8u) : -1;
        if (t >= 0)
            hit((uint32_t)t, v, n >= 44u || v > 110u, 0);
        break;
    }
    if (t >= 0)                                  /* (a note: a pad's, if a press comes with it) */
        press_note((uint32_t)t);
}
static void set_value(const char *key, const char *val)
{
    uint32_t t = 0, i = 0;
    int k = key_find(key, &t, &i), ok;
    int32_t v;
    if (k < 0)
        return;
    v = val_parse(k ? &SDEF[i] : &GDEF[i], val, &ok);
    if (!ok)
        return;
    if (k)
        s_set(t, i, v);
    else
        g_set(i, v);
}
static void e_set(const char *key, const char *val)
{
    if (!strcmp(key, "kit")) {
        uint32_t k = (uint32_t)clamp(atoi(val), 0, (int32_t)kit_count() - 1);
        if (k_guard && k == e_kit) {             /* (the kit the state was made from: keep the state's sound) */
            k_guard = 0;
            return;
        }
        k_guard = 0;
        kit_load(k);
    } else if (!strcmp(key, "k_save")) {         /* SAVE KIT: its name */
        kit_save(val);
    } else if (!strcmp(key, "k_delete")) {       /* DELETE KIT: the kit of yours loaded */
        if ((!strcmp(val, "Delete") || !strcmp(val, "1")) && e_kit >= LXK_NKITS)
            lxu_delete(f_kits, e_kit - LXK_NKITS);
    } else if (!strcmp(key, "ui_press")) {       /* a finger on a pad (its note comes on its own) */
        pr_on = 1;
        pr_at = e_clk;
    } else if (!strcmp(key, "state")) {
        state_set(val);
        k_guard = 1;
    } else if (key[0] == 's' && key[1] >= '1' && key[1] <= '8' && !strcmp(key + 2, "_file")) {
        file_set((uint32_t)(key[1] - '1'), val);
    } else if (!strcmp(key, "file")) {           /* (bare: the sound in focus) */
        file_set(e_focus, val);
    } else if (!strcmp(key, "ui_voice")) {       /* "snd3", "<count>:2" or 2 (0-based) */
        const char *c = strchr(val, ':');
        int n = !strncmp(val, "snd", 3) ? atoi(val + 3) - 1 : atoi(c ? c + 1 : val);
        if (n >= 0 && n < NTRK)
            focus_to((uint32_t)n);
    } else {
        set_value(key, val);
    }
}
static int e_get(const char *key, char *buf, int buf_len)
{
    jb_t j = {buf, 0, buf_len > 0 ? buf_len - 1 : 0};
    uint32_t t = 0, i = 0;
    int k;
    if (buf_len <= 0)
        return -1;
    buf[0] = 0;
    if (!strcmp(key, "ui_hierarchy") || !strcmp(key, "chain_params")) {   /* (fixed: built once) */
        int n = key[0] == 'u' ? doc_hier_n : doc_cp_n;
        if (n < 0 || n >= buf_len)
            return -1;
        memcpy(buf, key[0] == 'u' ? doc_hier : doc_cp, (size_t)n + 1u);
        return n;
    } else if (!strcmp(key, "state")) {
        state_get(&j);
    } else if (!strcmp(key, "ui_voice")) {
        jb_put(&j, "%u:%u", focus_n, e_focus);
    } else if (!strcmp(key, "ui_press")) {
        jb_put(&j, "0");
    } else if (!strcmp(key, "k_save")) {         /* (the keyboard opens on the kit's name) */
        jb_put(&j, "%s", kit_name(e_kit));
    } else if (!strcmp(key, "k_delete")) {
        jb_put(&j, "-");
    } else if (!strcmp(key, "k_cpu")) {          /* the last second: average and peak, of the 2.9 ms of a block */
        jb_put(&j, "%u%% pk %u%%", cpu_avg_last * 100u / 2902u, cpu_peak_last * 100u / 2902u);
    } else if (!strcmp(key, "k_error")) {
        jb_put(&j, "%s", lxu_error(f_kits));
    } else if (key[0] == 's' && key[1] >= '1' && key[1] <= '8' && !strcmp(key + 2, "_file")) {
        jb_put(&j, "%s", f_path[key[1] - '1']);
    } else if (!strcmp(key, "file")) {
        jb_put(&j, "%s", f_path[e_focus]);
    } else if (key[0] == 's' && key[1] >= '1' && key[1] <= '8' && !strcmp(key + 2, "_file_status")) {
        static const char *const ST[] = {"Failed", "None", "Loading", "Loaded"};
        jb_put(&j, "%s", ST[clamp(lxw_status(f_ld, (uint32_t)(key[1] - '1')), -1, 2) + 1]);
    } else if (!strcmp(key, "name")) {
        jb_put(&j, "LUXR");
    } else if (!strcmp(key, "kit")) {
        jb_put(&j, "%u", e_kit);
    } else if (!strcmp(key, "kit_count")) {
        jb_put(&j, "%u", kit_count());
    } else if (!strcmp(key, "kit_name")) {
        jb_put(&j, "%s", kit_name(e_kit));
    } else if ((k = key_find(key, &t, &i)) >= 0) {
        jb_put(&j, "%d", (int)(k ? s_get(t, i) : g_get(i)));
    } else {
        return -1;
    }
    if (j.n >= j.cap)                            /* (cut short: say nothing rather than half a JSON) */
        return -1;
    buf[j.n] = 0;
    return j.n;
}
static void e_render_(int16_t *out, int frames, float bpm, double beat);
static void e_render(int16_t *out, int frames, float bpm, double beat)
{
    double t0 = now_us();
    uint32_t us, t, nv = 0;
    e_render_(out, frames, bpm, beat);
    us = (uint32_t)(now_us() - t0);
    for (t = 0; t < NTRK; t++)
        nv += voice[t].active + ghost[t].active;
    cpu_sum += us;
    cpu_n++;
    if (us > cpu_max) cpu_max = us;
    if (nv > cpu_vmax) cpu_vmax = nv;
    cpu_busy |= nv != 0;
    if (cpu_n >= 345u) {                         /* (a second) */
        cpu_avg_last = cpu_sum / cpu_n;
        cpu_peak_last = cpu_max;
        if (cpu_busy)
            lxu_cpu(f_kits, cpu_avg_last, cpu_peak_last, cpu_vmax, kit_name(e_kit));
        cpu_sum = cpu_max = cpu_n = cpu_busy = cpu_vmax = 0;
    }
}
static void e_render_(int16_t *out, int frames, float bpm, double beat)
{
    int32_t o[2 * CTL];
    int b, i;
    if (bpm >= 20.0f && (bpm > song.bpm + 0.75f || bpm < song.bpm - 0.75f))   /* (the delay's time, the LFOs, GATE:
                                                  * a tempo that wobbles around a value - a host measuring the clock -
                                                  * must not make the delay line jump; a step of a BPM or more passes) */
        song.bpm = (uint8_t)clamp((int32_t)(bpm + 0.5f), 60, 200);
    for (b = 0; b < NTRK; b++) {                 /* the WAVs the loader has ready (a pointer swap) */
        f_cur[b] = lxw_take(f_ld, (uint32_t)b, f_cur[b]);
        lx_file[b].d = f_cur[b] && f_cur[b]->n ? f_cur[b]->d : 0;
        lx_file[b].n = f_cur[b] ? f_cur[b]->n : 0;
        lx_file[b].rate = f_cur[b] ? f_cur[b]->rate : 0;
        lx_file[b].whole = f_whole[b];
    }
    {                                            /* the kits of yours: one read, one saved */
        const char *txt = lxu_loaded(f_kits);
        char nm[LXU_NAME];
        if (txt && k_pend) {
            uint32_t t;
            state_set(txt);
            e_kit = (uint8_t)(k_pend - 1u);
            k_pend = 0;
            for (t = 0; t < NTRK; t++)
                voice[t].active = ghost[t].active = 0;
        }
        if (lxu_saved(f_kits, nm, sizeof nm)) {  /* (selected in the list as it is now) */
            uint32_t k;
            for (k = 0; k < lxu_count(f_kits); k++)
                if (!strcmp(lxu_name(f_kits, k), nm))
                    e_kit = (uint8_t)(LXK_NKITS + k);
            k_saving[0] = 0;
        }
        if (e_kit >= kit_count())                /* (one of yours deleted) */
            e_kit = (uint8_t)(kit_count() - 1u);
    }
    if (beat >= 0.0)                             /* the LFOs on Move's beat (phase-locked while it plays) */
        lfo_clk = (uint32_t)(beat * 4.0 * step_base());
    {                                            /* QUIET: no voice sounding, the output silent and delay and reverb
                                                  * resting (1 s without a send or a sound of theirs) - the engine
                                                  * rests (silence costs nothing) until the next hit */
        int busy = 0;
        for (b = 0; b < NTRK && !busy; b++)
            busy = voice[b].active || ghost[b].active;
        if (!busy && e_quiet >= 10u && fx_still >= FX_IDLE_N) {   /* (and no echo on its way: lx_synth.c) */
            memset(out, 0, (size_t)frames * 2u * sizeof *out);
            lfo_clk += (uint32_t)frames;
            fm1_ms += (uint32_t)frames / 44u + 1u;
            e_clk += (uint32_t)frames;
            press_tick();
            return;
        }
    }
    {
        int32_t pk = 0;
        for (b = 0; b < frames; b += CTL) {
            int m = frames - b < CTL ? frames - b : CTL;
            mix_block(o, (uint32_t)m);
            for (i = 0; i < 2 * m; i++) {
                out[2 * b + i] = (int16_t)clamp(o[i], -32767, 32767);
                if (o[i] > pk) pk = o[i];
                else if (-o[i] > pk) pk = -o[i];
            }
            fm1_ms += (uint32_t)m / 44u + 1u;
        }
        e_quiet = pk < 3 ? e_quiet + 1u : 0u;
    }
    e_clk += (uint32_t)frames;
    press_tick();
}

static const lxe_ops_t OPS = {e_init, e_midi, e_set, e_get, e_render, e_fini};
HID const lxe_ops_t *LXE_CAT(lxe_ops_, LXE_SLOT)(void) { return &OPS; }

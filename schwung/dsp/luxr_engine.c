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
#include "lx_synth.c"

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
static const char *const O_SND[] = {"Kick", "Snare", "Clap", "Cl Hat", "Op Hat", "Tom", "Rim", "Cymbal"};
static const char *const O_LDEST[] = {"Off", "Pitch", "Filter", "Decay", "Level", "Pan", "Sample"};
static const char *const O_LWAVE[] = {"Sine", "Tri", "Saw", "Square", "S&H"};
static const char *const O_LRATE[] = {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"};
static char o_smp_names[TR_N][12];
static const char *o_smp[TR_N];                  /* the sample layer: OFF, SNAP, the LXR's, the sound's own, the machines' */

enum {                                           /* the kit */
    G_VOL, G_TUNE, G_DDEC, G_MAP, G_TSND, G_OGAIN,
    G_DRVM, G_DRVA, G_DRVT, G_DRVL, G_DLYD, G_DLYD2, G_DLYM, G_DLYF, G_DLYT, G_DLYL,
    G_REVS, G_REVD, G_REVP, G_REVL, G_MFLT, G_MRES, G_CTHR, G_CRAT, G_CATK, G_CREL, G_CMAK, G_PCLIP,
    G_CLIM, G_LCEIL, G_LREL, G_ALVL, G_ATGT, G_N
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
};
static uint8_t *const GU8[G_N] = {           /* (the song's fields; 0: kept here, or signed) */
    0, 0, &song.ddec, 0, 0, &song.ogain,
    &song.drv_mode, &song.drv_amt, &song.drv_tone, &song.drv_lvl, &song.dly_div, &song.dly_div2, &song.dly_mode,
    &song.dly_fb, &song.dly_tone, &song.dly_lvl, &song.rev_size, &song.rev_damp, &song.rev_pre, &song.rev_lvl,
    0, &song.mres, &song.cthr, &song.crat, &song.catk, &song.crel, &song.cmak, &song.pclip,
    &song.clim, 0, &song.lrel, &song.alvl, 0,
};
static int32_t g_get(uint32_t i)
{
    switch (i) {
    case G_VOL: return e_vol;
    case G_TUNE: return song.tune;
    case G_MAP: return e_map;
    case G_TSND: return e_tsnd;
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
    S_VAR, S_N
};
static const pdef_t SDEF[S_N] = {
    {"p1", 0, PT_INT, 0, 127, 0, 0}, {"p2", 0, PT_INT, 0, 127, 0, 0}, {"p3", 0, PT_INT, 0, 127, 0, 0},
    {"p4", 0, PT_INT, 0, 127, 0, 0}, {"p5", 0, PT_INT, 0, 127, 0, 0}, {"p6", 0, PT_INT, 0, 127, 0, 0},
    {"p7", 0, PT_INT, 0, 127, 0, 0}, {"p8", 0, PT_INT, 0, 127, 0, 0},
    {"smp", "Sample", PT_ENUM, 0, TR_N - 1, 0, 0},
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
static int key_find(const char *key, uint32_t *t, uint32_t *i)
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
static void jb_param(jb_t *j, const char *key, const char *name, const pdef_t *d, int32_t val)
{
    jb_put(j, "{\"key\":\"%s\",\"name\":\"%s\",", key, name);
    if (d->type == PT_ENUM) {
        int32_t k;
        jb_put(j, "\"type\":\"enum\",\"options\":[");
        for (k = 0; k <= d->max; k++)
            jb_put(j, "%s\"%s\"", k ? "," : "", d->opt ? d->opt[k] : o_smp[k]);
        jb_put(j, "],\"default\":%d}", (int)val);
        return;
    }
    jb_put(j, "\"type\":\"int\",\"min\":%d,\"max\":%d,\"step\":1,\"default\":%d", d->min, d->max, (int)val);
    if (d->unit)
        jb_put(j, ",\"unit\":\"%s\"", d->unit);
    jb_put(j, "}");
}
static const uint8_t KIT_PAGE[][8] = {           /* the kit's pages (G_*; 0xFF a gap) */
    {G_VOL, G_TUNE, G_DDEC, G_OGAIN, G_MAP, G_TSND, G_MFLT, G_MRES},
    {G_DRVM, G_DRVA, G_DRVT, G_DRVL, G_REVS, G_REVD, G_REVP, G_REVL},
    {G_DLYM, G_DLYD, G_DLYD2, G_DLYF, G_DLYT, G_DLYL, 0xFF, 0xFF},
    {G_CTHR, G_CRAT, G_CATK, G_CREL, G_CMAK, G_PCLIP, G_ALVL, G_ATGT},
    {G_CLIM, G_LCEIL, G_LREL, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};
static const char *const KIT_PAGE_NAME[] = {"Kit", "Drive & Reverb", "Delay", "Compressor", "Limiter"};
static const char *const KIT_PAGE_ID[] = {"kit", "fx", "delay", "comp", "limit"};
static void hierarchy(jb_t *j)
{
    uint32_t t, k, i;
    jb_put(j, "{\"pad_layout\":\"drums\",\"levels\":{\"root\":{\"name\":\"LUXR\",\"list_param\":\"kit\","
              "\"count_param\":\"kit_count\",\"name_param\":\"kit_name\",\"params\":[");
    for (t = 0; t < NTRK; t++)
        jb_put(j, "{\"level\":\"snd%u\",\"label\":\"%s\"},", t + 1u, O_SND[t]);
    for (k = 0; k < NELEM(KIT_PAGE); k++)
        jb_put(j, "%s{\"level\":\"%s\",\"label\":\"%s\"}", k ? "," : "", KIT_PAGE_ID[k], KIT_PAGE_NAME[k]);
    jb_put(j, "],\"knobs\":[\"k_vol\",\"k_tune\",\"k_decay\",\"k_ogain\",\"k_map\",\"k_tsnd\",\"k_mflt\",\"k_mres\"]}");
    for (t = 0; t < NTRK; t++) {                 /* a voice a sound: its note as the MOVE map has it */
        static const char *const ROLE[NTRK] = {"kick", "snare", "clap", "hat", "hat", "tom", "rim", "cymbal"};
        char key[16];
        jb_put(j, ",\"snd%u\":{\"name\":\"%s\",\"note\":%u,\"role\":\"%s\",\"params\":[", t + 1u, O_SND[t], 36u + t,
               ROLE[t]);
        for (i = 0; i < S_N; i++) {
            snprintf(key, sizeof key, "s%u_%s", t + 1u, SDEF[i].key);
            if (i)
                jb_put(j, ",");
            jb_param(j, key, s_name(t, i), &SDEF[i], s_get(t, i));
        }
        jb_put(j, "],\"knobs\":[");
        for (i = 0; i < S_N; i++)
            jb_put(j, "%s\"s%u_%s\"", i ? "," : "", t + 1u, SDEF[i].key);
        jb_put(j, "]}");
    }
    for (k = 0; k < NELEM(KIT_PAGE); k++) {
        uint32_t n = 0, last = 0;
        jb_put(j, ",\"%s\":{\"name\":\"%s\",\"params\":[", KIT_PAGE_ID[k], KIT_PAGE_NAME[k]);
        for (i = 0; i < 8u; i++)
            if (KIT_PAGE[k][i] != 0xFF) {
                const pdef_t *d = &GDEF[KIT_PAGE[k][i]];
                if (n++)
                    jb_put(j, ",");
                jb_param(j, d->key, d->name, d, g_get(KIT_PAGE[k][i]));
                last = i;
            }
        jb_put(j, "],\"knobs\":[");
        for (i = 0; i <= last; i++)
            jb_put(j, "%s%s%s%s", i ? "," : "", KIT_PAGE[k][i] == 0xFF ? "" : "\"",
                   KIT_PAGE[k][i] == 0xFF ? "null" : GDEF[KIT_PAGE[k][i]].key, KIT_PAGE[k][i] == 0xFF ? "" : "\"");
        jb_put(j, "]}");
    }
    jb_put(j, "}}");
}
/* chain_params: what can be automated and locked by a scene - the kit's numbers and each sound's (no enums: their
 * pages own them, and the chain's cap of 256 stays far) */
static void chain_params(jb_t *j)
{
    uint32_t t, i, n = 0;
    char key[16], name[32];
    jb_put(j, "[{\"key\":\"kit\",\"name\":\"Kit\",\"type\":\"int\",\"min\":0,\"max\":%u,\"step\":1}", LXK_NKITS - 1u);
    for (i = 0; i < G_N; i++)
        if (GDEF[i].type == PT_INT) {
            jb_put(j, ",");
            jb_param(j, GDEF[i].key, GDEF[i].name, &GDEF[i], g_get(i));
        }
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < S_N; i++)
            if (SDEF[i].type == PT_INT && i != S_VAR && i != S_GATE && i != S_ATK && i != S_REL) {
                snprintf(key, sizeof key, "s%u_%s", t + 1u, SDEF[i].key);
                snprintf(name, sizeof name, "%s %s", TRK_SHORT[t], s_name(t, i));
                jb_put(j, ",");
                jb_param(j, key, name, &SDEF[i], s_get(t, i));
                n++;
            }
    (void)n;
    jb_put(j, "]");
}

/* state: the kit's number and every value, as "key":number (the host stores it with the set and the presets) */
static void state_get(jb_t *j)
{
    uint32_t t, i;
    jb_put(j, "{\"v\":1,\"kit\":%u", e_kit);
    for (i = 0; i < G_N; i++)
        jb_put(j, ",\"%s\":%d", GDEF[i].key, (int)g_get(i));
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < S_N; i++)
            jb_put(j, ",\"s%u_%s\":%d", t + 1u, SDEF[i].key, (int)s_get(t, i));
    jb_put(j, "}");
}
static void set_value(const char *key, const char *val);
static void state_set(const char *js)            /* a flat object of numbers: each key set as written */
{
    const char *p = js;
    char key[24], val[24];
    while ((p = strchr(p, '"'))) {
        const char *q = strchr(p + 1, '"');
        size_t n;
        if (!q)
            return;
        n = (size_t)(q - p - 1);
        p = q + 1;
        while (*p == ' ' || *p == ':')
            p++;
        if (n >= sizeof key || !((*p >= '0' && *p <= '9') || *p == '-'))
            continue;
        memcpy(key, q - n, n);
        key[n] = 0;
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
static void kit_load(uint32_t k)
{
    uint32_t t;
    e_kit = (uint8_t)(k < LXK_NKITS ? k : 0u);
    kit_apply(&LXK_KITS[e_kit]);
    for (t = 0; t < NTRK; t++)
        voice[t].active = 0;
}
static void e_init(uint32_t seed)
{
    uint32_t i;
    memset(voice, 0, sizeof voice);
    memset(&song, 0, sizeof song);
    rng_state = seed | 1u;
    song.bpm = 120;
    song.chain = 1;
    song.ogain = 1;                              /* (+3 dB: LUXR's headroom is for a headphone out) */
    e_vol = 100;
    e_map = 0;
    e_tsnd = 0;
    master_set();
    rv_init();
    for (i = 0; i < TR_N; i++) {                 /* the sample layer's names */
        char *n = o_smp_names[i];
        if (i == TR_OFF) snprintf(n, sizeof o_smp_names[i], "Off");
        else if (i == TR_SNAP) snprintf(n, sizeof o_smp_names[i], "Snap");
        else if (i < TR_OWN0) snprintf(n, sizeof o_smp_names[i], "LXR %s", lxe_LXR_TRANS_NAME[i - TR_SMP0]);
        else if (i < TR_MACH0) snprintf(n, sizeof o_smp_names[i], "Own %u", i - TR_OWN0 + 1u);
        else snprintf(n, sizeof o_smp_names[i], "%s", lxe_MACH_NAME[i - TR_MACH0]);
        o_smp[i] = n;
    }
    kit_load(0);
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
    switch (e_map) {
    case 1:                                      /* GM */
        if ((t = midi_track_gm(n)) >= 0)
            hit((uint32_t)t, v, v > 110u, 0);
        break;
    case 2:                                      /* TONAL: the sound chosen, C3 its own pitch */
        hit(e_tsnd, v, v > 110u, (int32_t)n - 60);
        break;
    default:                                     /* MOVE: the pads' first row 1..8, the second the same accented */
        if (n >= 36u && n < 52u)
            hit((n - 36u) % 8u, v, n >= 44u || v > 110u, 0);
        break;
    }
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
        kit_load((uint32_t)clamp(atoi(val), 0, LXK_NKITS - 1));
    } else if (!strcmp(key, "state")) {
        state_set(val);
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
    if (!strcmp(key, "ui_hierarchy")) {
        hierarchy(&j);
    } else if (!strcmp(key, "chain_params")) {
        chain_params(&j);
    } else if (!strcmp(key, "state")) {
        state_get(&j);
    } else if (!strcmp(key, "name")) {
        jb_put(&j, "LUXR");
    } else if (!strcmp(key, "kit")) {
        jb_put(&j, "%u", e_kit);
    } else if (!strcmp(key, "kit_count")) {
        jb_put(&j, "%u", LXK_NKITS);
    } else if (!strcmp(key, "kit_name")) {
        jb_put(&j, "%s", LXK_KITS[e_kit % LXK_NKITS].name);
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
static void e_render(int16_t *out, int frames, float bpm, double beat)
{
    int32_t o[2 * CTL];
    int b, i;
    if (bpm >= 20.0f)
        song.bpm = (uint8_t)clamp((int32_t)(bpm + 0.5f), 60, 200);   /* (the delay's time, the LFOs, GATE) */
    if (beat >= 0.0)                             /* the LFOs on Move's beat (phase-locked while it plays) */
        lfo_clk = (uint32_t)(beat * 4.0 * step_base());
    for (b = 0; b < frames; b += CTL) {
        int m = frames - b < CTL ? frames - b : CTL;
        mix_block(o, (uint32_t)m);
        for (i = 0; i < 2 * m; i++)
            out[2 * b + i] = (int16_t)clamp(o[i], -32767, 32767);
        fm1_ms += (uint32_t)m / 44u + 1u;
    }
}

static const lxe_ops_t OPS = {e_init, e_midi, e_set, e_get, e_render};
HID const lxe_ops_t *LXE_CAT(lxe_ops_, LXE_SLOT)(void) { return &OPS; }

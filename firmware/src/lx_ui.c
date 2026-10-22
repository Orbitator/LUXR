/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: the panel and the screen (after Felucca's conventions and ONE CLICK's layout). The full map: README.md.
 *
 *   KNOBS   SELECT a new sound for the selected one, rolled (SHIFT: tempo) · ALGORITHM the sound 1..8 (SHIFT: the
 *           pattern) · PRESETS the stored patches, kit and patterns (SHIFT: their patterns only) · KNOB 1..4 the page's
 *           four cards, SHIFT the page's second row · MASTER volume
 *   SHIFT   GLO held (from 250 ms, or at once when another control is used while it is down). GLO tapped: the MIXER,
 *           tapped again its page 2 (PAN) and back.
 *   PAGES   a button tapped again shows its next page: HOME kit · SEQ sequencer, live pads · EDIT synth layer, sample
 *           layer, sound mix · ENV sample layer · LFO lfo 1, lfo 2 · FX sends, drive, delay, reverb · SCL patterns ·
 *           ARP note repeat (tap: on / off, held: while held) · GLO mixer, its pans · SAVE store · REC held: quantize
 *   PLAY    start / stop (SHIFT: from the top)     REC tap: arm recording (SHIFT: arm ERASE), held: QUANTIZE
 *   OCT-/+  SEQ: the bar shown (SHIFT: a bar less / more) · MIXER: sounds 1-4 / 5-8 · SAVE: back / do it ·
 *           else the sound before / after. Both held 5 s: UBOOT.
 *   WHITE   the 16 steps of the bar shown (C#5: accents) · LIVE: F3..C4 sounds 1..8, D4..G5 the same accented ·
 *           NOTE REPEAT: F3..C4 the pads, D4..G5 the rate · SHIFT: F3..C4 mute sounds 1..8, D4..G5 solo them
 *   BLACK   F#3 sound◂ G#3 sound▸ A#3 mute C#4 solo D#4 audition F#4 copy G#4 paste A#4 clear C#5 accent D#5 follow
 *           F#5 tap tempo; SHIFT: undo redo, all on, solo off, new sound, copy / paste / clear the pattern, double,
 *           random, metronome
 * Platform hooks (oc_main.c on the FM-1; host/lx_host.c): io_pressed io_held io_keys io_enc io_led_button io_led_key
 * io_store_read io_store_erase io_store_prog, and lcd_blit / lcd_sync / lcd_fill under gfx.c. */

static uint32_t io_pressed(void);                /* B_* labels pressed since the last call */
static uint32_t io_held(void);                   /* B_* labels held */
static uint32_t io_keys(void);                   /* note keys held, bit k = key k (0 = F3 .. 26 = G5) */
static int32_t io_enc(uint32_t role);            /* detents since the last call, + clockwise */
static void io_led_button(uint32_t b, int on);
static void io_led_key(uint32_t k, int on);
static int io_store_read(uint32_t off, void *d, uint32_t n);         /* the store (OC_STORE_*): 0 = ok */
static int io_store_erase(uint32_t off);                            /* its 4 KiB sector at off */
static int io_store_prog(uint32_t off, const void *s, uint32_t n);  /* into erased flash */
#ifndef OC_VERSION
#define OC_VERSION "v0.1"
#endif

/* -------------------------------------------------------- the layout --- */
#define H_HEAD 24
#define Y_LABEL 28                               /* the cards */
#define CARD_W 57
#define CARD_H 44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH 76                               /* the page's picture */
#define H_GRAPH 122
#define Y_FOOT 202
#define H_FOOT 38
#define HOLD_MS 350u                             /* ARP / REC held this long: note repeat while held / QUANTIZE */
#define SHIFT_MS 250u                            /* GLO held this long: SHIFT */
#define PAL_AMBER 2u
static const uint16_t TRK_COL[NTRK] = {RGB(255, 96, 110), RGB(255, 170, 40), RGB(240, 220, 90), RGB(120, 226, 140),
                                       RGB(90, 210, 200), RGB(90, 160, 255), RGB(176, 140, 255), RGB(240, 130, 220)};
#define C_REC RGB(255, 72, 60)

enum { PG_KIT, PG_SEQ, PG_LIVE, PG_VOICE, PG_VOICE2, PG_SAMPLE, PG_SND, PG_LFO1, PG_LFO2, PG_FXS, PG_FXD, PG_FXDL, PG_FXR,
       PG_PAT, PG_NR, PG_MIX, PG_SAVE, PG_REC, PG_N };
static const char *const PG_NAME[PG_N] = {"LUXR KIT", "SEQUENCER", "LIVE PADS", "SYNTH 1-4", "SYNTH 5-8", "SAMPLE LAYER",
                                          "SOUND MIX", "LFO 1", "LFO 2", "FX SENDS", "DRIVE", "DELAY", "REVERB",
                                          "PATTERNS", "NOTE REPEAT", "MIXER", "STORE", "QUANTIZE"};
static const uint8_t PAGE_BTN[PG_N] = {B_HOME, B_SEQ, B_SEQ, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_LFO, B_LFO, B_FX, B_FX, B_FX,
                                       B_FX, B_SCL, B_ARP, B_GLO, B_SAVE, B_REC};
static const uint8_t PAGE_KC[PG_N] = {KC_HOME, KC_SEQ, KC_SEQ, KC_EDIT, KC_EDIT, KC_EDIT, KC_EDIT, KC_LFO, KC_LFO, KC_FX, KC_FX,
                                      KC_FX, KC_FX, KC_SCL, KC_ARP, KC_GLO, KC_SAVE, KC_REC};
/* a button's pages, in the order its taps show them (a button not listed: none, or its own logic) */
typedef struct { uint8_t btn, n, pg[4]; } group_t;
static const group_t GROUPS[] = {
    {B_HOME, 1, {PG_KIT}}, {B_SEQ, 2, {PG_SEQ, PG_LIVE}}, {B_EDIT, 4, {PG_VOICE, PG_VOICE2, PG_SAMPLE, PG_SND}},
    {B_ENV, 1, {PG_SAMPLE}}, {B_LFO, 2, {PG_LFO1, PG_LFO2}}, {B_FX, 4, {PG_FXS, PG_FXD, PG_FXDL, PG_FXR}},
    {B_SCL, 1, {PG_PAT}}, {B_SAVE, 1, {PG_SAVE}},
};

/* ----------------------------------------------------------- parameters --- */
enum {
    P_NONE, P_BPM, P_SWING, P_TUNE, P_KDEC, P_CLICK, P_MIDICH, P_THEME, P_VERSION,
    P_LEN, P_SVEL, P_ROT, P_EUCLID, P_PLEN, P_COPY, P_SACC, P_LVEL,
    P_V0, P_V1, P_V2, P_V3, P_V4, P_V5, P_V6, P_V7,
    P_SMP, P_SLVL, P_SPITCH, P_SDEC, P_SYN,
    P_L1DEST, P_L1WAVE, P_L1RATE, P_L1DEPTH, P_L2DEST, P_L2WAVE, P_L2RATE, P_L2DEPTH,
    P_LEVEL, P_PAN, P_DLY, P_REV, P_DRV,
    P_DMODE, P_DAMT, P_DTONE, P_DLVL, P_DTIME, P_DFB, P_DLTONE, P_DLLVL, P_RSIZE, P_RDAMP, P_RLVL,
    P_PAT, P_CHAIN, P_PCOPY, P_PPASTE,
    P_NRRATE, P_NRVEL, P_NRVMODE, P_NRLATCH, P_NRHUMAN,
    P_MIX0, P_MIX1, P_MIX2, P_MIX3, P_MPAN0, P_MPAN1, P_MPAN2, P_MPAN3,
    P_SLOT, P_SWHAT, P_QGRID, P_QROUND,
    P_COUNT
};
enum { FM_NUM, FM_PCT, FM_BIP, FM_NAME, FM_STEPS, FM_PAN, FM_X100, FM_BPM, FM_SMP, FM_PAT };
typedef struct { const char *label; uint8_t fmt; int16_t min, max; const char *const *names; } pdesc_t;
static const char *const N_OFFON[] = {"OFF", "ON"};
static const char *const N_LDEST[] = {"OFF", "PITCH", "FILTER", "DECAY", "LEVEL", "PAN", "SAMPLE"};
static const char *const N_LWAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "S&H"};
static const char *const N_LRATE[] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const char *const N_DLYT[] = {"1/8", "3/16", "1/4", "3/8", "1/2", "3/4"};
static const char *const N_DMODE[] = {"SOFT", "HARD", "FOLD"};
static const char *const N_VMODE[] = {"FIXED", "RAMP UP", "RAMP DN", "RANDOM"};
static const char *const N_SWHAT[] = {"KIT+PAT", "KIT", "PATTERN"};
static const char *const N_QGRID[] = {"1/16", "1/8", "1/4"};
static const char *const N_QROUND[] = {"NEAR", "BEFORE"};
static const char *const N_CLICK[] = {"OFF", "REC", "ON"};
static const char *const N_MIDICH[] = {"ALL", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                       "15", "16"};
static const char *const N_WAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "NOISE"};
static const char *const N_VERSION[] = {"LUXR " OC_VERSION};
static const char *N_THEME[UI_NPALETTES];
#define NSLOT 8u
static const pdesc_t PD[P_COUNT] = {
    [P_BPM] = {"TEMPO", FM_BPM, 60, 200, 0},
    [P_SWING] = {"SWING", FM_PCT, 0, 50, 0},
    [P_TUNE] = {"TUNE", FM_BIP, -24, 24, 0},
    [P_KDEC] = {"DECAY", FM_NUM, 8, 127, 0},
    [P_CLICK] = {"CLICK", FM_NAME, 0, CLICK_N - 1, N_CLICK},
    [P_MIDICH] = {"MIDI IN", FM_NAME, 0, 16, N_MIDICH},
    [P_THEME] = {"THEME", FM_NAME, 0, UI_NPALETTES - 1, N_THEME},
    [P_VERSION] = {"VERSION", FM_NAME, 0, 0, N_VERSION},
    [P_LEN] = {"LENGTH", FM_STEPS, 1, NSTEP, 0},
    [P_SVEL] = {"VELO", FM_NUM, 0, 127, 0},
    [P_ROT] = {"ROTATE", FM_BIP, -63, 63, 0},
    [P_EUCLID] = {"EUCLID", FM_NUM, 0, NSTEP, 0},
    [P_PLEN] = {"PAT LEN", FM_STEPS, 1, NSTEP, 0},
    [P_COPY] = {"COPY x2", FM_NAME, 0, 1, N_OFFON},
    [P_SACC] = {"ACCENT", FM_NAME, 0, 1, N_OFFON},
    [P_LVEL] = {"VELO", FM_NUM, 1, 127, 0},
    [P_V0] = {"", FM_NUM, 0, 127, 0}, [P_V1] = {"", FM_NUM, 0, 127, 0}, [P_V2] = {"", FM_NUM, 0, 127, 0},
    [P_V3] = {"", FM_NUM, 0, 127, 0}, [P_V4] = {"", FM_NUM, 0, 127, 0}, [P_V5] = {"", FM_NUM, 0, 127, 0},
    [P_V6] = {"", FM_NUM, 0, 127, 0}, [P_V7] = {"", FM_NUM, 0, 127, 0},
    [P_SMP] = {"SAMPLE", FM_SMP, 0, TR_N - 1, 0},
    [P_SLVL] = {"LEVEL", FM_NUM, 0, 127, 0},
    [P_SPITCH] = {"PITCH", FM_X100, 0, 127, 0},
    [P_SDEC] = {"DECAY", FM_NUM, 0, 127, 0},
    [P_SYN] = {"SYNTH", FM_NUM, 0, 127, 0},
    [P_L1DEST] = {"DEST", FM_NAME, 0, LD_N - 1, N_LDEST}, [P_L2DEST] = {"DEST", FM_NAME, 0, LD_N - 1, N_LDEST},
    [P_L1WAVE] = {"WAVE", FM_NAME, 0, LW_N - 1, N_LWAVE}, [P_L2WAVE] = {"WAVE", FM_NAME, 0, LW_N - 1, N_LWAVE},
    [P_L1RATE] = {"RATE", FM_NAME, 0, LR_N - 1, N_LRATE}, [P_L2RATE] = {"RATE", FM_NAME, 0, LR_N - 1, N_LRATE},
    [P_L1DEPTH] = {"DEPTH", FM_BIP, -64, 63, 0}, [P_L2DEPTH] = {"DEPTH", FM_BIP, -64, 63, 0},
    [P_LEVEL] = {"VOLUME", FM_NUM, 0, 127, 0},
    [P_PAN] = {"PAN", FM_PAN, -64, 63, 0},
    [P_DLY] = {"DELAY", FM_NUM, 0, 127, 0},
    [P_REV] = {"REVERB", FM_NUM, 0, 127, 0},
    [P_DRV] = {"DRIVE", FM_NUM, 0, 127, 0},
    [P_DMODE] = {"MODE", FM_NAME, 0, DM_N - 1, N_DMODE},
    [P_DAMT] = {"AMOUNT", FM_NUM, 0, 127, 0},
    [P_DTONE] = {"TONE", FM_NUM, 0, 127, 0},
    [P_DLVL] = {"LEVEL", FM_NUM, 0, 127, 0},
    [P_DTIME] = {"TIME", FM_NAME, 0, 5, N_DLYT},
    [P_DFB] = {"FEEDBK", FM_NUM, 0, 127, 0},
    [P_DLTONE] = {"TONE", FM_NUM, 0, 127, 0},
    [P_DLLVL] = {"LEVEL", FM_NUM, 0, 127, 0},
    [P_RSIZE] = {"SIZE", FM_NUM, 0, 127, 0},
    [P_RDAMP] = {"DAMP", FM_NUM, 0, 127, 0},
    [P_RLVL] = {"LEVEL", FM_NUM, 0, 127, 0},
    [P_PAT] = {"PATTERN", FM_NUM, 1, NPAT, 0},
    [P_CHAIN] = {"CHAIN", FM_NUM, 1, NPAT, 0},
    [P_PCOPY] = {"COPY", FM_PAT, 0, NPAT, 0},
    [P_PPASTE] = {"PASTE", FM_PAT, 0, NPAT, 0},
    [P_NRRATE] = {"RATE", FM_NAME, 0, NR_N - 1, NR_NAME},
    [P_NRVEL] = {"VELO", FM_NUM, 1, 127, 0},
    [P_NRVMODE] = {"VEL MODE", FM_NAME, 0, NV_N - 1, N_VMODE},
    [P_NRLATCH] = {"LATCH", FM_NAME, 0, 1, N_OFFON},
    [P_NRHUMAN] = {"HUMAN", FM_NUM, 0, 127, 0},
    [P_MIX0] = {"", FM_NUM, 0, 127, 0}, [P_MIX1] = {"", FM_NUM, 0, 127, 0},
    [P_MIX2] = {"", FM_NUM, 0, 127, 0}, [P_MIX3] = {"", FM_NUM, 0, 127, 0},
    [P_MPAN0] = {"", FM_PAN, -64, 63, 0}, [P_MPAN1] = {"", FM_PAN, -64, 63, 0},
    [P_MPAN2] = {"", FM_PAN, -64, 63, 0}, [P_MPAN3] = {"", FM_PAN, -64, 63, 0},
    [P_SLOT] = {"SLOT", FM_NUM, 1, NSLOT, 0},
    [P_SWHAT] = {"SAVES", FM_NAME, 0, 2, N_SWHAT},
    [P_QGRID] = {"GRID", FM_NAME, 0, Q_N - 1, N_QGRID},
    [P_QROUND] = {"TO", FM_NAME, 0, 1, N_QROUND},
};
/* the four cards of each page: row A, row B (SHIFT) */
static const uint8_t PAGE_P[PG_N][8] = {
    [PG_KIT] = {P_BPM, P_SWING, P_TUNE, P_KDEC, P_MIDICH, P_THEME, P_VERSION, 0},
    [PG_SEQ] = {P_LEN, P_SVEL, P_ROT, P_EUCLID, P_PLEN, P_COPY, P_SACC, 0},
    [PG_LIVE] = {P_LVEL, P_QGRID, P_QROUND, P_CLICK, 0, 0, 0, 0},
    [PG_VOICE] = {P_V0, P_V1, P_V2, P_V3, P_V4, P_V5, P_V6, P_V7},
    [PG_VOICE2] = {P_V4, P_V5, P_V6, P_V7, P_V0, P_V1, P_V2, P_V3},
    [PG_SAMPLE] = {P_SMP, P_SLVL, P_SYN, P_SPITCH, P_SDEC, 0, 0, 0},
    [PG_SND] = {P_LEVEL, P_PAN, P_DLY, P_REV, P_DRV, 0, 0, 0},
    [PG_LFO1] = {P_L1DEST, P_L1WAVE, P_L1RATE, P_L1DEPTH, 0, 0, 0, 0},
    [PG_LFO2] = {P_L2DEST, P_L2WAVE, P_L2RATE, P_L2DEPTH, 0, 0, 0, 0},
    [PG_FXS] = {P_DRV, P_DLY, P_REV, P_LEVEL, 0, 0, 0, 0},
    [PG_FXD] = {P_DMODE, P_DAMT, P_DTONE, P_DLVL, 0, 0, 0, 0},
    [PG_FXDL] = {P_DTIME, P_DFB, P_DLTONE, P_DLLVL, 0, 0, 0, 0},
    [PG_FXR] = {P_RSIZE, P_RDAMP, P_RLVL, 0, 0, 0, 0, 0},
    [PG_PAT] = {P_PAT, P_CHAIN, P_PCOPY, P_PPASTE, 0, 0, 0, 0},
    [PG_NR] = {P_NRRATE, P_NRVEL, P_NRVMODE, P_NRLATCH, P_NRHUMAN, 0, 0, 0},
    [PG_MIX] = {P_MIX0, P_MIX1, P_MIX2, P_MIX3, P_MPAN0, P_MPAN1, P_MPAN2, P_MPAN3},
    [PG_SAVE] = {P_SLOT, P_SWHAT, 0, 0, 0, 0, 0, 0},
    [PG_REC] = {P_QGRID, P_QROUND, P_CLICK, 0, 0, 0, 0, 0},
};

static struct {
    uint8_t page, back, hot, uboot, force, shift, bar, bank, cur, acc_mode, follow, slot, swhat, patch;
    uint8_t glo_used, arp_from, rec_long, copy, lvel, eu[NTRK];
    uint8_t mixpan;                              /* the MIXER's page 2: KNOB 1..4 the pans */
    int8_t rot;
    uint32_t hot_t, msg_t, both_t, burst_t, burst_tag, glo_t0, arp_t0, rec_t0, tap_t, tap_n, tap_sum;
    char msg[28];
    uint32_t sig[8];                             /* the areas: 0 header, 1..4 cards, 5 graph, 6 footer, 7 uboot */
    uint32_t keys, held_prev;
    uint8_t used[NSLOT];                         /* the store's slots in use */
    uint32_t lit_t[NTRK];                        /* LIVE: when each sound was last hit (its pad lit a moment) */
} ui;
#define TS (&snd[song.sel % NTRK])

static void ui_say(const char *s)
{
    str_cpy(ui.msg, s, sizeof ui.msg);
    ui.msg_t = fm1_ms | 1u;
}
static void str_cat(char *d, const char *s, uint32_t n) { str_cpy(d + str_len(d), s, n - str_len(d)); }

/* ---------------------------------------------------------------- undo --- */
#define UNDO_N 8u
typedef struct { pattern_t p; sound_t s[NTRK]; uint8_t pat; } snap_t;
static struct { snap_t s[UNDO_N]; uint32_t cur, n_undo, n_redo; } undo __attribute__((section(".pool")));
static void snap_take(snap_t *s)
{
    memcpy(&s->p, PAT, sizeof s->p);
    memcpy(s->s, snd, sizeof s->s);
    s->pat = song.pat;
}
static void undo_take(void)
{
    snap_take(&undo.s[undo.cur % UNDO_N]);
    undo.cur++;
    if (undo.n_undo < UNDO_N - 1u)
        undo.n_undo++;
    undo.n_redo = 0;
}
static void undo_burst(uint32_t tag)             /* one undo for a run of turns (1.5 s apart at most) */
{
    if (tag != ui.burst_tag || fm1_ms - ui.burst_t > 1500u)
        undo_take();
    ui.burst_tag = tag;
    ui.burst_t = fm1_ms;
}
static void undo_do(int redo)
{
    snap_t now;
    if (redo ? !undo.n_redo : !undo.n_undo) {
        ui_say(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    snap_take(&now);
    if (redo) {
        undo.cur++;
        undo.n_redo--;
        undo.n_undo++;
    } else {
        undo.cur--;
        undo.n_undo--;
        undo.n_redo++;
    }
    {
        snap_t *s = &undo.s[(redo ? undo.cur - 1u : undo.cur) % UNDO_N];
        memcpy(&pat[s->pat % NPAT], &s->p, sizeof s->p);
        memcpy(snd, s->s, sizeof snd);
        *s = now;
    }
    ui.burst_tag = 0;
    ui_say(redo ? "REDO" : "UNDO");
}

static void fmt_u(char *b, uint32_t v);
/* --------------------------------------------------------------- store --- */
/* the store (OC_STORE_*, the area ONE CLICK and Felucca use too): settings at 0, eight patches of 12 KiB after it. A
 * patch is a kit (the eight sounds, tune, decay, the global FX) and its patterns (16, tempo, swing, chain, mute, solo);
 * SAVE writes both or one of them (the other as the slot has it), PRESETS loads both or (SHIFT) the patterns. */
#define ST_SET 0x0000u
#define ST_PROJ(i) (0x1000u + 0x3000u * (uint32_t)(i))
#define MAGIC_SET 0x3254584Cu                    /* "LXT2" */
#define MAGIC_PROJ 0x3250584Cu                   /* "LXP2" (LXP1, before the two LFOs and the FX: not read) */
enum { SW_ALL, SW_KIT, SW_PAT };
typedef struct { uint32_t magic; uint8_t theme, midi_ch, click, slot, qgrid, qround, pad[2]; } set_rec_t;
typedef struct {                                 /* the kit */
    sound_t s[NTRK];
    int8_t tune;
    uint8_t ddec, drv_mode, drv_amt, drv_tone, drv_lvl, dly_div, dly_fb, dly_tone, dly_lvl, rev_size, rev_damp, rev_lvl;
} kit_rec_t;
typedef struct {                                 /* the patterns */
    pattern_t p[NPAT];
    uint8_t bpm, swing, chain, pat, mute, solo;
} pat_rec_t;
typedef struct { uint32_t magic, size, sum; kit_rec_t k; pat_rec_t p; } proj_t;
static proj_t proj_buf __attribute__((section(".pool")));
static int store_write(uint32_t off, const void *src, uint32_t n)   /* whole sectors from off, erased, then written */
{
    const uint8_t *b = src;
    uint32_t k;
    for (k = 0; k < n; k += 0x1000u) {
        int rc = io_store_erase(off + k);
        if (rc || (rc = io_store_prog(off + k, b + k, n - k < 0x1000u ? n - k : 0x1000u)) != 0)
            return rc;
    }
    return 0;
}
static uint32_t proj_sum(const proj_t *r)
{
    const uint8_t *p = (const uint8_t *)&r->k;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof r->k + sizeof r->p; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void settings_save(void)
{
    set_rec_t st = {MAGIC_SET, song.theme, song.midi_ch, song.click, ui.slot, song.qgrid, song.qround, {0, 0}};
    store_write(ST_SET, &st, sizeof st);
}
static int proj_read(uint32_t slot)              /* into proj_buf; 0 = a patch is there */
{
    return io_store_read(ST_PROJ(slot), &proj_buf, sizeof proj_buf) || proj_buf.magic != MAGIC_PROJ ||
           proj_buf.size != sizeof proj_buf || proj_buf.sum != proj_sum(&proj_buf);
}
static void store_scan(void)
{
    set_rec_t st;
    uint32_t i;
    for (i = 0; i < NSLOT; i++) {
        uint32_t m = 0;
        ui.used[i] = io_store_read(ST_PROJ(i), &m, 4) == 0 && m == MAGIC_PROJ;
    }
    if (io_store_read(ST_SET, &st, sizeof st) == 0 && st.magic == MAGIC_SET) {
        song.theme = st.theme < UI_NPALETTES ? st.theme : PAL_AMBER;
        song.midi_ch = st.midi_ch <= 16u ? st.midi_ch : 0;
        song.click = st.click < CLICK_N ? st.click : CLICK_OFF;
        song.qgrid = st.qgrid < Q_N ? st.qgrid : Q_16;
        song.qround = st.qround & 1u;
        ui.slot = ui.patch = st.slot % NSLOT;
    }
}
static void kit_take(kit_rec_t *k)
{
    memcpy(k->s, snd, sizeof k->s);
    k->tune = song.tune; k->ddec = song.ddec;
    k->drv_mode = song.drv_mode; k->drv_amt = song.drv_amt; k->drv_tone = song.drv_tone; k->drv_lvl = song.drv_lvl;
    k->dly_div = song.dly_div; k->dly_fb = song.dly_fb; k->dly_tone = song.dly_tone; k->dly_lvl = song.dly_lvl;
    k->rev_size = song.rev_size; k->rev_damp = song.rev_damp; k->rev_lvl = song.rev_lvl;
}
static void kit_put(const kit_rec_t *k)
{
    uint32_t t, j;
    memcpy(snd, k->s, sizeof snd);
    for (t = 0; t < NTRK; t++) {                 /* (stored values as indexes: kept in range) */
        snd[t].smp %= TR_N;
        for (j = 0; j < 2u; j++) {
            snd[t].lfo[j].dest %= LD_N;
            snd[t].lfo[j].wave %= LW_N;
            snd[t].lfo[j].rate %= LR_N;
        }
    }
    song.tune = (int8_t)clamp(k->tune, -24, 24);
    song.ddec = (uint8_t)clamp(k->ddec, 8, 127);
    song.drv_mode = k->drv_mode % DM_N; song.drv_amt = k->drv_amt & 127u; song.drv_tone = k->drv_tone & 127u;
    song.drv_lvl = k->drv_lvl & 127u;
    song.dly_div = k->dly_div % 6u; song.dly_fb = k->dly_fb & 127u; song.dly_tone = k->dly_tone & 127u;
    song.dly_lvl = k->dly_lvl & 127u;
    song.rev_size = k->rev_size & 127u; song.rev_damp = k->rev_damp & 127u; song.rev_lvl = k->rev_lvl & 127u;
}
static void pat_take(pat_rec_t *r)
{
    memcpy(r->p, pat, sizeof r->p);
    r->bpm = song.bpm; r->swing = song.swing; r->chain = song.chain; r->pat = song.pat;
    r->mute = song.mute; r->solo = song.solo;
}
static void pat_put(const pat_rec_t *r)
{
    uint32_t i, t;
    memcpy(pat, r->p, sizeof pat);
    for (i = 0; i < NPAT; i++)
        for (t = 0; t < NTRK; t++)
            pat[i].len[t] = (uint8_t)clamp(pat[i].len[t], 1, NSTEP);
    song.bpm = (uint8_t)clamp(r->bpm, 60, 200);
    song.swing = (uint8_t)clamp(r->swing, 0, 50);
    song.chain = (uint8_t)clamp(r->chain, 1, NPAT);
    song.mute = r->mute;
    song.solo = r->solo;
    if (!song.playing)
        song.pat = song.chain0 = r->pat % NPAT;
    else
        song.pat_next = r->pat % NPAT;          /* (playing: at the end of the pattern) */
}
static void proj_save(uint32_t slot, uint32_t what)   /* SW_ALL, or the kit / the patterns into what the slot has */
{
    proj_t *r = &proj_buf;
    int had = what != SW_ALL && !proj_read(slot);
    if (what != SW_PAT || !had)
        kit_take(&r->k);
    if (what != SW_KIT || !had)
        pat_take(&r->p);
    r->magic = MAGIC_PROJ;
    r->size = sizeof *r;
    r->sum = proj_sum(r);
    if (store_write(ST_PROJ(slot), r, sizeof *r)) {
        ui_say("STORE FAILED");
        return;
    }
    ui.used[slot % NSLOT] = 1;
    ui.slot = ui.patch = (uint8_t)slot;
    settings_save();
    ui_say(what == SW_KIT ? "KIT SAVED" : what == SW_PAT ? "PATTERNS SAVED" : "PATCH SAVED");
}
static int proj_load(uint32_t slot, uint32_t what)   /* SW_ALL: kit and patterns; SW_KIT / SW_PAT: one of them */
{
    char m[24];
    if (proj_read(slot)) {
        ui_say("EMPTY SLOT");
        return -1;
    }
    undo_take();
    if (what != SW_PAT)
        kit_put(&proj_buf.k);
    if (what != SW_KIT)
        pat_put(&proj_buf.p);
    ui.patch = (uint8_t)slot;
    str_cpy(m, what == SW_PAT ? "PATTERNS " : what == SW_KIT ? "KIT " : "PATCH ", sizeof m);
    fmt_u(m + str_len(m), slot + 1u);
    ui_say(m);
    return 0;
}
static void patch_turn(int32_t d, uint32_t what)   /* PRESETS: the next / previous patch in use, loaded */
{
    uint32_t k, s = ui.patch;
    for (k = 0; k < NSLOT; k++) {
        s = (s + NSLOT + (d > 0 ? 1u : NSLOT - 1u)) % NSLOT;
        if (ui.used[s]) {
            proj_load(s, what);
            return;
        }
    }
    ui_say("NO PATCH SAVED");
}

/* ------------------------------------------------------- the pattern --- */
static uint32_t tlen(uint32_t t) { return (uint32_t)clamp(PAT->len[t % NTRK], 1, NSTEP); }
static uint32_t nbars(uint32_t t) { return (tlen(t) + 15u) / 16u; }
static uint8_t clip_bar[16], clip_ok;
static pattern_t clip_pat;
static uint8_t clip_pat_ok;                      /* the pattern copied: its number + 1 (0: none) */
static void rotate_track(uint32_t t, int32_t d)  /* the track's steps 1..LEN moved d steps later */
{
    uint8_t tmp[NSTEP];
    uint32_t len = tlen(t), i;
    for (i = 0; i < len; i++)
        tmp[(uint32_t)((int32_t)i + d + 64 * (int32_t)len) % len] = PAT->s[t][i];
    memcpy(PAT->s[t], tmp, len);
}
static void euclid_track(uint32_t t, uint32_t hits)   /* HITS spread evenly over LEN (Bjorklund by the bucket) */
{
    uint32_t len = tlen(t), i, b = 0;
    for (i = 0; i < len; i++) {
        b += hits;
        PAT->s[t][i] = b >= len ? (uint8_t)100u : 0u;
        if (b >= len)
            b -= len;
    }
    if (hits)                                    /* (the first step on the downbeat) */
        rotate_track(t, 1);
}
static void random_track(uint32_t t)             /* a pattern by the sound's role */
{
    static const uint8_t DENS[NTRK] = {35, 25, 12, 60, 12, 12, 15, 6};   /* % of the steps */
    uint32_t len = tlen(t), i;
    for (i = 0; i < len; i++) {
        uint32_t r = rng() % 100u, on = r < DENS[t];
        if (t == 0u && i % 4u == 0u) on = 1;     /* the kick on the beats, the snare on 2 and 4 */
        if (t == 1u) on = i % 8u == 4u || (r < 8u);
        PAT->s[t][i] = on ? (uint8_t)(70u + rng() % 50u) | (rng() % 5u == 0u ? S_ACC : 0u) : 0u;
    }
}
static void double_track(uint32_t t)
{
    uint32_t len = tlen(t), i;
    if (len * 2u > NSTEP) {
        ui_say("64 STEPS MAX");
        return;
    }
    for (i = 0; i < len; i++)
        PAT->s[t][len + i] = PAT->s[t][i];
    PAT->len[t] = (uint8_t)(len * 2u);
}
static void demo_pattern(pattern_t *p)           /* the groove at power-on */
{
    static const uint32_t HITS[NTRK] = {0x1111u | 0x4000u, 0x1010u, 0x1000u, 0x5555u, 0x4040u >> 4, 0x8000u, 0x0080u, 0x0001u};
    uint32_t t, i;
    memset(p, 0, sizeof *p);
    for (t = 0; t < NTRK; t++) {
        p->len[t] = 16;
        for (i = 0; i < 16u; i++)
            if ((HITS[t] >> i) & 1u)
                p->s[t][i] = (uint8_t)(t == 3u ? (i % 4u == 2u ? 110u : 70u) : 100u) | (t == 0u && i == 0u ? S_ACC : 0u);
    }
}

static void plen_scale(int up)                    /* SEQ + SHIFT, KNOB 1: every track twice as long (COPY x2: its notes
                                                  * again in the new bars) or half as long */
{
    uint32_t t, i, len, cut = 0;
    undo_take();
    for (t = 0; t < NTRK; t++) {
        len = tlen(t);
        if (up && len * 2u > NSTEP) {
            cut = 1;
            continue;
        }
        if (up)
            for (i = 0; i < len; i++)
                PAT->s[t][len + i] = ui.copy ? PAT->s[t][i] : 0u;
        PAT->len[t] = (uint8_t)(up ? len * 2u : (len > 1u ? len / 2u : 1u));
    }
    if (ui.bar >= nbars(song.sel))
        ui.bar = (uint8_t)(nbars(song.sel) - 1u);
    ui_say(cut ? "64 STEPS MAX" : up ? (ui.copy ? "x2, NOTES COPIED" : "x2, NEW BARS EMPTY") : "HALF LENGTH");
}

/* ----------------------------------------------------- the parameters --- */
static lfo_t *lfo_of(uint32_t id) { return &TS->lfo[id >= P_L2DEST]; }
static pdesc_t pdesc_of(uint32_t id)
{
    pdesc_t d = PD[id % P_COUNT];
    if (id >= P_V0 && id <= P_V7)
        d.label = LX_LABEL[TRK_KIND[song.sel % NTRK]][id - P_V0];
    if (id >= P_MIX0 && id <= P_MPAN3)
        d.label = TRK_NAME[(ui.bank * 4u + (id - P_MIX0) % 4u) % NTRK];
    return d;
}
static int32_t pget(uint32_t id)
{
    const sound_t *s = TS;
    switch (id) {
    case P_BPM: return song.bpm;
    case P_SWING: return song.swing;
    case P_TUNE: return song.tune;
    case P_KDEC: return song.ddec;
    case P_CLICK: return song.click;
    case P_MIDICH: return song.midi_ch;
    case P_THEME: return song.theme;
    case P_LEN: return (int32_t)tlen(song.sel);
    case P_SVEL: return S_VEL(PAT->s[song.sel][ui.cur % NSTEP]);
    case P_ROT: return ui.rot;
    case P_EUCLID: return ui.eu[song.sel % NTRK];
    case P_PLEN: return (int32_t)pat_len(PAT);
    case P_COPY: return ui.copy;
    case P_SACC: return (PAT->s[song.sel][ui.cur % NSTEP] & S_ACC) != 0;
    case P_LVEL: return ui.lvel;
    case P_SMP: return s->smp;
    case P_SLVL: return s->slvl;
    case P_SPITCH: return s->spitch;
    case P_SDEC: return s->sdec;
    case P_SYN: return s->syn;
    case P_L1DEST: case P_L2DEST: return lfo_of(id)->dest;
    case P_L1WAVE: case P_L2WAVE: return lfo_of(id)->wave;
    case P_L1RATE: case P_L2RATE: return lfo_of(id)->rate;
    case P_L1DEPTH: case P_L2DEPTH: return lfo_of(id)->depth;
    case P_LEVEL: return s->level;
    case P_PAN: return s->pan;
    case P_DLY: return s->dly;
    case P_REV: return s->rev;
    case P_DRV: return s->drv;
    case P_DMODE: return song.drv_mode;
    case P_DAMT: return song.drv_amt;
    case P_DTONE: return song.drv_tone;
    case P_DLVL: return song.drv_lvl;
    case P_DTIME: return song.dly_div;
    case P_DFB: return song.dly_fb;
    case P_DLTONE: return song.dly_tone;
    case P_DLLVL: return song.dly_lvl;
    case P_RSIZE: return song.rev_size;
    case P_RDAMP: return song.rev_damp;
    case P_RLVL: return song.rev_lvl;
    case P_PAT: return (song.pat_next < NPAT ? song.pat_next : song.pat) + 1;
    case P_CHAIN: return song.chain;
    case P_PCOPY: return clip_pat_ok;
    case P_PPASTE: return pget(P_PAT);
    case P_NRRATE: return nr.rate;
    case P_NRVEL: return nr.vel;
    case P_NRVMODE: return nr.vmode;
    case P_NRLATCH: return nr.latch;
    case P_NRHUMAN: return nr.human;
    case P_SLOT: return ui.slot + 1;
    case P_SWHAT: return ui.swhat;
    case P_QGRID: return song.qgrid;
    case P_QROUND: return song.qround;
    }
    if (id >= P_V0 && id <= P_V7)
        return s->p[id - P_V0];
    if (id >= P_MIX0 && id <= P_MIX3)
        return snd[(ui.bank * 4u + id - P_MIX0) % NTRK].level;
    if (id >= P_MPAN0 && id <= P_MPAN3)
        return snd[(ui.bank * 4u + id - P_MPAN0) % NTRK].pan;
    return 0;
}
static void pset(uint32_t id, int32_t v)
{
    pdesc_t d = pdesc_of(id);
    sound_t *s = TS;
    uint32_t t = song.sel % NTRK;
    if (id == P_PCOPY || id == P_PPASTE) {       /* PATTERNS, KNOB 3 / 4: a turn copies / pastes the pattern KNOB 1 shows */
        uint32_t k = (uint32_t)pget(P_PAT) - 1u;
        char m[24];
        if (v == pget(id))
            return;
        if (id == P_PCOPY) {
            memcpy(&clip_pat, &pat[k % NPAT], sizeof clip_pat);
            clip_pat_ok = (uint8_t)(k + 1u);
            str_cpy(m, "P", sizeof m);
            fmt_u(m + 1, k + 1u);
            str_cat(m, " COPIED", sizeof m);
        } else if (!clip_pat_ok) {
            str_cpy(m, "COPY ONE FIRST (K3)", sizeof m);
        } else {
            if (k == song.pat)
                undo_take();                     /* (undo keeps the pattern playing) */
            memcpy(&pat[k % NPAT], &clip_pat, sizeof clip_pat);
            str_cpy(m, "PASTED INTO P", sizeof m);
            fmt_u(m + str_len(m), k + 1u);
        }
        ui_say(m);
        return;
    }
    if (id == P_PLEN) {                          /* (a turn doubles or halves, whatever its size) */
        if (v != pget(P_PLEN))
            plen_scale(v > pget(P_PLEN));
        return;
    }
    v = clamp(v, d.min, d.max);
    switch (id) {
    case P_BPM: song.bpm = (uint8_t)v; break;
    case P_SWING: song.swing = (uint8_t)v; break;
    case P_TUNE: song.tune = (int8_t)v; break;
    case P_KDEC: song.ddec = (uint8_t)v; break;
    case P_CLICK: song.click = (uint8_t)v; settings_save(); break;
    case P_MIDICH: song.midi_ch = (uint8_t)v; settings_save(); break;
    case P_THEME: song.theme = (uint8_t)v; palette_set(song.theme); ui.force = 1; settings_save(); break;
    case P_LEN: undo_burst(0x100u + t); PAT->len[t] = (uint8_t)v; if (ui.bar >= nbars(t)) ui.bar = (uint8_t)(nbars(t) - 1u); break;
    case P_SVEL: {
        uint8_t *st = &PAT->s[t][ui.cur % NSTEP];
        undo_burst(0x200u + ui.cur);
        *st = v ? (uint8_t)((*st & S_ACC) | (uint32_t)v) : 0u;
        break;
    }
    case P_ROT: undo_burst(0x300u + t); rotate_track(t, v - ui.rot); ui.rot = (int8_t)v; break;
    case P_EUCLID: undo_burst(0x400u + t); ui.eu[t] = (uint8_t)clamp(v, 0, (int32_t)tlen(t)); euclid_track(t, ui.eu[t]); break;
    case P_COPY: ui.copy = (uint8_t)v; break;
    case P_SACC: {
        uint8_t *st = &PAT->s[t][ui.cur % NSTEP];
        if (*st)
            *st = (uint8_t)(v ? *st | S_ACC : *st & ~S_ACC);
        break;
    }
    case P_LVEL: ui.lvel = (uint8_t)v; break;
    case P_SMP: s->smp = (uint8_t)v; break;
    case P_SLVL: s->slvl = (uint8_t)v; break;
    case P_SPITCH: s->spitch = (uint8_t)v; break;
    case P_SDEC: s->sdec = (uint8_t)v; break;
    case P_SYN: s->syn = (uint8_t)v; break;
    case P_L1DEST: case P_L2DEST: lfo_of(id)->dest = (uint8_t)v; break;
    case P_L1WAVE: case P_L2WAVE: lfo_of(id)->wave = (uint8_t)v; break;
    case P_L1RATE: case P_L2RATE: lfo_of(id)->rate = (uint8_t)v; break;
    case P_L1DEPTH: case P_L2DEPTH: lfo_of(id)->depth = (int8_t)v; break;
    case P_LEVEL: s->level = (uint8_t)v; break;
    case P_PAN: s->pan = (int8_t)v; break;
    case P_DLY: s->dly = (uint8_t)v; break;
    case P_REV: s->rev = (uint8_t)v; break;
    case P_DRV: s->drv = (uint8_t)v; break;
    case P_DMODE: song.drv_mode = (uint8_t)v; break;
    case P_DAMT: song.drv_amt = (uint8_t)v; break;
    case P_DTONE: song.drv_tone = (uint8_t)v; break;
    case P_DLVL: song.drv_lvl = (uint8_t)v; break;
    case P_DTIME: song.dly_div = (uint8_t)v; break;
    case P_DFB: song.dly_fb = (uint8_t)v; break;
    case P_DLTONE: song.dly_tone = (uint8_t)v; break;
    case P_DLLVL: song.dly_lvl = (uint8_t)v; break;
    case P_RSIZE: song.rev_size = (uint8_t)v; break;
    case P_RDAMP: song.rev_damp = (uint8_t)v; break;
    case P_RLVL: song.rev_lvl = (uint8_t)v; break;
    case P_PAT:
        if (song.playing) {
            song.pat_next = (uint8_t)(v - 1);    /* at the end of the pattern playing */
        } else {
            song.pat = song.chain0 = (uint8_t)(v - 1);
            song.pat_next = 0xFF;
        }
        if (ui.bar >= nbars(t)) ui.bar = 0;
        break;
    case P_CHAIN: song.chain = (uint8_t)v; song.chain0 = song.pat; break;
    case P_NRRATE: nr.rate = (uint8_t)v; break;
    case P_NRVEL: nr.vel = (uint8_t)v; break;
    case P_NRVMODE: nr.vmode = (uint8_t)v; break;
    case P_NRLATCH: nr.latch = (uint8_t)v; if (!v) ev_push(EV_UNLATCH, 0, 0); break;
    case P_NRHUMAN: nr.human = (uint8_t)v; break;
    case P_SLOT: ui.slot = (uint8_t)(v - 1); break;
    case P_SWHAT: ui.swhat = (uint8_t)v; break;
    case P_QGRID: song.qgrid = (uint8_t)v; settings_save(); break;
    case P_QROUND: song.qround = (uint8_t)v; settings_save(); break;
    default:
        if (id >= P_V0 && id <= P_V7)
            s->p[id - P_V0] = (uint8_t)v;
        else if (id >= P_MIX0 && id <= P_MIX3)
            snd[(ui.bank * 4u + id - P_MIX0) % NTRK].level = (uint8_t)v;
        else if (id >= P_MPAN0 && id <= P_MPAN3)
            snd[(ui.bank * 4u + id - P_MPAN0) % NTRK].pan = (int8_t)v;
        break;
    }
}

static void fmt_u(char *b, uint32_t v)
{
    char t[12];
    uint32_t n = 0;
    do t[n++] = (char)('0' + v % 10u); while ((v /= 10u) && n < 11u);
    while (n) *b++ = t[--n];
    *b = 0;
}
static void fmt_s(char *b, int32_t v, int plus)  /* signed; plus: "+3" */
{
    if (v < 0) *b++ = '-';
    else if (plus && v > 0) *b++ = '+';
    fmt_u(b, (uint32_t)(v < 0 ? -v : v));
}
static void smp_name(char *b, uint32_t smp, uint32_t t)   /* the sample layer's: OFF, SNAP, the LXR's, the sound's own */
{
    if (smp >= TR_OWN0) {
        str_cpy(b, TRK_SHORT[t % NTRK], 16);
        b[2] = ' ';
        b[3] = (char)('0' + (smp - TR_OWN0 + 1u) / 10u);
        b[4] = (char)('0' + (smp - TR_OWN0 + 1u) % 10u);
        b[5] = 0;
    } else {
        str_cpy(b, smp == TR_OFF ? "OFF" : smp == TR_SNAP ? "SNAP" : LXR_TRANS_NAME[(smp - TR_SMP0) % LXR_TRANS_N], 16);
    }
}
static void pformat(uint32_t id, char *val, const char **unit)
{
    pdesc_t d = pdesc_of(id);
    int32_t v = pget(id);
    *unit = "";
    switch (d.fmt) {
    case FM_NAME: str_cpy(val, d.names ? d.names[clamp(v, d.min, d.max) - d.min] : "", 16); break;
    case FM_SMP: smp_name(val, (uint32_t)v, song.sel); break;
    case FM_PAT: if (v) { val[0] = 'P'; fmt_u(val + 1, (uint32_t)v); } else str_cpy(val, "-", 16); break;
    case FM_PCT: fmt_u(val, (uint32_t)v); *unit = "%"; break;
    case FM_BIP: fmt_s(val, v, 1); break;
    case FM_PAN:
        if (!v) str_cpy(val, "C", 16);
        else { val[0] = v < 0 ? 'L' : 'R'; fmt_u(val + 1, (uint32_t)(v < 0 ? -v : v)); }
        break;
    case FM_STEPS:
        fmt_u(val, (uint32_t)v);
        *unit = v % 16 ? "ST" : "";
        if (!(v % 16)) { fmt_u(val, (uint32_t)v / 16u); *unit = v == 16 ? "BAR" : "BARS"; }
        break;
    case FM_X100: fmt_u(val, (uint32_t)(25 + v * 150 / 127)); *unit = "%"; break;
    case FM_BPM: fmt_u(val, (uint32_t)v); *unit = "BPM"; break;
    default:
        if (id == P_SVEL && !v) str_cpy(val, "OFF", 16);
        else if (id >= P_V0 && id <= P_V7 && TRK_KIND[song.sel % NTRK] == K_DRUM && id == P_V4)
            str_cpy(val, N_WAVE[(uint32_t)v * 5u / 128u], 16);
        else if (id == P_SDEC && v == 127) str_cpy(val, "FULL", 16);
        else fmt_u(val, (uint32_t)v);
        break;
    }
}
static uint32_t knob_id(uint32_t c)              /* (the MIXER: page 2 the pans, SHIFT the other row) */
{
    uint32_t b = ui.page == PG_MIX ? (ui.mixpan ^ ui.shift) & 1u : ui.shift;
    return PAGE_P[ui.page % PG_N][(c & 3u) + (b ? 4u : 0u)];
}

/* ----------------------------------------------------------------- keys --- */
/* key k: 0 = F3 .. 26 = G5; the white ones counted 0..15 from F3, the black ones 0..10 from F#3 */
static int key_black(uint32_t k) { uint32_t n = (k + 5u) % 12u; return n == 1u || n == 3u || n == 6u || n == 8u || n == 10u; }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, w = 0, b = 0;
    for (i = 0; i < k; i++) {
        if (key_black(i)) b++;
        else w++;
    }
    return key_black(k) ? b : w;
}
enum { BK_PREV, BK_NEXT, BK_MUTE, BK_SOLO, BK_AUDITION, BK_COPY, BK_PASTE, BK_CLEAR, BK_ACCENT, BK_FOLLOW, BK_TAP, BK_N };

static void sel_track(int32_t t)
{
    song.sel = (uint8_t)((t + NTRK) % NTRK);
    if (ui.bar >= nbars(song.sel))
        ui.bar = 0;
    ui.rot = 0;
}
static void go_page(uint32_t pg)
{
    if (ui.page == PG_NR && pg != PG_NR) {       /* leaving NOTE REPEAT: the pads let go */
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            ev_push(EV_PAD_OFF, t, 0);
        ev_push(EV_UNLATCH, 0, 0);
    }
    if (pg != ui.page)
        ui.back = ui.page;
    ui.page = (uint8_t)pg;
    ui.rot = 0;
}
static void black_key(uint32_t b)
{
    uint32_t t = song.sel % NTRK;
    char m[28];
    switch (b) {
    case BK_PREV: if (ui.shift) undo_do(0); else sel_track((int32_t)t - 1); break;
    case BK_NEXT: if (ui.shift) undo_do(1); else sel_track((int32_t)t + 1); break;
    case BK_MUTE:
        if (ui.shift) { song.mute = 0; ui_say("ALL ON"); }
        else { song.mute ^= (uint8_t)(1u << t); ui_say((song.mute >> t) & 1u ? "MUTED" : "UNMUTED"); }
        break;
    case BK_SOLO:
        if (ui.shift) { song.solo = 0; ui_say("SOLO OFF"); }
        else { song.solo ^= (uint8_t)(1u << t); ui_say((song.solo >> t) & 1u ? "SOLO" : "SOLO OFF"); }
        break;
    case BK_AUDITION:
        if (ui.shift) {                          /* a new sound for the track */
            undo_take();
            sound_preset(t, 1u + rng() % 99u);
            ui_say("NEW SOUND");
        }
        ev_push(EV_HIT, t, 100);
        break;
    case BK_COPY:
        if (ui.shift) { memcpy(&clip_pat, PAT, sizeof clip_pat); clip_pat_ok = (uint8_t)(song.pat + 1u); ui_say("PATTERN COPIED"); }
        else { memcpy(clip_bar, &PAT->s[t][ui.bar * 16u % NSTEP], 16); clip_ok = 1; ui_say("BAR COPIED"); }
        break;
    case BK_PASTE:
        if (ui.shift ? !clip_pat_ok : !clip_ok) { ui_say("NOTHING COPIED"); break; }
        undo_take();
        if (ui.shift) memcpy(PAT, &clip_pat, sizeof clip_pat);
        else memcpy(&PAT->s[t][ui.bar * 16u % NSTEP], clip_bar, 16);
        ui_say("PASTED");
        break;
    case BK_CLEAR:
        undo_take();
        if (ui.shift) { memset(PAT->s, 0, sizeof PAT->s); ui_say("PATTERN CLEARED"); }
        else { memset(PAT->s[t], 0, NSTEP); ui_say("TRACK CLEARED"); }
        break;
    case BK_ACCENT:
        if (ui.shift) { undo_take(); double_track(t); ui_say("DOUBLED"); }
        else { ui.acc_mode ^= 1u; ui_say(ui.acc_mode ? "ACCENT EDIT" : "STEP EDIT"); }
        break;
    case BK_FOLLOW:
        if (ui.shift) { undo_take(); random_track(t); ui_say("RANDOM"); }
        else { ui.follow ^= 1u; ui_say(ui.follow ? "FOLLOW ON" : "FOLLOW OFF"); }
        break;
    case BK_TAP:
        if (ui.shift) {
            song.click = (uint8_t)((song.click + 1u) % CLICK_N);
            settings_save();
            ui_say(song.click == CLICK_ON ? "METRONOME ON" : song.click == CLICK_REC ? "METRONOME REC" : "METRONOME OFF");
            break;
        }
        if (ui.tap_t && fm1_ms - ui.tap_t < 2000u) {   /* the mean of the last taps */
            uint32_t d = fm1_ms - ui.tap_t;
            ui.tap_sum = ui.tap_n < 4u ? ui.tap_sum + d : ui.tap_sum - ui.tap_sum / 4u + d;
            if (ui.tap_n < 4u) ui.tap_n++;
            song.bpm = (uint8_t)clamp((int32_t)(60000u * ui.tap_n / ui.tap_sum), 60, 200);
            str_cpy(m, "TAP ", sizeof m);
            fmt_u(m + 4, song.bpm);
            ui_say(m);
        } else {
            ui.tap_n = 0;
            ui.tap_sum = 0;
            ui_say("TAP");
        }
        ui.tap_t = fm1_ms;
        break;
    }
}
static void white_key(uint32_t w, int down)
{
    uint32_t t = song.sel % NTRK, idx;
    uint8_t *s;
    if (ui.page == PG_NR) {                      /* the pads and the rate */
        if (w < 8u) {
            ev_push(down ? EV_PAD_ON : EV_PAD_OFF, w, 0);
            if (down) song.sel = (uint8_t)w;
        } else if (down) {
            nr.rate = (uint8_t)(w - 8u);
        }
        return;
    }
    if (ui.page != PG_SEQ && !ui.shift) {        /* off SEQ (LIVE and every other page): the eight sounds at once,
                                                  * D4..G5 accented; REC + PLAY writes them */
        if (down)
            ev_push(EV_HIT, w % 8u, w < 8u ? ui.lvel : 127u | S_ACC);
        return;
    }
    if (!down)
        return;
    if (ui.shift) {                              /* mute 1..8, solo 1..8 */
        if (w < 8u) song.mute ^= (uint8_t)(1u << w);
        else song.solo ^= (uint8_t)(1u << (w - 8u));
        return;
    }
    idx = ui.bar * 16u + w;
    if (idx >= tlen(t)) {
        ui_say("PAST THE LENGTH");
        return;
    }
    s = &PAT->s[t][idx];
    undo_burst(0x600u + t);
    if (ui.acc_mode)
        *s = *s ? (uint8_t)(*s ^ S_ACC) : (uint8_t)(100u | S_ACC);
    else
        *s = *s ? 0u : 100u;
    ui.cur = (uint8_t)idx;
    if (*s && !song.playing)
        ev_push(EV_HIT, t, (uint32_t)*s);
}
static void oct_key(int up)
{
    uint32_t t = song.sel % NTRK;
    if (ui.page == PG_SAVE) {                    /* OCT+ does it, OCT- goes back */
        if (!up) { go_page(ui.back == PG_SAVE ? PG_SEQ : ui.back); return; }
        proj_save(ui.slot, ui.swhat);
        return;
    }
    if (ui.page == PG_MIX) {
        ui.bank = (uint8_t)(up ? 1u : 0u);
        return;
    }
    if (ui.page == PG_SEQ) {
        if (ui.shift) {                          /* a bar more (a copy of the last) / less */
            uint32_t len = tlen(t), i;
            undo_take();
            if (up && len + 16u <= NSTEP) {
                uint32_t b0 = (len - 1u) / 16u * 16u;
                for (i = 0; i < 16u; i++)
                    PAT->s[t][b0 + 16u + i] = PAT->s[t][b0 + i];
                PAT->len[t] = (uint8_t)((len + 15u) / 16u * 16u + 16u);
                ui.bar = (uint8_t)(nbars(t) - 1u);
                ui_say("BAR ADDED");
            } else if (!up && len > 16u) {
                PAT->len[t] = (uint8_t)((len - 1u) / 16u * 16u);
                if (ui.bar >= nbars(t)) ui.bar = (uint8_t)(nbars(t) - 1u);
                ui_say("BAR REMOVED");
            }
            return;
        }
        ui.follow = 0;
        if (up ? ui.bar + 1u < nbars(t) : ui.bar > 0u)
            ui.bar = (uint8_t)(ui.bar + (up ? 1 : -1));
        else
            ui_say(up ? "LAST BAR" : "FIRST BAR");
        return;
    }
    sel_track((int32_t)t + (up ? 1 : -1));
}

static void ui_input(void)
{
    uint32_t p = io_pressed(), held = io_held(), keys = io_keys(), released = ui.held_prev & ~held, kd, ku, k, b;
    int32_t e[NE], any = 0;
    uint32_t glo = (held >> B_GLO) & 1u;
    ui.held_prev = held;
    for (k = 0; k < NE; k++)
        any |= e[k] = io_enc(k);
    kd = keys & ~ui.keys;
    ku = ui.keys & ~keys;
    ui.keys = keys;
    if (p & (1u << B_GLO)) {
        ui.glo_t0 = fm1_ms;
        ui.glo_used = 0;
    }
    /* SHIFT: GLO held long, or used together with another control */
    if (glo && (fm1_ms - ui.glo_t0 >= SHIFT_MS || (p & ~(1u << B_GLO)) || kd || any)) {
        if (fm1_ms - ui.glo_t0 < SHIFT_MS)
            ui.glo_used = 1;
        ui.shift = 1;
    }
    if (released & (1u << B_GLO)) {
        if (!ui.glo_used && fm1_ms - ui.glo_t0 < SHIFT_MS)
        {                                        /* GLO tapped: the mixer; on it, page 2 (pan) and back */
            if (ui.page == PG_MIX) ui.mixpan ^= 1u;
            else { go_page(PG_MIX); ui.mixpan = 0; }
        }
        ui.shift = 0;
    }
    if (!glo)
        ui.shift = 0;
    if (p) {
        static uint8_t seeded;
        if (!seeded) {                            /* the first press seeds the dice */
            seeded = 1;
            rng_state ^= fm1_ms * 2654435761u + 1u;
        }
    }
    if (p & (1u << B_PLAY)) {
        if (ui.shift) seq_restart();
        else seq_play(!song.playing);
    }
    if (p & (1u << B_REC)) {
        ui.rec_t0 = fm1_ms;
        ui.rec_long = 0;
        if (ui.shift) {                          /* ERASE armed / back to REC */
            song.erase ^= 1u;
            song.rec = 1;
            ui.rec_long = 2;                     /* (done: nothing on the release) */
            ui_say(song.erase ? "ERASE ARMED" : "REC ARMED");
        }
    }
    if ((held >> B_REC) & 1u && !ui.rec_long && fm1_ms - ui.rec_t0 >= HOLD_MS) {
        ui.rec_long = 1;                         /* REC held: QUANTIZE */
        go_page(PG_REC);
    }
    if ((released >> B_REC) & 1u && !ui.rec_long) {   /* REC tapped: armed / off */
        song.rec ^= 1u;
        if (!song.rec) song.erase = 0;
        ui_say(song.rec ? (song.erase ? "ERASE ARMED" : "REC ARMED") : "REC OFF");
    }
    /* OCT- OCT+ both held 5 s: UBOOT (from Felucca, keep); else the page's OCT */
    if ((held & (3u << B_OCTDN)) == (3u << B_OCTDN)) {
        uint32_t s = fm1_ms - ui.both_t;
        ui.uboot = (uint8_t)(s > 2000u ? (s >= 5000u ? 9u : 1u + (5000u - s) / 1000u) : 0u);
    } else {
        if (ui.uboot && ui.uboot < 9u)
            ui_say("UPDATE CANCELLED");
        ui.uboot = 0;
        ui.both_t = fm1_ms;
        if (p & (1u << B_OCTDN)) oct_key(0);
        if (p & (1u << B_OCTUP)) oct_key(1);
    }
    /* the page buttons: the first page of the button's group, again the next one */
    for (b = 0; b < NELEM(GROUPS); b++) {
        const group_t *g = &GROUPS[b];
        uint32_t i;
        if (!((p >> g->btn) & 1u))
            continue;
        for (i = 0; i < g->n && g->pg[i] != ui.page; i++)
            ;
        go_page(g->pg[i < g->n ? (i + 1u) % g->n : 0u]);
    }
    if (p & (1u << B_ARP)) {                     /* ARP: tap on / off, held: while held */
        ui.arp_t0 = fm1_ms;
        ui.arp_from = ui.page == PG_NR;
        if (ui.page != PG_NR)
            go_page(PG_NR);
    }
    if (released & (1u << B_ARP)) {
        if (fm1_ms - ui.arp_t0 >= HOLD_MS || ui.arp_from)
            go_page(ui.back == PG_NR ? PG_SEQ : ui.back);
    }
    /* the keys */
    for (k = 0; k < 27u; k++) {
        uint32_t bit = 1u << k, w = key_place(k);
        if (!((kd | ku) & bit))
            continue;
        if (key_black(k)) {
            if (kd & bit)
                black_key(w);
        } else {
            white_key(w, (kd & bit) != 0u);
        }
    }
    /* the knobs */
    if (e[EN_SELECT]) {                          /* SELECT: a new sound, rolled (its own transients); SHIFT: tempo */
        if (ui.shift) {
            pset(P_BPM, pget(P_BPM) + e[EN_SELECT]);
        } else {
            undo_burst(0x700u + song.sel);
            sound_preset(song.sel, 1u + rng() % 255u);
            ev_push(EV_HIT, song.sel, 100);
            ui_say("NEW SOUND");
        }
    }
    if (e[EN_ALGO]) {
        if (ui.shift) pset(P_PAT, pget(P_PAT) + e[EN_ALGO]);
        else sel_track((int32_t)song.sel + e[EN_ALGO]);
    }
    if (e[EN_PRESET])                            /* PRESETS: the stored patches (SHIFT: their patterns only) */
        patch_turn(e[EN_PRESET], ui.shift ? SW_PAT : SW_ALL);
    for (k = 0; k < 4u; k++) {
        uint32_t id = knob_id(k);
        int32_t d = e[EN_K1 + k];
        if (!d || !id)
            continue;
        if (id >= P_V0 && id <= P_DRV)
            undo_burst(0x800u + song.sel * 64u + id);
        pset(id, pget(id) + d);
        if (id == P_SMP || id == P_SLVL || id == P_SYN || id == P_SPITCH)   /* (heard as it is chosen) */
            ev_push(EV_HIT, song.sel, 110);
        ui.hot = (uint8_t)(k + 1u);
        ui.hot_t = fm1_ms | 1u;
    }
    if (ui.follow && song.playing && ui.page != PG_NR)   /* FOLLOW: the bar playing */
        ui.bar = (uint8_t)((song.pstep % tlen(song.sel)) / 16u);
    if (ui.hot_t && fm1_ms - ui.hot_t > 1200u)
        ui.hot_t = 0;
    if (ui.msg_t && fm1_ms - ui.msg_t > 1600u)
        ui.msg_t = 0;
}

static void ui_leds(void)
{
    uint32_t k, b, t = song.sel % NTRK, ps = song.pstep % tlen(t), blink = (fm1_ms / 250u) & 1u;
    uint8_t flash = hit_flash;
    hit_flash = 0;
    for (k = 0; k < NTRK; k++)
        if ((flash >> k) & 1u)
            ui.lit_t[k] = fm1_ms | 1u;
    for (b = 0; b < NB; b++) {
        int on = (b == PAGE_BTN[ui.page % PG_N] && b != B_REC) || (b == B_ENV && ui.page == PG_SAMPLE) ||
                 (b == B_PLAY && song.playing) || (b == B_GLO && ui.shift) ||
                 (b == B_REC && song.rec && (!song.erase || blink)) ||
                 (b == B_OCTDN && ui.page == PG_SEQ && ui.bar > 0u) ||
                 (b == B_OCTUP && ui.page == PG_SEQ && ui.bar + 1u < nbars(t)) ||
                 (b == B_OCTDN && ui.page == PG_MIX && !ui.bank) || (b == B_OCTUP && ui.page == PG_MIX && ui.bank);
        io_led_button(b, on);
    }
    for (k = 0; k < 27u; k++) {
        uint32_t w = key_place(k);
        int on = 0;
        if (key_black(k)) {
            on = (w == BK_MUTE && ((song.mute >> t) & 1u)) || (w == BK_SOLO && ((song.solo >> t) & 1u)) ||
                 (w == BK_ACCENT && ui.acc_mode) || (w == BK_FOLLOW && ui.follow) ||
                 (w == BK_TAP && song.playing && song.pstep % 4u == 0u && song.pos < FS / 20u);
        } else if (ui.page == PG_NR) {
            on = w < 8u ? (((nr.held | nr.latched | flash) >> w) & 1u) != 0u : w - 8u == nr.rate;
        } else if (ui.shift) {
            on = w < 8u ? !((song.mute >> w) & 1u) : ((song.solo >> (w - 8u)) & 1u) != 0u;
        } else if (ui.page != PG_SEQ) {          /* (the steps only on SEQ; elsewhere the pads, lit as hit) */
            on = ((flash >> (w % 8u)) & 1u) != 0u;
        } else {
            uint32_t idx = ui.bar * 16u + w;
            uint8_t s = idx < tlen(t) ? PAT->s[t][idx] : 0u;
            on = ui.acc_mode ? (s & S_ACC) != 0u : s != 0u;
            if (song.playing && idx == ps)
                on = !on;
        }
        io_led_key(k, on);
    }
}

/* ------------------------------------------------------------ drawing --- */
static uint32_t hsh(uint32_t h, const char *s) { while (*s) h = h * 33u + (uint8_t)*s++; return h; }
static void draw_head(int force)
{
    char b[24];
    uint32_t t = song.sel % NTRK, blink = song.rec && song.playing ? (fm1_ms / 250u) & 1u : 0u;
    uint32_t sig = t * 7u + song.playing * 131u + song.bpm * 1009u + (ui.msg_t ? hsh(5u, ui.msg) : 0u) +
                   ui.shift * 7333u + song.rec * 191u + song.erase * 4099u + blink * 37u + song.pat * 7919u +
                   (song.pat_next < NPAT ? song.pat_next * 104729u + 3u : 0u) + ui.bar * 271u + nbars(t) * 6151u;
    int32_t x;
    if (!force && sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, H_HEAD, T_BG);
    cv_rrect(4, 4, 16, 16, 3, TRK_COL[t], T_BG);  /* the sound: its colour, number and name */
    b[0] = (char)('1' + t);
    b[1] = 0;
    cv_text_in(4, 4 + CAP_IN(S, 16), 16, &AF_S, b, T_BG, TRK_COL[t]);
    x = cv_text(26, CAP_IN(M, H_HEAD), &AF_M, TRK_NAME[t], T_TEXT);
    if (song.rec) {
        x += 6;
        cv_rrect(x, 8, 9, 9, 4, blink ? T_BG : C_REC, T_BG);
        cv_text(x + 12, CAP_IN(S, H_HEAD), &AF_S, song.erase ? "ERS" : "REC", C_REC);
    }
    if (ui.shift) {
        cv_rrect(150, 4, 46, 16, 3, T_ACCENT, T_BG);
        cv_text_in(150, 4 + CAP_IN(S, 16), 46, &AF_S, "SHIFT", T_BG, T_ACCENT);
    } else if (ui.msg_t) {
        cv_text_r(222, CAP_IN(M, H_HEAD), &AF_M, ui.msg, T_ACCENT, T_BG);
    } else {
        b[0] = 'P';                              /* the pattern (and the one cued), the bar shown, the tempo */
        fmt_u(b + 1, song.pat + 1u);
        if (song.pat_next < NPAT) {
            str_cat(b, ">", sizeof b);
            fmt_u(b + str_len(b), song.pat_next + 1u);
        }
        str_cat(b, " B", sizeof b);
        fmt_u(b + str_len(b), ui.bar + 1u);
        str_cat(b, "/", sizeof b);
        fmt_u(b + str_len(b), nbars(t));
        cv_text_r(170, CAP_IN(S, H_HEAD), &AF_S, b, T_MID, T_BG);
        fmt_u(b, song.bpm);
        cv_text_r(220, CAP_IN(M, H_HEAD), &AF_M, b, T_THEME, T_BG);
    }
    if (song.playing) {                          /* play: a triangle; stop: a square */
        int32_t i;
        for (i = 0; i <= 10; i++)
            cv_rect(228, 7 + i, 1 + (i <= 5 ? i : 10 - i), 1, T_THEME);
    } else {
        cv_rect(228, 9, 8, 8, T_MID);
    }
    cv_blit(0, 0);
}

static void draw_card(uint32_t c, uint32_t id, int force)
{
    char val[16], key[48];
    const char *unit = "";
    pdesc_t d = pdesc_of(id);
    int hot = ui.hot == c + 1u && ui.hot_t;
    int32_t ratio = -1, x;
    uint32_t sig;
    uint16_t surf = ui.shift && id ? T_RAISE : T_SURF;
    val[0] = 0;
    if (id) {
        pformat(id, val, &unit);
        ratio = d.max > d.min ? (pget(id) - d.min) * 1000 / (d.max - d.min) : -1;
        if (d.fmt == FM_NAME || id == P_ROT)
            ratio = -1;
    }
    str_cpy(key, id ? d.label : "", sizeof key);
    str_cat(key, val, sizeof key);
    str_cat(key, unit, sizeof key);
    sig = hsh(c * 31u + (uint32_t)hot * 7u + (uint32_t)(ratio + 2) * 101u + ui.shift * 5u + id * 977u, key);
    if (!force && sig == ui.sig[1 + c])
        return;
    ui.sig[1 + c] = sig;
    cv_begin(CARD_W, CARD_H, T_BG);
    cv_rrect(0, 0, CARD_W, CARD_H, 4, id ? surf : T_BG, T_BG);
    if (id) {
        const aafont_t *vf = text_w(&AF_M, val) + (unit[0] ? text_w(&AF_S, unit) + 3 : 0) > CARD_W - 8 ? &AF_S : &AF_M;
        cv_text_fit(5, 3, &AF_S, d.label, hot ? T_ACCENT : T_MID, surf, CARD_W - 7);
        x = cv_text_fit(5, vf == &AF_M ? 17 : 20, vf, val, hot ? T_ACCENT : T_THEME, surf, CARD_W - 8);
        if (unit[0])
            cv_text_on(x + 3, 20, &AF_S, unit, T_MID, surf);
        if (ratio >= 0) {
            int32_t gw = CARD_W - 10, fx = ratio * gw / 1000;
            cv_rrect(5, 38, gw, 3, 1, T_BG, surf);
            cv_rrect(5, 38, fx < 3 ? 3 : fx, 3, 1, hot ? T_ACCENT : T_THEME, T_BG);
        }
    } else {
        cv_text_in(0, CAP_IN(S, CARD_H), CARD_W, &AF_S, "-", T_DIM, T_BG);
    }
    cv_blit((uint32_t)CARD_X(c), Y_LABEL);
}

/* ---- the pictures (the graph area, 240 x 122, drawn on SURF) */
#define GW 234
#define GH 122
static void g_label(int32_t x, int32_t y, const char *s, uint16_t c) { cv_text_on(x, y, &AF_S, s, c, T_SURF); }
static void g_box(void) { cv_begin(240, H_GRAPH, T_BG); cv_rrect(3, 0, GW, GH, 5, T_SURF, T_BG); cv_bg = T_SURF; }
static void g_grid(void)                         /* the eight tracks, the bar shown (the selected track's) */
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        uint32_t len = tlen(t), bar = ui.bar % nbars(t), ps = song.pstep % len;
        int32_t y = 4 + (int32_t)t * 14;
        int on = trk_audible(t);
        uint16_t c = on ? TRK_COL[t] : T_DIM, bg = t == song.sel ? T_RAISE : T_SURF;
        if (t == song.sel)
            cv_rrect(6, y - 1, 228, 14, 3, T_RAISE, T_SURF);
        cv_text_fit(10, y + CAP_IN(S, 12), &AF_S, TRK_SHORT[t], on ? T_TEXT : T_DIM, bg, 20);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = bar * 16u + i;
            uint8_t s = idx < len ? pat[song.pat % NPAT].s[t][idx] : 0u;
            int32_t x = 32 + (int32_t)i * 12 + (int32_t)(i / 4u) * 2;
            uint16_t sc = idx >= len ? bg : s ? (s & S_ACC ? ux_mix(c, T_TEXT, 45) : c) : T_LINE;
            if (song.playing && idx == ps)
                sc = s ? T_TEXT : T_ACCENT;
            cv_rrect(x, y + 1, 10, 10, 2, sc, bg);
            if (t == song.sel && idx == ui.cur && ui.page == PG_SEQ)
                cv_rect(x + 2, y + 12, 6, 1, T_ACCENT);
        }
    }
}
static void g_params(void)                       /* SYNTH LAYER: the eight parameters as bars */
{
    uint32_t k, kind = TRK_KIND[song.sel % NTRK];
    const sound_t *s = TS;
    for (k = 0; k < 8u; k++) {
        int32_t x = 10 + (int32_t)(k / 4u) * 114, y = 8 + (int32_t)(k % 4u) * 28, w = 100;
        int sh = (k >= 4u) == ((ui.page == PG_VOICE2) != (ui.shift != 0));
        g_label(x, y, LX_LABEL[kind][k], sh ? T_TEXT : T_MID);
        cv_rrect(x, y + 14, w, 6, 2, T_LINE, T_SURF);
        cv_rrect(x, y + 14, 3 + s->p[k] * (w - 3) / 127, 6, 2, sh ? TRK_COL[song.sel % NTRK] : T_DIM, T_SURF);
    }
}
static void g_sample(void)                       /* SAMPLE LAYER: the transient's wave and its decay */
{
    const sound_t *s = TS;
    int32_t i, mid = 64, env = 32767, k = lx_k(20u + s->sdec * 50u / 127u);
    char b[24];
    if (s->smp < TR_SMP0) {
        cv_text_in(3, 40, GW, &AF_M, s->smp == TR_SNAP ? "SNAP: A PITCH SNAP" : "NO SAMPLE LAYER", T_MID, T_SURF);
        cv_text_in(3, 70, GW, &AF_S, s->smp == TR_SNAP ? "LEVEL = AMOUNT, PITCH = SPEED" : "KNOB 1: CHOOSE ONE", T_DIM, T_SURF);
        return;
    }
    {
    const int8_t *tr = s->smp >= TR_OWN0 ? OWN_TRANS[song.sel % NTRK][(s->smp - TR_OWN0) % OWN_TRANS_N]
                                         : LXR_TRANS[(s->smp - TR_SMP0) % LXR_TRANS_N];
    for (i = 0; i < 220; i++) {                  /* min / max a column, the decay over it */
        uint32_t a = (uint32_t)i * LXR_TRANS_LEN / 220u, e = (uint32_t)(i + 1) * LXR_TRANS_LEN / 220u, j;
        int32_t lo = 0, hi = 0;
        for (j = a; j < e; j++) {
            int32_t v = tr[j];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            if (s->sdec < 127u)                  /* (the decay as lx_render has it, a sample at a time) */
                env = lx_dec(env, k);
        }
        lo = lo * env / 32767;                   /* (the shape; LEVEL is on its card) */
        hi = hi * env / 32767;
        cv_rect(10 + i, mid - hi * 48 / 128, 1, 1 + (hi - lo) * 48 / 128, TRK_COL[song.sel % NTRK]);
    }
    smp_name(b, s->smp, song.sel);
    str_cat(b, s->smp >= TR_OWN0 ? "  OWN TRANSIENT" : "  LXR TRANSIENT", sizeof b);
    g_label(10, 104, b, T_MID);
    }
}
static void g_mixer(void)                        /* MIXER: eight strips, the bank's four lit */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        const sound_t *s = &snd[t];
        int32_t x = 10 + (int32_t)t * 28, h = s->level * 76 / 127, pk = clamp(peak[t] * 76 / 20000, 0, 76);
        int inb = t / 4u == ui.bank, on = trk_audible(t);
        uint16_t c = on ? TRK_COL[t] : T_DIM;
        if (inb)
            cv_rrect(x - 3, 2, 26, 118, 3, T_RAISE, T_SURF);
        cv_rrect(x + 2, 8, 6, 78, 2, T_LINE, inb ? T_RAISE : T_SURF);
        cv_rrect(x + 2, 86 - h, 6, h < 2 ? 2 : h, 2, c, T_LINE);
        cv_rect(x + 11, 86 - pk, 4, pk, on ? T_THEME : T_DIM);
        if (ui.mixpan) {                         /* PAN: a bar from the centre, the strip's colour */
            int32_t pw = s->pan * 10 / 64;
            cv_rect(x - 1, 94, 22, 1, T_LINE);
            cv_rect(pw < 0 ? x + 10 + pw : x + 10, 91, pw < 0 ? -pw + 1 : pw + 1, 7, c);
        } else {
            cv_rect(x + 10 + s->pan * 9 / 64, 92, 2, 5, T_MID);
        }
        cv_text_fit(x, 100, &AF_S, TRK_SHORT[t], on ? T_TEXT : T_DIM, inb ? T_RAISE : T_SURF, 22);
        if ((song.mute >> t) & 1u) cv_text_fit(x + 1, 108 + 2, &AF_S, "M", C_REC, inb ? T_RAISE : T_SURF, 10);
        if ((song.solo >> t) & 1u) cv_text_fit(x + 11, 108 + 2, &AF_S, "S", T_ACCENT, inb ? T_RAISE : T_SURF, 10);
        if (peak[t] > 0) peak[t] -= peak[t] / 6 + 40;
        if (peak[t] < 0) peak[t] = 0;
    }
}
static void g_repeat(void)                       /* NOTE REPEAT: the pads, the rate */
{
    uint32_t t;
    cv_text_in(3, 6, GW, &AF_L, NR_NAME[nr.rate % NR_N], T_THEME, T_SURF);
    for (t = 0; t < NTRK; t++) {
        int32_t x = 12 + (int32_t)t * 27, y = 52;
        int on = ((nr.held | nr.latched) >> t) & 1u;
        cv_rrect(x, y, 24, 30, 4, on ? TRK_COL[t] : T_RAISE, T_SURF);
        cv_text_in(x, y + CAP_IN(S, 30), 24, &AF_S, TRK_SHORT[t], on ? T_INK : T_MID, on ? TRK_COL[t] : T_RAISE);
    }
    g_label(12, 92, "F3..C4 PADS   D4..G5 RATE", T_MID);
    g_label(12, 106, nr.latch ? "LATCH: A PAD AGAIN STOPS IT" : (song.rec && song.playing ? "RECORDING" : "REC + PLAY: RECORD"),
            song.rec && song.playing ? C_REC : T_DIM);
}
static void g_patterns(void)                     /* PATTERNS: 16, the one playing, the one cued, the chain */
{
    uint32_t i, t, k;
    for (i = 0; i < NPAT; i++) {
        int32_t x = 12 + (int32_t)(i % 8u) * 27, y = 10 + (int32_t)(i / 8u) * 46;
        uint32_t used = 0, inch = (i + NPAT - song.chain0) % NPAT < song.chain;
        char b[4];
        uint16_t c;
        for (t = 0; t < NTRK && !used; t++)
            for (k = 0; k < NSTEP && !used; k++)
                used = pat[i].s[t][k] != 0u;
        c = i == song.pat ? T_THEME : i == song.pat_next ? T_ACCENT : inch ? ux_mix(T_RAISE, T_THEME, 30) : T_RAISE;
        cv_rrect(x, y, 24, 38, 4, c, T_SURF);
        fmt_u(b, i + 1u);
        cv_text_in(x, y + CAP_IN(S, 38), 24, &AF_S, b, i == song.pat || i == song.pat_next ? T_INK : used ? T_TEXT : T_DIM, c);
    }
    g_label(12, 106, "K3 COPIES, K4 PASTES THE PATTERN SHOWN", T_DIM);
}
static void g_store(void)
{
    uint32_t i;
    for (i = 0; i < NSLOT; i++) {
        int32_t x = 12 + (int32_t)(i % 4u) * 54, y = 10 + (int32_t)(i / 4u) * 44;
        char b[12];
        uint16_t c = i == ui.slot ? T_THEME : T_RAISE;
        cv_rrect(x, y, 50, 36, 4, c, T_SURF);
        fmt_u(b, i + 1u);
        cv_text_in(x, y + 4, 50, &AF_M, b, i == ui.slot ? T_INK : T_TEXT, c);
        cv_text_in(x, y + 22, 50, &AF_S, ui.used[i] ? "USED" : "EMPTY", i == ui.slot ? T_INK : T_MID, c);
    }
    g_label(12, 104, ui.swhat == SW_KIT ? "OCT+ SAVES THE KIT" : ui.swhat == SW_PAT ? "OCT+ SAVES THE PATTERNS" :
            "OCT+ SAVES KIT + PATTERNS", T_MID);
}
static void g_live(void)                         /* LIVE: the eight pads (lit as they are hit), their accented row */
{
    uint32_t t, r;
    for (r = 0; r < 2u; r++)
        for (t = 0; t < NTRK; t++) {
            int32_t x = 12 + (int32_t)t * 27, y = 8 + (int32_t)r * 40;
            int on = ui.lit_t[t] && fm1_ms - ui.lit_t[t] < 150u;
            uint16_t c = on ? TRK_COL[t] : r ? ux_mix(T_RAISE, TRK_COL[t], 25) : T_RAISE;
            cv_rrect(x, y, 24, 34, 4, c, T_SURF);
            cv_text_in(x, y + CAP_IN(S, 34), 24, &AF_S, TRK_SHORT[t], on ? T_INK : T_MID, c);
        }
    g_label(12, 92, "F3..C4 SOUNDS 1-8  D4..G5 ACCENTED", T_MID);
    g_label(12, 106, song.rec && song.playing ? "RECORDING (REC HELD: QUANTIZE)" : "REC + PLAY: RECORD", song.rec && song.playing ? C_REC : T_DIM);
}
static void g_lfo(void)                          /* LFO 1 / 2: one cycle of its wave at its depth, where it goes */
{
    const lfo_t *l = &TS->lfo[ui.page == PG_LFO2];
    int32_t i, mid = 50, prev = 0;
    char b[40];
    cv_rect(10, mid, 220, 1, T_LINE);
    for (i = 0; i < 220; i++) {
        uint32_t ph = (uint32_t)i * (0xFFFFFFFFu / 220u);
        int32_t v;
        switch (l->wave) {
        case LW_TRI: v = (int32_t)(ph >> 15); v = v < 65536 ? v - 32768 : 98303 - v; break;
        case LW_SAW: v = (int32_t)(ph >> 16) - 32768; break;
        case LW_SQR: v = ph & 0x80000000u ? -32767 : 32767; break;
        case LW_SH: v = ((int32_t)((i / 28) * 2654435761u >> 17) - 16384) * 2; break;
        default: v = sine(ph); break;
        }
        v = -(v >> 7) * l->depth / 64 * 40 / 256;
        if (i && (v - prev > 1 || prev - v > 1))
            cv_rect(10 + i, mid + (v < prev ? v : prev), 1, (v > prev ? v - prev : prev - v) + 1, l->dest ? TRK_COL[song.sel % NTRK] : T_DIM);
        cv_rect(10 + i, mid + v, 1, 2, l->dest ? TRK_COL[song.sel % NTRK] : T_DIM);
        prev = v;
    }
    str_cpy(b, l->dest ? N_LDEST[l->dest % LD_N] : "OFF", sizeof b);
    str_cat(b, "  ", sizeof b);
    str_cat(b, N_LRATE[l->rate % LR_N], sizeof b);
    str_cat(b, "  IN TIME WITH THE TEMPO", sizeof b);
    g_label(10, 104, b, T_MID);
}
static void g_quant(void)                        /* QUANTIZE: the grid over a bar, where a late / early hit goes */
{
    uint32_t i, q = Q_STEPS[song.qgrid % Q_N];
    for (i = 0; i < 16u; i++) {
        int32_t x = 12 + (int32_t)i * 13 + (int32_t)(i / 4u) * 2;
        cv_rrect(x, 20, 11, 30, 2, i % q ? T_LINE : T_THEME, T_SURF);
    }
    g_label(12, 62, song.qround ? "A HIT GOES TO THE GRID POINT BEFORE IT" : "A HIT GOES TO THE NEAREST GRID POINT", T_TEXT);
    g_label(12, 80, song.click == CLICK_REC ? "METRONOME WHILE RECORDING" : song.click == CLICK_ON ? "METRONOME ALWAYS" : "METRONOME OFF", T_MID);
    g_label(12, 104, "REC TAP: ARM   SHIFT + REC: ERASE", T_DIM);
}
static uint32_t graph_sig(void)
{
    uint32_t h = ui.page * 7u + ui.shift * 3u + song.sel * 131u + ui.bar * 17u + song.pat * 7919u + ui.cur * 37u;
    uint32_t i, t;
    if (ui.page == PG_MIX)
        return h + fm1_ms / 50u;                 /* (the meters move) */
    if (ui.page == PG_NR)
        return h + nr.rate * 101u + (nr.held | nr.latched) * 1009u + nr.latch + song.rec * 3u + song.playing * 5u;
    if (ui.page == PG_SAMPLE)
        return h + TS->smp * 101u + TS->slvl * 1009u + TS->sdec * 10007u;
    if (ui.page == PG_VOICE || ui.page == PG_VOICE2)
        return h + hsh(1u, (const char *)TS->p) + TS->p[0] * 3u;
    if (ui.page == PG_PAT)
        return h + song.pat_next * 11u + song.chain * 101u + song.chain0 * 13u;
    if (ui.page == PG_LIVE) {
        for (t = 0; t < NTRK; t++)
            h = h * 3u + (ui.lit_t[t] && fm1_ms - ui.lit_t[t] < 150u);
        return h + song.rec * 5u + song.playing * 7u + ui.lvel;
    }
    if (ui.page == PG_LFO1 || ui.page == PG_LFO2) {
        const lfo_t *l = &TS->lfo[ui.page == PG_LFO2];
        return h + l->dest * 3u + l->wave * 37u + l->rate * 101u + (uint32_t)(l->depth + 64) * 1009u;
    }
    if (ui.page == PG_REC)
        return h + song.qgrid * 11u + song.qround * 101u + song.click * 1009u;
    if (ui.page == PG_SAVE)
        return h + ui.slot * 11u + ui.swhat * 101u + ui.used[0] + ui.used[1] * 2u + ui.used[2] * 4u + ui.used[3] * 8u +
               ui.used[4] * 16u + ui.used[5] * 32u + ui.used[6] * 64u + ui.used[7] * 128u;
    for (t = 0; t < NTRK; t++) {                 /* the grid: the bar shown, the playhead, mute, solo */
        h = h * 31u + tlen(t) + (trk_audible(t) ? 7u : 0u);
        for (i = 0; i < 16u; i++)
            h = h * 33u + pat[song.pat % NPAT].s[t][(ui.bar % nbars(t)) * 16u + i];
    }
    return h + (song.playing ? song.pstep * 977u + 1u : 0u);
}
static void draw_graph(int force)
{
    uint32_t sig = graph_sig();
    if (!force && sig == ui.sig[5])
        return;
    ui.sig[5] = sig;
    g_box();
    switch (ui.page) {
    case PG_VOICE: case PG_VOICE2: g_params(); break;
    case PG_SAMPLE: g_sample(); break;
    case PG_MIX: g_mixer(); break;
    case PG_NR: g_repeat(); break;
    case PG_PAT: g_patterns(); break;
    case PG_SAVE: g_store(); break;
    case PG_LIVE: g_live(); break;
    case PG_LFO1: case PG_LFO2: g_lfo(); break;
    case PG_REC: g_quant(); break;
    default: g_grid(); break;
    }
    cv_blit(0, Y_GRAPH);
}
static void draw_foot(int force)
{
    static const khint_t H_SEQ[3] = {{KC_KEYS, "STEPS"}, {KC_OCTPM, "BAR"}, {KC_GLO, "SHIFT"}};
    static const khint_t H_MIX[3] = {{KC_K14, "LEVEL"}, {KC_OCTPM, "BANK"}, {KC_GLO, "PAN"}};
    static const khint_t H_MIXPAN[3] = {{KC_K14, "PAN"}, {KC_OCTPM, "BANK"}, {KC_GLO, "LEVEL"}};
    static const khint_t H_NR[3] = {{KC_KEYS, "PADS"}, {KC_K1, "RATE"}, {KC_ARP, "OFF"}};
    static const khint_t H_SAVE[3] = {{KC_OCTUP, "DO IT"}, {KC_K1, "SLOT"}, {KC_OCTDN, "BACK"}};
    static const khint_t H_PAGE[3] = {{KC_ALGO, "SOUND"}, {KC_SELECT, "NEW"}, {KC_PRESETS, "PATCH"}};
    static const khint_t H_LIVE[3] = {{KC_KEYS, "PADS"}, {KC_REC, "REC"}, {KC_SEQ, "STEPS"}};
    static const khint_t H_REC[3] = {{KC_K1, "GRID"}, {KC_K2, "TO"}, {KC_REC, "ARM"}};
    static const khint_t H_SHIFT[3] = {{KC_K14, "ROW B"}, {KC_KEYS, "MUTE"}, {KC_PLAY, "TOP"}};
    char b[32];
    uint32_t sig;
    const khint_t *h = ui.shift ? H_SHIFT : ui.page == PG_SEQ ? H_SEQ : ui.page == PG_MIX ? (ui.mixpan ? H_MIXPAN : H_MIX) :
                       ui.page == PG_NR ? H_NR : ui.page == PG_SAVE ? H_SAVE : ui.page == PG_LIVE ? H_LIVE :
                       ui.page == PG_REC ? H_REC : H_PAGE;
    uint32_t gi, gn = 0, gk = 0;
    str_cpy(b, PG_NAME[ui.page % PG_N], sizeof b);
    if (ui.page == PG_MIX)
        str_cat(b, ui.mixpan ? (ui.bank ? " 5-8 PAN" : " 1-4 PAN") : ui.bank ? " 5-8" : " 1-4", sizeof b);
    if (ui.page == PG_SEQ && ui.acc_mode)
        str_cat(b, " ACC", sizeof b);
    if (ui.shift)
        str_cat(b, " B", sizeof b);
    sig = hsh(ui.page * 31u + ui.shift * 101u + ui.mixpan * 7u, b);
    if (!force && sig == ui.sig[6])
        return;
    ui.sig[6] = sig;
    cv_begin(240, H_FOOT, T_BG);
    cv_keycap(6, 2, ui.shift ? KC_GLO : PAGE_KC[ui.page % PG_N], T_KEY, T_INK, T_BG);
    cv_text(52, CAP_IN(M, 18), &AF_M, b, ui.shift ? T_ACCENT : T_TEXT);
    for (gi = 0; gi < NELEM(GROUPS); gi++)        /* the button's pages: dots, the one shown lit */
        if (GROUPS[gi].btn == PAGE_BTN[ui.page % PG_N]) {
            gn = GROUPS[gi].n;
            for (gk = 0; gk < gn && GROUPS[gi].pg[gk] != ui.page; gk++)
                ;
        }
    if (ui.page == PG_MIX) {
        gn = 2;
        gk = ui.mixpan;
    }
    for (gi = 0; !ui.shift && gn > 1u && gi < gn; gi++)
        cv_rrect(236 - (int32_t)(gn - gi) * 9, 7, 6, 6, 3, gi == gk ? T_THEME : T_LINE, T_BG);
    cv_key_row(6, 234, 21, h, 3u, 7u, T_BG);
    cv_blit(0, Y_FOOT);
}
static void draw_uboot(int force)
{
    char b[24];
    uint32_t sg = 0xB00700u + ui.uboot;
    if (!force && ui.sig[7] == sg)
        return;
    ui.sig[7] = sg;
    lcd_fill(0, 0, 240, 240, T_BG);
    if (ui.uboot >= 9u)
        str_cpy(b, "UBOOT", sizeof b);
    else {
        str_cpy(b, "UPDATE MODE IN ", sizeof b);
        fmt_u(b + str_len(b), ui.uboot);
    }
    draw_text_box(0, 108, 240, &AF_M, b, T_ACCENT, 1);
}
static void ui_draw(int force)
{
    uint32_t i;
    if (ui.force) {
        ui.force = 0;
        force = 1;
    }
    if (ui.uboot) {                               /* the UBOOT countdown over everything */
        draw_uboot(force);
        for (i = 0; i < 7u; i++)
            ui.sig[i] = 0;
        return;
    }
    if (ui.sig[7]) {
        ui.sig[7] = 0;
        force = 1;
    }
    if (force) {
        lcd_fill(0, H_HEAD, 240, Y_LABEL - H_HEAD, T_BG);
        lcd_fill(0, Y_LABEL + CARD_H, 240, Y_GRAPH - Y_LABEL - CARD_H, T_BG);
        for (i = 0; i < 4u; i++)
            lcd_fill((uint32_t)(CARD_X(i) + CARD_W), Y_LABEL, i < 3u ? 2u : 240u - (uint32_t)(CARD_X(3) + CARD_W), CARD_H, T_BG);
        lcd_fill(0, Y_LABEL, 3, CARD_H, T_BG);
        lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_FOOT - Y_GRAPH - H_GRAPH, T_BG);
    }
    draw_head(force);
    for (i = 0; i < 4u; i++)
        draw_card(i, knob_id(i), force);
    draw_graph(force);
    draw_foot(force);
}

/* the companion's frames (ONE CLICK's protocol): none here, the updater keeps its own (ota_service) */
static int proto_frame(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }

static void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < UI_NPALETTES; i++)
        N_THEME[i] = UI_PALETTES[i].name;
    song.bpm = 120;
    song.swing = 0;
    song.sel = 0;
    song.master_q12 = 2048;
    song.theme = PAL_AMBER;
    song.ddec = 64;
    song.chain = 1;
    song.pat_next = 0xFF;
    song.drv_mode = DM_SOFT;
    song.drv_amt = 40;
    song.drv_tone = 90;
    song.drv_lvl = 100;
    song.dly_div = 1;
    song.dly_fb = 50;
    song.dly_tone = 60;
    song.dly_lvl = 110;
    song.rev_size = 70;
    song.rev_damp = 30;
    song.rev_lvl = 110;
    song.qgrid = Q_16;
    song.click = CLICK_REC;
    ui.copy = 1;
    ui.lvel = 100;
    for (i = 0; i < NTRK; i++)
        sound_init(i);
    for (i = 0; i < NPAT; i++) {
        uint32_t t;
        memset(&pat[i], 0, sizeof pat[i]);
        for (t = 0; t < NTRK; t++)
            pat[i].len[t] = 16;
    }
    demo_pattern(&pat[0]);
    store_scan();                                 /* the settings (theme, MIDI IN, metronome), the slots in use */
    palette_set(song.theme);
    undo.n_undo = undo.n_redo = 0;
    ui.page = ui.back = PG_SEQ;
}

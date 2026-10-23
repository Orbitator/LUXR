/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: the panel and the screen (after Felucca's conventions and ONE CLICK's layout). The full map: README.md.
 *
 *   KNOBS   SELECT the sound 1..8 (SHIFT: the pattern) · ALGORITHM a new sound for the selected one, rolled (SHIFT:
 *           the sample layer's samples in turn, the synth layer kept) · PRESETS the stored patches (what STORE's KNOB 3 says: kit + patterns, kit, patterns) · KNOB 1..4 the page's
 *           four cards, SHIFT the page's second row · MASTER volume
 *   SHIFT   GLO held (from 250 ms, or at once when another control is used while it is down). GLO tapped: the MIXER,
 *           tapped again its page 2 (PAN) and back.
 *   PAGES   a button tapped again shows its next page: HOME kit, midi (held: system) · SEQ sequencer, live pads, tracks · EDIT
 *           synth layer, sample layer, sound mix · ENV sample layer · LFO lfo 1, lfo 2 · FX sends, drive, delay, reverb,
 *           master · SCL patterns, song · ARP note repeat (tap: on / off, held: while held) · GLO mixer, its pans ·
 *           SAVE store · REC held: quantize
 *   PLAY    start / stop (SHIFT: from the top)     REC tap: arm recording (SHIFT: arm ERASE), held: QUANTIZE
 *   OCT-/+  SEQ: the bar shown (SHIFT: a bar less / more) · MIXER: sounds 1-4 / 5-8 · SAVE: back / do it ·
 *           else the sound before / after. Both held 5 s: UBOOT.
 *   WHITE   the 16 steps of the bar shown (C#5: accents) · LIVE: F3..C4 sounds 1..8, D4..G5 the same accented ·
 *           NOTE REPEAT: F3..C4 the pads, D4..G5 the rate · SHIFT: F3..C4 mute sounds 1..8, D4..G5 solo them
 *   BLACK   F#3 sound◂ G#3 sound▸ A#3 mute C#4 solo D#4 random mute F#4 copy G#4 paste A#4 undo C#5 P-LOCK D#5 NUDGE
 *           F#5 FILL (held); SHIFT: undo redo, all on, solo off, new sound, copy / paste the pattern, redo, accent mode,
 *           follow, tap tempo. P-LOCK / NUDGE: the white keys are the steps on every page (a step held + a knob: its lock /
 *           its timing); P-LOCK + REC + PLAY: the knobs write locks (motion rec)
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
static int32_t io_batt(void);                    /* the battery divider, 0..1023 (-1: none) */
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

enum { PG_KIT, PG_SEQ, PG_LIVE, PG_VOICE, PG_VOICE2, PG_SAMPLE, PG_SAMPLE2, PG_SND, PG_LFO1, PG_LFO2, PG_FXS, PG_FXD, PG_FXDL, PG_FXDL2, PG_FXR,
       PG_PAT, PG_NR, PG_MIX, PG_SAVE, PG_REC, PG_INFO, PG_MIDI, PG_SONG, PG_MFX, PG_NEW, PG_ENV, PG_LEARN, PG_TRK, PG_N };
static const char *const PG_NAME[PG_N] = {"LUXR KIT", "SEQUENCER", "LIVE PADS", "SYNTH 1-4", "SYNTH 5-8", "SAMPLE LAYER", "SAMPLE 2",
                                          "SOUND MIX", "LFO 1", "LFO 2", "FX SENDS", "DRIVE", "DELAY", "DELAY 2", "REVERB",
                                          "PATTERNS", "NOTE REPEAT", "MIXER", "STORE", "QUANTIZE", "SYSTEM", "MIDI", "SONG",
                                          "MASTER", "NEW PATCH", "ENVELOPE", "CC LEARN", "TRACKS"};
static const uint8_t PAGE_BTN[PG_N] = {B_HOME, B_SEQ, B_SEQ, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_EDIT, B_LFO, B_LFO, B_FX, B_FX, B_FX, B_FX,
                                       B_FX, B_SCL, B_ARP, B_GLO, B_SAVE, B_REC, B_HOME, B_HOME, B_SCL, B_FX, B_SAVE, B_ENV, B_HOME, B_SEQ};
static const uint8_t PAGE_KC[PG_N] = {KC_HOME, KC_SEQ, KC_SEQ, KC_EDIT, KC_EDIT, KC_EDIT, KC_EDIT, KC_EDIT, KC_LFO, KC_LFO, KC_FX, KC_FX, KC_FX,
                                      KC_FX, KC_FX, KC_SCL, KC_ARP, KC_GLO, KC_SAVE, KC_REC, KC_HOME, KC_HOME, KC_SCL, KC_FX, KC_SAVE, KC_ENV, KC_HOME, KC_SEQ};
/* a button's pages, in the order its taps show them (a button not listed: none, or its own logic) */
typedef struct { uint8_t btn, n, pg[6]; } group_t;
static const group_t GROUPS[] = {
    {B_SEQ, 3, {PG_SEQ, PG_LIVE, PG_TRK}}, {B_EDIT, 5, {PG_VOICE, PG_VOICE2, PG_SAMPLE, PG_SAMPLE2, PG_SND}},
    {B_ENV, 1, {PG_ENV}}, {B_LFO, 2, {PG_LFO1, PG_LFO2}}, {B_FX, 6, {PG_FXS, PG_FXD, PG_FXDL, PG_FXDL2, PG_FXR, PG_MFX}},
    {B_SCL, 2, {PG_PAT, PG_SONG}}, {B_SAVE, 2, {PG_SAVE, PG_NEW}},
};

/* ----------------------------------------------------------- parameters --- */
enum {
    P_NONE, P_BPM, P_SWING, P_TUNE, P_KDEC, P_CLICK, P_MIDICH, P_THEME, P_VERSION,
    P_LEN, P_SVEL, P_ROT, P_EUCLID, P_PLEN, P_COPY, P_SACC, P_LVEL,
    P_V0, P_V1, P_V2, P_V3, P_V4, P_V5, P_V6, P_V7,
    P_SMP, P_SLVL, P_SPITCH, P_SDEC, P_SYN, P_SSTART, P_STONE, P_SBEND,
    P_L1DEST, P_L1WAVE, P_L1RATE, P_L1DEPTH, P_L2DEST, P_L2WAVE, P_L2RATE, P_L2DEPTH,
    P_LEVEL, P_PAN, P_DLY, P_REV, P_DRV,
    P_DMODE, P_DAMT, P_DTONE, P_DLVL, P_DLMODE, P_DTIME, P_DTIME2, P_DFB, P_DLTONE, P_DLLVL, P_RSIZE, P_RDAMP, P_RLVL,
    P_PAT, P_CHAIN, P_PCOPY, P_PPASTE,
    P_NRRATE, P_NRVEL, P_NRVMODE, P_NRLATCH, P_NRHUMAN,
    P_MIX0, P_MIX1, P_MIX2, P_MIX3, P_MPAN0, P_MPAN1, P_MPAN2, P_MPAN3,
    P_SLOT, P_SWHAT, P_LWHAT, P_LOAD, P_QGRID, P_QROUND,
    P_EVEL, P_EPROB, P_EPAN, P_ESUB,
    /* (LXP7; the CC map stores these numbers: new ones only ever at the end) */
    P_SPEED, P_EMICRO, P_MFLT, P_MRES, P_CLKSRC, P_CLKOUT, P_OCH, P_ONOTE, P_LEARN,
    P_SROW, P_SPAT, P_SREP, P_SMODE, P_SLEN, P_SEND,
    P_NLEN, P_NSPD, P_NBPM, P_NGO, P_NKIT,
    P_EATK, P_EREL, P_EGATE, P_RPRE, P_LSEL, P_LDEL,
    P_TSEL, P_TMUTE, P_TSOLO, P_TCLR, P_TALL, P_PCLR,
    P_COUNT
};
enum { FM_NUM, FM_PCT, FM_BIP, FM_NAME, FM_STEPS, FM_PAN, FM_X100, FM_BPM, FM_SMP, FM_PAT, FM_LOAD, FM_TONE, FM_PROB,
       FM_SPAN, FM_SUB, FM_MICRO, FM_FILT, FM_NOTE, FM_CH, FM_ATK, FM_TIME, FM_GATE, FM_PRE };
typedef struct { const char *label; uint8_t fmt; int16_t min, max; const char *const *names; } pdesc_t;
static const char *const N_OFFON[] = {"OFF", "ON"};
static const char *const N_LDEST[] = {"OFF", "PITCH", "FILTER", "DECAY", "LEVEL", "PAN", "SAMPLE"};
static const char *const N_LWAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "S&H"};
static const char *const N_LRATE[] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const char *const N_DLYT[] = {"1/8", "3/16", "1/4", "3/8", "1/2", "3/4"};
static const char *const N_DMODE[] = {"SOFT", "HARD", "FOLD"};
static const char *const N_DLMODE[] = {"MONO", "STEREO", "PINGPONG"};
static const char *const N_VMODE[] = {"FIXED", "RAMP UP", "RAMP DN", "RANDOM"};
static const char *const N_SWHAT[] = {"KIT+PAT", "KIT", "PATTERN"};
static const char *const N_QGRID[] = {"1/16", "1/8", "1/4"};
static const char *const N_QROUND[] = {"NEAR", "BEFORE"};
static const char *const N_CLICK[] = {"OFF", "REC", "ON"};
static const char *const N_MIDICH[] = {"ALL", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                       "15", "16"};
static const char *const N_WAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "NOISE"};
static const char *const N_VERSION[] = {"LUXR " OC_VERSION};
static const char *const N_SPEED[] = {"1/8X", "1/4X", "1/2X", "3/4X", "1X", "3/2X", "2X"};
static const char *const N_CLKSRC[] = {"INT", "EXT"};
static const char *const N_LEARN[] = {"OFF", "ON", "CLEAR"};
static const char *const N_SEND[] = {"LOOP", "STOP"};
static const char *const N_NKIT[] = {"FACTORY", "KEEP"};
static const char *N_THEME[UI_NPALETTES];
#define NSLOT 36u                                /* (8 KiB each; LAYOUT 2 had 14 of 20 KiB) */
#define NMAP_UI 32                               /* (= NMAP: the CC map's entries) */
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
    [P_SSTART] = {"START", FM_NUM, 0, 127, 0},
    [P_STONE] = {"TONE", FM_TONE, -64, 63, 0},
    [P_SBEND] = {"BEND", FM_BIP, -64, 63, 0},
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
    [P_DLMODE] = {"MODE", FM_NAME, 0, DL_N - 1, N_DLMODE},
    [P_DTIME] = {"TIME", FM_NAME, 0, 5, N_DLYT},
    [P_DTIME2] = {"TIME R", FM_NAME, 0, 5, N_DLYT},
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
    [P_LWHAT] = {"LOADS", FM_NAME, 0, 2, N_SWHAT},
    [P_LOAD] = {"LOAD", FM_LOAD, 0, NSLOT, 0},
    [P_QGRID] = {"GRID", FM_NAME, 0, Q_N - 1, N_QGRID},
    [P_QROUND] = {"TO", FM_NAME, 0, 1, N_QROUND},
    [P_EVEL] = {"VELO", FM_NUM, 1, 127, 0},
    [P_EPROB] = {"PROB", FM_PROB, 1, C_N, 0},
    [P_EPAN] = {"PAN", FM_SPAN, 0, 7, 0},
    [P_ESUB] = {"SUBSTEP", FM_SUB, 1, 6, 0},
    [P_SPEED] = {"SPEED", FM_NAME, SPD_MIN, SPD_MAX, N_SPEED},
    [P_EMICRO] = {"NUDGE", FM_MICRO, -15, 15, 0},
    [P_MFLT] = {"FILTER", FM_FILT, -64, 63, 0},
    [P_MRES] = {"RESO", FM_NUM, 0, 127, 0},
    [P_CLKSRC] = {"CLOCK", FM_NAME, 0, CK_N - 1, N_CLKSRC},
    [P_CLKOUT] = {"CLK OUT", FM_NAME, 0, 1, N_OFFON},
    [P_OCH] = {"OUT CH", FM_CH, 0, 16, 0},
    [P_ONOTE] = {"NOTE", FM_NOTE, 0, 127, 0},
    [P_LEARN] = {"LEARN", FM_NAME, 0, 2, N_LEARN},
    [P_SROW] = {"ROW", FM_NUM, 1, NSONG, 0},
    [P_SPAT] = {"PATTERN", FM_NUM, 1, NPAT, 0},
    [P_SREP] = {"REPEAT", FM_NUM, 1, 16, 0},
    [P_SMODE] = {"SONG", FM_NAME, 0, 1, N_OFFON},
    [P_SLEN] = {"ROWS", FM_NUM, 1, NSONG, 0},
    [P_SEND] = {"AT END", FM_NAME, 0, 1, N_SEND},
    [P_NLEN] = {"LENGTH", FM_STEPS, 1, NSTEP, 0},
    [P_NSPD] = {"SPEED", FM_NAME, SPD_MIN, SPD_MAX, N_SPEED},
    [P_NBPM] = {"TEMPO", FM_BPM, 60, 200, 0},
    [P_NGO] = {"CREATE", FM_LOAD, 0, 2, 0},
    [P_NKIT] = {"KIT", FM_NAME, 0, 1, N_NKIT},
    [P_EATK] = {"ATTACK", FM_ATK, 0, 127, 0},
    [P_EREL] = {"RELEASE", FM_TIME, 0, 127, 0},
    [P_EGATE] = {"GATE", FM_GATE, 0, 64, 0},
    [P_RPRE] = {"PRE-DLY", FM_PRE, 0, 127, 0},
    [P_LSEL] = {"ENTRY", FM_NUM, 1, NMAP_UI, 0},
    [P_LDEL] = {"DELETE", FM_LOAD, 0, 2, 0},
    [P_TSEL] = {"TRACK", FM_NAME, 0, NTRK - 1, TRK_NAME},
    [P_TMUTE] = {"MUTE", FM_NAME, 0, 1, N_OFFON},
    [P_TSOLO] = {"SOLO", FM_NAME, 0, 1, N_OFFON},
    [P_TCLR] = {"CLEAR", FM_LOAD, 0, 2, 0},
    [P_TALL] = {"ALL ON", FM_LOAD, 0, 2, 0},
    [P_PCLR] = {"CLR PAT", FM_LOAD, 0, 2, 0},
};
/* the four cards of each page: row A, row B (SHIFT) */
static const uint8_t PAGE_P[PG_N][8] = {
    [PG_KIT] = {P_BPM, P_SWING, P_TUNE, P_KDEC, P_MIDICH, P_THEME, P_VERSION, 0},
    [PG_SEQ] = {P_LEN, P_SVEL, P_ROT, P_EUCLID, P_PLEN, P_COPY, P_SACC, P_SPEED},
    [PG_LIVE] = {P_LVEL, P_QGRID, P_QROUND, P_CLICK, 0, 0, 0, 0},
    [PG_VOICE] = {P_V0, P_V1, P_V2, P_V3, P_V4, P_V5, P_V6, P_V7},
    [PG_VOICE2] = {P_V4, P_V5, P_V6, P_V7, P_V0, P_V1, P_V2, P_V3},
    [PG_SAMPLE] = {P_SMP, P_SLVL, P_SPITCH, P_SDEC, P_SYN, 0, 0, 0},
    [PG_SAMPLE2] = {P_SSTART, P_STONE, P_SBEND, P_SYN, 0, 0, 0, 0},
    [PG_SND] = {P_LEVEL, P_PAN, P_DLY, P_REV, P_DRV, 0, 0, 0},
    [PG_LFO1] = {P_L1DEST, P_L1WAVE, P_L1RATE, P_L1DEPTH, 0, 0, 0, 0},
    [PG_LFO2] = {P_L2DEST, P_L2WAVE, P_L2RATE, P_L2DEPTH, 0, 0, 0, 0},
    [PG_FXS] = {P_DRV, P_DLY, P_REV, P_LEVEL, 0, 0, 0, 0},
    [PG_FXD] = {P_DMODE, P_DAMT, P_DTONE, P_DLVL, 0, 0, 0, 0},
    [PG_FXDL] = {P_DLMODE, P_DTIME, P_DTIME2, P_DFB, 0, 0, 0, 0},
    [PG_FXDL2] = {P_DLTONE, P_DLLVL, 0, 0, 0, 0, 0, 0},
    [PG_FXR] = {P_RSIZE, P_RDAMP, P_RLVL, P_RPRE, 0, 0, 0, 0},
    [PG_PAT] = {P_PAT, P_CHAIN, P_PCOPY, P_PPASTE, 0, 0, 0, 0},
    [PG_NR] = {P_NRRATE, P_NRVEL, P_NRVMODE, P_NRLATCH, P_NRHUMAN, 0, 0, 0},
    [PG_MIX] = {P_MIX0, P_MIX1, P_MIX2, P_MIX3, P_MPAN0, P_MPAN1, P_MPAN2, P_MPAN3},
    [PG_SAVE] = {P_SLOT, P_SWHAT, P_LWHAT, P_LOAD, 0, 0, 0, 0},
    [PG_REC] = {P_QGRID, P_QROUND, P_CLICK, 0, 0, 0, 0, 0},
    [PG_MIDI] = {P_CLKSRC, P_CLKOUT, P_OCH, P_ONOTE, P_MIDICH, 0, 0, 0},
    [PG_LEARN] = {P_LEARN, P_LSEL, P_LDEL, 0, 0, 0, 0, 0},
    [PG_SONG] = {P_SROW, P_SPAT, P_SREP, P_SMODE, P_SLEN, P_SEND, 0, 0},
    [PG_MFX] = {P_MFLT, P_MRES, 0, 0, 0, 0, 0, 0},
    [PG_NEW] = {P_NLEN, P_NSPD, P_NBPM, P_NGO, P_NKIT, 0, 0, 0},
    [PG_ENV] = {P_EATK, P_V1, P_EREL, P_EGATE, 0, 0, 0, 0},
    [PG_TRK] = {P_TSEL, P_TMUTE, P_TSOLO, P_TCLR, 0, P_TALL, 0, P_PCLR},
};

static struct {
    uint8_t page, back, hot, uboot, force, shift, bar, bank, cur, acc_mode, follow, slot, swhat, lwhat, patch,
        loaded;                                  /* the patch loaded (or saved) last: its slot + 1, 0: none */
    uint8_t glo_used, arp_from, rec_long, copy, lvel, eu[NTRK];
    uint8_t mixpan;                              /* the MIXER's page 2: KNOB 1..4 the pans */
    uint16_t wheld, wkeep, sedit;                /* SEQ: white keys held, kept when let go, held long (STEP EDIT) */
    uint32_t wdown[16];
    int8_t rot;
    uint32_t hot_t, msg_t, both_t, burst_t, burst_tag, glo_t0, arp_t0, rec_t0, home_t0, tap_t, tap_n, tap_sum;
    uint8_t home_long;
    uint8_t lockmode;                            /* LM_*: the white keys are steps on every page (P-LOCK, NUDGE) */
    uint8_t learn, lpid, ltrk;                   /* CC LEARN: 1 on (a knob turned arms it), 2 armed: the next CC maps */
    uint8_t scur;                                /* SONG: the row shown */
    uint8_t nlen, nbpm, nkit;                    /* NEW PATCH: its length, tempo, kit (factory / kept) */
    uint8_t pkey;                                /* PATTERNS: the pattern key held + 1 (a second key: a chain) */
    uint8_t lsel;                                /* CC LEARN: the entry shown (0 ..) */
    uint8_t sfresh;                              /* SONG: the next key starts a new song (on entering the page) */
    int8_t nspd;                                 /* .. its tracks' speed */
    uint8_t restoring, rs_slot, rs_done;         /* patches coming in over USB (the update page): the loading screen */
    uint32_t rs_off, rs_t;
    char msg[28];
    uint32_t sig[8];                             /* the areas: 0 header, 1..4 cards, 5 graph, 6 footer, 7 uboot */
    uint8_t logo;                                /* the SYSTEM page's logo is on the screen (header and cards are not) */
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
typedef struct { pattern_t p; lock_t lk[NLOCK]; sound_t s[NTRK]; uint8_t pat; } snap_t;
static struct { snap_t s[UNDO_N]; uint32_t cur, n_undo, n_redo; } undo __attribute__((section(".pool")));
static void snap_take(snap_t *s)
{
    memcpy(&s->p, PAT, sizeof s->p);
    memcpy(s->lk, plk[song.pat % NPAT], sizeof s->lk);
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
    static snap_t now __attribute__((section(".pool")));
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
        memcpy(plk[s->pat % NPAT], s->lk, sizeof s->lk);
        memcpy(snd, s->s, sizeof snd);
        *s = now;
    }
    ui.burst_tag = 0;
    ui_say(redo ? "REDO" : "UNDO");
}

static void fmt_u(char *b, uint32_t v);
/* --------------------------------------------------------------- store --- */
/* the store (OC_STORE_*, the area ONE CLICK and Felucca use too): settings at 0, fourteen patches of 20 KiB after it.
 * A patch is a kit (the eight sounds, tune, decay, the global FX) and its patterns (16 with their locks, tempo, swing,
 * chain, mute, solo, the song); SAVE writes both or one of them (the other as the slot has it), PRESETS loads both or
 * (SHIFT) the patterns.
 * LXP7 keeps the patterns packed (a pattern: its steps one by one, or all of them when that is shorter) so the locks,
 * the timing and the speeds fit in the slot; a patch past the slot's room is not saved ("PATCH TOO FULL").
 * The formats before are read too: LXP4..6 as they are in their slots (converted when loaded, written as LXP7 when
 * saved again); LXP1..3 (eight slots of 12 KiB) moved to this layout at the first boot (layout 2 in the settings); any
 * of them taken by a restore over USB. */
#define ST_SET 0x0000u
#define SLOT_SZ 0x2000u
#define ST_PROJ(i) (0x1000u + SLOT_SZ * (uint32_t)(i))
#define ST_L2(i) (0x1000u + 0x5000u * (uint32_t)(i))   /* (LAYOUT 2: 14 slots of 20 KiB) */
#define NSLOT_L2 14u
#define ST_END 0x49000u
#define ST_OLD(i) (0x1000u + 0x3000u * (uint32_t)(i))   /* (LXP1..3) */
#define NSLOT_OLD 8u
#define MAGIC_SET 0x3354584Cu                    /* "LXT3": + the MIDI clock, the CC map */
#define MAGIC_SET2 0x3254584Cu                   /* "LXT2" */
#define MAGIC_SET1 0x3154584Cu                   /* "LXT1" (LUXR's first) */
#define MAGIC_PROJ 0x4150584Cu                   /* "LXPA": LXP9 in a slot of 8 KiB */
#define MAGIC_P9 0x3950584Cu                     /* "LXP9": the reverb's pre-delay, and room in the kit for more (20 KiB) */
#define MAGIC_P8 0x3850584Cu                     /* (the sounds' envelope: attack, release, gate) */
#define MAGIC_P7 0x3750584Cu                     /* (packed patterns, the locks, NUDGE, the speeds, the song, MIDI OUT) */
#define MAGIC_P6 0x3650584Cu                     /* (the conditions A:B, the delay's modes) */
#define MAGIC_P5 0x3550584Cu                     /* (the substeps; probability and pan in nibbles; one delay time) */
#define MAGIC_P4 0x3450584Cu                     /* (the extras, no substeps) */
#define KIT_OLD 253u                             /* (the kit of LXP3..5: no delay mode, no second time) */
#define MAGIC_P1 0x3150584Cu
#define MAGIC_P2 0x3250584Cu
#define MAGIC_P3 0x3350584Cu
#define LAYOUT 3u                                /* 3: 36 slots of 8 KiB (2: 14 of 20 KiB, 1: 8 of 12 KiB) */
#define NMAP 32u
enum { SW_ALL, SW_KIT, SW_PAT };
typedef struct { uint8_t ch, cc, pid, trk; } ccmap_t;   /* a controller (channel 0..15, number) onto a parameter (of a sound) */
typedef struct {
    uint32_t magic;
    uint8_t theme, midi_ch, click, slot, qgrid, qround, layout, pad;
    uint8_t clk_src, clk_out, pad2[2];           /* (LXT3) */
    ccmap_t map[NMAP];
} set_rec_t;
static ccmap_t ccmap[NMAP];
static uint32_t cc_count(void)                   /* the entries in use (kept together from the start) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NMAP; i++)
        n += ccmap[i].pid != 0u;
    return n;
}
static void cc_pack(void)                        /* the entries in use to the start, in their order */
{
    uint32_t i, o = 0;
    for (i = 0; i < NMAP; i++)
        if (ccmap[i].pid)
            ccmap[o++] = ccmap[i];
    for (; o < NMAP; o++)
        memset(&ccmap[o], 0, sizeof ccmap[o]);
}
/* the formats before LXP7, as they were laid out (read only) */
typedef struct {                                 /* a sound of LXP4..6 */
    uint8_t p[8], smp, slvl, spitch, sdec, sstart;
    int8_t stone, sbend;
    uint8_t syn, level, dly, rev, drv;
    int8_t pan;
    lfo_t lfo[2];
    uint8_t preset;
} sound6_t;
typedef struct {
    sound6_t s[NTRK];
    int8_t tune;
    uint8_t ddec, drv_mode, drv_amt, drv_tone, drv_lvl, dly_div, dly_fb, dly_tone, dly_lvl, rev_size, rev_damp, rev_lvl;
    uint8_t dly_mode, dly_div2;                  /* (LXP6) */
} kit6_t;
typedef struct {                                 /* a pattern as LXP5..6 stored it: the substeps 3 bits a step */
    uint8_t s[NTRK][NSTEP], x[NTRK][NSTEP], r3[NTRK * NSTEP * 3u / 8u], len[NTRK];
} pat_st_t;
typedef struct { pat_st_t p[NPAT]; uint8_t bpm, swing, chain, pat, mute, solo; } pat_rec6_t;
typedef struct { uint32_t magic, size, sum; kit6_t k; pat_rec6_t p; } proj6_t;
typedef char kit6_fits[sizeof(sound6_t) == 30u && sizeof(kit6_t) == KIT_OLD + 2u ? 1 : -1];
typedef struct {                                 /* a sound of LXP7 */
    sound6_t s6;
    uint8_t och, onote;
} sound7_t;
typedef struct {
    sound7_t s[NTRK];
    int8_t tune;
    uint8_t ddec, drv_mode, drv_amt, drv_tone, drv_lvl, dly_div, dly_fb, dly_tone, dly_lvl, rev_size, rev_damp, rev_lvl;
    uint8_t dly_mode, dly_div2;
} kit7_t;
#define PBLOB7 (0x5000u - 16u - (uint32_t)sizeof(kit7_t))
typedef char kit7_fits[sizeof(sound7_t) == 32u && sizeof(kit7_t) == 271u ? 1 : -1];
typedef struct {                                 /* the kit of LXP8 */
    sound_t s[NTRK];
    int8_t tune;
    uint8_t ddec, drv_mode, drv_amt, drv_tone, drv_lvl, dly_div, dly_fb, dly_tone, dly_lvl, rev_size, rev_damp, rev_lvl;
    uint8_t dly_mode, dly_div2;
} kit8_t;
#define PBLOB8 (0x5000u - 16u - (uint32_t)sizeof(kit8_t))
/* LXP9 */
typedef struct {                                 /* the kit: LXP8's (the same bytes first), the pre-delay, room for more */
    sound_t s[NTRK];
    int8_t tune;
    uint8_t ddec, drv_mode, drv_amt, drv_tone, drv_lvl, dly_div, dly_fb, dly_tone, dly_lvl, rev_size, rev_damp, rev_lvl;
    uint8_t dly_mode, dly_div2;
    uint8_t rev_pre;
    uint8_t rsv[15];                             /* (zero) */
} kit_rec_t;
typedef char kit9_fits[sizeof(kit_rec_t) == sizeof(kit8_t) + 16u ? 1 : -1];
#define PBLOB (SLOT_SZ - 16u - (uint32_t)sizeof(kit_rec_t))
#define PBLOB9 (0x5000u - 16u - (uint32_t)sizeof(kit_rec_t))
typedef struct { uint32_t magic, size, sum; uint16_t blen, pad; kit_rec_t k; uint8_t blob[PBLOB]; } proj_t;
typedef char proj_fits[sizeof(proj_t) == SLOT_SZ && ST_PROJ(NSLOT) <= ST_END && ST_L2(NSLOT_L2) <= ST_END ? 1 : -1];
static proj_t proj_buf __attribute__((section(".pool")));
static uint8_t stage[0x5000] __attribute__((section(".pool"), aligned(4)));   /* a patch as stored (any format): USB, moving */
static pattern_t cv_pat __attribute__((section(".pool")));               /* a pattern on its way (converting, loading) */
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
static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static uint32_t proj_sum(const proj_t *r)        /* the blob's length, the kit and the blob */
{
    return fnv((const uint8_t *)&r->blen, (uint32_t)((const uint8_t *)r->blob - (const uint8_t *)&r->blen) + (r->blen <= PBLOB ? r->blen : 0u));
}
static void settings_save(void)
{
    static set_rec_t st;
    memset(&st, 0, sizeof st);
    st.magic = MAGIC_SET;
    st.theme = song.theme; st.midi_ch = song.midi_ch; st.click = song.click; st.slot = ui.slot;
    st.qgrid = song.qgrid; st.qround = song.qround; st.layout = LAYOUT;
    st.clk_src = song.clk_src; st.clk_out = song.clk_out;
    memcpy(st.map, ccmap, sizeof st.map);
    store_write(ST_SET, &st, sizeof st);
}
static int proj_convert(const uint8_t *raw, uint32_t n);
static int proj_read_at(uint32_t off)           /* into proj_buf (from any format); 0 = a patch is there */
{
    uint32_t n = ST_END - off < sizeof stage ? ST_END - off : (uint32_t)sizeof stage;
    return io_store_read(off, stage, n) || proj_convert(stage, n);
}
static int proj_read(uint32_t slot) { return proj_read_at(ST_PROJ(slot)); }
static int slot_magic(uint32_t m) { return m == MAGIC_PROJ || m == MAGIC_P9 || m == MAGIC_P8 || m == MAGIC_P7 || m == MAGIC_P6 || m == MAGIC_P5 || m == MAGIC_P4; }
static uint32_t rd32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* ---- the blob: the patterns packed, then the rest of them
 * a pattern: mode (0 its steps one by one, 1 all of them with the substeps in 3 bits, 2 all of them), len[8], spd[8];
 *            mode 0: the count (2) and each step: t << 6 | step (2), s, x, r; mode 1: s[512] x[512] r3[192];
 *            mode 2: s[512] x[512] r[512]; then its locks: the count (1) and each: key (2), pid, value
 * the rest:  bpm swing chain pat mute solo, SONG on, rows, at its end, the rows (pattern, repeats) */
static struct { uint8_t *b; uint32_t n, cap, over; } bw;
static void bw_put(uint32_t v)
{
    if (bw.n < bw.cap)
        bw.b[bw.n++] = (uint8_t)v;
    else
        bw.over = 1;
}
static void bw_put16(uint32_t v) { bw_put(v & 0xFFu); bw_put(v >> 8); }
static int r3_ok(const pattern_t *p)             /* the substeps alone, 0..5 (mode 1 keeps 3 bits a step) */
{
    uint32_t k;
    for (k = 0; k < NTRK * NSTEP; k++)
        if ((&p->r[0][0])[k] > 5u)
            return 0;
    return 1;
}
static uint32_t trk_ext(const pattern_t *p, uint32_t t)   /* the track's last step set + 1 (0: none) */
{
    uint32_t i = NSTEP;
    while (i && !p->s[t][i - 1u])
        i--;
    return i;
}
static void enc_bits(const pattern_t *p, uint32_t t, uint32_t ext, int extras)   /* a bit a step to ext: set (or set with extras) */
{
    uint32_t i, k;
    for (i = 0; i < ext; i += 8u) {
        uint32_t v = 0;
        for (k = 0; k < 8u && i + k < ext; k++)
            if (p->s[t][i + k] && (!extras || p->x[t][i + k] || p->r[t][i + k]))
                v |= 1u << k;
        bw_put(v);
    }
}
static void enc_pattern(const pattern_t *p, const lock_t *lk)
{
    uint32_t t, i, k, n = 0, nl = 0, sp, d3 = r3_ok(p), cp = NTRK, dn = d3 ? 1216u : 1536u, mode;
    for (k = 0; k < NTRK * NSTEP; k++)
        n += (&p->s[0][0])[k] != 0u;
    sp = 2u + 5u * n;
    for (t = 0; t < NTRK; t++) {                 /* mode 3 (LXPA): a track to its last step: bits of the steps set, their
                                                  * values, bits of those with extras, the extras (the shortest, mostly) */
        uint32_t ext = trk_ext(p, t);
        cp += 2u * ((ext + 7u) / 8u);
        for (i = 0; i < ext; i++)
            if (p->s[t][i])
                cp += 1u + (p->x[t][i] || p->r[t][i] ? 2u : 0u);
    }
    mode = cp <= sp && cp <= dn ? 3u : sp <= dn ? 0u : d3 ? 1u : 2u;
    bw_put(mode);
    for (t = 0; t < NTRK; t++)
        bw_put(p->len[t]);
    for (t = 0; t < NTRK; t++)
        bw_put((uint8_t)p->spd[t]);
    if (mode == 3u) {
        for (t = 0; t < NTRK; t++) {
            uint32_t ext = trk_ext(p, t);
            bw_put(ext);
            enc_bits(p, t, ext, 0);
            for (i = 0; i < ext; i++)
                if (p->s[t][i])
                    bw_put(p->s[t][i]);
            enc_bits(p, t, ext, 1);
            for (i = 0; i < ext; i++)
                if (p->s[t][i] && (p->x[t][i] || p->r[t][i])) {
                    bw_put(p->x[t][i]);
                    bw_put(p->r[t][i]);
                }
        }
    } else if (mode == 0u) {
        bw_put16(n);
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < NSTEP; i++)
                if (p->s[t][i]) {
                    bw_put16(t << 6 | i);
                    bw_put(p->s[t][i]);
                    bw_put(p->x[t][i]);
                    bw_put(p->r[t][i]);
                }
    } else {
        for (k = 0; k < NTRK * NSTEP; k++)
            bw_put((&p->s[0][0])[k]);
        for (k = 0; k < NTRK * NSTEP; k++)
            bw_put((&p->x[0][0])[k]);
        if (d3) {                                /* (3 bits a step, little end first) */
            uint8_t r3[NTRK * NSTEP * 3u / 8u];
            memset(r3, 0, sizeof r3);
            for (k = 0; k < NTRK * NSTEP; k++) {
                uint32_t v = (&p->r[0][0])[k] & 7u, b = 3u * k;
                r3[b >> 3] |= (uint8_t)(v << (b & 7u));
                if ((b & 7u) > 5u)
                    r3[(b >> 3) + 1u] |= (uint8_t)(v >> (8u - (b & 7u)));
            }
            for (k = 0; k < sizeof r3; k++)
                bw_put(r3[k]);
        } else {
            for (k = 0; k < NTRK * NSTEP; k++)
                bw_put((&p->r[0][0])[k]);
        }
    }
    for (i = 0; lk && i < NLOCK; i++)
        nl += lk[i].pid != 0u;
    bw_put(nl);
    for (i = 0; lk && i < NLOCK; i++)
        if (lk[i].pid) {
            bw_put16(lk[i].key);
            bw_put(lk[i].pid);
            bw_put(lk[i].val);
        }
}
typedef struct { uint8_t bpm, swing, chain, pat, mute, solo; } ptail_t;
static void enc_tail(const ptail_t *g, uint32_t smode, uint32_t slen, uint32_t send, const srow_t *rows)
{
    uint32_t i;
    bw_put(g->bpm); bw_put(g->swing); bw_put(g->chain); bw_put(g->pat); bw_put(g->mute); bw_put(g->solo);
    bw_put(smode); bw_put(slen); bw_put(send);
    for (i = 0; i < slen && i < NSONG; i++) {
        bw_put(rows[i].pat);
        bw_put(rows[i].rep);
    }
}
static void enc_begin(proj_t *r)
{
    bw.b = r->blob;
    bw.n = 0;
    bw.cap = PBLOB;
    bw.over = 0;
}
static int enc_end(proj_t *r)                    /* 0 = it fits */
{
    r->magic = MAGIC_PROJ;
    r->size = sizeof *r;
    r->blen = (uint16_t)(bw.over ? 0u : bw.n);
    r->pad = 0;
    r->sum = proj_sum(r);
    return bw.over ? -1 : 0;
}
/* the blob read back: apply 0 only checks it; 1 into the patterns, their locks, the song (0 = ok) */
static int dec_blob_raw(const uint8_t *b, uint32_t n, int apply)
{
    uint32_t o = 0, i, t, k, m, cnt;
#define NEED(c) do { if (o + (c) > n) return -1; } while (0)
    for (i = 0; i < NPAT; i++) {
        pattern_t *p = &cv_pat;
        NEED(17u);
        m = b[o++];
        memset(p, 0, sizeof *p);
        for (t = 0; t < NTRK; t++)
            p->len[t] = (uint8_t)clamp(b[o++], 1, NSTEP);
        for (t = 0; t < NTRK; t++)
            p->spd[t] = (int8_t)clamp((int8_t)b[o++], SPD_MIN, SPD_MAX);
        if (m == 3u) {
            for (t = 0; t < NTRK; t++) {
                uint32_t ext, nb, on, ex;
                NEED(1u);
                ext = b[o++];
                if (ext > NSTEP)
                    return -1;
                nb = (ext + 7u) / 8u;
                NEED(nb);
                on = o;
                o += nb;
                for (k = 0; k < ext; k++)
                    if ((b[on + k / 8u] >> (k % 8u)) & 1u) {
                        NEED(1u);
                        p->s[t][k] = b[o++];
                    }
                NEED(nb);
                ex = o;
                o += nb;
                for (k = 0; k < ext; k++)
                    if ((b[ex + k / 8u] >> (k % 8u)) & 1u) {
                        NEED(2u);
                        p->x[t][k] = b[o];
                        p->r[t][k] = b[o + 1u];
                        o += 2u;
                    }
            }
        } else if (m == 0u) {
            NEED(2u);
            cnt = b[o] | (uint32_t)b[o + 1] << 8;
            o += 2u;
            NEED(5u * cnt);
            for (k = 0; k < cnt; k++, o += 5u) {
                uint32_t key = (b[o] | (uint32_t)b[o + 1] << 8) & 0x1FFu;
                p->s[key >> 6][key & 63u] = b[o + 2];
                p->x[key >> 6][key & 63u] = b[o + 3];
                p->r[key >> 6][key & 63u] = b[o + 4];
            }
        } else if (m == 1u || m == 2u) {
            NEED(1024u + (m == 1u ? 192u : 512u));
            memcpy(p->s, b + o, NTRK * NSTEP);
            memcpy(p->x, b + o + NTRK * NSTEP, NTRK * NSTEP);
            o += 1024u;
            if (m == 1u) {
                for (k = 0; k < NTRK * NSTEP; k++) {
                    uint32_t bb = 3u * k, v = (uint32_t)b[o + (bb >> 3)] >> (bb & 7u);
                    if ((bb & 7u) > 5u)
                        v |= (uint32_t)b[o + (bb >> 3) + 1u] << (8u - (bb & 7u));
                    (&p->r[0][0])[k] = (uint8_t)((v & 7u) <= 5u ? v & 7u : 0u);
                }
                o += 192u;
            } else {
                memcpy(p->r, b + o, NTRK * NSTEP);
                o += 512u;
            }
        } else {
            return -1;
        }
        NEED(1u);
        cnt = b[o++];
        if (cnt > NLOCK)
            return -1;
        NEED(4u * cnt);
        if (apply) {
            memcpy(&pat[i], p, sizeof *p);
            memset(plk[i], 0, sizeof plk[i]);
        }
        for (k = 0; k < cnt; k++, o += 4u)
            if (apply) {
                plk[i][k].key = (uint16_t)((b[o] | (uint32_t)b[o + 1] << 8) & 0x1FFu);
                plk[i][k].val = b[o + 3];
                plk[i][k].pid = b[o + 2];
            }
    }
    NEED(9u);
    if (apply) {
        song.bpm = (uint8_t)clamp(b[o], 60, 200);
        song.swing = (uint8_t)clamp(b[o + 1], 0, 50);
        song.chain = (uint8_t)clamp(b[o + 2], 1, NPAT);
        song.mute = b[o + 4];
        song.solo = b[o + 5];
        if (!song.playing)
            song.pat = song.chain0 = b[o + 3] % NPAT;
        else
            song.pat_next = b[o + 3] % NPAT;     /* (playing: at the end of the pattern) */
        song.smode = b[o + 6] & 1u;
        song.send = b[o + 8] & 1u;
    }
    m = b[o + 7];
    o += 9u;
    if (m > NSONG)
        return -1;
    NEED(2u * m);
    if (apply) {
        song.slen = (uint8_t)(m ? m : 1u);
        memset(srows, 0, sizeof srows);
        for (k = 0; k < m; k++) {
            srows[k].pat = b[o + 2u * k] % NPAT;
            srows[k].rep = (uint8_t)clamp(b[o + 2u * k + 1u], 1, 16);
        }
        if (!m)
            srows[0].rep = 1;
        if (song.srow >= song.slen)
            song.srow = 0;
    }
#undef NEED
    return 0;
}
static int dec_blob(const proj_t *r, int apply)
{
    return r->blen > PBLOB ? -1 : dec_blob_raw(r->blob, r->blen, apply);
}
static int pat_take(proj_t *r);
static int blob_into(proj_t *r, const uint8_t *b, uint32_t bl)   /* a blob of a format before into r: as it is when it
                                                  * fits, else read into the patterns and packed again (0 = ok) */
{
    if (bl <= PBLOB) {
        memcpy(r->blob, b, bl);
        r->blen = (uint16_t)bl;
        r->pad = 0;
        r->magic = MAGIC_PROJ;
        r->size = sizeof *r;
        r->sum = proj_sum(r);
        return dec_blob(r, 0) ? -2 : 0;
    }
    if (dec_blob_raw(b, bl, 0) || dec_blob_raw(b, bl, 1))
        return -2;
    return pat_take(r) ? -3 : 0;
}

/* ---- the formats before, into LXP7 (proj_buf) */
static void sound_from6(sound_t *d, const sound6_t *o, uint32_t t)
{
    memset(d, 0, sizeof *d);
    memcpy(d->p, o->p, 8);
    d->smp = o->smp; d->slvl = o->slvl; d->spitch = o->spitch; d->sdec = o->sdec; d->sstart = o->sstart;
    d->stone = o->stone; d->sbend = o->sbend; d->syn = o->syn; d->level = o->level; d->dly = o->dly; d->rev = o->rev;
    d->drv = o->drv; d->pan = o->pan;
    memcpy(d->lfo, o->lfo, sizeof d->lfo);
    d->preset = o->preset;
    d->och = 0;
    d->onote = GM_NOTE[t % NTRK];
    d->atk = 0;                                  /* (ENV as it was: the decay alone) */
    d->rel = 60;
    d->gate = 0;
}
static void kit_from6(kit_rec_t *d, const kit6_t *o)
{
    uint32_t t;
    memset(d, 0, sizeof *d);
    for (t = 0; t < NTRK; t++)
        sound_from6(&d->s[t], &o->s[t], t);
    d->tune = o->tune; d->ddec = o->ddec; d->drv_mode = o->drv_mode; d->drv_amt = o->drv_amt; d->drv_tone = o->drv_tone;
    d->drv_lvl = o->drv_lvl; d->dly_div = o->dly_div; d->dly_fb = o->dly_fb; d->dly_tone = o->dly_tone; d->dly_lvl = o->dly_lvl;
    d->rev_size = o->rev_size; d->rev_damp = o->rev_damp; d->rev_lvl = o->rev_lvl; d->dly_mode = o->dly_mode; d->dly_div2 = o->dly_div2;
}
static void cv_from_st(const uint8_t *st, uint32_t has_r3)   /* a pattern as pat_st_t (LXP5..6) laid it out, into cv_pat */
{
    const pat_st_t *q = (const pat_st_t *)st;    /* (bytes only: no alignment needed) */
    uint32_t k;
    memset(&cv_pat, 0, sizeof cv_pat);
    memcpy(cv_pat.s, q->s, sizeof cv_pat.s);
    memcpy(cv_pat.x, q->x, sizeof cv_pat.x);
    memcpy(cv_pat.len, q->len, sizeof cv_pat.len);
    for (k = 0; has_r3 && k < NTRK * NSTEP; k++) {
        uint32_t b = 3u * k, v = (uint32_t)q->r3[b >> 3] >> (b & 7u);
        if ((b & 7u) > 5u)
            v |= (uint32_t)q->r3[(b >> 3) + 1u] << (8u - (b & 7u));
        (&cv_pat.r[0][0])[k] = (uint8_t)((v & 7u) <= 5u ? v & 7u : 0u);
    }
}
static const srow_t SONG0[1] = {{0, 1}};
static void sound_from_old(sound6_t *d, const uint8_t *o, uint32_t ver)   /* LXP1 (22 B), LXP2 (27), LXP3 (30) */
{
    static const uint8_t LD1[5] = {LD_OFF, LD_PITCH, LD_FILTER, LD_LEVEL, LD_PAN};
    uint32_t k = 12;
    memset(d, 0, sizeof *d);
    memcpy(d->p, o, 8);
    d->smp = o[8] % TR_N; d->slvl = o[9]; d->spitch = o[10]; d->sdec = o[11];
    if (ver == 3u) { d->sstart = o[12]; d->stone = (int8_t)o[13]; d->sbend = (int8_t)o[14]; k = 15; }
    d->syn = o[k++]; d->level = o[k++]; d->dly = o[k++]; d->rev = o[k++];
    if (ver >= 2u) d->drv = o[k++];
    d->pan = (int8_t)o[k++];
    if (ver >= 2u) {
        memcpy(d->lfo, o + k, sizeof d->lfo);
        k += sizeof d->lfo;
    } else {                                     /* (one free LFO then: in time now, at half its depth) */
        d->lfo[0].dest = LD1[o[k + 2] % 5u];
        d->lfo[0].wave = o[k + 3] % LW_N;
        d->lfo[0].rate = LR_1BAR;
        d->lfo[0].depth = (int8_t)(o[k + 1] / 2u);
        d->lfo[1].rate = LR_1BAR;
        k += 4;
    }
    d->preset = o[k];
}
static int proj_convert(const uint8_t *raw, uint32_t n)
{
    proj_t *r = &proj_buf;
    static kit6_t k6;
    ptail_t g;
    uint32_t magic = n >= 12u ? rd32(raw) : 0, ver, ss, i, t, at;
    if (magic == MAGIC_PROJ) {
        if (n < sizeof *r)
            return -2;
        memcpy(r, raw, sizeof *r);
        return r->size == sizeof *r && r->blen <= PBLOB && r->sum == proj_sum(r) && !dec_blob(r, 0) ? 0 : -2;
    }
    if (magic == MAGIC_P9) {                     /* LXP9 (20 KiB): its kit, its blob as it is, if it fits 8 KiB */
        uint32_t bl = n >= 16u ? (uint32_t)(raw[12] | raw[13] << 8) : 0u;
        if (n < 16u + sizeof(kit_rec_t) + bl || rd32(raw + 4) != 0x5000u || bl > PBLOB9 ||
            rd32(raw + 8) != fnv(raw + 12, 4u + sizeof(kit_rec_t) + bl))
            return -2;
        memcpy(&r->k, raw + 16, sizeof(kit_rec_t));
        return blob_into(r, raw + 16u + sizeof(kit_rec_t), bl);
    }
    if (magic == MAGIC_P8) {                     /* LXP8: its kit (no pre-delay), its blob as it is */
        uint32_t bl = n >= 16u ? (uint32_t)(raw[12] | raw[13] << 8) : 0u;
        if (n < 16u + sizeof(kit8_t) + bl || rd32(raw + 4) != 0x5000u || bl > PBLOB8 ||
            rd32(raw + 8) != fnv(raw + 12, 4u + sizeof(kit8_t) + bl))
            return -2;
        memset(&r->k, 0, sizeof r->k);
        memcpy(&r->k, raw + 16, sizeof(kit8_t));
        return blob_into(r, raw + 16u + sizeof(kit8_t), bl);
    }
    if (magic == MAGIC_P7) {                     /* LXP7: its kit (no envelope yet), its blob as it is */
        static kit7_t k7;
        uint32_t bl = n >= 16u ? (uint32_t)(raw[12] | raw[13] << 8) : 0u;
        if (n < 16u + sizeof k7 + bl || rd32(raw + 4) != 0x5000u || bl > PBLOB7 ||
            rd32(raw + 8) != fnv(raw + 12, 4u + sizeof k7 + bl))
            return -2;
        memcpy(&k7, raw + 16, sizeof k7);
        memset(&r->k, 0, sizeof r->k);
        for (t = 0; t < NTRK; t++) {
            sound_from6(&r->k.s[t], &k7.s[t].s6, t);
            r->k.s[t].och = k7.s[t].och;
            r->k.s[t].onote = k7.s[t].onote;
        }
        r->k.tune = k7.tune; r->k.ddec = k7.ddec; r->k.drv_mode = k7.drv_mode; r->k.drv_amt = k7.drv_amt;
        r->k.drv_tone = k7.drv_tone; r->k.drv_lvl = k7.drv_lvl; r->k.dly_div = k7.dly_div; r->k.dly_fb = k7.dly_fb;
        r->k.dly_tone = k7.dly_tone; r->k.dly_lvl = k7.dly_lvl; r->k.rev_size = k7.rev_size; r->k.rev_damp = k7.rev_damp;
        r->k.rev_lvl = k7.rev_lvl; r->k.dly_mode = k7.dly_mode; r->k.dly_div2 = k7.dly_div2;
        return blob_into(r, raw + 16u + sizeof k7, bl);
    }
    memset(&k6, 0, sizeof k6);
    memset(&g, 0, sizeof g);
    if (magic == MAGIC_P6) {                     /* LXP6: its kit and patterns as they were laid out */
        const uint8_t *pp = raw + 12u + sizeof(kit6_t);
        if (n < sizeof(proj6_t) || rd32(raw + 4) != sizeof(proj6_t) || rd32(raw + 8) != fnv(raw + 12, sizeof(kit6_t) + sizeof(pat_rec6_t)))
            return -2;
        memcpy(&k6, raw + 12, sizeof k6);
        kit_from6(&r->k, &k6);
        enc_begin(r);
        for (i = 0; i < NPAT; i++) {
            cv_from_st(pp + i * sizeof(pat_st_t), 1);
            enc_pattern(&cv_pat, 0);
        }
        memcpy(&g, pp + NPAT * sizeof(pat_st_t), sizeof g);
        enc_tail(&g, 0, 1, 0, SONG0);
        return enc_end(r) ? -2 : 0;
    }
    if (magic == MAGIC_P4 || magic == MAGIC_P5) {   /* LXP4 (no substeps, 1032 B a pattern), LXP5 (1224 B): their kit, the
                                                     * extras' nibbles (probability, pan) remapped */
        uint32_t pl = magic == MAGIC_P4 ? 1032u : 1224u, body = KIT_OLD + NPAT * pl + 6u, k;
        if (n < 12u + body || rd32(raw + 8) != fnv(raw + 12, body))
            return -2;
        memcpy(&k6, raw + 12, KIT_OLD);
        k6.dly_div2 = k6.dly_div;
        kit_from6(&r->k, &k6);
        enc_begin(r);
        for (t = 0, i = 12u + KIT_OLD; t < NPAT; t++, i += pl) {
            memset(&cv_pat, 0, sizeof cv_pat);
            memcpy(cv_pat.s, raw + i, NTRK * NSTEP);
            for (k = 0; k < NTRK * NSTEP; k++) {
                uint32_t o = raw[i + NTRK * NSTEP + k], pn = o & 15u, v = pn ? (uint32_t)clamp(((int32_t)pn * 8 + 8) / 16, 1, 7) : 0u;
                (&cv_pat.x[0][0])[k] = (uint8_t)((o >> 4) << 3 | v);
            }
            if (magic == MAGIC_P5) {
                static pat_st_t q;
                memcpy(q.r3, raw + i + 2u * NTRK * NSTEP, sizeof q.r3);
                for (k = 0; k < NTRK * NSTEP; k++) {
                    uint32_t b = 3u * k, v = (uint32_t)q.r3[b >> 3] >> (b & 7u);
                    if ((b & 7u) > 5u)
                        v |= (uint32_t)q.r3[(b >> 3) + 1u] << (8u - (b & 7u));
                    (&cv_pat.r[0][0])[k] = (uint8_t)((v & 7u) <= 5u ? v & 7u : 0u);
                }
            }
            memcpy(cv_pat.len, raw + i + 2u * NTRK * NSTEP + (magic == MAGIC_P5 ? NTRK * NSTEP * 3u / 8u : 0u), NTRK);
            enc_pattern(&cv_pat, 0);
        }
        memcpy(&g, raw + i, sizeof g);
        enc_tail(&g, 0, 1, 0, SONG0);
        return enc_end(r) ? -2 : 0;
    }
    ver = magic == MAGIC_P1 ? 1u : magic == MAGIC_P2 ? 2u : magic == MAGIC_P3 ? 3u : 0u;
    ss = ver == 1u ? 22u : ver == 2u ? 27u : 30u;
    at = ver == 1u ? 28u : 12u;                  /* (the summed part) */
    if (!ver)
        return -2;
    i = ver == 1u ? NTRK * ss + NPAT * 520u : NTRK * ss + 13u + NPAT * 520u + 6u;   /* (its summed bytes) */
    if (n < at + i || rd32(raw + 8) != fnv(raw + at, i))
        return -2;
    for (t = 0; t < NTRK; t++)
        sound_from_old(&k6.s[t], raw + at + t * ss, ver);
    k6.drv_mode = DM_SOFT; k6.drv_amt = 40; k6.drv_tone = 90; k6.drv_lvl = 100;
    k6.dly_tone = 60; k6.dly_lvl = 110; k6.rev_lvl = 110;
    k6.dly_mode = 0;
    if (ver == 1u) {                             /* bpm swing ddec dly_div dly_fb rev_size rev_damp chain pat mute solo pad tune */
        const uint8_t *h = raw + 12;
        g.bpm = h[0]; g.swing = h[1]; k6.ddec = h[2]; k6.dly_div = h[3]; k6.dly_fb = h[4];
        k6.rev_size = h[5]; k6.rev_damp = h[6]; g.chain = h[7]; g.pat = h[8]; g.mute = h[9];
        g.solo = h[10]; k6.tune = (int8_t)h[12];
        i = at + NTRK * ss;
    } else {                                     /* tune ddec drv_mode drv_amt drv_tone drv_lvl dly_div dly_fb dly_tone ... */
        const uint8_t *h = raw + at + NTRK * ss;
        k6.tune = (int8_t)h[0]; k6.ddec = h[1]; k6.drv_mode = h[2]; k6.drv_amt = h[3]; k6.drv_tone = h[4];
        k6.drv_lvl = h[5]; k6.dly_div = h[6]; k6.dly_fb = h[7]; k6.dly_tone = h[8]; k6.dly_lvl = h[9];
        k6.rev_size = h[10]; k6.rev_damp = h[11]; k6.rev_lvl = h[12];
        i = at + NTRK * ss + 13u;
    }
    k6.dly_div2 = k6.dly_div;
    kit_from6(&r->k, &k6);
    enc_begin(r);
    for (t = 0; t < NPAT; t++, i += 520u) {      /* the patterns: their steps and lengths, no extras yet */
        memset(&cv_pat, 0, sizeof cv_pat);
        memcpy(cv_pat.s, raw + i, NTRK * NSTEP);
        memcpy(cv_pat.len, raw + i + NTRK * NSTEP, NTRK);
        enc_pattern(&cv_pat, 0);
    }
    if (ver >= 2u)
        memcpy(&g, raw + i, sizeof g);
    enc_tail(&g, 0, 1, 0, SONG0);
    return enc_end(r) ? -2 : 0;
}
static void store_migrate(void)                  /* LXP1..3 (eight slots of 12 KiB) -> LXP7, the last slot first */
{
    uint32_t i, moved = 0;
    for (i = NSLOT_OLD; i-- > 0;) {
        uint32_t m = 0;
        if (io_store_read(ST_OLD(i), &m, 4) || (m != MAGIC_P1 && m != MAGIC_P2 && m != MAGIC_P3))
            continue;
        if (io_store_read(ST_OLD(i), stage, 9000u) || proj_convert(stage, 9000u))
            continue;
        if (!store_write(ST_L2(i), &proj_buf, sizeof proj_buf))   /* (LAYOUT 2's place: moved on below) */
            moved++;
    }
    if (moved)
        ui_say("PATCHES KEPT");
}
static void store_migrate3(void)                 /* LAYOUT 2 (14 slots of 20 KiB) -> 3 (36 of 8 KiB), slot by slot from the
                                                  * first: slot i's new place ends before the old slot i + 1 begins */
{
    uint32_t i, moved = 0, lost = 0;
    char m[28];
    for (i = 0; i < NSLOT; i++) {
        uint32_t mg = 0, done = 0;
        if (i < NSLOT_L2 && io_store_read(ST_L2(i), &mg, 4) == 0 && slot_magic(mg)) {
            int rc = proj_read_at(ST_L2(i));
            if (!rc && !store_write(ST_PROJ(i), &proj_buf, sizeof proj_buf)) {
                moved++;
                done = 1;
            } else {
                lost++;
            }
        }
        mg = 0;
        if (!done && io_store_read(ST_PROJ(i), &mg, 4) == 0 && mg != 0xFFFFFFFFu)
            io_store_erase(ST_PROJ(i));          /* (what was there before: an empty slot now) */
    }
    str_cpy(m, "PATCHES MOVED: ", sizeof m);
    fmt_u(m + str_len(m), moved);
    if (lost) {
        str_cat(m, ", LOST ", sizeof m);
        fmt_u(m + str_len(m), lost);
    }
    if (moved || lost)
        ui_say(m);
}
static void store_scan(void)
{
    static set_rec_t st;
    uint32_t i;
    for (i = 0; i < NSLOT; i++) {
        uint32_t m = 0;
        ui.used[i] = io_store_read(ST_PROJ(i), &m, 4) == 0 && slot_magic(m);
    }
    if (io_store_read(ST_SET, &st, sizeof st) == 0 && (st.magic == MAGIC_SET || st.magic == MAGIC_SET2 || st.magic == MAGIC_SET1) &&
        (st.magic == MAGIC_SET1 || st.layout != LAYOUT)) {   /* a store of the formats before: moved, once */
        if (st.magic == MAGIC_SET1 || st.layout < 2u)
            store_migrate();
        store_migrate3();
        if (st.magic == MAGIC_SET1)
            st.qgrid = st.qround = 0;
        if (st.magic != MAGIC_SET) {
            st.clk_src = st.clk_out = 0;
            memset(st.map, 0, sizeof st.map);
        }
        st.magic = MAGIC_SET;
        st.layout = LAYOUT;
        store_write(ST_SET, &st, sizeof st);
        for (i = 0; i < NSLOT; i++) {
            uint32_t m = 0;
            ui.used[i] = io_store_read(ST_PROJ(i), &m, 4) == 0 && slot_magic(m);
        }
    }
    if (io_store_read(ST_SET, &st, sizeof st) == 0 && (st.magic == MAGIC_SET || st.magic == MAGIC_SET2)) {
        song.theme = st.theme < UI_NPALETTES ? st.theme : PAL_AMBER;
        song.midi_ch = st.midi_ch <= 16u ? st.midi_ch : 0;
        song.click = st.click < CLICK_N ? st.click : CLICK_OFF;
        song.qgrid = st.qgrid < Q_N ? st.qgrid : Q_16;
        song.qround = st.qround & 1u;
        ui.slot = ui.patch = st.slot % NSLOT;
        if (st.magic == MAGIC_SET) {
            song.clk_src = st.clk_src < CK_N ? st.clk_src : CK_INT;
            song.clk_out = st.clk_out & 1u;
            for (i = 0; i < NMAP; i++) {
                ccmap[i] = st.map[i];
                if (ccmap[i].pid >= P_COUNT || ccmap[i].ch > 15u || ccmap[i].cc > 127u)
                    ccmap[i].pid = 0;
            }
        }
    }
}
static void kit_take(kit_rec_t *k)
{
    memcpy(k->s, snd, sizeof k->s);
    k->tune = song.tune; k->ddec = song.ddec;
    k->drv_mode = song.drv_mode; k->drv_amt = song.drv_amt; k->drv_tone = song.drv_tone; k->drv_lvl = song.drv_lvl;
    k->dly_div = song.dly_div; k->dly_fb = song.dly_fb; k->dly_tone = song.dly_tone; k->dly_lvl = song.dly_lvl;
    k->rev_size = song.rev_size; k->rev_damp = song.rev_damp; k->rev_lvl = song.rev_lvl;
    k->dly_mode = song.dly_mode; k->dly_div2 = song.dly_div2;
    k->rev_pre = song.rev_pre;
    memset(k->rsv, 0, sizeof k->rsv);
}
static void kit_put(const kit_rec_t *k)
{
    uint32_t t, j;
    memcpy(snd, k->s, sizeof snd);
    for (t = 0; t < NTRK; t++) {                 /* (stored values as indexes: kept in range) */
        snd[t].smp %= TR_N;
        snd[t].stone = (int8_t)clamp(snd[t].stone, -64, 63);
        snd[t].sbend = (int8_t)clamp(snd[t].sbend, -64, 63);
        snd[t].och = snd[t].och <= 16u ? snd[t].och : 0u;
        snd[t].onote &= 0x7Fu;
        snd[t].atk &= 0x7Fu;
        snd[t].rel &= 0x7Fu;
        snd[t].gate = snd[t].gate <= 64u ? snd[t].gate : 0u;
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
    song.dly_mode = k->dly_mode % DL_N; song.dly_div2 = k->dly_div2 % 6u;
    song.rev_pre = k->rev_pre & 127u;
}
static int pat_take(proj_t *r)                   /* the patterns, their locks, the song into r's blob (0 = it fits) */
{
    ptail_t g = {song.bpm, song.swing, song.chain, song.pat, song.mute, song.solo};
    uint32_t i;
    enc_begin(r);
    for (i = 0; i < NPAT; i++)
        enc_pattern(&pat[i], plk[i]);
    enc_tail(&g, song.smode, song.slen ? song.slen : 1u, song.send, srows);
    return enc_end(r);
}
static void proj_save(uint32_t slot, uint32_t what)   /* SW_ALL, or the kit / the patterns into what the slot has */
{
    proj_t *r = &proj_buf;
    int had = what != SW_ALL && !proj_read(slot);
    if (what != SW_PAT || !had)
        kit_take(&r->k);
    if ((what != SW_KIT || !had) && pat_take(r)) {
        ui_say("PATCH TOO FULL");
        return;
    }
    r->magic = MAGIC_PROJ;
    r->size = sizeof *r;
    r->sum = proj_sum(r);
    if (store_write(ST_PROJ(slot), r, sizeof *r)) {
        ui_say("STORE FAILED");
        return;
    }
    ui.used[slot % NSLOT] = 1;
    ui.slot = ui.patch = (uint8_t)slot;
    ui.loaded = (uint8_t)(slot + 1u);
    settings_save();
    {
        char m[28];
        str_cpy(m, what == SW_KIT ? "KIT SAVED: " : what == SW_PAT ? "PATTERNS SAVED: " : "PATCH SAVED: ", sizeof m);
        fmt_u(m + str_len(m), slot + 1u);
        ui_say(m);
    }
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
        dec_blob(&proj_buf, 1);
    ui.patch = (uint8_t)slot;
    ui.loaded = (uint8_t)(slot + 1u);
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
static uint32_t tpos(uint32_t t) { return trk_k[t % NTRK] % tlen(t); }   /* the step track t is at */

/* ---- parameter locks (the pattern playing's pool, lx_core.h lock_t) */
enum { LM_OFF, LM_LOCK, LM_NUDGE };
static int lockable(uint32_t id)                 /* a sound's parameter a step can lock */
{
    return (id >= P_V0 && id <= P_V7) || (id >= P_SMP && id <= P_SBEND) || (id >= P_LEVEL && id <= P_DRV) ||
           (id >= P_EATK && id <= P_EGATE);
}
static int32_t lock_val(uint32_t pid, uint32_t raw) /* the stored byte as the parameter's value */
{
    return pid == P_PAN || pid == P_STONE || pid == P_SBEND ? (int32_t)(int8_t)raw : (int32_t)raw;
}
static void lock_apply(sound_t *s, uint32_t pid, uint32_t val)   /* (the audio ISR: a hit's sound, lx_synth.c) */
{
    if (pid >= P_V0 && pid <= P_V7) { s->p[pid - P_V0] = (uint8_t)(val & 127u); return; }
    switch (pid) {
    case P_SMP: s->smp = (uint8_t)(val % TR_N); break;
    case P_SLVL: s->slvl = (uint8_t)(val & 127u); break;
    case P_SPITCH: s->spitch = (uint8_t)(val & 127u); break;
    case P_SDEC: s->sdec = (uint8_t)(val & 127u); break;
    case P_SYN: s->syn = (uint8_t)(val & 127u); break;
    case P_SSTART: s->sstart = (uint8_t)(val & 127u); break;
    case P_STONE: s->stone = (int8_t)clamp((int8_t)val, -64, 63); break;
    case P_SBEND: s->sbend = (int8_t)clamp((int8_t)val, -64, 63); break;
    case P_LEVEL: s->level = (uint8_t)(val & 127u); break;
    case P_PAN: s->pan = (int8_t)clamp((int8_t)val, -64, 63); break;
    case P_DLY: s->dly = (uint8_t)(val & 127u); break;
    case P_REV: s->rev = (uint8_t)(val & 127u); break;
    case P_DRV: s->drv = (uint8_t)(val & 127u); break;
    case P_EATK: s->atk = (uint8_t)(val & 127u); break;
    case P_EREL: s->rel = (uint8_t)(val & 127u); break;
    case P_EGATE: s->gate = (uint8_t)(val <= 64u ? val : 64u); break;
    }
}
static lock_t *lock_find(const lock_t *pool, uint32_t t, uint32_t idx, uint32_t pid)
{
    uint32_t i, key = (t % NTRK) << 6 | (idx % NSTEP);
    for (i = 0; i < NLOCK; i++)
        if (pool[i].pid == pid && pool[i].key == key)
            return (lock_t *)&pool[i];
    return 0;
}
static int lock_set(lock_t *pool, uint32_t t, uint32_t idx, uint32_t pid, int32_t v)   /* 0, or -1: the pool is full */
{
    lock_t *l = lock_find(pool, t, idx, pid);
    uint32_t i;
    if (!l)
        for (i = 0; i < NLOCK && !l; i++)
            if (!pool[i].pid)
                l = &pool[i];
    if (!l)
        return -1;
    l->key = (uint16_t)((t % NTRK) << 6 | (idx % NSTEP));
    l->val = (uint8_t)v;
    RING_PUBLISH();
    l->pid = (uint8_t)pid;                       /* (last: the audio ISR reads a whole lock or none) */
    return 0;
}
static uint32_t lock_clear(lock_t *pool, uint32_t t, uint32_t from, uint32_t n)   /* track t's steps from .. from + n - 1 (t NTRK: all) */
{
    uint32_t i, c = 0;
    for (i = 0; i < NLOCK; i++) {
        uint32_t lt = pool[i].key >> 6, ls = pool[i].key & 63u;
        if (pool[i].pid && (t >= NTRK || lt == t) && ls >= from && ls < from + n) {
            pool[i].pid = 0;
            c++;
        }
    }
    return c;
}
static int lock_dup(lock_t *pool, uint32_t t, uint32_t from, uint32_t to, uint32_t n)   /* steps from.. copied to to.. */
{
    uint32_t i;
    int full = 0;
    lock_clear(pool, t, to, n);
    for (i = 0; i < NLOCK; i++) {
        uint32_t ls = pool[i].key & 63u;
        if (pool[i].pid && (pool[i].key >> 6) == t && ls >= from && ls < from + n)
            full |= lock_set(pool, t, to + ls - from, pool[i].pid, pool[i].val) != 0;
    }
    return full;
}
static uint8_t clip_bar[16], clip_xbar[16], clip_rbar[16], clip_ok;
static lock_t clip_blk[64];                      /* the bar copied: its locks (steps 0..15) */
static pattern_t clip_pat;
static lock_t clip_plk[NLOCK] __attribute__((section(".pool")));
static uint8_t clip_pat_ok;                      /* the pattern copied: its number + 1 (0: none) */
static void rotate_track(uint32_t t, int32_t d)  /* the track's steps 1..LEN moved d steps later */
{
    uint8_t tmp[NSTEP];
    uint32_t len = tlen(t), i;
    lock_t *pl = plk[song.pat % NPAT];
    for (i = 0; i < len; i++)
        tmp[(uint32_t)((int32_t)i + d + 64 * (int32_t)len) % len] = PAT->s[t][i];
    memcpy(PAT->s[t], tmp, len);
    for (i = 0; i < len; i++)                    /* (the extras go with their steps, the locks too) */
        tmp[(uint32_t)((int32_t)i + d + 64 * (int32_t)len) % len] = PAT->x[t][i];
    memcpy(PAT->x[t], tmp, len);
    for (i = 0; i < len; i++)
        tmp[(uint32_t)((int32_t)i + d + 64 * (int32_t)len) % len] = PAT->r[t][i];
    memcpy(PAT->r[t], tmp, len);
    for (i = 0; i < NLOCK; i++) {
        uint32_t ls = pl[i].key & 63u;
        if (pl[i].pid && (pl[i].key >> 6) == t && ls < len)
            pl[i].key = (uint16_t)(t << 6 | (uint32_t)((int32_t)ls + d + 64 * (int32_t)len) % len);
    }
}
static void euclid_track(uint32_t t, uint32_t hits)   /* HITS spread evenly over LEN (Bjorklund by the bucket) */
{
    uint32_t len = tlen(t), i, b = 0;
    lock_clear(plk[song.pat % NPAT], t, 0, NSTEP);
    for (i = 0; i < len; i++) {
        b += hits;
        PAT->s[t][i] = b >= len ? (uint8_t)100u : 0u;
        PAT->x[t][i] = PAT->r[t][i] = 0;
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
    lock_clear(plk[song.pat % NPAT], t, 0, NSTEP);
    for (i = 0; i < len; i++) {
        uint32_t r = rng() % 100u, on = r < DENS[t];
        if (t == 0u && i % 4u == 0u) on = 1;     /* the kick on the beats, the snare on 2 and 4 */
        if (t == 1u) on = i % 8u == 4u || (r < 8u);
        PAT->s[t][i] = on ? (uint8_t)(70u + rng() % 50u) | (rng() % 5u == 0u ? S_ACC : 0u) : 0u;
        PAT->x[t][i] = PAT->r[t][i] = 0;
    }
}
static void double_track(uint32_t t)
{
    uint32_t len = tlen(t), i;
    if (len * 2u > NSTEP) {
        ui_say("64 STEPS MAX");
        return;
    }
    for (i = 0; i < len; i++) {
        PAT->s[t][len + i] = PAT->s[t][i];
        PAT->x[t][len + i] = PAT->x[t][i];
        PAT->r[t][len + i] = PAT->r[t][i];
    }
    lock_dup(plk[song.pat % NPAT], t, 0, len, len);
    PAT->len[t] = (uint8_t)(len * 2u);
    ui_say("DOUBLED");
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
        if (up) {
            for (i = 0; i < len; i++) {
                PAT->s[t][len + i] = ui.copy ? PAT->s[t][i] : 0u;
                PAT->x[t][len + i] = ui.copy ? PAT->x[t][i] : 0u;
                PAT->r[t][len + i] = ui.copy ? PAT->r[t][i] : 0u;
            }
            if (ui.copy)
                lock_dup(plk[song.pat % NPAT], t, 0, len, len);
            else
                lock_clear(plk[song.pat % NPAT], t, len, len);
        }
        PAT->len[t] = (uint8_t)(up ? len * 2u : (len > 1u ? len / 2u : 1u));
    }
    if (ui.bar >= nbars(song.sel))
        ui.bar = (uint8_t)(nbars(song.sel) - 1u);
    ui_say(cut ? "64 STEPS MAX" : up ? (ui.copy ? "x2, NOTES COPIED" : "x2, NEW BARS EMPTY") : "HALF LENGTH");
}

/* the kit at power-on: the factory sounds, the global FX as they come */
static void kit_factory(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        sound_init(i);
    song.tune = 0;
    song.ddec = 64;
    song.drv_mode = DM_SOFT; song.drv_amt = 40; song.drv_tone = 90; song.drv_lvl = 100;
    song.dly_div = 1; song.dly_div2 = 2; song.dly_mode = DL_MONO; song.dly_fb = 50; song.dly_tone = 60; song.dly_lvl = 110;
    song.rev_size = 50; song.rev_damp = 40; song.rev_lvl = 90; song.rev_pre = 0;
}
/* NEW PATCH: stopped, 16 empty patterns of len steps at speed spd, the tempo, no song; the factory kit or (keep) the
 * one there. Nothing is saved: SAVE puts it in a slot. */
static void patch_new(uint32_t len, int32_t spd, uint32_t bpm, uint32_t keep)
{
    uint32_t i, t;
    undo_take();
    seq_play(0);
    for (i = 0; i < NPAT; i++) {
        memset(&pat[i], 0, sizeof pat[i]);
        for (t = 0; t < NTRK; t++) {
            pat[i].len[t] = (uint8_t)clamp((int32_t)len, 1, NSTEP);
            pat[i].spd[t] = (int8_t)clamp(spd, SPD_MIN, SPD_MAX);
        }
    }
    memset(plk, 0, sizeof plk);
    memset(srows, 0, sizeof srows);
    srows[0].rep = 1;
    song.slen = 1;
    song.smode = 0;
    song.srow = song.srep = 0;
    song.bpm = (uint8_t)clamp((int32_t)bpm, 60, 200);
    song.swing = 0;
    song.chain = 1;
    song.pat = song.chain0 = 0;
    song.pat_next = 0xFF;
    song.mute = song.solo = 0;
    if (!keep)
        kit_factory();
    ui.loaded = 0;
    ui.bar = 0;
    ui.rot = 0;
    memset(ui.eu, 0, sizeof ui.eu);
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
    if (id == P_DTIME && song.dly_mode == DL_STEREO)
        d.label = "TIME L";
    if (id == P_DTIME2 && song.dly_mode != DL_STEREO)
        d.label = "TIME R";                     /* (STEREO only) */
    return d;
}
static uint32_t held_first(void)                /* the first step held (P-LOCK, NUDGE, STEP EDIT) */
{
    uint32_t m = ui.sedit ? ui.sedit : ui.wheld;
    return (ui.bar * 16u + (uint32_t)__builtin_ctz(m ? m : 1u)) % NSTEP;
}
static int lock_view(uint32_t id)                /* P-LOCK with a step held: its card shows the step's value */
{
    return ui.lockmode == LM_LOCK && ui.page != PG_SEQ && (ui.wheld || ui.sedit) && lockable(id);
}
static int32_t pget(uint32_t id)
{
    const sound_t *s = TS;
    if (lock_view(id)) {
        const lock_t *l = lock_find(plk[song.pat % NPAT], song.sel, held_first(), id);
        if (l)
            return lock_val(id, l->val);
    }
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
    case P_SSTART: return s->sstart;
    case P_STONE: return s->stone;
    case P_SBEND: return s->sbend;
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
    case P_DLMODE: return song.dly_mode;
    case P_DTIME: return song.dly_div;
    case P_DTIME2: return song.dly_div2;
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
    case P_LWHAT: return ui.lwhat;
    case P_LOAD: return ui.slot + 1;
    case P_QGRID: return song.qgrid;
    case P_QROUND: return song.qround;
    case P_EVEL: case P_EPROB: case P_EPAN: case P_ESUB: case P_EMICRO: {   /* STEP EDIT: the first step held */
        uint32_t i = held_first();
        uint8_t st = PAT->s[song.sel][i], x = PAT->x[song.sel][i], r = PAT->r[song.sel][i];
        return id == P_EVEL ? (int32_t)S_VEL(st) : id == P_ESUB ? (int32_t)R_SUB(r) + 1 : id == P_EMICRO ? R_MICRO(r) :
               id == P_EPAN ? (int32_t)X_PAN(x) : !X_COND(x) ? 16 : X_COND(x) < C_PROB_N ? (int32_t)X_COND(x) : (int32_t)X_COND(x) + 1;
    }
    case P_SPEED: return PAT->spd[song.sel % NTRK];
    case P_RPRE: return song.rev_pre;
    case P_LSEL: return ui.lsel + 1;
    case P_LDEL: return 1;
    case P_TSEL: return song.sel % NTRK;
    case P_TMUTE: return (song.mute >> (song.sel % NTRK)) & 1u;
    case P_TSOLO: return (song.solo >> (song.sel % NTRK)) & 1u;
    case P_TCLR: case P_TALL: case P_PCLR: return 1;
    case P_EATK: return s->atk;
    case P_EREL: return s->rel;
    case P_EGATE: return s->gate;
    case P_MFLT: return song.mflt;
    case P_MRES: return song.mres;
    case P_CLKSRC: return song.clk_src;
    case P_CLKOUT: return song.clk_out;
    case P_OCH: return s->och;
    case P_ONOTE: return s->onote;
    case P_LEARN: return ui.learn ? 1 : 0;
    case P_SROW: return ui.scur + 1;
    case P_SPAT: return srows[ui.scur % NSONG].pat + 1;
    case P_SREP: return srows[ui.scur % NSONG].rep ? srows[ui.scur % NSONG].rep : 1;
    case P_SMODE: return song.smode;
    case P_SLEN: return song.slen ? song.slen : 1;
    case P_SEND: return song.send;
    case P_NLEN: return ui.nlen ? ui.nlen : 16;
    case P_NSPD: return ui.nspd;
    case P_NBPM: return ui.nbpm ? ui.nbpm : 120;
    case P_NGO: return 1;
    case P_NKIT: return ui.nkit;
    }
    if (id >= P_V0 && id <= P_V7)
        return s->p[id - P_V0];
    if (id >= P_MIX0 && id <= P_MIX3)
        return snd[(ui.bank * 4u + id - P_MIX0) % NTRK].level;
    if (id >= P_MPAN0 && id <= P_MPAN3)
        return snd[(ui.bank * 4u + id - P_MPAN0) % NTRK].pan;
    return 0;
}
static void trk_action(uint32_t id)             /* TRACKS, a turn: KNOB 4 the track cleared, B: all on / the pattern cleared */
{
    uint32_t t = song.sel % NTRK;
    if (id == P_TALL) {
        song.mute = song.solo = 0;
        ui_say("ALL ON");
        return;
    }
    undo_take();
    if (id == P_PCLR) {
        memset(PAT->s, 0, sizeof PAT->s); memset(PAT->x, 0, sizeof PAT->x); memset(PAT->r, 0, sizeof PAT->r);
        memset(plk[song.pat % NPAT], 0, sizeof plk[0]);
        ui_say("PATTERN CLEARED");
    } else {
        memset(PAT->s[t], 0, NSTEP); memset(PAT->x[t], 0, NSTEP); memset(PAT->r[t], 0, NSTEP);
        lock_clear(plk[song.pat % NPAT], t, 0, NSTEP);
        ui_say("TRACK CLEARED");
    }
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
            memcpy(clip_plk, plk[k % NPAT], sizeof clip_plk);
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
            memcpy(plk[k % NPAT], clip_plk, sizeof clip_plk);
            str_cpy(m, "PASTED INTO P", sizeof m);
            fmt_u(m + str_len(m), k + 1u);
        }
        ui_say(m);
        return;
    }
    if (id == P_LOAD) {                          /* STORE, KNOB 4: a turn loads the slot (what KNOB 3 says); an empty slot: a new,
                                                  * empty patch (nothing of the one before keeps playing) */
        if (v != pget(id)) {
            if (ui.used[ui.slot % NSLOT]) {
                proj_load(ui.slot, ui.lwhat);
            } else {
                patch_new(16, 0, song.bpm, 0);
                ui_say("EMPTY: NEW PATCH");
            }
        }
        return;
    }
    if (id == P_LDEL) {                          /* CC LEARN, KNOB 3: a turn deletes the entry shown */
        if (v != pget(id)) {
            uint32_t n = cc_count();
            char m[28];
            if (!n) {
                ui_say("NOTHING MAPPED");
                return;
            }
            cc_pack();
            str_cpy(m, "DELETED: CC", sizeof m);
            fmt_u(m + str_len(m), ccmap[ui.lsel % NMAP].cc);
            memset(&ccmap[ui.lsel % NMAP], 0, sizeof ccmap[0]);
            cc_pack();
            if (ui.lsel && ui.lsel >= cc_count())
                ui.lsel--;
            settings_save();
            ui_say(m);
        }
        return;
    }
    if (id == P_TCLR || id == P_TALL || id == P_PCLR) {   /* TRACKS: a turn clears the track / all on / clears the pattern */
        if (v != pget(id))
            trk_action(id);
        return;
    }
    if (id == P_NGO) {                           /* NEW PATCH, KNOB 4: a turn makes it */
        if (v != pget(id)) {
            patch_new(pget(P_NLEN), ui.nspd, pget(P_NBPM), ui.nkit);
            ui_say(ui.nkit ? "NEW PATCH, KIT KEPT" : "NEW PATCH");
        }
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
    case P_TSEL: song.sel = (uint8_t)v; if (ui.bar >= nbars(song.sel)) ui.bar = 0; ui.rot = 0; break;
    case P_TMUTE: song.mute = (uint8_t)((song.mute & ~(1u << t)) | ((uint32_t)v << t)); break;
    case P_TSOLO: song.solo = (uint8_t)((song.solo & ~(1u << t)) | ((uint32_t)v << t)); break;
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
    case P_SSTART: s->sstart = (uint8_t)v; break;
    case P_STONE: s->stone = (int8_t)v; break;
    case P_SBEND: s->sbend = (int8_t)v; break;
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
    case P_DLMODE: song.dly_mode = (uint8_t)v; break;
    case P_DTIME: song.dly_div = (uint8_t)v; break;
    case P_DTIME2: song.dly_div2 = (uint8_t)v; break;
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
    case P_LWHAT: ui.lwhat = (uint8_t)v; break;
    case P_QGRID: song.qgrid = (uint8_t)v; settings_save(); break;
    case P_QROUND: song.qround = (uint8_t)v; settings_save(); break;
    case P_EVEL: case P_EPROB: case P_EPAN: case P_ESUB: case P_EMICRO: {   /* STEP EDIT: every step held */
        uint32_t w;
        undo_burst(0x900u + id);
        for (w = 0; w < 16u; w++) {
            uint32_t i = (ui.bar * 16u + w) % NSTEP;
            uint8_t *st = &PAT->s[t][i], *x = &PAT->x[t][i], *r = &PAT->r[t][i];
            if (!((ui.sedit >> w) & 1u) || !*st)
                continue;
            if (id == P_EVEL) *st = (uint8_t)((*st & S_ACC) | (uint32_t)v);
            else if (id == P_ESUB) *r = R_MAKE((uint32_t)(v - 1), R_MICRO(*r));
            else if (id == P_EMICRO) *r = R_MAKE(R_SUB(*r), v);
            else if (id == P_EPAN) *x = (uint8_t)((*x & 0xF8u) | (uint32_t)v);
            else *x = (uint8_t)((*x & 7u) | (uint32_t)(v == 16 ? 0 : v < 16 ? v : v - 1) << 3);   /* (.. 100 %, A:B, FILL) */
        }
        break;
    }
    case P_SPEED: undo_burst(0xB00u + t); PAT->spd[t] = (int8_t)v; break;
    case P_MFLT: song.mflt = (int8_t)v; break;
    case P_MRES: song.mres = (uint8_t)v; break;
    case P_CLKSRC: song.clk_src = (uint8_t)v; settings_save(); break;
    case P_CLKOUT: song.clk_out = (uint8_t)v; settings_save(); break;
    case P_OCH: s->och = (uint8_t)v; break;
    case P_RPRE: song.rev_pre = (uint8_t)v; break;
    case P_LSEL: {
        uint32_t n = cc_count();
        ui.lsel = (uint8_t)(n ? ((uint32_t)v - 1u < n ? (uint32_t)v - 1u : n - 1u) : 0u);
        break;
    }
    case P_EATK: s->atk = (uint8_t)v; break;
    case P_EREL: s->rel = (uint8_t)v; break;
    case P_EGATE: s->gate = (uint8_t)v; break;
    case P_ONOTE: s->onote = (uint8_t)v; break;
    case P_LEARN:
        if (v == 2) {
            memset(ccmap, 0, sizeof ccmap);
            settings_save();
            ui.learn = 0;
            ui_say("CC MAP CLEARED");
        } else {
            ui.learn = (uint8_t)v;
            ui_say(v ? "LEARN: TURN A KNOB" : "LEARN OFF");
        }
        break;
    case P_SROW: ui.scur = (uint8_t)(v - 1); if (!srows[ui.scur].rep) srows[ui.scur].rep = 1; break;
    case P_SPAT: srows[ui.scur % NSONG].pat = (uint8_t)(v - 1); if (!srows[ui.scur % NSONG].rep) srows[ui.scur % NSONG].rep = 1; break;
    case P_SREP: srows[ui.scur % NSONG].rep = (uint8_t)v; break;
    case P_SMODE:
        song.smode = (uint8_t)v;
        if (!song.slen) song.slen = 1;
        if (v && !srows[0].rep) srows[0].rep = 1;
        break;
    case P_SLEN: {
        uint32_t k;
        for (k = song.slen; k < (uint32_t)v; k++)
            if (!srows[k].rep) { srows[k].rep = 1; srows[k].pat = k ? srows[k - 1u].pat : 0u; }
        song.slen = (uint8_t)v;
        if (song.srow >= song.slen) song.srow = 0;
        if (ui.scur >= song.slen) ui.scur = (uint8_t)(song.slen - 1u);
        break;
    }
    case P_SEND: song.send = (uint8_t)v; break;
    case P_NLEN: ui.nlen = (uint8_t)v; break;
    case P_NSPD: ui.nspd = (int8_t)v; break;
    case P_NBPM: ui.nbpm = (uint8_t)v; break;
    case P_NKIT: ui.nkit = (uint8_t)v; break;
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
static void smp_name(char *b, uint32_t smp, uint32_t t)   /* the sample layer's: OFF, SNAP, the LXR's, the sound's own,
                                                           * the machines' (808 A ...) */
{
    if (smp >= TR_MACH0) {
        str_cpy(b, MACH_NAME[(smp - TR_MACH0) % MACH_TRANS_N], 16);
    } else if (smp >= TR_OWN0) {
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
    case FM_SUB: if (v <= 1) str_cpy(val, "OFF", 16); else { fmt_u(val, (uint32_t)v); *unit = "X"; } break;
    case FM_MICRO: if (!v) str_cpy(val, "0", 16); else { fmt_s(val, v, 1); *unit = "/32"; } break;
    case FM_FILT:
        if (!v) str_cpy(val, "OFF", 16);
        else { str_cpy(val, v < 0 ? "LP " : "HP ", 16); fmt_u(val + 3, (uint32_t)(v < 0 ? -v : v)); }
        break;
    case FM_NOTE: {
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        str_cpy(val, NN[v % 12], 16);
        if (v < 12) str_cat(val, "-", 16);
        fmt_u(val + str_len(val), (uint32_t)(v < 12 ? 1 : v / 12 - 1));
        break;
    }
    case FM_CH: if (!v) str_cpy(val, "OFF", 16); else fmt_u(val, (uint32_t)v); break;
    case FM_ATK:                                 /* (ms: v * v / 64) */
        if (!v) str_cpy(val, "OFF", 16);
        else { fmt_u(val, (uint32_t)(v * v / 64 ? v * v / 64 : 1)); *unit = "MS"; }
        break;
    case FM_TIME: {                              /* (to -60 dB) */
        uint32_t ms = TIME_MS[v & 127];
        if (ms < 1000u) { fmt_u(val, ms); *unit = "MS"; }
        else { fmt_u(val, ms / 1000u); str_cat(val, ".", 16); fmt_u(val + str_len(val), ms / 100u % 10u); *unit = "S"; }
        break;
    }
    case FM_PRE: if (!v) str_cpy(val, "OFF", 16); else { fmt_u(val, (uint32_t)v * 180u / 127u); *unit = "MS"; } break;
    case FM_GATE:                                /* (quarters of a step) */
        if (!v) str_cpy(val, "OFF", 16);
        else {
            fmt_u(val, (uint32_t)v / 4u);
            if (v % 4) { str_cat(val, v % 4 == 2 ? ".5" : v % 4 == 1 ? ".25" : ".75", 16); }
            *unit = "ST";
        }
        break;
    case FM_PROB:
        if (v == 31 || v == 32) {
            str_cpy(val, v == 31 ? "FILL" : "!FILL", 16);
        } else if (v > 16) {                     /* A:B */
            fmt_u(val, COND_A[(v - 17) % 14]);
            str_cat(val, ":", 16);
            fmt_u(val + str_len(val), COND_B[(v - 17) % 14]);
        } else {
            fmt_u(val, (uint32_t)(v >= 16 ? 100 : v * 100 / 16));
            *unit = "%";
        }
        break;
    case FM_SPAN:
        if (!v) str_cpy(val, "SOUND", 16);
        else if (v == 4) str_cpy(val, "C", 16);
        else { val[0] = v < 4 ? 'L' : 'R'; fmt_u(val + 1, (uint32_t)(v < 4 ? (4 - v) * 16 : (v - 4) * 16)); }
        break;
    case FM_TONE:
        if (!v) str_cpy(val, "OFF", 16);
        else { str_cpy(val, v < 0 ? "LP " : "HP ", 16); fmt_u(val + 3, (uint32_t)(v < 0 ? -v : v)); }
        break;
    case FM_PAT: if (v) { val[0] = 'P'; fmt_u(val + 1, (uint32_t)v); } else str_cpy(val, "-", 16); break;
    case FM_LOAD: str_cpy(val, id == P_NGO || id == P_LDEL || id == P_TCLR || id == P_TALL || id == P_PCLR || ui.used[(uint32_t)(v - 1) % NSLOT] ? "TURN" : "EMPTY", 16); break;
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
    static const uint8_t STEP_P[4] = {P_EVEL, P_EPROB, P_EPAN, P_ESUB};
    static const uint8_t NUDGE_P[4] = {P_EMICRO, P_EVEL, P_EPROB, P_ESUB};
    if (ui.lockmode == LM_NUDGE && ui.sedit && ui.page != PG_NR)   /* NUDGE: the steps held, their timing first */
        return NUDGE_P[c & 3u];
    if (ui.page == PG_SEQ && ui.sedit)           /* STEP EDIT: the steps held */
        return STEP_P[c & 3u];
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
enum { BK_PREV, BK_NEXT, BK_MUTE, BK_SOLO, BK_AUDITION /* (D#4: RANDOM MUTE) */, BK_COPY, BK_PASTE, BK_UNDO, BK_PLOCK, BK_NUDGE, BK_FILL, BK_N };

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
    if (pg == PG_SONG && ui.page != PG_SONG)
        ui.sfresh = 1;
    ui.page = (uint8_t)pg;
    ui.rot = 0;
    ui.pkey = 0;
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
    case BK_AUDITION:                            /* RANDOM MUTE: the other sounds muted by chance (the one selected stays) */
        if (ui.shift) {                          /* (SHIFT: a new sound for the track, silently) */
            undo_take();
            sound_preset(t, 1u + rng() % 99u);
            ui_say("NEW SOUND");
            break;
        }
        {
            uint32_t mk, k, n, tries = 0;
            do {                                 /* 1 .. 6 of the other seven, never the same set twice in a row */
                for (mk = 0, n = 0, k = 0; k < NTRK; k++)
                    if (k != t && rng() % 100u < 40u) {
                        mk |= 1u << k;
                        n++;
                    }
            } while ((n < 1u || n > 6u || mk == song.mute) && ++tries < 32u);
            song.mute = (uint8_t)mk;
            song.solo = 0;
            str_cpy(m, "RANDOM MUTE: ", sizeof m);
            fmt_u(m + str_len(m), n);
            str_cat(m, " OFF", sizeof m);
            ui_say(m);
        }
        break;
    case BK_COPY:
        if (ui.shift) {
            memcpy(&clip_pat, PAT, sizeof clip_pat);
            memcpy(clip_plk, plk[song.pat % NPAT], sizeof clip_plk);
            clip_pat_ok = (uint8_t)(song.pat + 1u);
            ui_say("PATTERN COPIED");
        } else {
            uint32_t i, n = 0, b0 = ui.bar * 16u % NSTEP;
            const lock_t *pl = plk[song.pat % NPAT];
            memcpy(clip_bar, &PAT->s[t][b0], 16);
            memcpy(clip_xbar, &PAT->x[t][b0], 16);
            memcpy(clip_rbar, &PAT->r[t][b0], 16);
            memset(clip_blk, 0, sizeof clip_blk);
            for (i = 0; i < NLOCK && n < NELEM(clip_blk); i++)
                if (pl[i].pid && (pl[i].key >> 6) == t && (pl[i].key & 63u) >= b0 && (pl[i].key & 63u) < b0 + 16u) {
                    clip_blk[n] = pl[i];
                    clip_blk[n++].key = (uint16_t)((pl[i].key & 63u) - b0);
                }
            clip_ok = 1;
            ui_say("BAR COPIED");
        }
        break;
    case BK_PASTE:
        if (ui.shift ? !clip_pat_ok : !clip_ok) { ui_say("NOTHING COPIED"); break; }
        undo_take();
        if (ui.shift) {
            memcpy(PAT, &clip_pat, sizeof clip_pat);
            memcpy(plk[song.pat % NPAT], clip_plk, sizeof clip_plk);
        } else {
            uint32_t i, b0 = ui.bar * 16u % NSTEP, full = 0;
            memcpy(&PAT->s[t][b0], clip_bar, 16);
            memcpy(&PAT->x[t][b0], clip_xbar, 16);
            memcpy(&PAT->r[t][b0], clip_rbar, 16);
            lock_clear(plk[song.pat % NPAT], t, b0, 16);
            for (i = 0; i < NELEM(clip_blk); i++)
                if (clip_blk[i].pid)
                    full |= (uint32_t)(lock_set(plk[song.pat % NPAT], t, b0 + clip_blk[i].key, clip_blk[i].pid, clip_blk[i].val) != 0);
            if (full) { ui_say("LOCKS FULL"); break; }
        }
        ui_say("PASTED");
        break;
    case BK_UNDO: undo_do(ui.shift); break;     /* UNDO (SHIFT: REDO); clearing: SEQ page 3, TRACKS */
    case BK_PLOCK:                               /* P-LOCK (SHIFT: the accent mode) */
        if (ui.shift) { ui.acc_mode ^= 1u; ui_say(ui.acc_mode ? "ACCENT EDIT" : "STEP EDIT"); break; }
        if (ui.lockmode == LM_LOCK && ui.wheld) {   /* steps held: their locks go */
            uint32_t w, c = 0;
            undo_take();
            for (w = 0; w < 16u; w++)
                if ((ui.wheld >> w) & 1u)
                    c += lock_clear(plk[song.pat % NPAT], t, (ui.bar * 16u + w) % NSTEP, 1);
            ui.wkeep |= ui.wheld;
            ui_say(c ? "LOCKS CLEARED" : "NO LOCKS THERE");
            break;
        }
        ui.lockmode = ui.lockmode == LM_LOCK ? LM_OFF : LM_LOCK;
        ui.wheld = ui.wkeep = ui.sedit = 0;
        ui_say(ui.lockmode ? "P-LOCK ON" : "P-LOCK OFF");
        break;
    case BK_NUDGE:                               /* NUDGE (SHIFT: follow) */
        if (ui.shift) { ui.follow ^= 1u; ui_say(ui.follow ? "FOLLOW ON" : "FOLLOW OFF"); break; }
        if (ui.lockmode == LM_NUDGE && ui.wheld) {   /* steps held: on time again */
            uint32_t w;
            undo_take();
            for (w = 0; w < 16u; w++)
                if ((ui.wheld >> w) & 1u) {
                    uint8_t *r = &PAT->r[t][(ui.bar * 16u + w) % NSTEP];
                    *r = R_MAKE(R_SUB(*r), 0);
                }
            ui.wkeep |= ui.wheld;
            ui_say("ON THE GRID");
            break;
        }
        ui.lockmode = ui.lockmode == LM_NUDGE ? LM_OFF : LM_NUDGE;
        ui.wheld = ui.wkeep = ui.sedit = 0;
        ui_say(ui.lockmode ? "NUDGE ON" : "NUDGE OFF");
        break;
    case BK_FILL:                                /* FILL while held (SHIFT: tap tempo) */
        if (!ui.shift) {
            song.fill = 1;
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
/* the white keys on STORE, PATTERNS and SONG: no sounds there, the page's own choices */
static void page_key(uint32_t w, int down)
{
    char m[28];
    if (ui.page == PG_SAVE) {                    /* key k: slot k of the sixteen KNOB 1 is in (1..16, 17..32, 33..36) */
        w += ui.slot / 16u * 16u;
        if (down && w < NSLOT) {
            ui.slot = (uint8_t)w;
            str_cpy(m, "SLOT ", sizeof m);
            fmt_u(m + str_len(m), w + 1u);
            str_cat(m, ui.used[w] ? " (IN USE)" : " (EMPTY)", sizeof m);
            ui_say(m);
        }
        return;
    }
    if (ui.page == PG_PAT) {                     /* key k: pattern k alone; a second key while one is held: the chain
                                                  * from the lower to the higher (2 and 5: 2 3 4 5) */
        if (!down) {
            if (ui.pkey == w + 1u)
                ui.pkey = 0;
            return;
        }
        if (ui.pkey && ui.pkey != w + 1u) {
            uint32_t a = ui.pkey - 1u, lo = a < w ? a : w, hi = a < w ? w : a;
            song.chain0 = (uint8_t)lo;
            song.chain = (uint8_t)(hi - lo + 1u);
            if (song.playing) {
                if (song.pat < lo || song.pat > hi)
                    song.pat_next = (uint8_t)lo;  /* (from the chain's first at the pattern's end) */
                else
                    song.pat_next = 0xFF;
            } else {
                song.pat = (uint8_t)lo;
                song.pat_next = 0xFF;
            }
            str_cpy(m, "CHAIN ", sizeof m);
            fmt_u(m + str_len(m), lo + 1u);
            str_cat(m, "-", sizeof m);
            fmt_u(m + str_len(m), hi + 1u);
            ui_say(m);
            return;
        }
        ui.pkey = (uint8_t)(w + 1u);
        song.chain = 1;
        pset(P_PAT, (int32_t)w + 1);             /* (playing: cued for the pattern's end) */
        song.chain0 = (uint8_t)w;
        ui_say(song.playing ? "PATTERN CUED" : "PATTERN");
        return;
    }
    if (ui.page == PG_SONG) {                    /* key k after key k: the song's rows in that order (the first key on
                                                  * this page starts a new song) */
        if (!down)
            return;
        if (ui.sfresh || !song.slen) {
            ui.sfresh = 0;
            song.slen = 0;
            song.srow = song.srep = 0;
        }
        if (song.slen >= NSONG) {
            ui_say("SONG FULL (32 ROWS)");
            return;
        }
        srows[song.slen].pat = (uint8_t)w;
        srows[song.slen].rep = 1;
        ui.scur = song.slen;
        song.slen++;
        song.smode = 1;
        str_cpy(m, "ROW ", sizeof m);
        fmt_u(m + str_len(m), song.slen);
        str_cat(m, ": P", sizeof m);
        fmt_u(m + str_len(m), w + 1u);
        ui_say(m);
        return;
    }
    (void)w;                                     /* (NEW PATCH: the keys do nothing) */
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
    if (!ui.shift && (ui.page == PG_SAVE || ui.page == PG_NEW || ui.page == PG_PAT || ui.page == PG_SONG)) {
        page_key(w, down);                       /* (STORE: the slots; PATTERNS: patterns and chains; SONG: its rows) */
        return;
    }
    if (ui.page != PG_SEQ && !ui.lockmode && !ui.shift) {   /* off SEQ (LIVE and every other page): the eight sounds at once,
                                                  * D4..G5 accented; REC + PLAY writes them */
        if (down) {
            ev_push(EV_HIT, w % 8u, w < 8u ? ui.lvel : 127u | S_ACC);
            if (ui.page == PG_VOICE || ui.page == PG_VOICE2 || ui.page == PG_SAMPLE || ui.page == PG_SAMPLE2 ||
                ui.page == PG_SND || ui.page == PG_LFO1 || ui.page == PG_LFO2 || ui.page == PG_FXS || ui.page == PG_ENV)
                sel_track((int32_t)(w % 8u));    /* (a sound's own pages: the sound played is the one shown) */
        }
        return;
    }
    /* SEQ: a tap sets an empty step at once and clears a set one when let go (so a hold can edit it: STEP EDIT) */
    idx = ui.bar * 16u + w;
    if (!down) {
        uint32_t b = 1u << w;
        if ((ui.wheld & b) && !(ui.wkeep & b) && idx < tlen(t)) {
            undo_burst(0x600u + t);
            PAT->s[t][idx] = 0;                  /* tapped, not held: cleared (its locks with it) */
            PAT->x[t][idx] = PAT->r[t][idx] = 0;
            lock_clear(plk[song.pat % NPAT], t, idx, 1);
        }
        ui.wheld &= ~b;
        ui.wkeep &= ~b;
        if (!ui.wheld)
            ui.sedit = 0;
        return;
    }
    if (ui.shift) {                              /* mute 1..8, solo 1..8 */
        if (w < 8u) song.mute ^= (uint8_t)(1u << w);
        else song.solo ^= (uint8_t)(1u << (w - 8u));
        return;
    }
    if (idx >= tlen(t)) {
        ui_say("PAST THE LENGTH");
        return;
    }
    s = &PAT->s[t][idx];
    ui.cur = (uint8_t)idx;
    if (ui.acc_mode) {                           /* (accent edit: at once) */
        undo_burst(0x600u + t);
        *s = *s ? (uint8_t)(*s ^ S_ACC) : (uint8_t)(100u | S_ACC);
        return;
    }
    ui.wheld |= 1u << w;
    ui.wdown[w] = fm1_ms;
    if (!*s) {                                   /* empty: set now (and kept when let go) */
        undo_burst(0x600u + t);
        *s = 100u;
        PAT->x[t][idx] = PAT->r[t][idx] = 0;
        lock_clear(plk[song.pat % NPAT], t, idx, 1);
        ui.wkeep |= 1u << w;                     /* (set silently: the step sounds when the pattern plays) */
    }
}
static void oct_key(int up)
{
    uint32_t t = song.sel % NTRK;
    if (ui.page == PG_SAVE || ui.page == PG_NEW) {   /* OCT+ saves into the slot chosen, at once; OCT- goes back */
        if (!up) { go_page(ui.back == PG_SAVE || ui.back == PG_NEW ? PG_SEQ : ui.back); return; }
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
                {
                    PAT->s[t][b0 + 16u + i] = PAT->s[t][b0 + i];
                    PAT->x[t][b0 + 16u + i] = PAT->x[t][b0 + i];
                    PAT->r[t][b0 + 16u + i] = PAT->r[t][b0 + i];
                }
                lock_dup(plk[song.pat % NPAT], t, b0, b0 + 16u, 16u);
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
    }
    if (ui.page == PG_SEQ || (ui.lockmode && ui.page != PG_NR)) {   /* (SEQ, P-LOCK, NUDGE: the bar the keys show) */
        ui.follow = 0;
        if (up ? ui.bar + 1u < nbars(t) : ui.bar > 0u)
            ui.bar = (uint8_t)(ui.bar + (up ? 1 : -1));
        else
            ui_say(up ? "LAST BAR" : "FIRST BAR");
        return;
    }
    sel_track((int32_t)t + (up ? 1 : -1));
}

/* ---- the CC map: a controller from MIDI IN onto a parameter (of the sound it was learnt on) */
static int cc_ok(uint32_t id)                    /* a parameter a controller may move (no store, no editing of steps) */
{
    pdesc_t d = PD[id % P_COUNT];
    if (!id || id >= P_COUNT || d.max <= d.min)
        return 0;
    switch (id) {
    case P_CLICK: case P_MIDICH: case P_THEME: case P_VERSION: case P_LEN: case P_SVEL: case P_ROT: case P_EUCLID:
    case P_PLEN: case P_COPY: case P_SACC: case P_PCOPY: case P_PPASTE: case P_SLOT: case P_SWHAT: case P_LWHAT:
    case P_LOAD: case P_QGRID: case P_QROUND: case P_EVEL: case P_EPROB: case P_EPAN: case P_ESUB: case P_EMICRO:
    case P_CLKSRC: case P_CLKOUT: case P_LEARN: case P_SROW: case P_SLEN: case P_SEND: case P_SPAT: case P_SREP:
    case P_NLEN: case P_NSPD: case P_NBPM: case P_NGO: case P_NKIT: case P_LSEL: case P_LDEL:
    case P_TSEL: case P_TCLR: case P_TALL: case P_PCLR:
        return 0;
    }
    return 1;
}
static int sound_param(uint32_t id)              /* a parameter of the selected sound (a map keeps which sound) */
{
    return lockable(id) || (id >= P_L1DEST && id <= P_L2DEPTH) || id == P_OCH || id == P_ONOTE || id == P_SPEED ||
           id == P_TMUTE || id == P_TSOLO;
}
static void cc_name(char *b, uint32_t pid, uint32_t trk, uint32_t n)
{
    uint8_t sv = song.sel;
    if (trk < NTRK) {
        song.sel = (uint8_t)trk;                 /* (the synth's labels are the sound's kind's) */
        str_cpy(b, TRK_SHORT[trk], n);
        str_cat(b, " ", n);
    } else {
        b[0] = 0;
    }
    str_cat(b, pdesc_of(pid).label, n);
    song.sel = sv;
}
static void cc_learn_arm(uint32_t id)            /* LEARN: the knob turned names the parameter */
{
    char m[28];
    if (id >= P_MIX0 && id <= P_MPAN3) {         /* (the mixer's strips: the sound's own level / pan) */
        ui.ltrk = (uint8_t)((ui.bank * 4u + (id - P_MIX0) % 4u) % NTRK);
        id = id <= P_MIX3 ? P_LEVEL : P_PAN;
    } else {
        ui.ltrk = sound_param(id) ? song.sel : 0xFFu;
    }
    if (!cc_ok(id)) {
        ui_say("CAN'T BE LEARNT");
        return;
    }
    ui.lpid = (uint8_t)id;
    ui.learn = 2;
    cc_name(m, id, ui.ltrk, sizeof m);
    str_cat(m, ": SEND CC", sizeof m);
    ui_say(m);
}
static void cc_in(uint32_t ch, uint32_t cc, uint32_t val)
{
    uint32_t i;
    char m[28];
    if (ui.learn == 2) {                         /* LEARN armed: this controller, that parameter */
        ccmap_t *f = 0;
        for (i = 0; i < NMAP && !f; i++)
            if (ccmap[i].pid && ccmap[i].ch == ch && ccmap[i].cc == cc)
                f = &ccmap[i];
        cc_pack();
        for (i = 0; i < NMAP && !f; i++)
            if (!ccmap[i].pid)
                f = &ccmap[i];
        if (!f) {
            ui_say("CC MAP FULL (32)");
            ui.learn = 1;
            return;
        }
        f->ch = (uint8_t)ch; f->cc = (uint8_t)cc; f->pid = ui.lpid; f->trk = ui.ltrk;
        ui.lsel = (uint8_t)(f - ccmap);
        settings_save();
        ui.learn = 1;
        str_cpy(m, "CC", sizeof m);
        fmt_u(m + 2, cc);
        str_cat(m, " > ", sizeof m);
        cc_name(m + str_len(m), ui.lpid, ui.ltrk, sizeof m - str_len(m));
        ui_say(m);
        return;
    }
    for (i = 0; i < NMAP; i++) {
        const ccmap_t *c = &ccmap[i];
        if (c->pid && c->ch == ch && c->cc == cc && cc_ok(c->pid)) {
            uint8_t sv = song.sel, ws = ui.lockmode;
            pdesc_t d;
            if (c->trk < NTRK)
                song.sel = c->trk;
            ui.lockmode = LM_OFF;                /* (the sound's own value, not a step's) */
            d = pdesc_of(c->pid);
            pset(c->pid, d.min + ((int32_t)val * (d.max - d.min) + 63) / 127);
            ui.lockmode = ws;
            song.sel = sv;
        }
    }
}

static void ui_input(void)
{
    uint32_t p = io_pressed(), held = io_held(), keys = io_keys(), released = ui.held_prev & ~held, kd, ku, k, b;
    int32_t e[NE], any = 0;
    uint32_t glo = (held >> B_GLO) & 1u, stepmode;
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
    if (ui.restoring && fm1_ms - ui.rs_t > 2500u) {   /* the restore is over (nothing for 2.5 s): the patch in, the GUI */
        uint32_t s0 = ui.used[ui.patch % NSLOT] ? ui.patch % NSLOT : 0u;
        char m[28];
        ui.restoring = 0;
        if (ui.used[s0])
            proj_load(s0, SW_ALL);
        undo.n_undo = undo.n_redo = 0;
        str_cpy(m, "PATCHES RESTORED: ", sizeof m);
        fmt_u(m + str_len(m), ui.rs_done);
        ui_say(m);
        ui.force = 1;
        ui.logo = 0;
    }
    if (ui.restoring)                            /* (meanwhile the panel waits: what was pressed is dropped) */
        return;
    while (cc_r != cc_w) {                       /* MIDI IN controllers: the CC map, LEARN */
        uint32_t c = ccq[cc_r % CCQ];
        RING_PUBLISH();
        cc_r++;
        cc_in(c & 15u, (c >> 8) & 0x7Fu, (c >> 16) & 0x7Fu);
    }
    if (p & (1u << B_PLAY)) {
        if (ui.shift) seq_restart();
        else seq_play(!song.playing && !xc.arm);
        if (!song.playing && song.clk_src == CK_EXT && (seq_start || xc.arm))
            ui_say("WAITING FOR CLOCK");
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
    if (p & (1u << B_HOME)) {                    /* HOME: tapped the KIT, held SYSTEM (the version, the memory, about) */
        ui.home_t0 = fm1_ms;
        ui.home_long = 0;
    }
    if ((held >> B_HOME) & 1u && !ui.home_long && fm1_ms - ui.home_t0 >= HOLD_MS) {
        ui.home_long = 1;
        go_page(PG_INFO);
    }
    if ((released >> B_HOME) & 1u && !ui.home_long)   /* (tapped again: MIDI, and back) */
        go_page(ui.page == PG_KIT ? PG_MIDI : ui.page == PG_MIDI ? PG_LEARN : PG_KIT);
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
            else if (w == BK_FILL)               /* FILL: let go */
                song.fill = 0;
        } else {
            white_key(w, (kd & bit) != 0u);
        }
    }
    stepmode = (ui.page == PG_SEQ || ui.lockmode) && ui.page != PG_NR;
    /* the knobs (a knob turned while a step is held: STEP EDIT at once; P-LOCK: the step's lock) */
    if (stepmode && ui.wheld && !ui.shift && (e[EN_K1] | e[EN_K2] | e[EN_K3] | e[EN_K4])) {
        uint32_t w;
        for (w = 0; w < 16u; w++)
            if (((ui.wheld >> w) & 1u) && PAT->s[song.sel][(ui.bar * 16u + w) % NSTEP])
                ui.sedit = (uint16_t)(ui.sedit | 1u << w);
        ui.wkeep = (uint16_t)(ui.wkeep | ui.sedit);
    }
    if (e[EN_ALGO]) {                            /* ALGORITHM: a new sound, rolled (its own transients); SHIFT: the
                                                  * sample layer's next / previous sample, the synth layer kept (SEQ:
                                                  * a random pattern for the track, turned back: the track doubled) */
        if (ui.shift && ui.page == PG_SEQ) {
            undo_take();
            if (e[EN_ALGO] > 0) { random_track(song.sel); ui_say("RANDOM"); }
            else double_track(song.sel);
        } else if (ui.shift) {
            sound_t *s = TS;
            char m[24];
            int32_t n = TR_N - TR_SMP0, k = s->smp >= TR_SMP0 ? s->smp - TR_SMP0 : (e[EN_ALGO] > 0 ? -1 : 0);
            undo_burst(0xA00u + song.sel);
            s->smp = (uint8_t)(TR_SMP0 + ((k + e[EN_ALGO]) % n + n) % n);
            smp_name(m, s->smp, song.sel);
            ui_say(m);
        } else {
            undo_burst(0x700u + song.sel);
            sound_preset(song.sel, 1u + rng() % 255u);
            ev_push(EV_HIT, song.sel, 100);
            ui_say("NEW SOUND");
        }
    }
    if (e[EN_SELECT]) {                          /* SELECT: the sound 1..8; SHIFT: the pattern */
        if (ui.shift) pset(P_PAT, pget(P_PAT) + e[EN_SELECT]);
        else sel_track((int32_t)song.sel + e[EN_SELECT]);
    }
    if (e[EN_PRESET])                            /* PRESETS: the stored patches, loaded as STORE's KNOB 3 says (KIT+PAT) */
        patch_turn(e[EN_PRESET], ui.lwhat);
    for (k = 0; k < 4u; k++) {
        uint32_t id = knob_id(k);
        int32_t d = e[EN_K1 + k];
        if (!d || !id)
            continue;
        ui.hot = (uint8_t)(k + 1u);
        ui.hot_t = fm1_ms | 1u;
        if (ui.learn && id != P_LEARN)           /* LEARN: this knob's parameter, for the next CC */
            cc_learn_arm(id);
        if (ui.lockmode == LM_LOCK && ui.page != PG_SEQ && ui.sedit && lockable(id)) {   /* P-LOCK: the steps held */
            uint32_t w, full = 0;
            pdesc_t pd = pdesc_of(id);
            char m[28];
            undo_burst(0xC00u + id);
            for (w = 0; w < 16u; w++) {
                uint32_t i = (ui.bar * 16u + w) % NSTEP;
                const lock_t *l = lock_find(plk[song.pat % NPAT], song.sel, i, id);
                int32_t cur;
                if (!((ui.sedit >> w) & 1u) || !PAT->s[song.sel][i])
                    continue;
                if (l) cur = lock_val(id, l->val);
                else { uint32_t sv = ui.lockmode; ui.lockmode = LM_OFF; cur = pget(id); ui.lockmode = (uint8_t)sv; }
                full |= (uint32_t)(lock_set(plk[song.pat % NPAT], song.sel, i, id, clamp(cur + d, pd.min, pd.max)) != 0);
            }
            str_cpy(m, full ? "LOCKS FULL (128)" : "LOCK ", sizeof m);
            if (!full) str_cat(m, pd.label, sizeof m);
            ui_say(m);
            continue;
        }
        if (ui.lockmode == LM_LOCK && song.rec && song.playing && !song.erase && !ui.wheld && lockable(id)) {
            uint32_t t = song.sel % NTRK, i = tpos(t);   /* MOTION REC: the step playing, if it has a hit */
            if (PAT->s[t][i]) {
                const lock_t *l = lock_find(plk[song.pat % NPAT], t, i, id);
                pdesc_t pd = pdesc_of(id);
                uint32_t sv = ui.lockmode;
                int32_t cur;
                ui.lockmode = LM_OFF;
                cur = l ? lock_val(id, l->val) : pget(id);
                ui.lockmode = (uint8_t)sv;
                if (lock_set(plk[song.pat % NPAT], t, i, id, clamp(cur + d, pd.min, pd.max)))
                    ui_say("LOCKS FULL (128)");
            }
            continue;
        }
        if ((id >= P_V0 && id <= P_DRV) || (id >= P_SSTART && id <= P_SBEND) || (id >= P_EATK && id <= P_EGATE))
            undo_burst(0x800u + song.sel * 64u + id);
        pset(id, pget(id) + d);
    }
    if (stepmode && ui.wheld && !ui.shift) {     /* a step held long: STEP EDIT (P-LOCK, NUDGE) on the steps held */
        uint32_t w, m = 0;
        for (w = 0; w < 16u; w++)
            if (((ui.wheld >> w) & 1u) && fm1_ms - ui.wdown[w] >= HOLD_MS && PAT->s[song.sel][(ui.bar * 16u + w) % NSTEP])
                m |= 1u << w;
        if (m) {
            ui.sedit = (uint16_t)(ui.sedit | m);
            ui.wkeep = (uint16_t)(ui.wkeep | m);   /* (held: kept when let go) */
        }
    }
    if (!stepmode)
        ui.wheld = ui.wkeep = ui.sedit = 0;
    if (ui.follow && song.playing && ui.page != PG_NR)   /* FOLLOW: the bar playing */
        ui.bar = (uint8_t)(tpos(song.sel) / 16u);
    if (ui.hot_t && fm1_ms - ui.hot_t > 1200u)
        ui.hot_t = 0;
    if (ui.msg_t && fm1_ms - ui.msg_t > 1600u)
        ui.msg_t = 0;
}

static void ui_leds(void)
{
    uint32_t k, b, t = song.sel % NTRK, ps = tpos(t), blink = (fm1_ms / 250u) & 1u;
    uint8_t flash = hit_flash;
    hit_flash = 0;
    for (k = 0; k < NTRK; k++)
        if ((flash >> k) & 1u)
            ui.lit_t[k] = fm1_ms | 1u;
    for (b = 0; b < NB; b++) {
        int on = (b == PAGE_BTN[ui.page % PG_N] && b != B_REC) || (b == B_ENV && ui.page == PG_ENV) ||
                 (b == B_PLAY && song.playing) || (b == B_GLO && ui.shift) ||
                 (b == B_REC && song.rec && (!song.erase || blink)) ||
                 (b == B_OCTDN && (ui.page == PG_SEQ || ui.lockmode) && ui.bar > 0u) ||
                 (b == B_OCTUP && (ui.page == PG_SEQ || ui.lockmode) && ui.bar + 1u < nbars(t)) ||
                 (b == B_OCTDN && ui.page == PG_MIX && !ui.bank) || (b == B_OCTUP && ui.page == PG_MIX && ui.bank);
        io_led_button(b, on);
    }
    for (k = 0; k < 27u; k++) {
        uint32_t w = key_place(k);
        int on = 0;
        if (key_black(k)) {
            on = (w == BK_MUTE && ((song.mute >> t) & 1u)) || (w == BK_SOLO && ((song.solo >> t) & 1u)) ||
                 (w == BK_PLOCK && (ui.shift ? ui.acc_mode : ui.lockmode == LM_LOCK)) ||
                 (w == BK_NUDGE && (ui.shift ? ui.follow : ui.lockmode == LM_NUDGE)) ||
                 (w == BK_FILL && (ui.shift ? song.playing && song.pstep % 4u == 0u && song.pos < FS / 20u : song.fill));
        } else if (!ui.shift && ui.page == PG_SAVE) {   /* the slots: lit in use, the one chosen blinking */
            uint32_t sl = ui.slot / 16u * 16u + w;
            on = sl < NSLOT && (sl == ui.slot ? blink : ui.used[sl]);
        } else if (!ui.shift && ui.page == PG_PAT) {    /* the patterns: lit in the chain, the one playing blinking */
            on = (w + NPAT - song.chain0) % NPAT < song.chain;
            if (w == song.pat)
                on = blink;
        } else if (!ui.shift && ui.page == PG_SONG) {   /* the patterns the song uses, the one playing blinking */
            uint32_t r;
            for (r = 0; r < song.slen && r < NSONG && !on; r++)
                on = srows[r].pat == w;
            if (song.playing && song.smode && w == song.pat)
                on = blink;
        } else if (!ui.shift && ui.page == PG_NEW) {
            on = 0;
        } else if (ui.page == PG_NR) {
            on = w < 8u ? (((nr.held | nr.latched | flash) >> w) & 1u) != 0u : w - 8u == nr.rate;
        } else if (ui.shift) {
            on = w < 8u ? !((song.mute >> w) & 1u) : ((song.solo >> (w - 8u)) & 1u) != 0u;
        } else if (ui.page != PG_SEQ && !ui.lockmode) {   /* (the steps on SEQ, P-LOCK, NUDGE; elsewhere the pads, lit as hit) */
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
    const char *mode = song.fill ? "FILL" : ui.lockmode == LM_LOCK ? (song.rec && song.playing ? "LOCK REC" : "P-LOCK") :
                       ui.lockmode == LM_NUDGE ? "NUDGE" : ui.learn ? "LEARN" : song.smode && song.slen ? "SONG" : 0;
    uint32_t sig = t * 7u + song.playing * 131u + song.bpm * 1009u + (ui.msg_t ? hsh(5u, ui.msg) : 0u) +
                   ui.shift * 7333u + song.rec * 191u + song.erase * 4099u + blink * 37u + song.pat * 7919u +
                   (song.pat_next < NPAT ? song.pat_next * 104729u + 3u : 0u) + ui.bar * 271u + nbars(t) * 6151u +
                   (mode ? hsh(9u, mode) : 0u) + song.clk_src * 5u + (song.smode ? song.srow * 61u + 17u : 0u) + ui.loaded * 92821u;
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
        b[0] = 'P';                              /* the pattern (and the one cued), the bar shown, the tempo; a mode */
        fmt_u(b + 1, song.pat + 1u);
        if (song.pat_next < NPAT) {
            str_cat(b, ">", sizeof b);
            fmt_u(b + str_len(b), song.pat_next + 1u);
        }
        str_cat(b, " B", sizeof b);
        fmt_u(b + str_len(b), ui.bar + 1u);
        str_cat(b, "/", sizeof b);
        fmt_u(b + str_len(b), nbars(t));
        if (mode)
            str_cpy(b, mode, sizeof b);
        cv_text_r(170, CAP_IN(S, H_HEAD), &AF_S, b, mode ? T_ACCENT : T_MID, T_BG);
        if (ui.loaded) {                         /* the patch loaded: its number, red (where the name leaves room) */
            int32_t xr = 170 - (int32_t)text_w(&AF_S, b) - 6;
            b[0] = '#';
            fmt_u(b + 1, ui.loaded);
            if (xr - (int32_t)text_w(&AF_S, b) > x + (song.rec ? 40 : 4))
                cv_text_r(xr, CAP_IN(S, H_HEAD), &AF_S, b, C_REC, T_BG);
        }
        fmt_u(b, song.bpm);
        cv_text_r(220, CAP_IN(M, H_HEAD), &AF_M, b, song.clk_src == CK_EXT ? T_ACCENT : T_THEME, T_BG);
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
    if (id && lock_view(id) && lock_find(plk[song.pat % NPAT], song.sel, held_first(), id))
        hot = 1;                                 /* (a step's lock: its value lit) */
    if (id) {
        pformat(id, val, &unit);
        ratio = d.max > d.min ? (pget(id) - d.min) * 1000 / (d.max - d.min) : -1;
        if (d.fmt == FM_NAME || d.fmt == FM_SMP || d.fmt == FM_PAT || d.fmt == FM_LOAD || id == P_ROT)
            ratio = -1;
    }
    str_cpy(key, id ? d.label : "", sizeof key);
    str_cat(key, val, sizeof key);
    str_cat(key, unit, sizeof key);
    sig = hsh(c * 31u + (uint32_t)hot * 7u + (uint32_t)(ratio + 2) * 101u + ui.shift * 5u + id * 977u + ui.lockmode * 13u, key);
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
    uint16_t lb[NTRK] = {0};                     /* the bar's steps with locks, a track's in bits */
    for (i = 0; i < NLOCK; i++) {
        const lock_t *l = &plk[song.pat % NPAT][i];
        uint32_t lt = (l->key >> 6) % NTRK, ls = l->key & 63u;
        if (l->pid && ls / 16u == ui.bar % nbars(lt))
            lb[lt] = (uint16_t)(lb[lt] | 1u << (ls % 16u));
    }
    for (t = 0; t < NTRK; t++) {
        uint32_t len = tlen(t), bar = ui.bar % nbars(t), ps = tpos(t);
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
            uint8_t xx = idx < len ? pat[song.pat % NPAT].x[t][idx] : 0u, rr = idx < len ? pat[song.pat % NPAT].r[t][idx] : 0u;
            uint16_t sc = idx >= len ? bg : s ? (s & S_ACC ? ux_mix(c, T_TEXT, 45) : c) : T_LINE;
            if (s && X_COND(xx))                 /* (a step that plays now and then: paler) */
                sc = ux_mix(sc, bg, 55);
            if (song.playing && idx == ps)
                sc = s ? T_TEXT : T_ACCENT;
            if (t == song.sel && ((ui.sedit >> i) & 1u))
                cv_rrect(x - 1, y, 12, 12, 3, T_ACCENT, bg);   /* (STEP EDIT: the steps held) */
            if (s && R_MICRO(rr))                /* (NUDGE: the cell moved early / late) */
                x += R_MICRO(rr) * 2 / 15 + (R_MICRO(rr) > 0 ? 1 : -1);
            cv_rrect(x, y + 1, 10, 10, 2, sc, bg);
            if (s && ((lb[t] >> i) & 1u))        /* (a step with locks: a notch top left) */
                cv_rect(x + 1, y + 2, 3, 2, T_BG);
            if (s && idx < len && R_SUB(rr)) {      /* (substeps: the cell cut in as many) */
                uint32_t k, n = R_SUB(rr) + 1u;
                for (k = 1; k < n; k++)
                    cv_rect(x + (int32_t)(k * 10u / n), y + 1, 1, 10, bg);
            }
            if (s && X_PAN(xx))                  /* (its own pan: a dot left / right / centre) */
                cv_rect(x + 4 + ((int32_t)X_PAN(xx) - 4), y + 5, 2, 2, T_BG);
            if (t == song.sel && idx == ui.cur && ui.page == PG_SEQ)
                cv_rect(x + 2, y + 12, 6, 1, T_ACCENT);
        }
    }
    {                                            /* the bars of the selected track: a dot each, the one shown red, the one
                                                  * playing ringed */
        uint32_t n = nbars(song.sel), b, pb = tpos(song.sel) / 16u;
        int32_t w = 24, g = 8, x0 = 120 - ((int32_t)n * w + ((int32_t)n - 1) * g) / 2;
        for (b = 0; b < n; b++) {                /* (shown: red; playing: light; the rest: dark) */
            int32_t x = x0 + (int32_t)b * (w + g);
            uint16_t c = b == ui.bar % n ? C_REC : song.playing && b == pb ? ux_mix(T_SURF, T_TEXT, 70) : T_RAISE;
            cv_rrect(x, 115, w, 6, 3, c, T_SURF);
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
static uint16_t j0_col(uint32_t at, uint32_t start)   /* the wave before START dim */
{
    return at < start ? T_DIM : TRK_COL[song.sel % NTRK];
}
static void g_sample(void)                       /* SAMPLE LAYER: the transient's wave, its decay, where it starts */
{
    const sound_t *s = TS;
    int32_t i, mid = 64, env = 32767, k = lx_k(20u + s->sdec * 50u / 127u);
    uint32_t mach = s->smp >= TR_MACH0, n = mach ? MACH_TRANS_LEN : LXR_TRANS_LEN, st = n * s->sstart / 128u;
    const int8_t *tr;
    char b[28];
    if (s->smp < TR_SMP0) {
        cv_text_in(3, 40, GW, &AF_M, s->smp == TR_SNAP ? "SNAP: A PITCH SNAP" : "NO SAMPLE LAYER", T_MID, T_SURF);
        cv_text_in(3, 70, GW, &AF_S, s->smp == TR_SNAP ? "LEVEL = AMOUNT, PITCH = SPEED" : "KNOB 1: CHOOSE ONE", T_DIM, T_SURF);
        return;
    }
    tr = mach ? MACH_TRANS[song.sel % NTRK][(s->smp - TR_MACH0) % MACH_TRANS_N]
         : s->smp >= TR_OWN0 ? OWN_TRANS[song.sel % NTRK][(s->smp - TR_OWN0) % OWN_TRANS_N]
         : LXR_TRANS[(s->smp - TR_SMP0) % LXR_TRANS_N];
    for (i = 0; i < 220; i++) {                  /* min / max a column, the decay over it (from START) */
        uint32_t a = (uint32_t)i * n / 220u, e = (uint32_t)(i + 1) * n / 220u, j;
        int32_t lo = 0, hi = 0;
        for (j = a; j < e; j++) {
            int32_t v = tr[j];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            if (s->sdec < 127u && j >= st) {     /* (the decay as lx_render has it: a 44.1 kHz sample at a time) */
                env = lx_dec(env, k);
                if (mach)
                    env = lx_dec(env, k);
            }
        }
        lo = lo * env / 32767;                   /* (the shape; LEVEL is on its card) */
        hi = hi * env / 32767;
        cv_rect(10 + i, mid - hi * 48 / 128, 1, 1 + (hi - lo) * 48 / 128, j0_col(a, st));
    }
    smp_name(b, s->smp, song.sel);
    str_cat(b, mach ? "  CLASSIC MACHINE" : s->smp >= TR_OWN0 ? "  OWN TRANSIENT" : "  LXR TRANSIENT", sizeof b);
    g_label(10, 104, b, T_MID);
}
static void g_mixer(void)                        /* MIXER: the bank's four strips, each under its card */
{
    uint32_t c, t;
    for (c = 0; c < 4u; c++) {
        const sound_t *s;
        int32_t x, h, pk, on;
        uint16_t col;
        t = (ui.bank * 4u + c) % NTRK;
        s = &snd[t];
        x = CARD_X(c);
        h = s->level * 76 / 127;
        pk = clamp(peak[t] * 76 / 20000, 0, 76);
        on = trk_audible(t);
        col = on ? TRK_COL[t] : T_DIM;
        cv_rrect(x + 1, 3, CARD_W - 2, 114, 4, T_RAISE, T_SURF);
        cv_rrect(x + 14, 8, 10, 78, 3, T_LINE, T_RAISE);              /* the level */
        cv_rrect(x + 14, 86 - h, 10, h < 3 ? 3 : h, 3, col, T_LINE);
        cv_rect(x + 30, 8, 8, 78, T_SURF);                              /* the meter: this sound's output */
        cv_rect(x + 30, 86 - pk, 8, pk, on ? T_THEME : T_DIM);
        if (ui.mixpan) {                         /* PAN: a bar from the centre, the strip's colour */
            int32_t pw = s->pan * 20 / 64;
            cv_rect(x + 6, 94, 45, 1, T_LINE);
            cv_rect(pw < 0 ? x + 28 + pw : x + 28, 91, pw < 0 ? -pw + 1 : pw + 1, 7, col);
        } else {
            cv_rect(x + 28 + s->pan * 20 / 64, 92, 2, 5, T_MID);
        }
        cv_text_in(x, 100, CARD_W, &AF_S, TRK_SHORT[t], on ? T_TEXT : T_DIM, T_RAISE);
        if ((song.mute >> t) & 1u) cv_text_fit(x + 6, 100, &AF_S, "M", C_REC, T_RAISE, 10);
        if ((song.solo >> t) & 1u) cv_text_fit(x + 44, 100, &AF_S, "S", T_ACCENT, T_RAISE, 10);
    }
    for (t = 0; t < NTRK; t++) {                 /* (every meter falls, the other bank's too) */
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
    g_label(12, 106, "K3 COPIES, K4 PASTES IT", T_DIM);
}
static void g_store(void)
{
    uint32_t i;
    for (i = 0; i < NSLOT; i++) {                /* four rows of nine; a bar under the ones in use; the white keys' sixteen
                                                  * a shade lighter; the patch loaded red */
        int32_t x = 8 + (int32_t)(i % 9u) * 25, y = 4 + (int32_t)(i / 9u) * 22;
        char b[12];
        int ld = ui.loaded == i + 1u, keys = i / 16u == ui.slot / 16u;
        uint16_t c = i == ui.slot ? T_THEME : keys ? T_RAISE : ux_mix(T_SURF, T_RAISE, 45);
        if (ld && i != ui.slot) {
            cv_rrect(x, y, 23, 19, 3, C_REC, T_SURF);
            cv_rrect(x + 2, y + 2, 19, 15, 2, c, C_REC);
        } else {
            cv_rrect(x, y, 23, 19, 3, c, T_SURF);
        }
        fmt_u(b, i + 1u);
        cv_text_in(x, y + CAP_IN(S, 16), 23, &AF_S, b, ld && i != ui.slot ? C_REC : i == ui.slot ? T_INK : ui.used[i] ? T_TEXT : T_DIM, c);
        if (ui.used[i])
            cv_rect(x + 7, y + 15, 9, 2, ld ? C_REC : i == ui.slot ? T_INK : T_THEME);
    }
    g_label(12, 96, ui.swhat == SW_KIT ? "OCT+ SAVES THE KIT" : ui.swhat == SW_PAT ? "OCT+ SAVES THE PATTERNS" :
            "OCT+ SAVES KIT + PATTERNS", T_MID);
    g_label(12, 109, ui.lwhat == SW_KIT ? "K4 + PRESETS LOAD THE KIT" : ui.lwhat == SW_PAT ? "K4 + PRESETS LOAD PATTERNS" :
            "K4 + PRESETS LOAD KIT + PAT", T_MID);
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
static void g_info(void)                         /* SYSTEM: the version, the memory, the battery, about */
{
    char b[48];
    uint32_t i, used = 0;
    int32_t bat = io_batt();
    for (i = 0; i < NSLOT; i++)
        used += ui.used[i];
    str_cpy(b, "LUXR " OC_VERSION, sizeof b);
#ifdef FELUCCA_ID
    str_cat(b, "  (" FELUCCA_ID ")", sizeof b);
#endif
    cv_text_on(10, 6, &AF_M, b, T_THEME, T_SURF);
    str_cpy(b, "PATCHES ", sizeof b);
    fmt_u(b + str_len(b), used);
    str_cat(b, " OF ", sizeof b);
    fmt_u(b + str_len(b), NSLOT);
    str_cat(b, ", ", sizeof b);
    fmt_u(b + str_len(b), (NSLOT - used) * (SLOT_SZ / 1024u));
    str_cat(b, " KB FREE", sizeof b);
    g_label(10, 30, b, T_TEXT);
    str_cpy(b, "AUDIO LOAD ", sizeof b);
    fmt_u(b + str_len(b), song.cpu_q8 * 100u / 256u);
    str_cat(b, " %", sizeof b);
    g_label(10, 44, b, T_TEXT);
    if (bat >= 0) {                              /* (a divider of 1:2 at 3.3 V assumed: not calibrated) */
        uint32_t mv = (uint32_t)bat * 6600u / 1023u;
        str_cpy(b, "BATTERY ~", sizeof b);
        fmt_u(b + str_len(b), mv / 1000u);
        str_cat(b, ".", sizeof b);
        fmt_u(b + str_len(b), mv / 100u % 10u);
        fmt_u(b + str_len(b), mv / 10u % 10u);
        str_cat(b, " V (RAW ", sizeof b);
        fmt_u(b + str_len(b), (uint32_t)bat);
        str_cat(b, ")", sizeof b);
    } else {
        str_cpy(b, "BATTERY: NOT READABLE", sizeof b);
    }
    g_label(10, 58, b, T_TEXT);
    g_label(10, 76, "LXR-STYLE DRUMS FOR THE M-VAVE FM-1", T_MID);
    g_label(10, 90, "MADE BY HOLODECK-AUDIOTOOLS.COM", T_ACCENT);
    g_label(10, 104, "BATTERY: ESTIMATE, NOT CALIBRATED", T_DIM);
}
static void g_midi(void)                         /* MIDI: the clock, each sound's note out, the CC map */
{
    char b[48];
    uint32_t i, n = 0, y;
    if (song.clk_src == CK_EXT) {
        int live = xc.seen && clk_now - (xc.seen & ~1u) < FS / 2u;
        str_cpy(b, live ? "CLOCK IN: " : "CLOCK IN: NONE", sizeof b);
        if (live) { fmt_u(b + str_len(b), song.bpm); str_cat(b, " BPM", sizeof b); }
        g_label(10, 6, b, live ? T_TEXT : C_REC);
    } else {
        str_cpy(b, "CLOCK: INTERNAL ", sizeof b);
        fmt_u(b + str_len(b), song.bpm);
        str_cat(b, " BPM", sizeof b);
        g_label(10, 6, b, T_TEXT);
    }
    g_label(10, 20, song.clk_out ? "CLOCK OUT: 24 PPQ, START / STOP" : "CLOCK OUT: OFF", song.clk_out ? T_MID : T_DIM);
    for (i = 0; i < NTRK; i++) {                 /* the sounds' channels */
        int32_t x = 10 + (int32_t)(i % 4u) * 56, yy = 38 + (int32_t)(i / 4u) * 14;
        str_cpy(b, TRK_SHORT[i], sizeof b);
        str_cat(b, " ", sizeof b);
        if (snd[i].och) fmt_u(b + str_len(b), snd[i].och);
        else str_cat(b, "-", sizeof b);
        cv_text_on(x, yy, &AF_S, b, i == song.sel ? T_THEME : snd[i].och ? T_TEXT : T_DIM, T_SURF);
    }
    for (i = 0, y = 70; i < NMAP; i++)
        if (ccmap[i].pid) {
            if (n < 3u) {
                str_cpy(b, "CC", sizeof b);
                fmt_u(b + 2, ccmap[i].cc);
                str_cat(b, " CH", sizeof b);
                fmt_u(b + str_len(b), ccmap[i].ch + 1u);
                str_cat(b, "  ", sizeof b);
                cc_name(b + str_len(b), ccmap[i].pid, ccmap[i].trk, sizeof b - str_len(b));
                g_label(10, (int32_t)y, b, T_MID);
                y += 14;
            }
            n++;
        }
    if (!n)
        g_label(10, 70, ui.learn ? "LEARN: TURN A KNOB, THEN SEND A CC" : "NO CC MAPPED (HOME AGAIN: CC LEARN)", ui.learn ? T_ACCENT : T_DIM);
    else if (n > 3u) {
        str_cpy(b, "+ ", sizeof b);
        fmt_u(b + 2, n - 3u);
        str_cat(b, " MORE", sizeof b);
        g_label(10, (int32_t)y, b, T_DIM);
    }
}
static void g_tracks(void)                       /* TRACKS: the eight, their notes, mute / solo; the one selected marked */
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        int32_t y = 4 + (int32_t)t * 14, sel = t == song.sel % NTRK, on = trk_audible(t), n = 0;
        uint16_t bg = sel ? T_RAISE : T_SURF;
        char b[8];
        for (i = 0; i < tlen(t); i++)
            n += PAT->s[t][i] != 0;
        if (sel)
            cv_rrect(6, y - 1, 228, 14, 3, T_RAISE, T_SURF);
        cv_rrect(10, y + 2, 8, 8, 2, on ? TRK_COL[t] : T_DIM, bg);
        cv_text_fit(24, y + 1, &AF_S, TRK_NAME[t], on ? T_TEXT : T_DIM, bg, 70);
        fmt_u(b, (uint32_t)n);
        str_cat(b, n == 1 ? " NOTE" : " NOTES", sizeof b);
        cv_text_fit(98, y + 1, &AF_S, b, n ? T_MID : T_DIM, bg, 70);
        cv_rrect(176, y + 1, 24, 11, 3, (song.mute >> t) & 1u ? C_REC : T_LINE, bg);
        cv_text_in(176, y + 1 + CAP_IN(S, 11), 24, &AF_S, "M", (song.mute >> t) & 1u ? T_INK : T_DIM, (song.mute >> t) & 1u ? C_REC : T_LINE);
        cv_rrect(204, y + 1, 24, 11, 3, (song.solo >> t) & 1u ? T_ACCENT : T_LINE, bg);
        cv_text_in(204, y + 1 + CAP_IN(S, 11), 24, &AF_S, "S", (song.solo >> t) & 1u ? T_INK : T_DIM, (song.solo >> t) & 1u ? T_ACCENT : T_LINE);
    }
}
static void g_learn(void)                        /* CC LEARN: every mapping, seven at a time, the one shown marked */
{
    uint32_t n = cc_count(), k, first = ui.lsel >= 3u ? ui.lsel - 3u : 0u;
    char b[48];
    cc_pack();
    if (!n) {
        g_label(12, 20, "NO CONTROLLER MAPPED YET", T_TEXT);
        g_label(12, 44, "K1 ON, TURN THE KNOB OF A PARAMETER", T_MID);
        g_label(12, 60, "(ANY PAGE), THEN MOVE A CONTROL ON", T_MID);
        g_label(12, 76, "YOUR CONTROLLER. UP TO 32.", T_MID);
        return;
    }
    if (first + 7u > n)
        first = n > 7u ? n - 7u : 0u;
    for (k = first; k < first + 7u && k < n; k++) {
        int32_t y = 4 + (int32_t)(k - first) * 15;
        int cur = k == ui.lsel;
        uint16_t bg = cur ? T_RAISE : T_SURF;
        if (cur)
            cv_rrect(6, y - 1, 216, 14, 3, T_RAISE, T_SURF);
        b[0] = (char)('0' + (k + 1u) / 10u);
        b[1] = (char)('0' + (k + 1u) % 10u);
        b[2] = 0;
        cv_text_on(10, y + 1, &AF_S, b, T_DIM, bg);
        str_cpy(b, "CC", sizeof b);
        fmt_u(b + 2, ccmap[k].cc);
        str_cat(b, " CH", sizeof b);
        fmt_u(b + str_len(b), ccmap[k].ch + 1u);
        cv_text_on(32, y + 1, &AF_S, b, cur ? T_THEME : T_MID, bg);
        cc_name(b, ccmap[k].pid, ccmap[k].trk, sizeof b);
        cv_text_fit(104, y + CAP_IN(S, 12), &AF_S, b, cur ? T_TEXT : T_MID, bg, 116);
    }
    if (n > 7u) {                                /* (a scroll bar on the right) */
        int32_t h = 104 * 7 / (int32_t)n, y = 4 + (104 - h) * (int32_t)first / (int32_t)(n - 7u);
        cv_rrect(226, 4, 4, 104, 2, T_LINE, T_SURF);
        cv_rrect(226, y, 4, h, 2, T_THEME, T_LINE);
    }
    str_cpy(b, "", sizeof b);
    fmt_u(b, n);
    str_cat(b, n == 1u ? " MAPPING  K2 CHOOSES, K3 DELETES" : " MAPPINGS  K2 CHOOSES, K3 DELETES", sizeof b);
    g_label(12, 108, b, T_DIM);
}
static void g_song(void)                         /* SONG: its rows (pattern, repeats), the one shown, the one playing */
{
    uint32_t k, n = song.slen ? song.slen : 1u, first = ui.scur >= 3u ? ui.scur - 3u : 0u;
    char b[32];
    if (first + 7u > n)
        first = n > 7u ? n - 7u : 0u;
    for (k = first; k < first + 7u && k < n; k++) {
        int32_t y = 4 + (int32_t)(k - first) * 15;
        int on = song.smode && song.playing && k == song.srow, cur = k == ui.scur;
        if (cur)
            cv_rrect(6, y - 1, 228, 14, 3, T_RAISE, T_SURF);
        b[0] = (char)('0' + (k + 1u) / 10u);
        b[1] = (char)('0' + (k + 1u) % 10u);
        b[2] = 0;
        cv_text_on(12, y + 1, &AF_S, b, T_MID, cur ? T_RAISE : T_SURF);
        str_cpy(b, "P", sizeof b);
        fmt_u(b + 1, srows[k].pat + 1u);
        cv_text_on(44, y + 1, &AF_S, b, on ? T_ACCENT : T_TEXT, cur ? T_RAISE : T_SURF);
        str_cpy(b, "x", sizeof b);
        fmt_u(b + 1, srows[k].rep ? srows[k].rep : 1u);
        cv_text_on(90, y + 1, &AF_S, b, on ? T_ACCENT : T_MID, cur ? T_RAISE : T_SURF);
        if (on) {
            str_cpy(b, "PLAYING ", sizeof b);
            fmt_u(b + str_len(b), song.srep + 1u);
            str_cat(b, "/", sizeof b);
            fmt_u(b + str_len(b), srows[k].rep ? srows[k].rep : 1u);
            cv_text_on(140, y + 1, &AF_S, b, T_ACCENT, cur ? T_RAISE : T_SURF);
        }
    }
    g_label(12, 108, song.smode ? (song.send ? "SONG ON: STOPS AT THE END" : "SONG ON: LOOPS") : "SONG OFF: PATTERNS AND CHAIN", song.smode ? T_THEME : T_DIM);
}
static void g_env(void)                          /* ENVELOPE: attack, decay, the gate, the release over two steps */
{
    const sound_t *s = TS;
    uint32_t tot = 2u * step_base(), i, an = s->atk ? 1u + (uint32_t)s->atk * s->atk * 441u / 640u : 1u;
    uint32_t gn = s->gate ? s->gate * step_base() / 4u : 0u, per = tot / 220u;
    int32_t amp = 32767, att = s->atk ? 0 : 32767, ka = lx_k(25u + s->p[1] * 85u / 127u), kr = lx_k(s->rel & 127u), prev = 0;
    uint16_t c = TRK_COL[song.sel % NTRK];
    char b[40];
    if (TRK_KIND[song.sel % NTRK] == K_HATC) ka = lx_k(30u + s->p[1] * 40u / 127u);
    else if (TRK_KIND[song.sel % NTRK] == K_HATO) ka = lx_k(55u + s->p[1] * 45u / 127u);
    else if (TRK_KIND[song.sel % NTRK] == K_CYM) ka = lx_k(80u + s->p[1] * 40u / 127u);
    else if (TRK_KIND[song.sel % NTRK] == K_CLAP) ka = lx_k(30u + s->p[1] * 80u / 127u);
    if (song.ddec && song.ddec != 64)
        ka = clamp(ka * 64 / song.ddec, 1, 65535);
    cv_rect(10, 90, 220, 1, T_LINE);
    if (gn && gn < tot)                          /* (the gate: a line) */
        cv_rect(10 + (int32_t)(gn * 220u / tot), 8, 1, 82, T_DIM);
    cv_rect(10 + 110, 86, 1, 5, T_LINE);         /* (a step) */
    for (i = 0; i < 220u; i++) {
        uint32_t k, t0 = i * per;
        int32_t a, y;
        for (k = 0; k < per; k++) {
            uint32_t tt = t0 + k;
            if (att < 32767) att = att + (int32_t)(32767u / an) < 32767 ? att + (int32_t)(32767u / an) : 32767;
            amp = lx_dec(amp, gn && tt >= gn ? kr : ka);
        }
        a = (amp * att) >> 15;
        y = 90 - a * 80 / 32767;
        if (i && (y - prev > 1 || prev - y > 1))
            cv_rect(10 + (int32_t)i, y < prev ? y : prev, 1, (y > prev ? y - prev : prev - y) + 1, c);
        cv_rect(10 + (int32_t)i, y, 1, 2, c);
        prev = y;
    }
    str_cpy(b, s->gate ? "GATE, THEN THE RELEASE (2 STEPS)" : "NO GATE: THE DECAY ALONE (2 STEPS)", sizeof b);
    g_label(10, 104, b, T_MID);
}
static void g_new(void)                          /* NEW PATCH: what a turn of KNOB 4 makes */
{
    char b[48], v[16];
    const char *u;
    pformat(P_NLEN, v, &u);
    str_cpy(b, "16 EMPTY PATTERNS, EACH ", sizeof b);
    str_cat(b, v, sizeof b);
    str_cat(b, u[0] ? " " : "", sizeof b);
    str_cat(b, u, sizeof b);
    g_label(12, 10, b, T_TEXT);
    str_cpy(b, "EVERY TRACK AT ", sizeof b);
    str_cat(b, N_SPEED[SPD_I(ui.nspd)], sizeof b);
    str_cat(b, ", ", sizeof b);
    fmt_u(b + str_len(b), (uint32_t)pget(P_NBPM));
    str_cat(b, " BPM", sizeof b);
    g_label(12, 26, b, T_TEXT);
    g_label(12, 42, ui.nkit ? "THE KIT PLAYING NOW STAYS" : "THE FACTORY KIT (SOUNDS, FX)", T_TEXT);
    g_label(12, 66, "KNOB 4 MAKES IT, PLAYBACK STOPS", T_ACCENT);
    g_label(12, 84, "NOT SAVED YET: STORE PAGE, OCT+", T_MID);
    g_label(12, 104, "SHIFT + F#3: UNDO", T_DIM);
}
static void g_mfx(void)                          /* MASTER: the filter's curve */
{
    int32_t i, v = song.mflt, prev = 0;
    cv_rect(10, 70, 220, 1, T_LINE);
    for (i = 0; i < 220; i++) {
        int32_t f = i * 255 / 219, c = v < 0 ? 255 + v * 3 : v * 3, h;
        int32_t dd = v < 0 ? c - f : f - c;      /* (in the pass band: 0; past the corner: down 12 dB an octave or so) */
        h = !v ? 0 : dd >= 0 ? 0 : dd * 2;
        if (v && dd > -12 && dd <= 12)
            h += (int32_t)song.mres * (12 - (dd < 0 ? -dd : dd)) / 40;
        h = clamp(h, -34, 30);
        if (i && (h - prev > 1 || prev - h > 1))
            cv_rect(10 + i, 50 - (h > prev ? h : prev), 1, (h > prev ? h - prev : prev - h) + 1, v ? T_THEME : T_DIM);
        cv_rect(10 + i, 50 - h, 1, 2, v ? T_THEME : T_DIM);
        prev = h;
    }
    g_label(10, 92, v < 0 ? "LOW-PASS: TURN RIGHT TO OPEN" : v > 0 ? "HIGH-PASS: TURN LEFT TO OPEN" : "FILTER OFF (CENTRE)", v ? T_TEXT : T_MID);
    g_label(10, 106, "CC LEARN ON THE MIDI PAGE (HOME x2)", T_DIM);
}
#include "ui_logo.h"                             /* LOGO_W x LOGO_H, LOGO_RLE (tools/gen_logo.py) */
#define C_LOGO RGB(240, 140, 52)                 /* the logo's orange marks */
/* SYSTEM: the logo where the header and the cards are (no header on this page); the letters in TEXT, the marks in
 * a fixed orange (MONO: ACCENT), each a 4-bit coverage over BG */
static void logo_paint(uint32_t y0)              /* the logo into the canvas (begun 240 wide), its top at y0 */
{
    uint16_t lut[256];
    const uint8_t *p = LOGO_RLE, *e = LOGO_RLE + sizeof LOGO_RLE;
    uint32_t i = 0, v, x0 = (240u - LOGO_W) / 2u;
    uint16_t org = ux.mono ? T_ACCENT : C_LOGO;
    for (v = 0; v < 256u; v++)
        lut[v] = swap16(ux_mix(ux_mix(T_BG, T_TEXT, (int32_t)(v >> 4) * 100 / 15), org, (int32_t)(v & 15u) * 100 / 15));
    for (; p + 1 < e; p += 2) {
        uint32_t n = p[0];
        v = p[1];
        for (; n; n--, i++)
            if (v && y0 + i / LOGO_W < cv_h)
                cv_px[(y0 + i / LOGO_W) * 240u + x0 + i % LOGO_W] = lut[v];
    }
}
static void draw_logo(int force)
{
    uint32_t sig = 0x10600001u + ux.gen * 7u;
    if (!force && sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, Y_GRAPH, T_BG);
    logo_paint((Y_GRAPH - LOGO_H) / 2u);
    cv_blit(0, 0);
}
/* the loading screen: the logo, a bar (pct 0..100), what is happening. At power-on (oc_main.c: until the patch is
 * loaded and the audio runs) and while an update page restores the patches (until they are all in and loaded). */
static void boot_screen(uint32_t pct, const char *label)
{
    static uint32_t gen = 0xFFFFFFFFu;
    int32_t w = 160, f = (int32_t)(pct > 100u ? 100u : pct) * w / 100;
    if (gen != ux.gen) {                         /* (first, or a new palette: the whole screen) */
        gen = ux.gen;
        lcd_fill(0, 0, 240, 240, T_BG);
    }
    cv_begin(240, 124, T_BG);
    logo_paint(0);
    cv_text_in(0, 68, 240, &AF_S, "A Sonic Potions LXR-Adaption", T_TEXT, T_BG);
    cv_rrect(40, 92, w, 6, 3, T_LINE, T_BG);
    if (f > 0)
        cv_rrect(40, 92, f < 6 ? 6 : f, 6, 3, ux.mono ? T_ACCENT : C_LOGO, T_LINE);
    cv_text_in(0, 106, 240, &AF_S, label ? label : "", T_MID, T_BG);
    cv_blit(0, 58);
}
static uint32_t graph_sig(void)
{
    uint32_t h = ui.page * 7u + ui.shift * 3u + song.sel * 131u + ui.bar * 17u + song.pat * 7919u + ui.cur * 37u;
    uint32_t i, t;
    if (ui.page == PG_MIX)
        return h + fm1_ms / 50u;                 /* (the meters move) */
    if (ui.page == PG_NR)
        return h + nr.rate * 101u + (nr.held | nr.latched) * 1009u + nr.latch + song.rec * 3u + song.playing * 5u;
    if (ui.page == PG_SAMPLE || ui.page == PG_SAMPLE2)
        return h + TS->smp * 101u + TS->slvl * 1009u + TS->sdec * 10007u + TS->sstart * 7u;
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
    if (ui.page == PG_INFO)
        return h + fm1_ms / 1000u;                /* (the load, the battery: once a second) */
    if (ui.page == PG_MIDI) {
        for (i = 0; i < NMAP; i++)
            h = h * 7u + ccmap[i].pid * 131u + ccmap[i].cc;
        for (t = 0; t < NTRK; t++)
            h = h * 5u + snd[t].och;
        return h + song.bpm * 3u + song.clk_src * 7u + song.clk_out * 11u + ui.learn * 13u +
               (xc.seen && clk_now - (xc.seen & ~1u) < FS / 2u) * 17u;
    }
    if (ui.page == PG_SONG) {
        for (i = 0; i < NSONG; i++)
            h = h * 3u + srows[i].pat * 17u + srows[i].rep;
        return h + ui.scur * 7u + song.slen * 11u + song.smode * 13u + song.send * 5u + (song.playing ? song.srow * 23u + song.srep * 29u + 1u : 0u);
    }
    if (ui.page == PG_MFX)
        return h + (uint32_t)(song.mflt + 64) * 3u + song.mres * 257u;
    if (ui.page == PG_NEW)
        return h + ui.nlen * 3u + (uint32_t)(ui.nspd + 8) * 101u + ui.nbpm * 1009u + ui.nkit * 7u;
    if (ui.page == PG_TRK) {
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < NSTEP; i++)
                h = h * 3u + (PAT->s[t][i] != 0);
        return h + song.mute * 257u + song.solo * 65537u + song.sel * 11u;
    }
    if (ui.page == PG_LEARN) {
        for (i = 0; i < NMAP; i++)
            h = h * 7u + ccmap[i].pid * 131u + ccmap[i].cc * 3u + ccmap[i].trk;
        return h + ui.lsel * 101u + ui.learn * 13u;
    }
    if (ui.page == PG_ENV)
        return h + TS->atk * 3u + TS->rel * 101u + TS->gate * 1009u + TS->p[1] * 10007u + song.ddec * 7u + song.bpm * 13u;
    if (ui.page == PG_SAVE) {
        for (i = 0; i < NSLOT; i++)
            h = h * 3u + ui.used[i];
        return h + ui.slot * 11u + ui.swhat * 101u + ui.lwhat * 7u + ui.loaded * 3001u;
    }
    for (i = 0; i < NLOCK; i++)                  /* (the locks) */
        h = h * 3u + plk[song.pat % NPAT][i].key * plk[song.pat % NPAT][i].pid;
    for (t = 0; t < NTRK; t++) {                 /* the grid: the bar shown, the playhead, mute, solo */
        h = h * 31u + tlen(t) + (trk_audible(t) ? 7u : 0u) + tpos(t) * 41u;
        for (i = 0; i < 16u; i++)
            h = h * 33u + pat[song.pat % NPAT].s[t][(ui.bar % nbars(t)) * 16u + i] +
                pat[song.pat % NPAT].x[t][(ui.bar % nbars(t)) * 16u + i] * 7u +
                pat[song.pat % NPAT].r[t][(ui.bar % nbars(t)) * 16u + i] * 131u;
    }
    return h + ui.sedit * 13u + (song.playing ? song.pstep * 977u + 1u : 0u);
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
    case PG_SAMPLE: case PG_SAMPLE2: g_sample(); break;
    case PG_MIX: g_mixer(); break;
    case PG_NR: g_repeat(); break;
    case PG_PAT: g_patterns(); break;
    case PG_SAVE: g_store(); break;
    case PG_LIVE: g_live(); break;
    case PG_LFO1: case PG_LFO2: g_lfo(); break;
    case PG_REC: g_quant(); break;
    case PG_INFO: g_info(); break;
    case PG_MIDI: g_midi(); break;
    case PG_SONG: g_song(); break;
    case PG_MFX: g_mfx(); break;
    case PG_NEW: g_new(); break;
    case PG_ENV: g_env(); break;
    case PG_LEARN: g_learn(); break;
    case PG_TRK: g_tracks(); break;
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
    static const khint_t H_SAVE[3] = {{KC_OCTUP, "SAVE"}, {KC_K4, "LOAD"}, {KC_OCTDN, "BACK"}};
    static const khint_t H_PAGE[3] = {{KC_SELECT, "SOUND"}, {KC_ALGO, "NEW"}, {KC_PRESETS, "PATCH"}};
    static const khint_t H_LIVE[3] = {{KC_KEYS, "PADS"}, {KC_REC, "REC"}, {KC_SEQ, "STEPS"}};
    static const khint_t H_REC[3] = {{KC_K1, "GRID"}, {KC_K2, "TO"}, {KC_REC, "ARM"}};
    static const khint_t H_SHIFT[3] = {{KC_K14, "ROW B"}, {KC_KEYS, "MUTE"}, {KC_PLAY, "TOP"}};
    static const khint_t H_LOCK[3] = {{KC_KEYS, "STEP"}, {KC_K14, "LOCK"}, {KC_OCTPM, "BAR"}};
    static const khint_t H_NUDGE[3] = {{KC_KEYS, "STEP"}, {KC_K1, "NUDGE"}, {KC_OCTPM, "BAR"}};
    static const khint_t H_TRK[3] = {{KC_K1, "TRACK"}, {KC_K4, "CLEAR"}, {KC_GLO, "ROW B"}};
    char b[32];
    uint32_t sig;
    const khint_t *h = ui.shift ? H_SHIFT : ui.lockmode == LM_LOCK && ui.page != PG_SEQ && ui.page != PG_NR ? H_LOCK :
                       ui.lockmode == LM_NUDGE && ui.page != PG_NR ? H_NUDGE : ui.page == PG_SEQ ? H_SEQ : ui.page == PG_MIX ? (ui.mixpan ? H_MIXPAN : H_MIX) :
                       ui.page == PG_NR ? H_NR : ui.page == PG_SAVE ? H_SAVE : ui.page == PG_LIVE ? H_LIVE :
                       ui.page == PG_REC ? H_REC : ui.page == PG_TRK ? H_TRK : H_PAGE;
    uint32_t gi, gn = 0, gk = 0;
    str_cpy(b, PG_NAME[ui.page % PG_N], sizeof b);
    if (ui.page == PG_MIX)
        str_cat(b, ui.mixpan ? (ui.bank ? " 5-8 PAN" : " 1-4 PAN") : ui.bank ? " 5-8" : " 1-4", sizeof b);
    if (ui.page == PG_SEQ && ui.acc_mode)
        str_cat(b, " ACC", sizeof b);
    if (ui.page == PG_SEQ && ui.sedit)
        str_cpy(b, "STEP EDIT", sizeof b);
    if (ui.shift)
        str_cat(b, " B", sizeof b);
    sig = hsh(ui.page * 31u + ui.shift * 101u + ui.mixpan * 7u + (ui.sedit != 0) * 3u + ui.lockmode * 1009u, b);
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
    if (ui.restoring) {                          /* patches coming in: the loading screen */
        static uint32_t last;
        char b[28];
        uint32_t pct = ui.rs_off * 100u / sizeof(proj_t), sg = ui.rs_done * 1000u + ui.rs_slot * 101u + pct + ux.gen * 7u;
        if (sg != last || force) {
            last = sg;
            str_cpy(b, "RESTORING PATCH ", sizeof b);
            fmt_u(b + str_len(b), ui.rs_slot + 1u);
            if (ui.rs_done) {
                str_cat(b, " (", sizeof b);
                fmt_u(b + str_len(b), ui.rs_done);
                str_cat(b, " DONE)", sizeof b);
            }
            boot_screen(pct, b);
        }
        return;
    }
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
    if (ui.page == PG_INFO) {                     /* SYSTEM: the logo, no header, no cards */
        if (!ui.logo)
            force = 1;
        ui.logo = 1;
        if (force)
            lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_FOOT - Y_GRAPH - H_GRAPH, T_BG);
        draw_logo(force);
        draw_graph(force);
        draw_foot(force);
        return;
    }
    if (ui.logo) {                                /* (back from SYSTEM: everything again) */
        ui.logo = 0;
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

/* ---------------------------------------------------------------- USB --- */
/* the backup (the update page): SysEx 7D 'O' 'C', a command, its data packed 7 in 8 (as ONE CLICK's frames; the updater
 * keeps its own, ota_service). Every answer is 7D 'O' 'C' cmd + 1, packed:
 *   40                       info: "LUXR", the version (0-terminated), the slots, a slot's bytes (4), the slots in use (2)
 *   42 + slot off(4)         a slot as stored: 43 slot off(4) and up to 256 bytes
 *   44 + slot off(4) data    up to 256 bytes into the staging buffer at off: 45 code
 *   46 + slot len(4)         the staging buffer (a patch of any format, LXP1..4) into the slot: 47 code
 * codes: 0 ok, 2 bad request (or not a patch), 4 flash */
static int proto_send(const uint8_t *p, uint32_t n);   /* a whole F0..F7 message (oc_main.c; the host's) */
static uint8_t px_raw[300], px_wire[360];
static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t i, o = 0;
    for (i = 0; i < n; i += 7u) {
        uint32_t k, hi = 0, m = n - i < 7u ? n - i : 7u, h = o++;
        for (k = 0; k < m; k++) {
            hi |= ((in[i + k] >> 7) & 1u) << k;
            out[o++] = in[i + k] & 0x7Fu;
        }
        out[h] = (uint8_t)hi;
    }
    return o;
}
static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t hi = in[i++], k;
        for (k = 0; k < 7u && i < n && o < max; k++)
            out[o++] = (uint8_t)(in[i++] | ((hi >> k) & 1u) << 7);
    }
    return o;
}
static void wr32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static void proto_reply(uint32_t cmd, const uint8_t *d, uint32_t n)
{
    uint32_t w;
    px_wire[0] = 0xF0; px_wire[1] = 0x7D; px_wire[2] = 'O'; px_wire[3] = 'C'; px_wire[4] = (uint8_t)cmd;
    w = 5u + pack7(d, n, px_wire + 5);
    px_wire[w++] = 0xF7;
    (void)proto_send(px_wire, w);
}
/* 1 = the frame was ours */
static int proto_frame(const uint8_t *p, uint32_t n)
{
    static const char V[] = OC_VERSION;
    uint32_t k, m = 0, slot, off;
    uint8_t code;
    if (n < 4u || p[0] != 0x7Du || p[1] != 'O' || p[2] != 'C')
        return 0;
    k = unpack7(p + 4, n - 4u, px_raw, sizeof px_raw);
    slot = k ? px_raw[0] : 0xFFu;
    off = k >= 5u ? rd32(px_raw + 1) : 0;
    switch (p[3]) {
    case 0x40:
        memcpy(px_raw, "LUXR", 4);
        m = 4;
        for (k = 0; V[k] && k < 15u; k++) px_raw[m++] = (uint8_t)V[k];
        px_raw[m++] = 0;
        px_raw[m++] = NSLOT;
        wr32(px_raw + m, sizeof(proj_t));
        m += 4;
        for (k = 0; k < (NSLOT + 7u) / 8u; k++) px_raw[m + k] = 0;   /* (the slots in use: a bit each, as many bytes as it takes) */
        for (k = 0; k < NSLOT; k++) px_raw[m + k / 8u] |= (uint8_t)(ui.used[k] << (k % 8u));
        m += (NSLOT + 7u) / 8u;
        proto_reply(0x41, px_raw, m);
        return 1;
    case 0x42:                                   /* (a slot read: what flash has there, magic and all) */
        if (k < 5u || slot >= NSLOT || off >= sizeof(proj_t))
            return 1;
        m = sizeof(proj_t) - off < 256u ? sizeof(proj_t) - off : 256u;
        px_raw[0] = (uint8_t)slot;
        wr32(px_raw + 1, off);
        if (io_store_read(ST_PROJ(slot) + off, px_raw + 5, m))
            m = 0;
        proto_reply(0x43, px_raw, 5u + m);
        return 1;
    case 0x44:
        if (!ui.restoring) {                     /* a restore begins: stopped, the loading screen until it is over */
            ui.restoring = 1;
            ui.rs_done = 0;
            seq_play(0);
        }
        ui.rs_slot = (uint8_t)slot;
        ui.rs_off = off;
        ui.rs_t = fm1_ms;
        code = k >= 5u && off + (k - 5u) <= sizeof stage ? 0 : 2;
        if (!code)
            memcpy(stage + off, px_raw + 5, k - 5u);
        proto_reply(0x45, &code, 1);
        return 1;
    case 0x46:                                   /* (the staged patch into the slot, converted from its format) */
        code = 2;
        if (k >= 5u && slot < NSLOT && off <= sizeof stage && !proj_convert(stage, off)) {
            code = store_write(ST_PROJ(slot), &proj_buf, sizeof proj_buf) ? 4 : 0;
            if (!code)
                ui.used[slot] = 1;
        }
        proto_reply(0x47, &code, 1);
        if (!code && ui.rs_done < 255u)
            ui.rs_done++;
        ui.rs_off = 0;
        ui.rs_t = fm1_ms;
        return 1;
    }
    return 0;
}

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
    song.qgrid = Q_16;
    song.click = CLICK_REC;
    ui.copy = 1;
    ui.lvel = 100;
    song.slen = 1;
    song.mres = 30;
    ui.nlen = 16;
    ui.nbpm = 120;
    kit_factory();
    for (i = 0; i < NPAT; i++) {
        uint32_t t;
        memset(&pat[i], 0, sizeof pat[i]);
        for (t = 0; t < NTRK; t++)
            pat[i].len[t] = 16;
    }
    memset(plk, 0, sizeof plk);
    memset(srows, 0, sizeof srows);
    srows[0].rep = 1;
    memset(ccmap, 0, sizeof ccmap);
    seq_reset();
    demo_pattern(&pat[0]);
    boot_screen(15, "READING THE STORE");
    store_scan();                                 /* the settings (theme, MIDI IN, metronome), the slots in use */
    boot_screen(40, ui.used[0] ? "LOADING PATCH 1" : "FACTORY KIT");
    if (ui.used[0])                               /* at power-on: patch 1, kit and patterns */
        proj_load(0, SW_ALL);
    boot_screen(65, "STARTING THE SOUND");
    palette_set(song.theme);
    undo.n_undo = undo.n_redo = 0;
    ui.page = ui.back = PG_SEQ;
}

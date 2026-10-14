/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the panel and the screen, the way the FM-1's custom firmwares work (after Felucca's conventions).
 * Every function of the browser app is on a knob, a button or a page (README: the full map):
 *
 *   KNOBS   SELECT tempo (SHIFT: swing) · ALGORITHM the track (SHIFT: its TYPE) · PRESETS the track's sounds (turn on:
 *           a new one; back: the ones before) — on SEQ > STEP the step cursor, on SAVE the slot, in the MENU the row
 *           (SHIFT: the engine) · KNOB 1..4 the four columns of the page, SHIFT the page's second row · MASTER volume
 *   SHIFT   GLO held. With SHIFT: the knobs' second row, OCT-/+ transpose, PLAY from the top, REC the record mode,
 *           SAVE redo, the white keys F3..B3 mute tracks 1..4, C4..F4 solo them. GLO tapped: the mixer.
 *   PAGES   a tap (acting when let go) opens the button's page, again its next page:
 *           HOME  live CUTOFF RESO DECAY LEVEL | B: TUNE FINE OCTAVE TRANSPOSE        (held: the MENU)
 *           EDIT  the engine's pages, FILTER, VOICE (mono, glide, dual)               (held: a white key: the engine)
 *           ENV   amp ADSR | B: filter ADSR       LFO  RATE DEPTH DEST WAVE
 *           FX    LEVEL PAN DELAY REVERB | B: the delay's time and feedback, the reverb's size and damping
 *           SCL   ROOT SCALE CHORDS OCTAVE | B: TRANSPOSE SWING CLICK                 (held: a key sets the root)
 *           SEQ   STEP (the step editor) · EUCLID · SEQUENCE (LEN GATE SWING SHIFT | B: SEQ ON, MASTER, OCTAVE,
 *                 MUTATE)                                                              (held: the tools on the keys)
 *           ARP   the generator: DENSITY VARIATION STYLE RANGE | B: MUTATE TYPE       (held: a white key: the TYPE)
 *           GLO   the mixer: levels | B: pans     SAVE  SOUND slots · SONG slots · UNDO   (held: undo)
 *   REC     tap: arm recording (playing: live into the selected track; stopped: step entry at the cursor);
 *           held: ONE CLICK, a new song.   PLAY start / stop.   OCT- OCT+ the keys' octave (both: 0; both held 5 s:
 *           UBOOT). On SAVE and in the MENU: OCT+ does it, OCT- goes back.
 *   KEYS    play the selected track (drums: white keys F3..C4 are the lanes); on SEQ > STEP they write the step at the
 *           cursor (a chord: the keys held together); drum STEP: white keys the 16 steps of the bar, black keys the lane
 *
 * The screen as Felucca lays it out (gfx.c): the header (track, type, REC, tempo, play), four knob cards, the page's
 * picture, the footer (the page, its key hints). Each area is drawn only when what it shows changed.
 * Platform hooks (oc_main.c on the FM-1; host/, emu/): io_pressed io_held io_keys io_enc io_led_button io_led_key
 * io_store_read io_store_erase io_store_prog io_store_map, and lcd_blit / lcd_sync / lcd_fill under gfx.c. */

static uint32_t io_pressed(void);                /* B_* labels pressed since the last call */
static uint32_t io_held(void);                   /* B_* labels held */
static uint32_t io_keys(void);                   /* note keys held, bit k = key k (0 = F3 .. 26 = G5) */
static int32_t io_enc(uint32_t role);            /* detents since the last call, + clockwise */
static void io_led_button(uint32_t b, int on);
static void io_led_key(uint32_t k, int on);
static int io_store_read(uint32_t off, void *d, uint32_t n);         /* the store (OC_STORE_*): 0 = ok */
static int io_store_erase(uint32_t off);                            /* its 4 KiB sector at off */
static int io_store_prog(uint32_t off, const void *s, uint32_t n);  /* into erased flash */
static const uint8_t *io_store_map(void);                           /* the store as memory (0: not readable so) */
#ifndef OC_VERSION
#define OC_VERSION "v0.2"
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
#define HOLD_MS 350u                             /* a button held this long: its hold function */
#define PAL_AMBER 2u
static const uint16_t TRK_COL[NTRK] = {RGB(90, 200, 255), RGB(176, 140, 255), RGB(120, 226, 140), RGB(255, 170, 40),
                                       RGB(255, 96, 110), RGB(240, 220, 90)};
#define C_REC RGB(255, 72, 60)

/* --------------------------------------------------------- parameters --- */
enum {
    P_NONE,
    /* the track's sound (MASTER: a turn moves the other tracks' too) */
    P_KCUT, P_KRES, P_KDEC, P_KLVL,
    P_WAVE1, P_WAVE2, P_DETUNE, P_MIX2, P_OCT2, P_SUB, P_NOISE, P_GLIDE, P_KEYTRK, P_MONO,
    P_CUT, P_RES, P_FENV, P_FDEC, P_FATT, P_FSUS, P_FREL,
    P_RATIO1, P_RATIO2, P_RATIO3, P_INDEX, P_IDEC, P_FB, P_ALGO,
    P_SAWN, P_SPREAD, P_PARTS, P_TILT,
    P_KIT, P_DTUNE, P_DDEC,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_LRATE, P_LDEPTH, P_LDEST, P_LWAVE,
    P_LEVEL, P_PAN, P_DLY, P_REV,
    P_TUNE, P_FINE, P_DUAL, P_DMIX, P_COLOR,
    P_SOUND_END,
    /* the track */
    P_TOCT, P_LEN, P_GATE, P_SHIFT, P_SEQON, P_MASTER, P_MUT, P_TYPE,
    P_DENS, P_VAR, P_STYLE, P_RANGE,
    P_SNOTE, P_SVEL, P_SLEN, P_STYPE, P_SACC, P_SSLIDE, P_SCHORD, P_SLANE, P_SHIT,
    P_EHITS, P_EROT, P_ELANE,
    /* the song, the settings */
    P_ROOT, P_SCALE, P_PROG, P_OCT, P_TRANSP, P_SWING,
    P_DLYT, P_DLYFB, P_RSIZE, P_RDAMP,
    P_LV1, P_LV2, P_LV3, P_LV4, P_LV5, P_LV6, P_PN1, P_PN2, P_PN3, P_PN4, P_PN5, P_PN6,
    P_SMP, P_SSTART, P_MSLOT, P_MACT,
    P_LX0, P_LX1, P_LX2, P_LX3, P_LX4, P_LX5, P_LX6, P_LX7,   /* LXR: the voice of EDIT's page */
    P_RECMODE, P_CLICK, P_MIDIIN, P_THEME, P_VERSION,
    P_SSLOT, P_GSLOT, P_SACT,
    P_COUNT
};
enum { FM_X8, FM_NUM, FM_PCT, FM_BIP, FM_TIME, FM_CUT, FM_HZ, FM_RATIO, FM_SEMI16, FM_PAN, FM_NAME, FM_OCT, FM_STEPS,
       FM_SEMI, FM_CENT, FM_NOTE, FM_SLOT };
typedef struct { const char *label; uint8_t fmt; int16_t min, max; const char *const *names; } pdesc_t;
static const char *const N_WAVE[] = {"SAW", "SQUARE", "TRI", "SINE"};
static const char *const N_MONO[] = {"POLY", "MONO"};
static const char *const N_ALGO[] = {"3>2>1", "2+3>1", "2>1 +3"};
static const char *const N_KIT[] = {"TIGHT", "CLASSIC", "DEEP", "BOOM"};
static const char *const N_LDEST[] = {"OFF", "CUTOFF", "PITCH", "AMP", "PAN"};
static const char *const N_LWAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "S&H"};
static const char *const N_DUAL[] = {"OFF", "UNISON", "OCT UP", "FIFTH", "OCT DN"};
static const char *const N_COLOR[] = {"LOW", "BAND", "HIGH"};
static const char *const N_OFFON[] = {"OFF", "ON"};
static const char *const N_STYPE[] = {"REST", "NOTE", "TIE"};
static const char *const N_CHORD[] = {"SINGLE", "DYAD", "TRIAD", "7TH"};
static const char *const N_LANE[NLANE] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "TOM", "RIM", "BELL"};
static const char *const N_DLYT[] = {"1/8", "3/16", "1/4", "3/8", "1/2", "3/4"};
static const char *const N_RECMODE[] = {"OVERDUB", "REPLACE"};
static const char *const N_CLICK[] = {"OFF", "REC", "ON"};
static const char *const N_MIDIIN[] = {"CH 1-6", "SELECTED"};
static const char *const N_MACT[] = {"ASSIGN", "DELETE", "CLEAR ALL"};
static const char *N_SMP[NSMP];                  /* the slots' names (smp_names) */
static char smp_txt[NSMP][14];
static const char *const LX_VNAME[LX_N] = {"KICK", "TOM", "PERC", "SNARE", "CYMBAL", "HIHAT"};
static const char *const LX_LABEL[4][8] = {
    {"PITCH", "DECAY", "P.ENV", "P.DECAY", "WAVE", "FM", "CLICK", "DRIVE"},
    {"PITCH", "DECAY", "NOISE", "N.DECAY", "TONE", "SNAP", "CLICK", "DRIVE"},
    {"PITCH", "DECAY", "RATIO", "FM", "FILTER", "RESO", "NOISE", "DRIVE"},
    {"PITCH", "CLOSED", "OPEN", "FM", "FILTER", "NOISE", "CLICK", "DRIVE"}};
static const char *const N_LXWAVE[] = {"SINE", "TRI", "SAW", "SQUARE", "NOISE"};
static const char *const N_SACT[] = {"SAVE", "LOAD"};
static const char *const N_VERSION[] = {OC_VERSION};
static const char *const N_RN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
static const char *N_STYLE_CUR[8];               /* the selected track's styles (pdesc_of fills it) */
static const char *N_PROG[16];                   /* the progressions as "I-VI-IV-V" (ui_init) */
static const char *N_THEME[UI_NPALETTES];
static char prog_txt[16][16];
#define NSND_SLOT 16u
#define NSONG_SLOT 4u
static const pdesc_t PD[P_COUNT] = {
    [P_NONE] = {"", FM_NUM, 0, 0, 0},
    [P_KCUT] = {"CUTOFF", FM_BIP, -64, 63, 0}, [P_KRES] = {"RESO", FM_BIP, -64, 63, 0},
    [P_KDEC] = {"DECAY", FM_BIP, -40, 40, 0}, [P_KLVL] = {"LEVEL", FM_X8, 0, 16, 0},
    [P_WAVE1] = {"WAVE 1", FM_NAME, 0, 3, N_WAVE}, [P_WAVE2] = {"WAVE 2", FM_NAME, 0, 3, N_WAVE},
    [P_DETUNE] = {"DETUNE", FM_SEMI16, 0, 32, 0}, [P_MIX2] = {"OSC 2", FM_PCT, 0, 127, 0},
    [P_OCT2] = {"OCT 2", FM_OCT, -1, 1, 0}, [P_SUB] = {"SUB", FM_PCT, 0, 127, 0},
    [P_NOISE] = {"NOISE", FM_PCT, 0, 127, 0}, [P_GLIDE] = {"GLIDE", FM_NUM, 0, 40, 0},
    [P_KEYTRK] = {"KEYTRK", FM_PCT, 0, 127, 0}, [P_MONO] = {"VOICE", FM_NAME, 0, 1, N_MONO},
    [P_CUT] = {"CUTOFF", FM_CUT, 0, 127, 0}, [P_RES] = {"RESO", FM_PCT, 0, 127, 0},
    [P_FENV] = {"ENV AMT", FM_PCT, 0, 127, 0}, [P_FDEC] = {"F DECAY", FM_TIME, 0, 127, 0},
    [P_FATT] = {"F ATK", FM_TIME, 0, 127, 0}, [P_FSUS] = {"F SUS", FM_PCT, 0, 127, 0},
    [P_FREL] = {"F REL", FM_TIME, 0, 127, 0},
    [P_RATIO1] = {"RATIO 1", FM_RATIO, 4, 64, 0}, [P_RATIO2] = {"RATIO 2", FM_RATIO, 4, 255, 0},
    [P_RATIO3] = {"RATIO 3", FM_RATIO, 4, 255, 0}, [P_INDEX] = {"INDEX", FM_PCT, 0, 127, 0},
    [P_IDEC] = {"I DECAY", FM_TIME, 0, 127, 0}, [P_FB] = {"FEEDBK", FM_PCT, 0, 127, 0},
    [P_ALGO] = {"ALGO", FM_NAME, 0, 2, N_ALGO},
    [P_SAWN] = {"SAWS", FM_NUM, 1, 7, 0}, [P_SPREAD] = {"SPREAD", FM_SEMI16, 0, 32, 0},
    [P_PARTS] = {"PARTS", FM_NUM, 1, ADD_N, 0}, [P_TILT] = {"TILT", FM_PCT, 0, 127, 0},
    [P_KIT] = {"KIT", FM_NAME, 0, 3, N_KIT}, [P_DTUNE] = {"TUNE", FM_SEMI, -24, 24, 0},
    [P_DDEC] = {"DECAY", FM_PCT, 1, 127, 0},
    [P_ATK] = {"ATTACK", FM_TIME, 0, 127, 0}, [P_DEC] = {"DECAY", FM_TIME, 0, 127, 0},
    [P_SUS] = {"SUSTAIN", FM_PCT, 0, 127, 0}, [P_REL] = {"RELEASE", FM_TIME, 0, 127, 0},
    [P_LRATE] = {"RATE", FM_HZ, 0, 127, 0}, [P_LDEPTH] = {"DEPTH", FM_PCT, 0, 127, 0},
    [P_LDEST] = {"DEST", FM_NAME, 0, LD_N - 1, N_LDEST}, [P_LWAVE] = {"WAVE", FM_NAME, 0, LW_N - 1, N_LWAVE},
    [P_LEVEL] = {"LEVEL", FM_PCT, 0, 127, 0}, [P_PAN] = {"PAN", FM_PAN, -64, 63, 0},
    [P_DLY] = {"DELAY", FM_PCT, 0, 127, 0}, [P_REV] = {"REVERB", FM_PCT, 0, 127, 0},
    [P_TUNE] = {"PITCH", FM_SEMI, -24, 24, 0}, [P_FINE] = {"FINE", FM_CENT, -100, 100, 0},
    [P_DUAL] = {"DUAL", FM_NAME, 0, DU_N - 1, N_DUAL}, [P_DMIX] = {"DUAL LV", FM_PCT, 0, 127, 0},
    [P_COLOR] = {"COLOR", FM_NAME, 0, 2, N_COLOR},
    [P_SOUND_END] = {"", FM_NUM, 0, 0, 0},
    [P_TOCT] = {"OCTAVE", FM_OCT, -2, 2, 0}, [P_LEN] = {"LENGTH", FM_STEPS, 1, NSTEP, 0},
    [P_GATE] = {"GATE", FM_NUM, 1, 100, 0}, [P_SHIFT] = {"SHIFT", FM_NUM, -64, 64, 0},
    [P_SEQON] = {"SEQ", FM_NAME, 0, 1, N_OFFON}, [P_MASTER] = {"MASTER", FM_NAME, 0, 1, N_OFFON},
    [P_MUT] = {"MUTATE", FM_PCT, 0, 127, 0}, [P_TYPE] = {"TYPE", FM_NAME, 0, TY_N - 1, TYPE_NAME},
    [P_DENS] = {"DENSITY", FM_PCT, 0, 127, 0}, [P_VAR] = {"VARY", FM_PCT, 0, 127, 0},
    [P_STYLE] = {"STYLE", FM_NAME, 0, 7, N_STYLE_CUR}, [P_RANGE] = {"RANGE", FM_OCT, -1, 1, 0},
    [P_SNOTE] = {"NOTE", FM_NOTE, 12, 120, 0}, [P_SVEL] = {"VELO", FM_NUM, 1, 127, 0},
    [P_SLEN] = {"LENGTH", FM_STEPS, 1, 16, 0}, [P_STYPE] = {"STEP", FM_NAME, 0, 2, N_STYPE},
    [P_SACC] = {"ACCENT", FM_NAME, 0, 1, N_OFFON}, [P_SSLIDE] = {"SLIDE", FM_NAME, 0, 1, N_OFFON},
    [P_SCHORD] = {"CHORD", FM_NAME, 0, 3, N_CHORD}, [P_SLANE] = {"LANE", FM_NAME, 0, NLANE - 1, N_LANE},
    [P_SHIT] = {"HIT", FM_NAME, 0, 1, N_OFFON},
    [P_EHITS] = {"HITS", FM_NUM, 0, NSTEP, 0}, [P_EROT] = {"ROTATE", FM_NUM, 0, NSTEP - 1, 0},
    [P_ELANE] = {"LANE", FM_NAME, 0, NLANE - 1, N_LANE},
    [P_ROOT] = {"ROOT", FM_NAME, 0, 11, NOTE_NAME}, [P_SCALE] = {"SCALE", FM_NAME, 0, SC_N - 1, SCALE_NAME},
    [P_PROG] = {"CHORDS", FM_NAME, 0, 0, N_PROG}, [P_OCT] = {"KEYS", FM_OCT, -2, 2, 0},
    [P_TRANSP] = {"TRANSP", FM_SEMI, -24, 12, 0}, [P_SWING] = {"SWING", FM_NUM, 0, 50, 0},
    [P_DLYT] = {"DLY T", FM_NAME, 0, 5, N_DLYT}, [P_DLYFB] = {"DLY FB", FM_PCT, 0, 127, 0},
    [P_RSIZE] = {"REV SIZE", FM_PCT, 0, 127, 0}, [P_RDAMP] = {"REV DMP", FM_PCT, 0, 127, 0},
    [P_LV1] = {"LEVEL 1", FM_PCT, 0, 127, 0}, [P_LV2] = {"LEVEL 2", FM_PCT, 0, 127, 0},
    [P_LV3] = {"LEVEL 3", FM_PCT, 0, 127, 0}, [P_LV4] = {"LEVEL 4", FM_PCT, 0, 127, 0},
    [P_PN1] = {"PAN 1", FM_PAN, -64, 63, 0}, [P_PN2] = {"PAN 2", FM_PAN, -64, 63, 0},
    [P_PN3] = {"PAN 3", FM_PAN, -64, 63, 0}, [P_PN4] = {"PAN 4", FM_PAN, -64, 63, 0},
    [P_LV5] = {"KICK LV", FM_PCT, 0, 127, 0}, [P_LV6] = {"LOOP LV", FM_PCT, 0, 127, 0},
    [P_PN5] = {"KICK PAN", FM_PAN, -64, 63, 0}, [P_PN6] = {"LOOP PAN", FM_PAN, -64, 63, 0},
    [P_SMP] = {"SAMPLE", FM_NAME, 0, NSMP - 1, N_SMP}, [P_SSTART] = {"START", FM_PCT, 0, 127, 0},
    [P_MSLOT] = {"SLOT", FM_SLOT, 0, NSMP - 1, 0}, [P_MACT] = {"ACTION", FM_NAME, 0, 2, N_MACT},
    [P_RECMODE] = {"REC MODE", FM_NAME, 0, REC_N - 1, N_RECMODE}, [P_CLICK] = {"CLICK", FM_NAME, 0, CLICK_N - 1, N_CLICK},
    [P_MIDIIN] = {"MIDI IN", FM_NAME, 0, MI_N - 1, N_MIDIIN},
    [P_THEME] = {"THEME", FM_NAME, 0, UI_NPALETTES - 1, N_THEME}, [P_VERSION] = {"VERSION", FM_NAME, 0, 0, N_VERSION},
    [P_SSLOT] = {"SLOT", FM_SLOT, 0, NSND_SLOT - 1, 0}, [P_GSLOT] = {"SLOT", FM_SLOT, 0, NSONG_SLOT - 1, 0},
    [P_SACT] = {"ACTION", FM_NAME, 0, 1, N_SACT},
};

/* ------------------------------------------------------------- state --- */
enum { PG_HOME, PG_EDIT, PG_ENV, PG_LFO, PG_FX, PG_SCL, PG_SEQ, PG_GEN, PG_GLO, PG_SAVE, PG_MENU, PG_N };
enum { SQ_STEP, SQ_EUCLID, SQ_SEQ };              /* SEQ's pages */
enum { SV_SOUND, SV_SONG, SV_SAMPLES, SV_UNDO };  /* SAVE's pages */
enum { LY_NONE, LY_SEQ, LY_SCL, LY_EDIT, LY_TYPE };
static struct {
    uint8_t page, sub[PG_N], layer, hot, uboot, force, shift, menu_row, menu_back, menu_dirty;
    uint8_t cur, lane, last_note, eu_snap, step_at, sslot, gslot, sact, mslot, mact;
    uint32_t hot_t, msg_t, both_t, burst_t, burst_tag, ev;   /* ev: input events (the picture's signature) */
    char msg[28];
    uint32_t sig[8];                             /* the areas: 0 header, 1..4 cards, 5 graph, 6 footer */
    uint32_t keys, held_prev, btn_t0[NB], btn_used, step_keys;
    uint8_t keynote[27], keytrk[27];
} ui;
static int32_t shift_acc;                        /* SEQ > SHIFT: the steps moved since the page opened */
static step_t eu_base[NSTEP];                    /* EUCLID: the pattern as it was when the page began to change it */

/* undo: a ring of whole states; [cur - n_undo, cur) to go back to, (cur, cur + n_redo] forward */
#define UNDO_N 16u
typedef struct { track_t t[NTRK]; uint8_t root, scale, prog_i, prog[4]; } snap_t;
static struct { snap_t s[UNDO_N]; uint32_t cur, n_undo, n_redo; } undo __attribute__((section(".pool")));
#define HIST_N 8u                                /* PRESETS: the last sounds of each track */
static struct { patch_t p[NTRK][HIST_N]; uint8_t n[NTRK], at[NTRK]; } hist __attribute__((section(".pool")));

/* the store, 4 KiB sectors from OC_STORE_LO: 16 sounds, 4 songs (two sectors each), the settings, the samples'
 * directory, then the samples themselves (8-bit, ~11 s at 22 kHz) */
#define ST_SOUNDS 0x0000u
#define ST_SONG(i) (0x1000u + 0x2000u * (uint32_t)(i))
#define ST_SET 0x9000u
#define ST_SMPDIR 0xA000u
#define ST_SMP0 0xB000u
#define ST_SIZE 0x49000u
#define SMP_AREA (ST_SIZE - ST_SMP0)
#define MAGIC_SMP 0x31504E4Fu                    /* "ONP1" */
#define MAGIC_SND 0x31534E4Fu                    /* "ONS1" */
#define MAGIC_SONG 0x31474E4Fu                   /* "ONG1" */
#define MAGIC_SET 0x31544E4Fu                    /* "ONT1" */
typedef struct { uint8_t used, type, pad[2]; patch_t p; } snd_slot_t;
typedef struct { uint32_t magic; uint16_t psize, n; snd_slot_t s[NSND_SLOT]; } snd_bank_t;
typedef struct {
    uint32_t magic, sum;
    uint16_t size, tsize;
    uint8_t bpm, swing, root, scale, prog[4], prog_i, dly_div, dly_fb, rev_size, rev_damp;
    int8_t transpose;
    uint8_t pad[2];
    track_t t[NTRK];
} song_rec_t;
typedef struct { uint32_t magic; uint8_t theme, midi_in, click, rec_mode; } set_rec_t;
_Static_assert(sizeof(snd_bank_t) <= 0x1000u, "the sounds fit a sector");
_Static_assert(sizeof(song_rec_t) <= 0x2000u, "a song fits two sectors");
typedef struct { uint32_t magic; uint16_t n, size; smp_t s[NSMP]; } smp_dir_rec_t;
static snd_bank_t bank __attribute__((section(".pool")));
static song_rec_t song_buf __attribute__((section(".pool")));
static struct { uint8_t used, root, scale, bpm; } song_meta[NSONG_SLOT];

static void mmove(void *d, const void *s, uint32_t n);
static void ui_say(const char *s)
{
    str_cpy(ui.msg, s, sizeof ui.msg);
    ui.msg_t = fm1_ms | 1u;
}
static void str_cat(char *d, const char *s, uint32_t n) { str_cpy(d + str_len(d), s, n - str_len(d)); }

/* ---------------------------------------------------- undo and redo --- */
static void snap_take(snap_t *s)
{
    gen_lock();
    memcpy(s->t, trk, sizeof trk);
    gen_unlock();
    s->root = song.root;
    s->scale = song.scale;
    s->prog_i = song.prog_i;
    memcpy(s->prog, song.prog, 4);
}
static void tracks_load(const track_t *src)       /* whole tracks in (the runtime parts reset, the voices let go) */
{
    uint32_t i, l;
    gen_lock();
    for (i = 0; i < NTRK; i++) {
        track_panic(i);
        for (l = 0; l < NLANE; l++)
            dvoice[i][l].active = 0;
    }
    memcpy(trk, src, sizeof trk);
    for (i = 0; i < NTRK; i++) {
        trk[i].nsnd = 0;
        trk[i].off_at = 0;
        trk[i].hold = 0;
        trk[i].lfo_ph = 0;
    }
    gen_unlock();
}
static void snap_load(const snap_t *s)
{
    tracks_load(s->t);
    song.root = s->root;
    song.scale = s->scale;
    song.prog_i = s->prog_i;
    memcpy(song.prog, s->prog, 4);
}
static void undo_take(void)
{
    snap_take(&undo.s[undo.cur % UNDO_N]);
    undo.cur++;
    if (undo.n_undo < UNDO_N - 1u)
        undo.n_undo++;
    undo.n_redo = 0;
    ui.burst_tag = 0;
}
static void undo_burst(uint32_t tag)             /* one undo for a run of turns / keys (1.5 s apart at most) */
{
    if (tag != ui.burst_tag || fm1_ms - ui.burst_t > 1500u)
        undo_take();
    ui.burst_tag = tag;
    ui.burst_t = fm1_ms;
}
static void undo_do(int redo)
{
    if (redo ? !undo.n_redo : !undo.n_undo) {
        ui_say(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    snap_take(&undo.s[undo.cur % UNDO_N]);       /* (where we are: what the other way returns to) */
    if (redo) {
        undo.cur++;
        undo.n_redo--;
        undo.n_undo++;
    } else {
        undo.cur--;
        undo.n_undo--;
        undo.n_redo++;
    }
    snap_load(&undo.s[undo.cur % UNDO_N]);
    ui.burst_tag = 0;
    ui.eu_snap = 0;
    ui_say(redo ? "REDO" : "UNDO");
}
static void hist_push(uint32_t t)                /* the track's sound as it is now, at the end of its history */
{
    if (hist.n[t] && hist.at[t] + 1u < hist.n[t])
        hist.n[t] = (uint8_t)(hist.at[t] + 1u);   /* (browsed back, then a new one: the later ones go) */
    if (hist.n[t] == HIST_N) {
        mmove(hist.p[t], hist.p[t] + 1, (HIST_N - 1u) * sizeof(patch_t));
        hist.n[t]--;
    }
    hist.p[t][hist.n[t]] = trk[t].s;
    hist.at[t] = hist.n[t]++;
}
static void say_key(const char *pre)
{
    char b[28];
    str_cpy(b, pre, sizeof b);
    str_cat(b, NOTE_NAME[song.root % 12u], sizeof b);
    str_cat(b, " ", sizeof b);
    str_cat(b, SCALE_NAME[song.scale % SC_N], sizeof b);
    ui_say(b);
}
static void one_click(void)
{
    uint32_t t;
    undo_take();
    gen_all();
    for (t = 0; t < NTRK; t++)
        hist_push(t);
    say_key("ONE CLICK: ");
}

/* ---------------------------------------------------------- the store --- */
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

/* ------------------------------------------------------------ samples --- */
/* The directory in RAM (smp_dir, oc_core.h), the samples read in place. An upload (the companion, oc_proto.c):
 * begin (the slot, what it is, its length), chunks in order, commit (the directory written). New samples go after the
 * last one; a slot uploaded again reuses its place when it is the last. CLEAR: all gone. */
static struct { smp_t m; uint32_t at, erased, slot, target; uint8_t on; } up;
static uint32_t smp_end(void)                    /* the area used: after the last sample, a whole sector */
{
    uint32_t k, e = 0;
    for (k = 0; k < NSMP; k++)
        if (smp_dir[k].used && smp_dir[k].off + smp_dir[k].len > e)
            e = smp_dir[k].off + smp_dir[k].len;
    return (e + 0xFFFu) & ~0xFFFu;
}
static void smp_load(void)
{
    static smp_dir_rec_t d;
    uint32_t k;
    const uint8_t *m = io_store_map();
    memset(smp_dir, 0, sizeof smp_dir);
    smp_base = m ? m + ST_SMP0 : 0;
    if (io_store_read(ST_SMPDIR, &d, sizeof d) || d.magic != MAGIC_SMP || d.n != NSMP || d.size != sizeof(smp_t))
        return;
    for (k = 0; k < NSMP; k++)
        if (d.s[k].used == 1u && d.s[k].off < SMP_AREA && d.s[k].len <= SMP_AREA - d.s[k].off && d.s[k].len > 16u &&
            d.s[k].rate >= 4000u) {
            smp_dir[k] = d.s[k];
            smp_dir[k].name[11] = 0;
        }
}
static int smp_dir_save(void)
{
    static smp_dir_rec_t d;
    memset(&d, 0, sizeof d);
    d.magic = MAGIC_SMP;
    d.n = NSMP;
    d.size = sizeof(smp_t);
    memcpy(d.s, smp_dir, sizeof d.s);
    return store_write(ST_SMPDIR, &d, sizeof d);
}
static uint32_t smp_free(void) { return SMP_AREA - smp_end(); }
static void smp_assign(uint32_t t, uint32_t k)   /* track t plays sample k (the kick's one-shot, the loop) */
{
    if (t >= NTRK || k >= NSMP || !smp_dir[k].used || !USES_DV(t) ||
        (IS_LOOP(t) ? smp_dir[k].kind != SK_LOOP : smp_dir[k].kind != SK_ONESHOT))
        return;
    gen_lock();
    track_panic(t);
    gen_unlock();
    if (trk[t].s.engine != E_SAMPLE)
        gen_sound_engine(&trk[t], E_SAMPLE);
    trk[t].s.smp = (uint8_t)k;
    if (IS_LOOP(t))                              /* its slices, as many steps as the loop has */
        gen_pattern(&trk[t]);
}
/* 0 ok; -1 no space; -2 a bad slot / length */
static int smp_begin(uint32_t slot, const smp_t *m, uint32_t target)
{
    uint32_t start;
    if (slot >= NSMP || m->len < 16u || m->len > SMP_AREA || m->rate < 4000u)
        return -2;
    {
        uint8_t was = smp_dir[slot].used;
        smp_dir[slot].used = 0;                  /* (its place counts as free if it was the last) */
        start = smp_end();
        smp_dir[slot].used = was;
    }
    if (m->len > SMP_AREA - start)
        return -1;
    gen_lock();
    smp_dir[slot].used = 0;                      /* (the slot is silent until the new one is in) */
    gen_unlock();
    up.m = *m;
    up.m.off = start;
    up.m.used = 1;
    up.m.name[11] = 0;
    up.at = 0;
    up.erased = start;
    up.slot = slot;
    up.target = target;
    up.on = 1;
    return 0;
}
static int smp_chunk(uint32_t pos, const uint8_t *d, uint32_t n)   /* 0 ok; -3 out of order; -4 flash */
{
    if (!up.on || pos != up.at || n > up.m.len - up.at)
        return -3;
    while (up.m.off + up.at + n > up.erased) {   /* the sectors ahead, as the data reaches them */
        if (io_store_erase(ST_SMP0 + up.erased))
            return -4;
        up.erased += 0x1000u;
    }
    if (io_store_prog(ST_SMP0 + up.m.off + up.at, d, n))
        return -4;
    up.at += n;
    return 0;
}
static int smp_commit(void)
{
    if (!up.on || up.at != up.m.len)
        return -3;
    up.on = 0;
    gen_lock();
    smp_dir[up.slot] = up.m;
    gen_unlock();
    if (smp_dir_save())
        return -4;
    smp_assign(up.target, up.slot);
    ui_say(up.m.kind == SK_LOOP ? "LOOP LOADED" : "SAMPLE LOADED");
    return 0;
}
static int smp_clear(int slot)                   /* one slot (its place is reused if it was the last), or all (-1) */
{
    gen_lock();
    if (slot < 0)
        memset(smp_dir, 0, sizeof smp_dir);
    else if (slot < (int)NSMP)
        smp_dir[slot].used = 0;
    gen_unlock();
    return smp_dir_save() ? -4 : 0;
}
static void store_scan(void)
{
    uint32_t i;
    set_rec_t st;
    if (io_store_read(ST_SOUNDS, &bank, sizeof bank) || bank.magic != MAGIC_SND || bank.psize != sizeof(patch_t) ||
        bank.n != NSND_SLOT)
        memset(&bank, 0, sizeof bank);
    for (i = 0; i < NSONG_SLOT; i++) {
        song_meta[i].used = 0;
        if (!io_store_read(ST_SONG(i), &song_buf, 16u) && song_buf.magic == MAGIC_SONG &&
            song_buf.size == sizeof(song_rec_t) && song_buf.tsize == sizeof(track_t)) {
            io_store_read(ST_SONG(i), &song_buf, sizeof song_buf);
            song_meta[i].used = 1;
            song_meta[i].root = song_buf.root;
            song_meta[i].scale = song_buf.scale;
            song_meta[i].bpm = song_buf.bpm;
        }
    }
    if (!io_store_read(ST_SET, &st, sizeof st) && st.magic == MAGIC_SET) {
        song.theme = (uint8_t)(st.theme % UI_NPALETTES);
        song.midi_in = (uint8_t)(st.midi_in % MI_N);
        song.click = (uint8_t)(st.click % CLICK_N);
        song.rec_mode = (uint8_t)(st.rec_mode % REC_N);
    }
    smp_load();
}
static uint32_t rec_sum(const song_rec_t *r)
{
    const uint8_t *p = (const uint8_t *)r->t;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof r->t; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
static void settings_save(void)
{
    set_rec_t st = {MAGIC_SET, song.theme, song.midi_in, song.click, song.rec_mode};
    store_write(ST_SET, &st, sizeof st);
}
static void sound_save(uint32_t slot)
{
    bank.magic = MAGIC_SND;
    bank.psize = sizeof(patch_t);
    bank.n = NSND_SLOT;
    bank.s[slot].used = 1;
    bank.s[slot].type = TSEL->type;
    bank.s[slot].p = TSEL->s;
    ui_say(store_write(ST_SOUNDS, &bank, sizeof bank) ? "SAVE FAILED" : "SOUND SAVED");
}
static void sound_load(uint32_t slot)
{
    if (!bank.s[slot].used) {
        ui_say("EMPTY SLOT");
        return;
    }
    if ((TYPE_ROLE[bank.s[slot].type % TY_N] == ROLE_DRUM) != IS_DRUM(song.sel)) {
        ui_say(IS_DRUM(song.sel) ? "NOT A DRUM SOUND" : "A DRUM SOUND");
        return;
    }
    undo_take();
    gen_lock();
    track_panic(song.sel);
    gen_unlock();
    set_patch(TSEL, &bank.s[slot].p);
    hist_push(song.sel);
    ui_say("SOUND LOADED");
}
static void song_save(uint32_t slot)
{
    memset(&song_buf, 0, sizeof song_buf);
    song_buf.magic = MAGIC_SONG;
    song_buf.size = sizeof(song_rec_t);
    song_buf.tsize = sizeof(track_t);
    song_buf.bpm = song.bpm;
    song_buf.swing = song.swing;
    song_buf.root = song.root;
    song_buf.scale = song.scale;
    memcpy(song_buf.prog, song.prog, 4);
    song_buf.prog_i = song.prog_i;
    song_buf.dly_div = song.dly_div;
    song_buf.dly_fb = song.dly_fb;
    song_buf.rev_size = song.rev_size;
    song_buf.rev_damp = song.rev_damp;
    song_buf.transpose = song.transpose;
    gen_lock();
    memcpy(song_buf.t, trk, sizeof trk);
    gen_unlock();
    song_buf.sum = rec_sum(&song_buf);
    if (store_write(ST_SONG(slot), &song_buf, sizeof song_buf)) {
        ui_say("SAVE FAILED");
        return;
    }
    song_meta[slot].used = 1;
    song_meta[slot].root = song.root;
    song_meta[slot].scale = song.scale;
    song_meta[slot].bpm = song.bpm;
    ui_say("SONG SAVED");
}
static void song_load(uint32_t slot)
{
    uint32_t i;
    if (!song_meta[slot].used || io_store_read(ST_SONG(slot), &song_buf, sizeof song_buf) ||
        song_buf.magic != MAGIC_SONG || song_buf.sum != rec_sum(&song_buf)) {
        ui_say(song_meta[slot].used ? "SONG DAMAGED" : "EMPTY SLOT");
        return;
    }
    undo_take();
    tracks_load(song_buf.t);
    song.bpm = (uint8_t)clamp(song_buf.bpm, 60, 200);
    song.swing = (uint8_t)clamp(song_buf.swing, 0, 50);
    song.root = (uint8_t)(song_buf.root % 12u);
    song.scale = (uint8_t)(song_buf.scale % SC_N);
    memcpy(song.prog, song_buf.prog, 4);
    song.prog_i = song_buf.prog_i;
    song.dly_div = song_buf.dly_div;
    song.dly_fb = song_buf.dly_fb;
    song.rev_size = song_buf.rev_size;
    song.rev_damp = song_buf.rev_damp;
    song.transpose = (int8_t)clamp(song_buf.transpose, -24, 12);
    for (i = 0; i < NTRK; i++)
        hist_push(i);
    ui_say("SONG LOADED");
}

static void smp_action(void)                     /* SAVE > SAMPLES: OCT+ */
{
    uint32_t k = ui.mslot % NSMP;
    if (ui.mact == 2u) {
        undo_take();
        ui_say(smp_clear(-1) ? "SAVE FAILED" : "SAMPLES CLEARED");
    } else if (!smp_dir[k].used) {
        ui_say("EMPTY SLOT");
    } else if (ui.mact == 1u) {
        ui_say(smp_clear((int)k) ? "SAVE FAILED" : "SAMPLE DELETED");
    } else if (!USES_DV(song.sel) || (IS_LOOP(song.sel) ? smp_dir[k].kind != SK_LOOP : smp_dir[k].kind != SK_ONESHOT)) {
        ui_say(smp_dir[k].kind == SK_LOOP ? "LOOPS: LOOP TRACK" : "ONE-SHOTS: KICK, DRUMS");
    } else {
        undo_take();
        smp_assign(song.sel, k);
        ui_say("ASSIGNED");
    }
}

/* ------------------------------------------------------- the step editor --- */
static uint32_t cur_idx(const track_t *t) { return ui.cur % (uint32_t)clamp(t->len, 1, NSTEP); }
static step_t *cur_step(track_t *t) { return &t->step[cur_idx(t)]; }
static uint32_t tie_len(const track_t *t, uint32_t i)   /* a note's length in steps: it and the ties after it */
{
    uint32_t len = clamp(t->len, 1, NSTEP), n = 1;
    while (n < 16u && n < len && t->step[(i + n) % len].time == ST_TIE)
        n++;
    return n;
}
static void step_chord(track_t *t, step_t *s, uint32_t shape)   /* the notes of the shape over its first one */
{
    uint32_t k;
    (void)t;
    for (k = 1; k <= shape && k < 4u; k++)
        s->note[k] = (uint8_t)clamp(note_deg_shift(s->note[0], song.root, song.scale, 2 * (int32_t)k), 12, 120);
    s->n = (uint8_t)(shape + 1u);
}
static uint32_t default_note(const track_t *t)
{
    if (IS_LOOP(trk_i(t)))                       /* the loop: the slice the step is at */
        return cur_idx(t) % smp_slices(t->s.smp);
    if (ui.last_note)
        return ui.last_note;
    return trk_role(trk_i(t)) == ROLE_BASS ? 36u : 60u;
}

/* ------------------------------------------------------------- params --- */
/* the descriptor of id on track t (ranges that depend on the track) */
static pdesc_t pdesc_of(uint32_t id, const track_t *t)
{
    pdesc_t d = PD[id % P_COUNT];
    uint32_t r = trk_role(trk_i(t)), k, len = clamp(t->len, 1, NSTEP);
    if (id == P_STYLE) {
        for (k = 0; k < 8u; k++)
            N_STYLE_CUR[k] = STYLE_NAME[r][k];
        d.max = (int16_t)(STYLE_N[r] - 1u);
    }
    if (id == P_PROG)
        d.max = (int16_t)(NELEM(PROGS) - 1u);
    if (id == P_EHITS)
        d.max = (int16_t)len;
    if (id == P_EROT)
        d.max = (int16_t)(len - 1u);
    if (id == P_SLEN)
        d.max = (int16_t)(len < 16u ? len : 16u);
    if (id == P_SNOTE && IS_LOOP(trk_i(t))) {    /* the loop's steps: slices */
        d.min = 0;
        d.max = (int16_t)(smp_slices(t->s.smp) - 1u);
        d.label = "SLICE";
    }
    if (id >= P_LX0 && id <= P_LX7) {            /* LXR: the voice of EDIT's page */
        uint32_t vx = ui.sub[PG_EDIT] % LX_N, k2 = id - P_LX0;
        d.label = LX_LABEL[LX_KIND[vx]][k2];
        d.fmt = FM_PCT;
        d.min = 0;
        d.max = 127;
        if (LX_KIND[vx] == LK_DRUM && k2 == 4u) { d.fmt = FM_NAME; d.max = 4; d.names = N_LXWAVE; }
        if (k2 == 0u) d.fmt = FM_NUM;
    }
    if (id == P_SMP)                             /* the slots' names as they are now */
        for (k = 0; k < NSMP; k++) {
            str_cpy(smp_txt[k], smp_dir[k].used ? smp_dir[k].name : "-", sizeof smp_txt[k]);
            if (smp_dir[k].used && !smp_txt[k][0]) { smp_txt[k][0] = (char)('1' + k); smp_txt[k][1] = 0; }
            N_SMP[k] = smp_txt[k];
        }
    return d;
}
static int32_t pget(uint32_t id, const track_t *t)
{
    const patch_t *s = &t->s;
    const step_t *st = &t->step[cur_idx(t)];
    switch (id) {
    case P_KCUT: return t->k_cut;
    case P_KRES: return t->k_res;
    case P_KDEC: return t->k_dec;
    case P_KLVL: return t->k_lvl;
    case P_WAVE1: case P_COLOR: return s->wave1;
    case P_WAVE2: return s->wave2;
    case P_DETUNE: return s->detune;
    case P_MIX2: return s->mix2;
    case P_OCT2: return s->oct2;
    case P_SUB: return s->sub;
    case P_NOISE: return s->noise;
    case P_GLIDE: return s->glide;
    case P_KEYTRK: return s->keytrk;
    case P_MONO: return s->mono;
    case P_CUT: return s->cutoff;
    case P_RES: return s->res;
    case P_FENV: return s->fenv;
    case P_FDEC: return s->fdec;
    case P_FATT: return s->fatt;
    case P_FSUS: return s->fsus;
    case P_FREL: return s->frel;
    case P_RATIO1: return s->fm_ratio[0];
    case P_RATIO2: return s->fm_ratio[1];
    case P_RATIO3: return s->fm_ratio[2];
    case P_INDEX: return s->fm_index;
    case P_IDEC: return s->fm_idec;
    case P_FB: return s->fm_fb;
    case P_ALGO: return s->fm_algo;
    case P_SAWN: return s->saw_n;
    case P_SPREAD: return s->saw_spread;
    case P_PARTS: return s->add_n;
    case P_TILT: return s->add_tilt;
    case P_KIT: return s->kit;
    case P_DTUNE: return s->dtune;
    case P_DDEC: return s->ddec ? s->ddec : 64;
    case P_ATK: return s->atk;
    case P_DEC: return s->dec;
    case P_SUS: return s->sus;
    case P_REL: return s->rel;
    case P_LRATE: return s->lfo_rate;
    case P_LDEPTH: return s->lfo_depth;
    case P_LDEST: return s->lfo_dest;
    case P_LWAVE: return s->lfo_wave;
    case P_LEVEL: return s->level;
    case P_PAN: return s->pan;
    case P_DLY: return s->dly;
    case P_REV: return s->rev;
    case P_TUNE: return s->tune;
    case P_FINE: return s->fine;
    case P_DUAL: return s->dual;
    case P_DMIX: return s->dmix;
    case P_TOCT: return t->octave;
    case P_LEN: return t->len;
    case P_GATE: return t->gate;
    case P_SHIFT: return shift_acc;
    case P_SEQON: return !t->seq_off;
    case P_MASTER: return t->master;
    case P_MUT: return t->gen_mut;
    case P_TYPE: return t->type;
    case P_DENS: return t->gen_dens;
    case P_VAR: return t->gen_var;
    case P_STYLE: return t->gen_style;
    case P_RANGE: return t->gen_range;
    case P_SNOTE: return st->time == ST_NOTE && st->n ? st->note[0] : (int32_t)default_note(t);
    case P_SVEL: return st->vel ? st->vel : 100;
    case P_SLEN: return st->time == ST_NOTE ? (int32_t)tie_len(t, cur_idx(t)) : 1;
    case P_STYPE: return st->time;
    case P_SACC: return IS_DRUM(trk_i(t)) ? (st->acc >> ui.lane) & 1 : st->flags & SF_ACC ? 1 : 0;
    case P_SSLIDE: return st->flags & SF_SLIDE ? 1 : 0;
    case P_SCHORD: return st->n ? st->n - 1 : 0;
    case P_SLANE: return ui.lane;
    case P_SHIT: return st->time == ST_NOTE && ((st->hit >> ui.lane) & 1u);
    case P_EHITS: return t->eu_hits;
    case P_EROT: return t->eu_rot;
    case P_ELANE: return t->eu_lane;
    case P_ROOT: return song.root;
    case P_SCALE: return song.scale;
    case P_PROG: return song.prog_i;
    case P_OCT: return song.octave;
    case P_TRANSP: return song.transpose;
    case P_SWING: return song.swing;
    case P_DLYT: return song.dly_div;
    case P_DLYFB: return song.dly_fb;
    case P_RSIZE: return song.rev_size;
    case P_RDAMP: return song.rev_damp;
    case P_LV1: case P_LV2: case P_LV3: case P_LV4: case P_LV5: case P_LV6: return trk[id - P_LV1].s.level;
    case P_PN1: case P_PN2: case P_PN3: case P_PN4: case P_PN5: case P_PN6: return trk[id - P_PN1].s.pan;
    case P_SMP: return s->smp;
    case P_LX0: case P_LX1: case P_LX2: case P_LX3: case P_LX4: case P_LX5: case P_LX6: case P_LX7: {
        uint32_t vx = ui.sub[PG_EDIT] % LX_N, k2 = id - P_LX0, x = s->lx[vx][k2];
        return LX_KIND[vx] == LK_DRUM && k2 == 4u ? (int32_t)(x * 5u / 128u) : (int32_t)x;
    }
    case P_SSTART: return s->sstart;
    case P_MSLOT: return ui.mslot;
    case P_MACT: return ui.mact;
    case P_RECMODE: return song.rec_mode;
    case P_CLICK: return song.click;
    case P_MIDIIN: return song.midi_in;
    case P_THEME: return song.theme;
    case P_SSLOT: return ui.sslot;
    case P_GSLOT: return ui.gslot;
    case P_SACT: return ui.sact;
    default: return 0;
    }
}
static void rotate_steps(track_t *t, int32_t d)  /* the pattern's steps 1..LEN moved d steps later */
{
    static step_t tmp[NSTEP];
    uint32_t len = clamp(t->len, 1, NSTEP), i;
    memcpy(tmp, t->step, sizeof tmp);
    gen_lock();
    for (i = 0; i < len; i++)
        t->step[((int32_t)i + d % (int32_t)len + (int32_t)len) % (int32_t)len] = tmp[i];
    gen_unlock();
}
static void euclid_now(track_t *t)               /* EUCLID: the pattern as it was, with the rhythm over it */
{
    if (!ui.eu_snap) {
        undo_take();
        memcpy(eu_base, t->step, sizeof eu_base);
        ui.eu_snap = 1;
    }
    gen_lock();
    memcpy(t->step, eu_base, sizeof t->step);
    gen_unlock();
    euclid_apply(t, t->eu_hits, t->eu_rot, t->eu_lane % NLANE);
}
static void step_set(track_t *t, uint32_t id, int32_t v)   /* the step editor's columns */
{
    uint32_t len = clamp(t->len, 1, NSTEP), i = cur_idx(t), k;
    step_t *s = &t->step[i];
    undo_burst(0x5000u + i);
    gen_lock();
    switch (id) {
    case P_SNOTE: {
        int32_t d;
        if (s->time != ST_NOTE || !s->n) {
            memset(s, 0, sizeof *s);
            s->time = ST_NOTE;
            s->note[0] = (uint8_t)default_note(t);
            s->n = 1;
            s->vel = 100;
        }
        d = v - s->note[0];
        for (k = 0; k < s->n && k < 4u; k++)
            s->note[k] = (uint8_t)(IS_LOOP(trk_i(t)) ? clamp(s->note[k] + d, 0, (int32_t)smp_slices(t->s.smp) - 1)
                                                     : clamp(s->note[k] + d, 12, 120));
        ui.last_note = s->note[0];
        break;
    }
    case P_SVEL: s->vel = (uint8_t)v; break;
    case P_SLEN: {
        uint32_t old = s->time == ST_NOTE ? tie_len(t, i) : 1u, j;
        if (s->time != ST_NOTE)
            break;
        for (j = (uint32_t)v; j < old; j++)        /* shorter: the ties go */
            memset(&t->step[(i + j) % len], 0, sizeof(step_t));
        for (j = old; j < (uint32_t)v && j < len; j++) {   /* longer: ties over what was there */
            memset(&t->step[(i + j) % len], 0, sizeof(step_t));
            t->step[(i + j) % len].time = ST_TIE;
        }
        break;
    }
    case P_STYPE:
        memset(s, 0, sizeof *s);
        s->time = (uint8_t)v;
        if (v == ST_NOTE) {
            s->note[0] = (uint8_t)default_note(t);
            s->n = 1;
            s->vel = 100;
            if (IS_DRUM(trk_i(t))) {
                s->n = 0;
                s->hit = (uint8_t)(1u << ui.lane);
            }
        }
        break;
    case P_SACC:
        if (IS_DRUM(trk_i(t)))
            s->acc = (uint8_t)(v ? s->acc | 1u << ui.lane : s->acc & ~(1u << ui.lane));
        else
            s->flags = (uint8_t)(v ? s->flags | SF_ACC : s->flags & ~SF_ACC);
        break;
    case P_SSLIDE: s->flags = (uint8_t)(v ? s->flags | SF_SLIDE : s->flags & ~SF_SLIDE); break;
    case P_SCHORD:
        if (s->time == ST_NOTE && s->n)
            step_chord(t, s, (uint32_t)v);
        break;
    case P_SHIT:
        s->hit = (uint8_t)(v ? s->hit | 1u << ui.lane : s->hit & ~(1u << ui.lane));
        s->acc &= s->hit;
        s->time = s->hit ? ST_NOTE : ST_REST;
        s->vel = s->vel ? s->vel : 100;
        break;
    }
    gen_unlock();
}
static void pset(uint32_t id, track_t *t, int32_t v)
{
    patch_t *s = &t->s;
    pdesc_t d = pdesc_of(id, t);
    v = clamp(v, d.min, d.max);
    switch (id) {
    case P_KCUT: t->k_cut = (int8_t)v; break;
    case P_KRES: t->k_res = (int8_t)v; break;
    case P_KDEC: t->k_dec = (int8_t)v; break;
    case P_KLVL: t->k_lvl = (uint8_t)v; break;
    case P_WAVE1: case P_COLOR: s->wave1 = (uint8_t)v; break;
    case P_WAVE2: s->wave2 = (uint8_t)v; break;
    case P_DETUNE: s->detune = (int8_t)v; break;
    case P_MIX2: s->mix2 = (uint8_t)v; break;
    case P_OCT2: s->oct2 = (int8_t)v; break;
    case P_SUB: s->sub = (uint8_t)v; break;
    case P_NOISE: s->noise = (uint8_t)v; break;
    case P_GLIDE: s->glide = (uint8_t)v; break;
    case P_KEYTRK: s->keytrk = (uint8_t)v; break;
    case P_MONO: gen_lock(); track_panic(trk_i(t)); s->mono = (uint8_t)v; gen_unlock(); break;
    case P_CUT: s->cutoff = (uint8_t)v; break;
    case P_RES: s->res = (uint8_t)v; break;
    case P_FENV: s->fenv = (uint8_t)v; break;
    case P_FDEC: s->fdec = (uint8_t)v; break;
    case P_FATT: s->fatt = (uint8_t)v; break;
    case P_FSUS: s->fsus = (uint8_t)v; break;
    case P_FREL: s->frel = (uint8_t)v; break;
    case P_RATIO1: s->fm_ratio[0] = (uint8_t)v; break;
    case P_RATIO2: s->fm_ratio[1] = (uint8_t)v; break;
    case P_RATIO3: s->fm_ratio[2] = (uint8_t)v; break;
    case P_INDEX: s->fm_index = (uint8_t)v; break;
    case P_IDEC: s->fm_idec = (uint8_t)v; break;
    case P_FB: s->fm_fb = (uint8_t)v; break;
    case P_ALGO: s->fm_algo = (uint8_t)v; break;
    case P_SAWN: s->saw_n = (uint8_t)v; break;
    case P_SPREAD: s->saw_spread = (uint8_t)v; break;
    case P_PARTS: s->add_n = (uint8_t)v; break;
    case P_TILT: s->add_tilt = (uint8_t)v; break;
    case P_KIT: s->kit = (uint8_t)v; break;
    case P_DTUNE: s->dtune = (int8_t)v; break;
    case P_DDEC: s->ddec = (uint8_t)v; break;
    case P_ATK: s->atk = (uint8_t)v; break;
    case P_DEC: s->dec = (uint8_t)v; break;
    case P_SUS: s->sus = (uint8_t)v; break;
    case P_REL: s->rel = (uint8_t)v; break;
    case P_LRATE: s->lfo_rate = (uint8_t)v; break;
    case P_LDEPTH: s->lfo_depth = (uint8_t)v; break;
    case P_LDEST: s->lfo_dest = (uint8_t)v; break;
    case P_LWAVE: s->lfo_wave = (uint8_t)v; break;
    case P_LEVEL: s->level = (uint8_t)v; break;
    case P_PAN: s->pan = (int8_t)v; break;
    case P_DLY: s->dly = (uint8_t)v; break;
    case P_REV: s->rev = (uint8_t)v; break;
    case P_TUNE: s->tune = (int8_t)v; break;
    case P_FINE: s->fine = (int8_t)v; break;
    case P_DUAL: gen_lock(); track_panic(trk_i(t)); s->dual = (uint8_t)v; gen_unlock(); break;
    case P_DMIX: s->dmix = (uint8_t)v; break;
    case P_TOCT: gen_lock(); track_panic(trk_i(t)); t->octave = (int8_t)v; gen_unlock(); break;
    case P_LEN:
        t->len = (uint8_t)v;
        t->eu_hits = (uint8_t)(t->eu_hits > v ? v : t->eu_hits);
        t->eu_rot = (uint8_t)(t->eu_rot >= v ? v - 1 : t->eu_rot);
        if (ui.page == PG_SEQ && ui.sub[PG_SEQ] == SQ_EUCLID && ui.eu_snap)
            euclid_now(t);
        break;
    case P_GATE: t->gate = (uint8_t)v; break;
    case P_SHIFT: rotate_steps(t, v - shift_acc); shift_acc = v; break;
    case P_SEQON: t->seq_off = (uint8_t)!v; break;
    case P_MASTER: t->master = (uint8_t)v; break;
    case P_MUT: t->gen_mut = (uint8_t)v; break;
    case P_TYPE:
        if ((uint32_t)v != t->type) {
            undo_burst(0x7000u + trk_i(t));
            gen_type(t, (uint32_t)v);
            hist_push(trk_i(t));
            ui.sub[PG_EDIT] = 0;
            ui.eu_snap = 0;
            ui_say(TYPE_NAME[t->type % TY_N]);
        }
        break;
    case P_DENS: t->gen_dens = (uint8_t)v; break;
    case P_VAR: t->gen_var = (uint8_t)v; break;
    case P_STYLE: t->gen_style = (uint8_t)v; break;
    case P_RANGE: t->gen_range = (int8_t)v; break;
    case P_SNOTE: case P_SVEL: case P_SLEN: case P_STYPE: case P_SACC: case P_SSLIDE: case P_SCHORD: case P_SHIT:
        step_set(t, id, v);
        break;
    case P_SLANE: ui.lane = (uint8_t)v; break;
    case P_EHITS: t->eu_hits = (uint8_t)v; euclid_now(t); break;
    case P_EROT: t->eu_rot = (uint8_t)v; euclid_now(t); break;
    case P_ELANE: t->eu_lane = (uint8_t)v; ui.eu_snap = 0; break;   /* (another lane: the one before is kept) */
    case P_ROOT: if ((uint32_t)v != song.root) song_set_key((uint32_t)v, song.scale); break;
    case P_SCALE: if ((uint32_t)v != song.scale) song_set_key(song.root, (uint32_t)v); break;
    case P_PROG: if ((uint32_t)v != song.prog_i) song_set_prog((uint32_t)v); break;
    case P_OCT: song.octave = (int8_t)v; break;
    case P_TRANSP: gen_lock(); { uint32_t i; for (i = 0; i < NTRK; i++) track_panic(i); } song.transpose = (int8_t)v; gen_unlock(); break;
    case P_SWING: song.swing = (uint8_t)v; break;
    case P_DLYT: song.dly_div = (uint8_t)v; break;
    case P_DLYFB: song.dly_fb = (uint8_t)v; break;
    case P_RSIZE: song.rev_size = (uint8_t)v; break;
    case P_RDAMP: song.rev_damp = (uint8_t)v; break;
    case P_LV1: case P_LV2: case P_LV3: case P_LV4: case P_LV5: case P_LV6: trk[id - P_LV1].s.level = (uint8_t)v; break;
    case P_PN1: case P_PN2: case P_PN3: case P_PN4: case P_PN5: case P_PN6: trk[id - P_PN1].s.pan = (int8_t)v; break;
    case P_SMP: {                                /* another slot (the loop: its slices again when its length differs) */
        uint32_t was = smp_slices(s->smp);
        gen_lock(); track_panic(trk_i(t)); s->smp = (uint8_t)v; gen_unlock();
        if (IS_LOOP(trk_i(t)) && smp_slices(s->smp) != was) {
            undo_burst(0x7100u);
            gen_pattern(t);
        }
        break;
    }
    case P_SSTART: s->sstart = (uint8_t)v; break;
    case P_LX0: case P_LX1: case P_LX2: case P_LX3: case P_LX4: case P_LX5: case P_LX6: case P_LX7: {
        uint32_t vx = ui.sub[PG_EDIT] % LX_N, k2 = id - P_LX0;
        s->lx[vx][k2] = (uint8_t)(LX_KIND[vx] == LK_DRUM && k2 == 4u ? v * 26 : v);
        break;
    }
    case P_MSLOT: ui.mslot = (uint8_t)v; break;
    case P_MACT: ui.mact = (uint8_t)v; break;
    case P_RECMODE: song.rec_mode = (uint8_t)v; ui.menu_dirty = 1; break;
    case P_CLICK: song.click = (uint8_t)v; ui.menu_dirty = 1; break;
    case P_MIDIIN: song.midi_in = (uint8_t)v; ui.menu_dirty = 1; break;
    case P_THEME: song.theme = (uint8_t)v; palette_set(song.theme); ui.force = 1; ui.menu_dirty = 1; break;
    case P_SSLOT: ui.sslot = (uint8_t)v; break;
    case P_GSLOT: ui.gslot = (uint8_t)v; break;
    case P_SACT: ui.sact = (uint8_t)v; break;
    default: break;
    }
}
/* id's value as the card shows it: val (M), unit (S) */
static void fmt_u(char *b, uint32_t v)
{
    char t[12];
    uint32_t n = 0, i;
    do t[n++] = (char)('0' + v % 10u); while ((v /= 10u) && n < 11u);
    for (i = 0; i < n; i++)
        b[i] = t[n - 1u - i];
    b[n] = 0;
}
static void fmt_s(char *b, int32_t v, int plus)  /* signed; plus: "+3" */
{
    if (v < 0) { *b++ = '-'; v = -v; }
    else if (plus && v > 0) *b++ = '+';
    fmt_u(b, (uint32_t)v);
}
static void fmt_dec(char *b, uint32_t v, uint32_t div)   /* v / div with one decimal: 1.5 */
{
    uint32_t n;
    fmt_u(b, v / div);
    n = str_len(b);
    b[n] = '.';
    fmt_u(b + n + 1, v % div * 10u / div);
}
static void fmt_note(char *b, uint32_t n)         /* 60: C4 */
{
    str_cpy(b, NOTE_NAME[n % 12u], 4);
    fmt_s(b + str_len(b), (int32_t)(n / 12u) - 1, 0);
}
static void pformat(uint32_t id, const track_t *t, char *val, const char **unit)
{
    pdesc_t d = pdesc_of(id, t);
    int32_t v = pget(id, t);
    *unit = "";
    switch (d.fmt) {
    case FM_PCT: fmt_u(val, (uint32_t)(v * 100 + 63) / 127u); *unit = "%"; break;
    case FM_X8: fmt_u(val, (uint32_t)v * 100u / 8u); *unit = "%"; break;   /* (8 = as the sound is) */
    case FM_BIP: fmt_s(val, v, 1); break;
    case FM_TIME: {
        uint32_t ms = TIME_MS[v & 127];
        if (ms < 1000u) { fmt_u(val, ms); *unit = "MS"; }
        else { fmt_dec(val, ms, 1000u); *unit = "S"; }
        break;
    }
    case FM_CUT: {
        uint32_t hz = CUT_HZ[v & 127];
        if (hz < 1000u) { fmt_u(val, hz); *unit = "HZ"; }
        else { fmt_dec(val, hz, 1000u); *unit = "KHZ"; }
        break;
    }
    case FM_HZ: {
        uint32_t c = LFO_CHZ[v & 127];
        if (c < 1000u) { fmt_dec(val, c, 100u); }
        else fmt_u(val, c / 100u);
        *unit = "HZ";
        break;
    }
    case FM_RATIO: fmt_dec(val, (uint32_t)v, 16u); break;
    case FM_SEMI16: fmt_u(val, (uint32_t)v * 100u / 16u); *unit = "CT"; break;
    case FM_SEMI: fmt_s(val, v, 1); *unit = "ST"; break;
    case FM_CENT: fmt_s(val, v, 1); *unit = "CT"; break;
    case FM_PAN:
        if (!v) str_cpy(val, "C", 4);
        else { val[0] = v < 0 ? 'L' : 'R'; fmt_u(val + 1, (uint32_t)(v < 0 ? -v : v) * 100u / 64u); }
        break;
    case FM_NAME: str_cpy(val, d.names && v >= 0 && v <= d.max ? d.names[v] : "-", 12); break;
    case FM_OCT: fmt_s(val, v, 1); break;
    case FM_STEPS: fmt_u(val, (uint32_t)v); break;
    case FM_NOTE: {
        const step_t *s = &t->step[cur_idx(t)];
        if (s->time == ST_NOTE && s->n && IS_LOOP(trk_i(t))) { val[0] = 'S'; fmt_u(val + 1, (uint32_t)v + 1u); }
        else if (s->time == ST_NOTE && s->n) fmt_note(val, (uint32_t)v);
        else str_cpy(val, "---", 4);
        break;
    }
    case FM_SLOT: fmt_u(val, (uint32_t)v + 1u); break;
    default: fmt_s(val, v, 0); break;
    }
}

/* -------------------------------------------------------------- pages --- */
static const uint8_t PAGE_BTN[PG_N] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SCL, B_SEQ, B_ARP, B_GLO, B_SAVE, B_HOME};
static const uint8_t PAGE_KC[PG_N] = {KC_HOME, KC_EDIT, KC_ENV, KC_LFO, KC_FX, KC_SCL, KC_SEQ, KC_ARP, KC_GLO, KC_SAVE,
                                      KC_HOME};
typedef struct { const char *name; uint8_t p[8]; } sub_t;   /* p[0..3]: the knobs, p[4..7]: with SHIFT */
#define FILTER_SUB {"FILTER", {P_CUT, P_RES, P_FENV, P_KEYTRK, P_FATT, P_FDEC, P_FSUS, P_FREL}}
#define VOICE_SUB {"VOICE", {P_MONO, P_GLIDE, P_DUAL, P_DMIX, P_TUNE, P_FINE, P_TOCT, 0}}
static const sub_t SUB_SUB[] = {{"OSC", {P_WAVE1, P_WAVE2, P_DETUNE, P_MIX2, P_OCT2, P_SUB, P_NOISE, 0}},
                                FILTER_SUB, VOICE_SUB};
static const sub_t SUB_ACID[] = {{"ACID", {P_WAVE1, P_GLIDE, P_KEYTRK, P_SUB, 0, 0, 0, 0}}, FILTER_SUB, VOICE_SUB};
static const sub_t SUB_FM[] = {{"FM", {P_RATIO2, P_RATIO3, P_INDEX, P_IDEC, P_RATIO1, P_FB, P_ALGO, 0}},
                               FILTER_SUB, VOICE_SUB};
static const sub_t SUB_SAW[] = {{"SAW", {P_SAWN, P_SPREAD, P_DETUNE, P_SUB, P_NOISE, P_GLIDE, 0, 0}}, FILTER_SUB, VOICE_SUB};
static const sub_t SUB_ADD[] = {{"ADD", {P_PARTS, P_TILT, P_GLIDE, P_MONO, 0, 0, 0, 0}}, VOICE_SUB};
static const sub_t SUB_NOISE[] = {{"NOISE", {P_COLOR, P_CUT, P_RES, P_KEYTRK, P_FENV, P_FATT, P_FDEC, P_FSUS}}, VOICE_SUB};
static const sub_t SUB_KIT[] = {{"KIT", {P_KIT, P_DTUNE, P_DDEC, P_LEVEL, 0, 0, 0, 0}}};
static const sub_t SUB_HOME[] = {{"HOME", {P_KCUT, P_KRES, P_KDEC, P_KLVL, P_TUNE, P_FINE, P_TOCT, P_TRANSP}}};
static const sub_t SUB_HOME_DRM[] = {{"HOME", {P_KCUT, P_KRES, P_KDEC, P_KLVL, P_DTUNE, P_DDEC, P_KIT, P_TRANSP}}};
static const sub_t SUB_ENV[] = {{"ENVELOPE", {P_ATK, P_DEC, P_SUS, P_REL, P_FATT, P_FDEC, P_FSUS, P_FREL}}};
static const sub_t SUB_LFO[] = {{"LFO", {P_LRATE, P_LDEPTH, P_LDEST, P_LWAVE, P_TUNE, P_FINE, P_GLIDE, P_KEYTRK}}};
static const sub_t SUB_FX[] = {{"FX", {P_LEVEL, P_PAN, P_DLY, P_REV, P_DLYT, P_DLYFB, P_RSIZE, P_RDAMP}}};
static const sub_t SUB_SCL[] = {{"SCALE", {P_ROOT, P_SCALE, P_PROG, P_OCT, P_TRANSP, P_SWING, 0, 0}}};
static const sub_t SUB_SEQ_MEL[] = {{"STEP", {P_SNOTE, P_SVEL, P_SLEN, P_STYPE, P_SACC, P_SSLIDE, P_SCHORD, 0}},
                                    {"EUCLID", {P_LEN, P_EHITS, P_EROT, 0, 0, 0, 0, 0}},
                                    {"SEQUENCE", {P_LEN, P_GATE, P_SWING, P_SHIFT, P_SEQON, P_MASTER, P_TOCT, P_MUT}}};
static const sub_t SUB_SEQ_DRM[] = {{"STEP", {P_SLANE, P_SHIT, P_SVEL, P_SACC, 0, 0, 0, 0}},
                                    {"EUCLID", {P_LEN, P_EHITS, P_EROT, P_ELANE, 0, 0, 0, 0}},
                                    {"SEQUENCE", {P_LEN, P_GATE, P_SWING, P_SHIFT, P_SEQON, P_MASTER, 0, P_MUT}}};
static const sub_t SUB_GEN[] = {{"GENERATOR", {P_DENS, P_VAR, P_STYLE, P_RANGE, P_MUT, P_TYPE, 0, 0}}};
static const sub_t SUB_GLO[] = {{"MIX 1-4", {P_LV1, P_LV2, P_LV3, P_LV4, P_PN1, P_PN2, P_PN3, P_PN4}},
                                {"MIX 5-6", {P_LV5, P_LV6, 0, 0, P_PN5, P_PN6, 0, 0}}};
#define LXR_SUB(n) {n, {P_LX0, P_LX1, P_LX2, P_LX3, P_LX4, P_LX5, P_LX6, P_LX7}}
static const sub_t SUB_LXR[LX_N] = {LXR_SUB("KICK"), LXR_SUB("TOM"), LXR_SUB("PERC"), LXR_SUB("SNARE"), LXR_SUB("CYMBAL"),
                                    LXR_SUB("HIHAT")};
static const sub_t SUB_LXKIT[] = {{"LXR KIT", {P_DTUNE, P_DDEC, P_LEVEL, P_PAN, P_DLY, P_REV, 0, 0}}};
static const sub_t SUB_SMP_KICK[] = {{"SAMPLE", {P_SMP, P_SSTART, P_DTUNE, P_LEVEL, P_CUT, P_RES, 0, 0}}};
static const sub_t SUB_SMP_LOOP[] = {{"LOOP", {P_SMP, P_LEVEL, P_CUT, P_RES, P_PAN, P_DLY, P_REV, 0}}};
static const sub_t SUB_SEQ_LOOP[] = {{"STEP", {P_SNOTE, P_SVEL, P_STYPE, P_SACC, 0, 0, 0, 0}},
                                     {"EUCLID", {P_LEN, P_EHITS, P_EROT, 0, 0, 0, 0, 0}},
                                     {"SEQUENCE", {P_LEN, P_GATE, P_SWING, P_SHIFT, P_SEQON, P_MASTER, 0, P_MUT}}};
static const sub_t SUB_SAVE[] = {{"SOUND", {P_SSLOT, P_SACT, 0, 0, 0, 0, 0, 0}}, {"SONG", {P_GSLOT, P_SACT, 0, 0, 0, 0, 0, 0}},
                                 {"SAMPLES", {P_MSLOT, P_MACT, 0, 0, 0, 0, 0, 0}}, {"UNDO", {0, 0, 0, 0, 0, 0, 0, 0}}};
static const sub_t SUB_MENU[] = {{"MENU", {0, 0, 0, 0, 0, 0, 0, 0}}};
static const uint8_t MENU_ROWS[] = {P_TRANSP, P_RECMODE, P_CLICK, P_MIDIIN, P_THEME, P_SWING, P_VERSION};
/* the pages of page pg on track t (EDIT: the engine's; ENV on drums: the kit) */
static const sub_t *subs_of(uint32_t pg, const track_t *t, uint32_t *n)
{
    uint32_t e = t->s.engine, drum = IS_DRUM(trk_i(t));
    *n = 1;
    switch (pg) {
    case PG_EDIT:
        if (e == E_SAMPLE) return IS_LOOP(trk_i(t)) ? SUB_SMP_LOOP : SUB_SMP_KICK;
        if (IS_LOOP(trk_i(t))) return SUB_SMP_LOOP;
        if (e == E_LXR) { *n = LX_N; return SUB_LXR; }
        if (drum || e == E_DRUM) return SUB_KIT;
        if (e == E_SUB) { *n = NELEM(SUB_SUB); return SUB_SUB; }
        if (e == E_ACID) { *n = NELEM(SUB_ACID); return SUB_ACID; }
        if (e == E_FM) { *n = NELEM(SUB_FM); return SUB_FM; }
        if (e == E_SAW) { *n = NELEM(SUB_SAW); return SUB_SAW; }
        if (e == E_NOISE) { *n = NELEM(SUB_NOISE); return SUB_NOISE; }
        *n = NELEM(SUB_ADD);
        return SUB_ADD;
    case PG_ENV: if (e == E_LXR) return SUB_LXKIT;
        return e == E_SAMPLE || IS_LOOP(trk_i(t)) ? (IS_LOOP(trk_i(t)) ? SUB_SMP_LOOP : SUB_SMP_KICK) : drum ? SUB_KIT : SUB_ENV;
    case PG_LFO: return SUB_LFO;
    case PG_FX: return SUB_FX;
    case PG_SCL: return SUB_SCL;
    case PG_SEQ: *n = 3; return drum ? SUB_SEQ_DRM : IS_LOOP(trk_i(t)) ? SUB_SEQ_LOOP : SUB_SEQ_MEL;
    case PG_GEN: return SUB_GEN;
    case PG_GLO: *n = NELEM(SUB_GLO); return SUB_GLO;
    case PG_SAVE: *n = NELEM(SUB_SAVE); return SUB_SAVE;
    case PG_MENU: return SUB_MENU;
    default: return drum ? SUB_HOME_DRM : SUB_HOME;
    }
}
static uint32_t sub_cur(uint32_t pg)
{
    uint32_t n;
    subs_of(pg, TSEL, &n);
    return ui.sub[pg % PG_N] % n;
}
static int on_step(void) { return ui.page == PG_SEQ && sub_cur(PG_SEQ) == SQ_STEP; }

/* ------------------------------------------------------------- layers --- */
enum { GT_SONG, GT_TRACK, GT_SOUND, GT_PART, GT_KEY, GT_COOK, GT_CLEAR, GT_REV, GT_LEFT, GT_RIGHT,
       GT_RHYTHM, GT_NOTES, GT_OSC, GT_FILTER, GT_ENV, GT_OCTAVE, GT_N };
static const char *const GT_NAME[GT_N] = {"SONG", "TRACK", "SOUND", "PART", "KEY", "COOK", "CLEAR", "REVERSE",
                                          "SHIFT <", "SHIFT >", "RHYTHM", "NOTES", "OSC", "FILTER", "ENV", "OCTAVE"};
enum { GB_COPY, GB_PASTE, GB_DOUBLE, GB_N };      /* the black keys */
static const char *const GB_NAME[GB_N] = {"COPY", "PASTE", "DOUBLE"};
static const char *const ENGINE_SHORT[E_N] = {"SUB", "ACID", "FM", "SAW", "ADD", "NOISE", "KIT", "SAMPLE", "LXR"};
static const char W_NOTE[16] = {'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'};

static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)            /* white key k's place 0..15, black's 0..10 */
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += key_black(i) == key_black(k);
    return n;
}
static uint32_t key_note(uint32_t k) { return (uint32_t)clamp(53 + (int32_t)k + 12 * song.octave, 12, 120); }

/* ------------------------------------------------------------ actions --- */
static void cook(track_t *t)                     /* COOK: a few notes a scale step up or down, a few hits moved */
{
    uint32_t len = clamp(t->len, 1, NSTEP), i, k;
    gen_lock();
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        if (s->time != ST_NOTE || !chance(20))
            continue;
        if (IS_DRUM(trk_i(t))) {                 /* a hit to the next or previous step */
            step_t *d = &t->step[(i + (chance(50) ? 1u : len - 1u)) % len];
            uint32_t l = s->hit & (0u - s->hit);  /* (its lowest lane) */
            if (l == 1u && !i)
                continue;                        /* (the kick on 1 stays) */
            s->hit &= (uint8_t)~l;
            d->hit |= (uint8_t)l;
            d->time = ST_NOTE;
            d->vel = d->vel ? d->vel : 100;
            if (!s->hit)
                s->time = ST_REST;
        } else if (IS_LOOP(trk_i(t))) {         /* the loop: another slice nearby */
            uint32_t sl = smp_slices(t->s.smp);
            s->note[0] = (uint8_t)((s->note[0] + (chance(50) ? 1u : sl - 1u)) % sl);
        } else {
            int32_t sh = chance(50) ? 1 : -1;
            for (k = 0; k < s->n && k < 4u; k++)
                s->note[k] = (uint8_t)clamp(note_deg_shift(s->note[k], song.root, song.scale, sh), 12, 120);
        }
    }
    gen_unlock();
}
static void reverse_steps(track_t *t)
{
    static step_t tmp[NSTEP];
    uint32_t len = clamp(t->len, 1, NSTEP), i;
    memcpy(tmp, t->step, sizeof tmp);
    gen_lock();
    for (i = 0; i < len; i++)
        t->step[i] = tmp[len - 1u - i];
    for (i = 0; i < len; i++)                    /* a run of ties stays after its note */
        if (t->step[i].time == ST_TIE) {
            uint32_t j = i;
            while (j < len && t->step[j].time == ST_TIE)
                j++;
            if (j < len && t->step[j].time == ST_NOTE) {
                step_t nt = t->step[j];
                mmove(&t->step[i + 1u], &t->step[i], (j - i) * sizeof(step_t));
                t->step[i] = nt;
            }
            i = j;
        }
    gen_unlock();
}
static void gen_action(uint32_t a)               /* SEQ held: a white key's generator action */
{
    track_t *t = TSEL;
    if (a >= GT_N)
        return;
    if (a == GT_SONG) {
        one_click();
        return;
    }
    undo_take();
    switch (a) {
    case GT_TRACK: gen_track(t); hist_push(song.sel); break;
    case GT_SOUND: gen_sound(t); hist_push(song.sel); break;
    case GT_PART: gen_pattern(t); break;
    case GT_KEY: gen_key(); say_key("NEW KEY: "); return;
    case GT_COOK: cook(t); break;
    case GT_CLEAR:
        gen_lock();
        memset(t->step, 0, sizeof t->step);
        track_panic(song.sel);
        gen_unlock();
        break;
    case GT_REV: reverse_steps(t); break;
    case GT_LEFT: rotate_steps(t, -1); break;
    case GT_RIGHT: rotate_steps(t, 1); break;
    case GT_RHYTHM: mutate_part(t, 1); break;
    case GT_NOTES:
        if (IS_DRUM(song.sel)) { ui_say("DRUMS: NO NOTES"); return; }
        mutate_part(t, 0);
        break;
    case GT_OSC: mutate_sound(t, PART_OSC); hist_push(song.sel); break;
    case GT_FILTER: mutate_sound(t, PART_FILTER); hist_push(song.sel); break;
    case GT_ENV: mutate_sound(t, PART_ENV); hist_push(song.sel); break;
    case GT_OCTAVE: {
        int32_t o;
        if (IS_DRUM(song.sel)) { ui_say("DRUMS: NO OCTAVE"); return; }
        do o = rr(-1, 1); while (o == t->octave);
        pset(P_TOCT, t, o);
        break;
    }
    }
    ui.eu_snap = 0;
    ui_say(GT_NAME[a]);
}
static void gen_black(uint32_t b)                /* SEQ held: a black key's action */
{
    track_t *t = TSEL;
    if (b == GB_COPY) {
        pattern_copy(t);
        ui_say("COPIED");
    } else if (b == GB_PASTE) {
        undo_take();
        ui_say(pattern_paste(t) ? "PASTED" : "NOTHING TO PASTE");
    } else if (b == GB_DOUBLE) {                 /* the part twice as long, the second half as the first */
        uint32_t len = clamp(t->len, 1, NSTEP);
        if (len * 2u > NSTEP) { ui_say("64 STEPS ALREADY"); return; }
        undo_take();
        gen_lock();
        memcpy(&t->step[len], t->step, len * sizeof(step_t));
        t->len = (uint8_t)(len * 2u);
        gen_unlock();
        ui_say("DOUBLED");
    }
    ui.eu_snap = 0;
}
static void engine_turn(int32_t e)              /* SHIFT + PRESETS: the role's engines */
{
    const uint8_t *L = ROLE_ENGINES[trk_role(song.sel)];
    int32_t n = 0, i, at = 0;
    while (n < 6 && L[n] != 0xFF)
        n++;
    for (i = 0; i < n; i++)
        if (L[i] == TSEL->s.engine)
            at = i;
    at = clamp(at + (e > 0 ? 1 : -1), 0, n - 1);
    if (L[at] == TSEL->s.engine)
        return;
    undo_burst(0x8000u + song.sel);
    gen_sound_engine(TSEL, L[at]);
    hist_push(song.sel);
    ui.sub[PG_EDIT] = 0;
    ui_say(ENGINE_NAME[TSEL->s.engine % E_N]);
}

/* -------------------------------------------------------------- input --- */
static void page_open(uint32_t pg)
{
    uint32_t n;
    if (ui.page == PG_MENU && pg != PG_MENU && ui.menu_dirty) {
        settings_save();
        ui.menu_dirty = 0;
    }
    if (ui.page == pg) {                         /* again: the next of its pages */
        subs_of(pg, TSEL, &n);
        ui.sub[pg] = (uint8_t)((ui.sub[pg] + 1u) % n);
    }
    ui.page = (uint8_t)pg;
    shift_acc = 0;
    ui.eu_snap = 0;
}
static void menu_open(void)
{
    if (ui.page != PG_MENU)
        ui.menu_back = ui.page;
    ui.page = PG_MENU;
}
static uint32_t page_of_button(uint32_t b)
{
    uint32_t p;
    for (p = 0; p < PG_MENU; p++)
        if (PAGE_BTN[p] == b)
            return p;
    return PG_N;
}
static uint32_t layer_of_button(uint32_t b)
{
    return b == B_SEQ ? LY_SEQ : b == B_SCL ? LY_SCL : b == B_EDIT ? LY_EDIT : b == B_ARP ? LY_TYPE : LY_NONE;
}
static uint32_t knob_id(uint32_t c)
{
    uint32_t ns;
    const sub_t *sp;
    if (ui.page == PG_MENU)
        return c == 0u ? MENU_ROWS[ui.menu_row % NELEM(MENU_ROWS)] : 0u;
    sp = subs_of(ui.page, TSEL, &ns);
    return sp[ui.sub[ui.page] % ns].p[(c & 3u) + (ui.shift ? 4u : 0u)];
}
static void knob_turn(uint32_t c, int32_t n)
{
    track_t *t = TSEL;
    uint32_t id = knob_id(c), o;
    pdesc_t d;
    if (!id)
        return;
    d = pdesc_of(id, t);
    if (d.max - d.min > 40 && (n > 1 || n < -1))
        n *= 3;                                  /* a fast turn: bigger steps (wide values) */
    pset(id, t, pget(id, t) + n);
    if (t->master && id < P_SOUND_END)           /* MASTER: the other tracks of its kind follow the turn */
        for (o = 0; o < NTRK; o++)
            if (o != song.sel && IS_DRUM(o) == IS_DRUM(song.sel))
                pset(id, &trk[o], pget(id, &trk[o]) + n);
    ui.hot = (uint8_t)(c + 1u);
    ui.hot_t = fm1_ms | 1u;
}
static void layer_key(uint32_t k)
{
    uint32_t w = key_place(k), t = song.sel;
    if (ui.layer == LY_SEQ) {
        if (!key_black(k))
            gen_action(w);
        else if (w < GB_N)
            gen_black(w);
    } else if (ui.layer == LY_SCL) {
        song_set_key((k + 5u) % 12u, song.scale);   /* (F3 is F) */
        say_key("ROOT: ");
    } else if (ui.layer == LY_EDIT) {
        const uint8_t *L = ROLE_ENGINES[trk_role(t)];
        if (!key_black(k) && w < 6u && L[w] != 0xFF) {
            undo_take();
            gen_sound_engine(&trk[t], L[w]);
            hist_push(t);
            ui.sub[PG_EDIT] = 0;
            ui_say(ENGINE_NAME[trk[t].s.engine % E_N]);
        }
    } else if (ui.layer == LY_TYPE) {
        if (!key_black(k) && w < TY_N)
            pset(P_TYPE, &trk[t], (int32_t)w);
    }
}
static int step_input(void);
/* SHIFT + a black key: direct functions, F#3 .. F#5. F#3 undo and G#3 redo everywhere; the other nine follow the page
 * shown (CX_*): the step editor's step tools, the sound pages' sound tools, SCL's harmony, the generator, the mixer */
enum { A_UNDO, A_REDO, A_COPY, A_PASTE, A_CLEAR, A_NLEFT, A_NRIGHT, A_PART, A_SOUND, A_CLICK, A_TAP,
       A_SCOPY, A_SPASTE, A_SCLEAR, A_SINS, A_SDEL, A_NOCTD, A_NOCTU,
       A_REV, A_DOUBLE, A_HALF, A_RHYTHM,
       A_PCOPY, A_PPASTE, A_OSC, A_FILTER, A_ENV, A_SPREV, A_SNEXT, A_LIVE0,
       A_KEY, A_PROGP, A_PROGN, A_ROOTD, A_ROOTU, A_SCALEP, A_SCALEN, A_TRD, A_TRU,
       A_TRACK, A_NOTES, A_OCTAVE, A_COOK, A_STYLEP, A_STYLEN,
       A_ALLON, A_SOLO, A_MUTE, A_LVLD, A_LVLU, A_PANL, A_PANC, A_PANR, A_N };
static const char *const A_NAME[A_N] = {
    "UNDO", "REDO", "COPY", "PASTE", "CLEAR", "PART <", "PART >", "NEW PRT", "NEW SND", "CLICK", "TAP",
    "COPY ST", "PASTE", "CLR ST", "INSERT", "DELETE", "OCT -", "OCT +",
    "REVERSE", "DOUBLE", "HALF", "RHYTHM",
    "COPY SD", "PASTE", "OSC", "FILTER", "ENV", "SND <", "SND >", "LIVE 0",
    "NEW KEY", "CHORD <", "CHORD >", "ROOT -", "ROOT +", "SCALE <", "SCALE >", "TRANS -", "TRANS +",
    "TRACK", "NOTES", "OCTAVE", "COOK", "STYLE <", "STYLE >",
    "ALL ON", "SOLO", "MUTE", "LEVEL -", "LEVEL +", "PAN L", "PAN C", "PAN R"};
enum { CX_DEF, CX_STEP, CX_SEQ, CX_SOUND, CX_SCL, CX_GEN, CX_MIX, CX_N };
#define SB_N 11u
static const uint8_t CX_KEYS[CX_N][SB_N] = {
    {A_UNDO, A_REDO, A_COPY, A_PASTE, A_CLEAR, A_NLEFT, A_NRIGHT, A_PART, A_SOUND, A_CLICK, A_TAP},
    {A_UNDO, A_REDO, A_SCOPY, A_SPASTE, A_SCLEAR, A_NLEFT, A_NRIGHT, A_SINS, A_SDEL, A_NOCTD, A_NOCTU},
    {A_UNDO, A_REDO, A_COPY, A_PASTE, A_CLEAR, A_NLEFT, A_NRIGHT, A_REV, A_DOUBLE, A_HALF, A_RHYTHM},
    {A_UNDO, A_REDO, A_PCOPY, A_PPASTE, A_SOUND, A_OSC, A_FILTER, A_ENV, A_SPREV, A_SNEXT, A_LIVE0},
    {A_UNDO, A_REDO, A_KEY, A_PROGP, A_PROGN, A_ROOTD, A_ROOTU, A_SCALEP, A_SCALEN, A_TRD, A_TRU},
    {A_UNDO, A_REDO, A_PART, A_TRACK, A_SOUND, A_RHYTHM, A_NOTES, A_OCTAVE, A_COOK, A_STYLEP, A_STYLEN},
    {A_UNDO, A_REDO, A_ALLON, A_SOLO, A_MUTE, A_LVLD, A_LVLU, A_PANL, A_PANC, A_PANR, A_CLICK},
};
static uint32_t shift_cx(void)
{
    switch (ui.page) {
    case PG_SEQ: return sub_cur(PG_SEQ) == SQ_STEP ? CX_STEP : CX_SEQ;
    case PG_EDIT: case PG_ENV: case PG_LFO: case PG_FX: return CX_SOUND;
    case PG_SCL: return CX_SCL;
    case PG_GEN: return CX_GEN;
    case PG_GLO: return CX_MIX;
    default: return CX_DEF;
    }
}
static void sound_browse(int32_t e)              /* PRESETS: back through the track's sounds, on: a new one */
{
    uint32_t t = song.sel;
    if (!hist.n[t])
        hist_push(t);
    else
        hist.p[t][hist.at[t]] = trk[t].s;       /* (the sound as edited: what turning back returns to) */
    if (e < 0 && hist.at[t] > 0) {
        hist.at[t]--;
        set_patch(&trk[t], &hist.p[t][hist.at[t]]);
        ui_say("SOUND BEFORE");
    } else if (e > 0 && hist.at[t] + 1u < hist.n[t]) {
        hist.at[t]++;
        set_patch(&trk[t], &hist.p[t][hist.at[t]]);
        ui_say("SOUND AFTER");
    } else if (e > 0) {
        gen_sound(&trk[t]);
        hist_push(t);
        ui_say("NEW SOUND");
    } else {
        ui_say("FIRST SOUND");
    }
    ui.sub[PG_EDIT] = 0;
}
static struct { step_t s; uint8_t full, drum; } sclip;      /* COPY ST / PASTE (one step) */
static struct { patch_t p; uint8_t full, drum; } pclip;     /* COPY SD / PASTE (a sound) */
static void solo_toggle(uint32_t s)              /* the track alone; again: all back */
{
    uint32_t i, solo = !trk[s].mute;
    for (i = 0; i < NTRK; i++)
        solo &= i == s || trk[i].mute;
    for (i = 0; i < NTRK; i++)
        trk[i].mute = (uint8_t)(solo ? 0u : i != s);
    ui_say(solo ? "ALL ON" : "SOLO");
}
static void step_shift(int ins)                  /* INSERT: the steps from the cursor one later (a rest at it); DELETE */
{
    track_t *t = TSEL;
    uint32_t len = clamp(t->len, 1, NSTEP), i = cur_idx(t);
    undo_burst(0x9200u);
    gen_lock();
    if (ins) {
        mmove(&t->step[i + 1u], &t->step[i], (len - 1u - i) * sizeof(step_t));
        memset(&t->step[i], 0, sizeof(step_t));
    } else {
        mmove(&t->step[i], &t->step[i + 1u], (len - 1u - i) * sizeof(step_t));
        memset(&t->step[len - 1u], 0, sizeof(step_t));
    }
    track_panic(song.sel);
    gen_unlock();
}
static uint32_t tap_t[4], tap_n;
static void tap_tempo(void)                     /* the beat from the last taps (0.3 .. 1 s apart) */
{
    uint32_t now = fm1_ms, d;
    if (tap_n && (now - tap_t[(tap_n - 1u) % 4u] > 1500u))
        tap_n = 0;
    tap_t[tap_n % 4u] = now;
    tap_n++;
    if (tap_n >= 2u) {
        uint32_t n = tap_n > 4u ? 3u : tap_n - 1u;
        d = (now - tap_t[(tap_n - 1u - n) % 4u]) / n;
        if (d >= 300u && d <= 1000u)
            song.bpm = (uint8_t)clamp((int32_t)((60000u + d / 2u) / d), 60, 200);
    }
    ui_say("TAP");
}
/* NOTE < >: the note at the step cursor with its ties (drums: the lane's hit) one step earlier / later, past its
 * neighbour (a note with its ties, or a rest); the cursor follows. Away from the step editor: the whole part */
static void note_move(int32_t dir)
{
    track_t *t = TSEL;
    uint32_t len = clamp(t->len, 1, NSTEP), i = cur_idx(t), L, j, M, k;
    static step_t blk[NSTEP];
    if (!step_input() && !on_step()) {
        undo_burst(0x9000u);
        rotate_steps(t, dir);
        ui_say(dir < 0 ? "PART <" : "PART >");
        return;
    }
    undo_burst(0x9100u);
    if (IS_DRUM(song.sel)) {                     /* the lane's hit swaps places with the neighbour step's */
        step_t *a = &t->step[i], *b = &t->step[(i + len + (uint32_t)dir) % len];
        uint8_t m = (uint8_t)(1u << ui.lane), ha = a->hit & m, hb = b->hit & m, aa = a->acc & m, ab = b->acc & m;
        gen_lock();
        a->hit = (uint8_t)((a->hit & ~m) | hb); b->hit = (uint8_t)((b->hit & ~m) | ha);
        a->acc = (uint8_t)((a->acc & ~m) | ab); b->acc = (uint8_t)((b->acc & ~m) | aa);
        a->time = a->hit ? ST_NOTE : ST_REST; b->time = b->hit ? ST_NOTE : ST_REST;
        a->vel = a->vel ? a->vel : 100; b->vel = b->vel ? b->vel : 100;
        gen_unlock();
        ui.cur = (uint8_t)((i + len + (uint32_t)dir) % len);
        return;
    }
    if (t->step[i].time != ST_NOTE) {
        ui_say("NO NOTE HERE");
        return;
    }
    L = tie_len(t, i);
    if (L >= len)
        return;
    if (dir > 0) {                               /* the block after it: a note with its ties, or one step */
        j = (i + L) % len;
        M = t->step[j].time == ST_NOTE ? tie_len(t, j) : 1u;
        if (L + M > len) M = len - L;
        for (k = 0; k < M; k++) blk[k] = t->step[(j + k) % len];
        for (k = 0; k < L; k++) blk[M + k] = t->step[(i + k) % len];
        gen_lock();
        for (k = 0; k < L + M; k++) t->step[(i + k) % len] = blk[k];
        track_panic(song.sel);
        gen_unlock();
        ui.cur = (uint8_t)((i + M) % len);
    } else {                                     /* the block before it: back over ties to its start */
        M = 1;
        while (M < len - L && M < 16u && t->step[(i + len - M) % len].time == ST_TIE)
            M++;
        j = (i + len - M) % len;
        for (k = 0; k < L; k++) blk[k] = t->step[(i + k) % len];
        for (k = 0; k < M; k++) blk[L + k] = t->step[(j + k) % len];
        gen_lock();
        for (k = 0; k < L + M; k++) t->step[(j + k) % len] = blk[k];
        track_panic(song.sel);
        gen_unlock();
        ui.cur = (uint8_t)j;
    }
    ui.eu_snap = 0;
}
static void shift_action(uint32_t a)
{
    track_t *t = TSEL;
    step_t *st = cur_step(t);
    uint32_t k;
    switch (a) {
    case A_UNDO: undo_do(0); return;
    case A_REDO: undo_do(1); return;
    case A_COPY: gen_black(GB_COPY); return;
    case A_PASTE: gen_black(GB_PASTE); return;
    case A_CLEAR: gen_action(GT_CLEAR); return;
    case A_NLEFT: note_move(-1); return;
    case A_NRIGHT: note_move(1); return;
    case A_PART: gen_action(GT_PART); return;
    case A_SOUND: gen_action(GT_SOUND); return;
    case A_CLICK:
        song.click = (uint8_t)(song.click == CLICK_OFF ? CLICK_ON : CLICK_OFF);
        ui.menu_dirty = 1;
        ui_say(song.click ? "CLICK ON" : "CLICK OFF");
        return;
    case A_TAP: tap_tempo(); return;
    case A_SCOPY: sclip.s = *st; sclip.full = 1; sclip.drum = (uint8_t)IS_DRUM(song.sel); ui_say("STEP COPIED"); return;
    case A_SPASTE:
        if (!sclip.full || sclip.drum != (uint8_t)IS_DRUM(song.sel)) { ui_say("NOTHING TO PASTE"); return; }
        undo_burst(0x9300u);
        gen_lock(); *st = sclip.s; gen_unlock();
        break;
    case A_SCLEAR: undo_burst(0x9400u); gen_lock(); memset(st, 0, sizeof *st); gen_unlock(); break;
    case A_SINS: step_shift(1); break;
    case A_SDEL: step_shift(0); break;
    case A_NOCTD: case A_NOCTU:
        if (st->time != ST_NOTE || NO_NOTES(song.sel)) { ui_say("NO NOTE HERE"); return; }
        undo_burst(0x9500u);
        gen_lock();
        for (k = 0; k < st->n && k < 4u; k++)
            st->note[k] = (uint8_t)clamp(st->note[k] + (a == A_NOCTU ? 12 : -12), 12, 120);
        gen_unlock();
        break;
    case A_REV: gen_action(GT_REV); return;
    case A_DOUBLE: gen_black(GB_DOUBLE); return;
    case A_HALF:
        if (t->len < 2u) return;
        undo_take();
        gen_lock(); t->len = (uint8_t)(t->len / 2u); gen_unlock();
        break;
    case A_RHYTHM: gen_action(GT_RHYTHM); return;
    case A_PCOPY: pclip.p = t->s; pclip.full = 1; pclip.drum = (uint8_t)IS_DRUM(song.sel); ui_say("SOUND COPIED"); return;
    case A_PPASTE:
        if (!pclip.full || pclip.drum != (uint8_t)IS_DRUM(song.sel)) { ui_say("NOTHING TO PASTE"); return; }
        undo_take();
        gen_lock(); track_panic(song.sel); gen_unlock();
        set_patch(t, &pclip.p);
        hist_push(song.sel);
        ui.sub[PG_EDIT] = 0;
        break;
    case A_OSC: gen_action(GT_OSC); return;
    case A_FILTER: gen_action(GT_FILTER); return;
    case A_ENV: gen_action(GT_ENV); return;
    case A_SPREV: sound_browse(-1); return;
    case A_SNEXT: sound_browse(1); return;
    case A_LIVE0: t->k_cut = t->k_res = t->k_dec = 0; t->k_lvl = 8; break;
    case A_KEY: gen_action(GT_KEY); return;
    case A_PROGP: case A_PROGN:
        pset(P_PROG, t, ((int32_t)song.prog_i + (a == A_PROGN ? 1 : -1) + (int32_t)NELEM(PROGS)) % (int32_t)NELEM(PROGS));
        break;
    case A_ROOTD: case A_ROOTU: pset(P_ROOT, t, (song.root + (a == A_ROOTU ? 1u : 11u)) % 12u); say_key("KEY: "); return;
    case A_SCALEP: case A_SCALEN: pset(P_SCALE, t, (song.scale + (a == A_SCALEN ? 1u : SC_N - 1u)) % SC_N); say_key("KEY: "); return;
    case A_TRD: case A_TRU: pset(P_TRANSP, t, song.transpose + (a == A_TRU ? 1 : -1)); break;
    case A_TRACK: gen_action(GT_TRACK); return;
    case A_NOTES: gen_action(GT_NOTES); return;
    case A_OCTAVE: gen_action(GT_OCTAVE); return;
    case A_COOK: gen_action(GT_COOK); return;
    case A_STYLEP: case A_STYLEN: pset(P_STYLE, t, t->gen_style + (a == A_STYLEN ? 1 : -1)); break;
    case A_ALLON: for (k = 0; k < NTRK; k++) trk[k].mute = 0; break;
    case A_SOLO: solo_toggle(song.sel); return;
    case A_MUTE: t->mute ^= 1u; ui_say(t->mute ? "MUTED" : "UNMUTED"); return;
    case A_LVLD: case A_LVLU: pset(P_LEVEL, t, t->s.level + (a == A_LVLU ? 8 : -8)); break;
    case A_PANL: pset(P_PAN, t, -40); break;
    case A_PANC: pset(P_PAN, t, 0); break;
    case A_PANR: pset(P_PAN, t, 40); break;
    }
    ui_say(A_NAME[a % A_N]);
}
static void shift_key(uint32_t k)                /* SHIFT + a white key: F3..B3 mute, C4..F4 solo; black: the page's */
{
    uint32_t w = key_place(k);
    if (key_black(k)) {
        if (w < SB_N)
            shift_action(CX_KEYS[shift_cx()][w]);
        return;
    }
    if (w < NTRK) {
        trk[w].mute ^= 1u;
        ui_say(trk[w].mute ? "MUTED" : "UNMUTED");
    } else if (w < 2u * NTRK) {
        solo_toggle(w - NTRK);
    }
}
/* the step input: on SEQ > STEP (a key: the cursor's step, no advance), or REC armed while stopped (written, then the
 * cursor moves on when the keys are let go) */
static int step_input(void) { return (on_step() && !(song.rec && song.playing)) || (song.rec && !song.playing); }
static void step_key(uint32_t k, int down)
{
    uint32_t t = song.sel, len = clamp(trk[t].len, 1, NSTEP), adv = song.rec && !song.playing;
    track_t *tr = &trk[t];
    if (IS_DRUM(t)) {
        uint32_t w = key_place(k);
        if (!down) {
            if (!ui.step_keys)
                return;
            ui.step_keys &= ~(1u << k);
            if (!ui.step_keys && adv && !on_step())
                ui.cur = (uint8_t)((cur_idx(tr) + 1u) % len);
            return;
        }
        if (on_step()) {                         /* the grid: white keys the bar's steps, black keys the lane */
            if (key_black(k)) {
                if (w < NLANE) {
                    ui.lane = (uint8_t)w;
                    ev_push(EV_ON, t, 1u << w, 110);
                }
            } else {
                uint32_t i = cur_idx(tr) / 16u * 16u + w;
                if (i < len) {
                    step_t *s = &tr->step[i];
                    undo_burst(0x6000u);
                    gen_lock();
                    s->hit ^= (uint8_t)(1u << ui.lane);
                    s->acc &= s->hit;
                    s->time = s->hit ? ST_NOTE : ST_REST;
                    s->vel = s->vel ? s->vel : 100;
                    gen_unlock();
                    ui.cur = (uint8_t)i;
                    if (s->hit & 1u << ui.lane)
                        ev_push(EV_ON, t, 1u << ui.lane, 100);
                }
            }
            return;
        }
        if (!key_black(k) && w < NLANE) {        /* step recording: the lane at the cursor */
            step_t *s = cur_step(tr);
            undo_burst(0x6000u);
            gen_lock();
            s->hit |= (uint8_t)(1u << w);
            s->time = ST_NOTE;
            s->vel = s->vel ? s->vel : 100;
            gen_unlock();
            ui.step_keys |= 1u << k;
            ev_push(EV_ON, t, 1u << w, 110);
        }
        return;
    }
    if (down) {
        uint32_t n = key_note(k);
        step_t *s = cur_step(tr);
        if (IS_LOOP(t)) {                        /* the loop: a white key the slice (of the cursor's bar) */
            if (key_black(k))
                return;
            n = (cur_idx(tr) / 16u * 16u + key_place(k)) % smp_slices(tr->s.smp);
        }
        ui.keynote[k] = (uint8_t)n;
        ui.keytrk[k] = (uint8_t)t;
        ev_push(EV_ON, t, n, 100);
        undo_burst(0x6000u);
        gen_lock();
        if (!ui.step_keys) {                     /* the first key of a chord: the step anew */
            uint8_t vel = s->time == ST_NOTE && s->vel ? s->vel : 100;
            memset(s, 0, sizeof *s);
            s->time = ST_NOTE;
            s->vel = vel;
        }
        if (s->n < 4u)
            s->note[s->n++] = (uint8_t)n;
        gen_unlock();
        ui.last_note = (uint8_t)n;
        ui.step_keys |= 1u << k;
    } else if (ui.step_keys & (1u << k)) {
        ui.step_keys &= ~(1u << k);
        if (!ui.step_keys && adv)
            ui.cur = (uint8_t)((cur_idx(tr) + 1u) % len);
    }
}
static void ui_input(void)
{
    uint32_t p = io_pressed(), held = io_held(), keys = io_keys(), released = ui.held_prev & ~held, k, b, t;
    int32_t e;
    ui.held_prev = held;
    ui.shift = (uint8_t)((held >> B_GLO) & 1u);
    if (p | released | (keys ^ ui.keys))
        ui.ev++;
    if (p) {
        static uint8_t seeded;
        if (!seeded) {                            /* the first press seeds the dice (its moment is random enough) */
            seeded = 1;
            rng_state ^= fm1_ms * 2654435761u + 1u;
        }
    }
    /* PLAY: on the press (SHIFT: from the top) */
    if (p & (1u << B_PLAY)) {
        if (ui.shift) {
            seq_restart();
            ui.btn_used |= 1u << B_GLO;
        } else {
            seq_play(!song.playing);
        }
    }
    /* OCT- OCT+: the keys' octave (SHIFT: transpose); both: 0; on SAVE / MENU (action pages): OCT+ does it, OCT- back */
    if ((held & (3u << B_OCTDN)) == (3u << B_OCTDN)) {
        uint32_t s = fm1_ms - ui.both_t;
        if (p & (3u << B_OCTDN)) {
            if (ui.shift) pset(P_TRANSP, TSEL, 0);
            else song.octave = 0;
        }
        ui.uboot = (uint8_t)(s > 2000u ? (s >= 5000u ? 9u : 1u + (5000u - s) / 1000u) : 0u);
    } else {
        if (ui.uboot && ui.uboot < 9u)
            ui_say("UPDATE CANCELLED");
        ui.uboot = 0;
        ui.both_t = fm1_ms;
        if (ui.shift && (p & (3u << B_OCTDN))) {
            pset(P_TRANSP, TSEL, song.transpose + ((p & (1u << B_OCTUP)) ? 1 : -1));
            ui.btn_used |= 1u << B_GLO;
            ui.hot = 0;
        } else if (ui.page == PG_SAVE && !ui.layer) {
            uint32_t sv = sub_cur(PG_SAVE);
            if (p & (1u << B_OCTUP)) {
                if (sv == SV_UNDO) undo_do(0);
                else if (sv == SV_SOUND) { if (ui.sact) sound_load(ui.sslot); else sound_save(ui.sslot); }
                else if (sv == SV_SAMPLES) smp_action();
                else { if (ui.sact) song_load(ui.gslot); else song_save(ui.gslot); }
            }
            if (p & (1u << B_OCTDN))
                page_open(PG_HOME);
        } else if (ui.page == PG_MENU) {
            if (p & (1u << B_OCTDN))
                page_open(ui.menu_back < PG_MENU ? ui.menu_back : PG_HOME);
        } else {
            if ((p & (1u << B_OCTDN)) && song.octave > -2)
                song.octave--;
            if ((p & (1u << B_OCTUP)) && song.octave < 2)
                song.octave++;
        }
    }
    /* the buttons with a page: held -> their hold function, let go without one -> the page (or REC's tap) */
    for (b = 0; b < NB; b++) {
        uint32_t bit = 1u << b;
        if (b == B_PLAY || b == B_OCTDN || b == B_OCTUP)
            continue;
        if (p & bit) {
            ui.btn_t0[b] = fm1_ms;
            ui.btn_used &= ~bit;
            if (ui.shift && b != B_GLO)
                ui.btn_used |= 1u << B_GLO;      /* (GLO with another button: SHIFT, not the mixer) */
        }
        if (b == B_GLO) {                        /* SHIFT: tapped alone, the mixer */
            if (released & bit) {
                if (!(ui.btn_used & bit))
                    page_open(PG_GLO);
                ui.btn_used &= ~bit;
            }
            continue;
        }
        if ((held & bit) && !(ui.btn_used & bit) && fm1_ms - ui.btn_t0[b] >= HOLD_MS) {
            ui.btn_used |= bit;                  /* (held: no page when let go) */
            if (b == B_SAVE)
                undo_do(ui.shift);
            else if (b == B_REC)
                one_click();
            else if (b == B_HOME)
                menu_open();
            else if (layer_of_button(b) != LY_NONE && !ui.layer)
                ui.layer = (uint8_t)layer_of_button(b);
        }
        if (released & bit) {
            if (ui.layer && ui.layer == layer_of_button(b)) {
                ui.layer = LY_NONE;
            } else if (!(ui.btn_used & bit)) {
                if (b == B_REC) {
                    if (ui.shift) {
                        song.rec_mode = (uint8_t)((song.rec_mode + 1u) % REC_N);
                        ui_say(N_RECMODE[song.rec_mode]);
                        ui.menu_dirty = 1;
                    } else {
                        song.rec = (uint8_t)!song.rec;
                        if (song.rec) {
                            undo_take();
                            ui_say(song.playing ? "RECORDING" : "STEP RECORD");
                        } else {
                            ui_say("REC OFF");
                        }
                    }
                } else if (b == B_SAVE && ui.shift) {
                    undo_do(1);
                } else if (page_of_button(b) < PG_N) {
                    page_open(page_of_button(b));
                }
            }
            ui.btn_used &= ~bit;
        }
    }
    /* encoders */
    if ((e = io_enc(EN_SELECT)) != 0) {
        if (ui.shift) {
            song.swing = (uint8_t)clamp(song.swing + e, 0, 50);
            ui.btn_used |= 1u << B_GLO;
        } else {
            song.bpm = (uint8_t)clamp(song.bpm + e, 60, 200);
        }
    }
    if ((e = io_enc(EN_ALGO)) != 0) {
        if (ui.shift) {                          /* SHIFT: the track's TYPE */
            pset(P_TYPE, TSEL, (int32_t)TSEL->type + (e > 0 ? 1 : -1));
            ui.btn_used |= 1u << B_GLO;
        } else {
            song.sel = (uint8_t)clamp((int32_t)song.sel + e, 0, NTRK - 1);
            shift_acc = 0;
            ui.eu_snap = 0;
            ui.step_keys = 0;
        }
    }
    if ((e = io_enc(EN_PRESET)) != 0) {
        t = song.sel;
        if (ui.shift) {
            engine_turn(e);
            ui.btn_used |= 1u << B_GLO;
        } else if (on_step() || (song.rec && !song.playing)) {   /* the step cursor */
            uint32_t len = clamp(trk[t].len, 1, NSTEP);
            ui.cur = (uint8_t)(((int32_t)cur_idx(&trk[t]) + e % (int32_t)len + (int32_t)len) % (int32_t)len);
        } else if (ui.page == PG_MENU) {
            ui.menu_row = (uint8_t)clamp((int32_t)ui.menu_row + e, 0, (int32_t)NELEM(MENU_ROWS) - 1);
        } else if (ui.page == PG_SAVE && sub_cur(PG_SAVE) != SV_UNDO) {
            knob_turn(0, e);
        } else {                                 /* the track's sounds: back through them, on: a new one */
            sound_browse(e);
        }
    }
    for (k = 0; k < 4u; k++)
        if ((e = io_enc(EN_K1 + k)) != 0) {
            knob_turn(k, e);
            if (ui.shift)
                ui.btn_used |= 1u << B_GLO;
        }
    /* the keys: a layer's actions, SHIFT's mutes, the step input, else the selected track */
    t = song.sel;
    for (k = 0; k < 27u; k++) {
        uint32_t now = (keys >> k) & 1u, was = (ui.keys >> k) & 1u;
        if (now == was)
            continue;
        if (!now) {                              /* let go: its note ends, wherever it went */
            if (ui.keynote[k]) {
                ev_push(EV_OFF, ui.keytrk[k], ui.keynote[k], 0);
                ui.keynote[k] = 0;
            }
            if (ui.step_keys & (1u << k))
                step_key(k, 0);
            continue;
        }
        if (ui.layer) {
            layer_key(k);
            for (b = 0; b < NB; b++)              /* (a key used in the layer: its button does no page) */
                if (layer_of_button(b) == ui.layer)
                    ui.btn_used |= 1u << b;
            continue;
        }
        if (ui.shift) {
            shift_key(k);
            ui.btn_used |= 1u << B_GLO;
            continue;
        }
        if (step_input()) {
            step_key(k, 1);
            continue;
        }
        if (IS_DRUM(t)) {
            if (!key_black(k) && key_place(k) < NLANE)
                ev_push(EV_ON, t, 1u << key_place(k), 110);
            continue;
        }
        if (IS_LOOP(t)) {                        /* the loop: the white keys its first 16 slices */
            if (!key_black(k))
                ev_push(EV_ON, t, key_place(k) % smp_slices(trk[t].s.smp), 110);
            continue;
        }
        ui.keynote[k] = (uint8_t)key_note(k);
        ui.keytrk[k] = (uint8_t)t;
        ev_push(EV_ON, t, ui.keynote[k], 100);
    }
    ui.keys = keys;
    if (ui.hot_t && fm1_ms - ui.hot_t > 1200u)
        ui.hot_t = 0;
    if (ui.msg_t && fm1_ms - ui.msg_t > 1600u)
        ui.msg_t = 0;
}

static void ui_leds(void)
{
    uint32_t k, b, cur = PAGE_BTN[ui.page % PG_N], st = song.step % 16u;
    const track_t *t = TSEL;
    uint32_t blink = (fm1_ms / 250u) & 1u, ci = cur_idx(t);
    for (b = 0; b < NB; b++) {
        int on = b == cur || (b == B_PLAY && song.playing) || (b == B_GLO && ui.shift) ||
                 (b == B_REC && song.rec && (!song.playing || blink)) ||
                 (b == B_SAVE && ui.page == PG_SAVE && undo.n_undo);
        io_led_button(b, on);
    }
    for (k = 0; k < 27u; k++) {
        int on = 0;
        uint32_t w = key_place(k);
        if (ui.layer == LY_SEQ)
            on = key_black(k) ? w < GB_N : w < GT_N;
        else if (ui.layer == LY_EDIT)
            on = !key_black(k) && w < 6u && ROLE_ENGINES[trk_role(song.sel)][w] != 0xFF;
        else if (ui.layer == LY_TYPE)
            on = !key_black(k) && w < TY_N && (w != t->type || blink);
        else if (ui.layer == LY_SCL)
            on = (k + 5u) % 12u == song.root;
        else if (ui.shift)
            on = key_black(k) ? w < SB_N : (w < NTRK && !trk[w].mute) || (w >= NTRK && w < 2u * NTRK);
        else if (on_step() && IS_DRUM(song.sel)) {   /* the lane's hits in the cursor's bar; the cursor blinks */
            uint32_t i = ci / 16u * 16u + w;
            if (key_black(k))
                on = w == ui.lane;
            else
                on = (i < (uint32_t)clamp(t->len, 1, NSTEP) && t->step[i].time == ST_NOTE &&
                      ((t->step[i].hit >> ui.lane) & 1u)) ^ (w == ci % 16u && blink);
        } else if (step_input() && !IS_DRUM(song.sel)) {   /* the cursor's notes */
            const step_t *s = &t->step[ci];
            uint32_t j;
            for (j = 0; s->time == ST_NOTE && j < s->n && j < 4u; j++)
                on |= key_note(k) == s->note[j];
        } else
            on = !key_black(k) && song.playing && w == st;
        io_led_key(k, on);
    }
}

/* ------------------------------------------------------------ drawing --- */
static void mmove(void *d, const void *s, uint32_t n)   /* (overlapping; the freestanding build has no memmove) */
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b)
        while (n--) *a++ = *b++;
    else
        while (n--) a[n] = b[n];
}
static uint32_t hsh(uint32_t h, const char *s) { while (*s) h = h * 33u + (uint8_t)*s++; return h; }

static void draw_head(int force)
{
    char b[24];
    uint32_t blink = song.rec && song.playing ? (fm1_ms / 250u) & 1u : 0u;
    uint32_t sig = song.sel * 7u + song.playing * 131u + song.bpm * 1009u + (uint32_t)(song.octave + 3) * 77777u +
                   (ui.msg_t ? hsh(5u, ui.msg) : 0u) + ui.layer * 99991u + TSEL->type * 523u + ui.shift * 7333u +
                   song.rec * 191u + song.rec_mode * 4099u + blink * 37u + (uint32_t)(song.transpose + 30) * 1301u;
    int32_t x;
    if (!force && sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, H_HEAD, T_BG);
    cv_rrect(4, 4, 16, 16, 3, TRK_COL[song.sel], T_BG);              /* the track: its colour, number and type */
    b[0] = (char)('1' + song.sel);
    b[1] = 0;
    cv_text_in(4, 4 + CAP_IN(S, 16), 16, &AF_S, b, T_BG, TRK_COL[song.sel]);
    x = cv_text(26, CAP_IN(M, H_HEAD), &AF_M, TYPE_NAME[TSEL->type % TY_N], T_TEXT);
    if (song.rec) {                              /* REC: a dot, the mode */
        x += 8;
        cv_rrect(x, 8, 9, 9, 4, blink ? T_BG : C_REC, T_BG);
        if (!ui.msg_t || ui.shift)
            cv_text(x + 12, CAP_IN(S, H_HEAD), &AF_S, song.rec_mode == REC_REPLACE ? "RPL" : "REC", C_REC);
        x += 12;
    }
    if (ui.shift) {
        cv_rrect(150, 4, 46, 16, 3, T_ACCENT, T_BG);
        cv_text_in(150, 4 + CAP_IN(S, 16), 46, &AF_S, "SHIFT", T_BG, T_ACCENT);
    }
    if (ui.msg_t && !ui.shift) {                 /* a message: right of the track */
        cv_text_r(236, CAP_IN(M, H_HEAD), &AF_M, ui.msg, T_ACCENT, T_BG);
    } else {
        fmt_u(b, song.bpm);
        if (!ui.shift) {
            cv_text_r(196, CAP_IN(M, H_HEAD), &AF_M, b, T_THEME, T_BG);
            cv_text(199, CAP_IN(S, H_HEAD), &AF_S, "BPM", T_MID);
        }
        if (song.playing) {                      /* play: a triangle; stop: a square */
            int32_t i;
            for (i = 0; i <= 10; i++)
                cv_rect(227, 7 + i, 1 + (i <= 5 ? i : 10 - i), 1, T_THEME);
        } else {
            cv_rect(227, 9, 8, 8, T_MID);
        }
        if ((song.octave || song.transpose) && !ui.shift && x < 112) {
            char o[6];
            b[0] = 0;
            if (song.octave) {
                fmt_s(o, song.octave, 1);
                str_cpy(b, "O", sizeof b);
                str_cat(b, o, sizeof b);
            }
            if (song.transpose) {
                fmt_s(o, song.transpose, 1);
                str_cat(b, b[0] ? " T" : "T", sizeof b);
                str_cat(b, o, sizeof b);
            }
            cv_text_r(164, CAP_IN(S, H_HEAD), &AF_S, b, T_MID, T_BG);
        }
    }
    cv_blit(0, 0);
}

/* a knob card: label, value + unit, the gauge; hot: just turned */
static void draw_card(uint32_t c, uint32_t id, const track_t *t, int force)
{
    char val[16], key[48];
    const char *unit = "";
    pdesc_t d = pdesc_of(id, t);
    int hot = ui.hot == c + 1u && ui.hot_t;
    int32_t ratio = -1, x;
    uint32_t sig;
    uint16_t surf = ui.shift && id ? T_RAISE : T_SURF;
    val[0] = 0;
    if (id) {
        pformat(id, t, val, &unit);
        ratio = d.max > d.min ? (pget(id, t) - d.min) * 1000 / (d.max - d.min) : -1;
        if (d.fmt == FM_NAME || id == P_SHIFT || d.fmt == FM_SLOT || d.fmt == FM_NOTE)
            ratio = -1;
    }
    str_cpy(key, d.label, sizeof key);
    str_cat(key, val, sizeof key);
    str_cat(key, unit, sizeof key);
    sig = hsh(c * 31u + (uint32_t)hot * 7u + (uint32_t)(ratio + 2) * 101u + ui.shift * 5u, key);
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
static void g_box(void) { cv_begin(240, H_GRAPH, T_BG); cv_rrect(3, 0, GW, GH, 5, T_SURF, T_BG); cv_bg = T_SURF; }
static void g_label(int32_t x, int32_t y, const char *s, uint16_t c) { cv_text_on(x, y, &AF_S, s, c, T_SURF); }
static void g_steps(const track_t *t, int32_t y0, int32_t rows)   /* the pattern: rows of 16 steps */
{
    uint32_t len = clamp(t->len, 1, NSTEP), r, i, cur = song.step % len;
    uint16_t col = TRK_COL[trk_i(t)];
    for (r = 0; r < (uint32_t)rows; r++)
        for (i = 0; i < 16u; i++) {
            uint32_t idx = r * 16u + i;
            const step_t *s = &t->step[idx % NSTEP];
            int32_t x = 9 + (int32_t)i * 14, y = y0 + (int32_t)r * 14;
            uint16_t c = idx >= len ? T_BG : s->time == ST_NOTE ? col : s->time == ST_TIE ? ux_mix(T_SURF, col, 40) : T_RAISE;
            if (song.playing && idx == cur)
                c = s->time == ST_NOTE ? T_TEXT : T_ACCENT;
            cv_rrect(x, y, 12, 12, 2, c, T_SURF);
        }
}
static void g_overview(void)                     /* HOME: the six tracks, their bar playing */
{
    uint32_t i, s;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = clamp(t->len, 1, NSTEP), cur = song.step % len, base = cur / 16u * 16u;
        int32_t y = 3 + (int32_t)i * 19;
        uint16_t c = t->mute ? T_DIM : TRK_COL[i], bg = i == song.sel ? T_RAISE : T_SURF;
        const char *en = t->mute ? "MUTED" : t->seq_off ? "SEQ OFF" : ENGINE_NAME[t->s.engine % E_N];
        if (t->s.engine == E_SAMPLE && !t->mute)
            en = smp_dir[t->s.smp % NSMP].used ? smp_dir[t->s.smp % NSMP].name : IS_LOOP(i) ? "NO LOOP" : "NO SAMPLE";
        if (i == song.sel)
            cv_rrect(6, y - 1, 228, 19, 3, T_RAISE, T_SURF);
        cv_rect(8, y + 1, 3, 15, c);
        cv_text_fit(15, y, &AF_S, TYPE_NAME[t->type % TY_N], t->mute ? T_DIM : T_TEXT, bg, 52);
        for (s = 0; s < 16u; s++) {
            const step_t *st = &t->step[(base + s) % NSTEP];
            uint16_t sc = st->time == ST_NOTE ? c : st->time == ST_TIE ? ux_mix(T_SURF, c, 40) : T_LINE;
            if (song.playing && s == cur % 16u)
                sc = st->time == ST_NOTE ? T_TEXT : T_ACCENT;
            cv_rect(70 + (int32_t)s * 7, y + 4, 6, 9, sc);
        }
        cv_text_fit(184, y, &AF_S, en, c, bg, 48);
    }
}
static void g_adsr(int32_t a, int32_t d, int32_t s, int32_t r, int32_t y0, int32_t top, uint16_t c, int thick)
{
    int32_t x0 = 12, h = y0 - top, ax = x0 + a * 50 / 127, dx = ax + d * 60 / 127, rx = dx + 40, ex = rx + r * 60 / 127;
    int32_t sy = y0 - s * h / 127;
    cv_line_t(x0, y0, ax, top, c, thick);
    cv_line_t(ax, top, dx, sy, c, thick);
    cv_line_t(dx, sy, rx, sy, c, thick);
    cv_line_t(rx, sy, ex, y0, c, thick);
}
static void g_env(const patch_t *s)              /* ENV: the amp envelope bright, the filter's dim (SHIFT: swapped) */
{
    int32_t y0 = 104;
    g_adsr(s->fatt, s->fdec, s->fsus, s->frel, y0, 18, ui.shift ? T_THEME : T_LINE, ui.shift ? 2 : 1);
    g_adsr(s->atk, s->dec, s->sus, s->rel, y0, 18, ui.shift ? T_LINE : T_THEME, ui.shift ? 1 : 2);
    cv_rect(12, y0 + 1, 220, 1, T_LINE);
    g_label(12, 2, ui.shift ? "FILTER ENVELOPE" : "AMP ENVELOPE", T_MID);
    g_label(160, 2, ui.shift ? "AMP: DIM" : "FILTER: DIM", T_DIM);
}
static int32_t lfo_shape(uint32_t w, uint32_t ph, uint32_t i)
{
    int32_t v;
    switch (w) {
    case LW_TRI: v = (int32_t)(ph >> 15); return v < 65536 ? v - 32768 : 98303 - v;
    case LW_SAW: return (int32_t)(ph >> 16) - 32768;
    case LW_SQR: return ph & 0x80000000u ? -32767 : 32767;
    case LW_SH: return (int32_t)((((ph >> 29) + i / 40u) * 2654435761u) >> 16) - 32768;
    default: return sine(ph);
    }
}
static void g_lfo(const track_t *t)              /* LFO: two cycles at the depth; where it is now */
{
    const patch_t *s = &t->s;
    int32_t x, py = 0, mid = 58, amp = s->lfo_dest ? 8 + s->lfo_depth * 40 / 127 : 0;
    uint16_t c = s->lfo_dest ? T_THEME : T_DIM;
    for (x = 0; x < 216; x++) {
        uint32_t ph = (uint32_t)x * (0xFFFFFFFFu / 108u);
        int32_t y = mid - ((lfo_shape(s->lfo_wave, ph, (uint32_t)x) >> 8) * amp >> 7);
        if (x)
            cv_line_t(11 + x - 1, py, 11 + x, y, c, 2);
        py = y;
    }
    cv_rect(11, mid, 216, 1, T_LINE);
    if (s->lfo_dest && song.playing) {
        int32_t y = mid - ((t->lfo_v >> 8) * amp >> 7);
        cv_rrect(222, y - 3, 7, 7, 3, T_ACCENT, T_SURF);
    }
    g_label(12, 104, s->lfo_dest ? "TO" : "LFO OFF", T_MID);
    if (s->lfo_dest)
        g_label(34, 104, N_LDEST[s->lfo_dest % LD_N], T_TEXT);
}
static void g_filter(const patch_t *s)           /* FILTER: the response, the cutoff and its peak (NOISE: its colour) */
{
    int32_t x, xc = 12 + s->cutoff * 212 / 127, peak = s->res * 34 / 127, py = 0, base = 70;
    uint32_t col = s->engine == E_NOISE ? s->wave1 % 3u : 0u;
    for (x = 12; x < 226; x++) {
        int32_t y, dx = x - xc, lp, hpv;
        if (dx < -24) lp = base;
        else if (dx < 0) lp = base - peak * (24 + dx) * (24 + dx) / 576;   /* up to the resonance */
        else lp = base - peak * (dx < 10 ? 10 - dx : 0) / 10 + dx * dx / 30;   /* then down, -12 dB an octave */
        hpv = dx > 24 ? base : dx > 0 ? base - peak * (24 - dx) * (24 - dx) / 576 : base - peak * (dx > -10 ? 10 + dx : 0) / 10 + dx * dx / 30;
        y = col == 0u ? lp : col == 2u ? hpv : (lp > hpv ? lp : hpv);
        y = clamp(y, 12, 112);
        if (x > 12)
            cv_line_t(x - 1, py, x, y, T_THEME, 2);
        py = y;
    }
    cv_rect(xc, 12, 1, 100, T_LINE);
    g_label(14, 104, "ENV", T_MID);
    cv_rrect(40, 107, 60 * s->fenv / 127 + 3, 5, 2, T_THEME, T_SURF);
    if (s->engine == E_NOISE)
        g_label(140, 104, N_COLOR[col], T_MID);
}
static void g_osc(const track_t *t)              /* the engine's pages: what the oscillators are */
{
    const patch_t *s = &t->s;
    char b[24];
    int32_t x, py = 0, k;
    if (s->engine == E_NOISE) {
        g_filter(s);
        return;
    }
    if (s->engine == E_SAMPLE || IS_LOOP(trk_i(t))) {   /* the sample: its peaks; the loop's slices; where it starts */
        const smp_t *d = &smp_dir[s->smp % NSMP];
        char b2[24];
        if (!d->used || !smp_base) {
            g_label(12, 40, IS_LOOP(trk_i(t)) ? "NO LOOP IN THIS SLOT" : "NO SAMPLE IN THIS SLOT", T_TEXT);
            g_label(12, 60, "LOAD ONE WITH THE COMPANION APP", T_MID);
            g_label(12, 74, "(USB): SAVE > SAMPLES ASSIGNS IT", T_MID);
            return;
        }
        for (x = 0; x < 216; x++) {
            uint32_t a = d->len * (uint32_t)x / 216u, e = d->len * (uint32_t)(x + 1) / 216u, j, st = (e - a) / 24u + 1u;
            int32_t mx = 0;
            for (j = a; j < e; j += st) {
                int32_t v = ULAW[smp_base[d->off + j]];
                mx = v > mx ? v : -v > mx ? -v : mx;
            }
            k = mx * 40 >> 15;
            cv_rect(11 + x, 52 - k, 1, 2 * k + 1, T_THEME);
        }
        if (d->kind == SK_LOOP) {
            uint32_t sl = smp_slices(s->smp), j;
            for (j = 0; j <= sl; j += (sl > 32u ? 4u : 1u))
                cv_rect(11 + (int32_t)(j * 216u / sl), 8, 1, j % 16u ? 4 : 92, j % 16u ? T_MID : T_LINE);
        } else if (s->sstart) {
            cv_rect(11 + s->sstart * 216 / 128, 8, 2, 92, T_ACCENT);
        }
        str_cpy(b2, d->name, sizeof b2);
        str_cat(b2, d->kind == SK_LOOP ? "  LOOP " : "  ", sizeof b2);
        if (d->kind == SK_LOOP) fmt_u(b2 + str_len(b2), d->bars ? d->bars : 1u), str_cat(b2, " BAR", sizeof b2);
        g_label(12, 104, b2, T_MID);
        fmt_dec(b2, d->len * 10u / (d->rate ? d->rate : 22050u), 10u);
        str_cat(b2, " S", sizeof b2);
        cv_text_r(228, 104, &AF_S, b2, T_MID, T_SURF);
        return;
    }
    if (s->engine == E_ADD) {                    /* the partials: their ratios and levels */
        for (k = 0; k < s->add_n && k < ADD_N; k++) {
            int32_t px = 14 + (int32_t)(s->add_ratio[k] > 1600 ? 1600 : s->add_ratio[k]) * 208 / 1600;
            int32_t h = s->add_amp[k] * 90 / 127 * (127 - s->add_tilt * k / ADD_N) / 127;
            cv_rect(px, 104 - h, 4, h, T_THEME);
        }
        cv_rect(12, 105, 216, 1, T_LINE);
        g_label(14, 6, "PARTIALS", T_MID);
        return;
    }
    if (s->engine == E_FM) {                     /* the algorithm: three operators, how they feed */
        static const int8_t POS[3][3][2] = {{{100, 82}, {100, 46}, {100, 10}}, {{100, 82}, {60, 36}, {140, 36}},
                                             {{70, 82}, {70, 36}, {150, 82}}};
        uint32_t a = s->fm_algo % 3u, o;
        for (o = 0; o < 3u; o++) {
            int32_t ox = POS[a][o][0], oy = POS[a][o][1];
            char r[8];
            cv_rrect(ox, oy, 40, 26, 4, o ? T_RAISE : T_THEME, T_SURF);
            b[0] = (char)('1' + o);
            b[1] = 0;
            cv_text_in(ox, oy + 2, 18, &AF_M, b, o ? T_TEXT : T_INK, o ? T_RAISE : T_THEME);
            fmt_dec(r, s->fm_ratio[o], 16u);
            cv_text_on(ox + 17, oy + 9, &AF_S, r, o ? T_MID : T_INK, o ? T_RAISE : T_THEME);
        }
        if (a == 0) { cv_rect(119, 36, 2, 10, T_MID); cv_rect(119, 72, 2, 10, T_MID); }
        else if (a == 1) { cv_line_t(80, 62, 112, 82, T_MID, 2); cv_line_t(160, 62, 128, 82, T_MID, 2); }
        else cv_rect(89, 62, 2, 20, T_MID);
        g_label(12, 6, "OP 1 SOUNDS", T_MID);
        return;
    }
    if (s->engine == E_DRUM || s->engine == E_LXR) {   /* the kit: its lanes over the bar (LXR: this page's voice lit) */
        static const char *const LN[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
        uint32_t l, i, cur = song.step % 16u;
        for (l = 0; l < NLANE; l++) {
            g_label(10, 3 + (int32_t)l * 14, LN[l], s->engine == E_LXR && ui.page == PG_EDIT && LX_OF_LANE[l] == ui.sub[PG_EDIT] % LX_N ? T_ACCENT : T_MID);
            for (i = 0; i < 16u; i++) {
                const step_t *st = &t->step[i];
                int on = st->time == ST_NOTE && ((st->hit >> l) & 1u);
                uint16_t c = on ? TRK_COL[trk_i(t)] : T_RAISE;
                if (song.playing && i == cur)
                    c = on ? T_TEXT : T_LINE;
                cv_rrect(32 + (int32_t)i * 12, 4 + (int32_t)l * 14, 10, 11, 2, c, T_SURF);
            }
        }
        return;
    }
    for (x = 0; x < 216; x++) {                  /* SUB, ACID, SAW: a cycle of the mix, drawn plainly */
        uint32_t ph = (uint32_t)x * (0xFFFFFFFFu / 108u);
        int32_t v, y;
        if (s->engine == E_SAW) {
            int32_t n = clamp(s->saw_n, 1, 7), j;
            v = 0;
            for (j = 0; j < n; j++)
                v += (int32_t)((ph + (uint32_t)j * (uint32_t)s->saw_spread * 0x1000000u) >> 16) - 32768;
            v /= n;
        } else {
            uint32_t w = s->wave1;
            v = w == W_SQR ? (ph & 0x80000000u ? -26000 : 26000) : w == W_TRI ? lfo_shape(LW_TRI, ph, 0) :
                w == W_SINE ? sine(ph) : (int32_t)(ph >> 16) - 32768;
            if (s->engine == E_SUB && s->mix2) {
                uint32_t p2 = ph << (s->oct2 > 0 ? 1 : 0), w2 = s->wave2;
                int32_t v2 = w2 == W_SQR ? (p2 & 0x80000000u ? -26000 : 26000) : w2 == W_TRI ? lfo_shape(LW_TRI, p2, 0) :
                             w2 == W_SINE ? sine(p2) : (int32_t)(p2 >> 16) - 32768;
                v = (v + v2 * s->mix2 / 127) * 2 / 3;
            }
        }
        y = 58 - (v >> 10);
        if (x)
            cv_line_t(11 + x - 1, py, 11 + x, y, T_THEME, 2);
        py = y;
    }
    cv_rect(11, 58, 216, 1, T_LINE);
    str_cpy(b, ENGINE_NAME[s->engine % E_N], sizeof b);
    g_label(12, 104, b, T_MID);
}
static void g_voice(const patch_t *s)            /* VOICE: poly / mono, the second voice */
{
    char b[24];
    g_label(12, 6, s->mono ? "MONO: ONE NOTE, GLIDING" : "POLY: CHORDS", T_TEXT);
    cv_rrect(20, 34, 90, 30, 4, T_THEME, T_SURF);
    cv_text_in(20, 34 + CAP_IN(M, 30), 90, &AF_M, "VOICE 1", T_INK, T_THEME);
    cv_rrect(126, 34, 90, 30, 4, s->dual ? T_RAISE : T_BG, T_SURF);
    cv_text_in(126, 34 + CAP_IN(M, 30), 90, &AF_M, s->dual ? N_DUAL[s->dual % DU_N] : "DUAL OFF", s->dual ? T_TEXT : T_DIM,
               s->dual ? T_RAISE : T_BG);
    str_cpy(b, "PITCH ", sizeof b);
    fmt_s(b + str_len(b), s->tune, 1);
    str_cat(b, "  FINE ", sizeof b);
    fmt_s(b + str_len(b), s->fine, 1);
    g_label(12, 80, b, T_MID);
}
static void g_fx(const track_t *t)               /* FX: the track's sends and where it sits; the global effects */
{
    const patch_t *s = &t->s;
    static const char *const L[3] = {"LEVEL", "DELAY", "REVERB"};
    int32_t v[3] = {s->level, s->dly, s->rev}, i, px = 120 + s->pan * 100 / 64;
    char b[24];
    for (i = 0; i < 3; i++) {
        g_label(12, 8 + i * 24, L[i], T_MID);
        cv_rrect(70, 10 + i * 24, 150, 8, 3, T_RAISE, T_SURF);
        cv_rrect(70, 10 + i * 24, 4 + v[i] * 146 / 127, 8, 3, T_THEME, T_RAISE);
    }
    g_label(12, 88, "PAN", T_MID);
    cv_rect(20, 106, 120, 2, T_LINE);
    cv_rect(79, 100, 2, 14, T_MID);
    cv_rrect(80 + s->pan * 60 / 64 - 5, 101, 10, 12, 3, T_ACCENT, T_SURF);
    (void)px;
    str_cpy(b, "DLY ", sizeof b);
    str_cat(b, N_DLYT[song.dly_div % 6u], sizeof b);
    g_label(152, 84, b, ui.shift ? T_TEXT : T_DIM);
    str_cpy(b, "REV ", sizeof b);
    fmt_u(b + 4, (uint32_t)song.rev_size * 100u / 127u);
    str_cat(b, "%", sizeof b);
    g_label(152, 100, b, ui.shift ? T_TEXT : T_DIM);
}
static void g_scale(void)                        /* SCL: the keys F3..G5 with the scale; the chords per bar */
{
    uint32_t k, w = 0, b, cur = song.playing ? (song.step / 16u) % 4u : 4u;
    for (k = 0; k < 27u; k++) {                  /* white keys first, black over them */
        uint32_t pc = (k + 5u) % 12u, in = 0, j;
        for (j = 0; j < 7u; j++)
            in |= SCALES[song.scale % SC_N][j] == (pc + 12u - song.root) % 12u;
        if (!key_black(k)) {
            uint16_t c = pc == song.root ? T_ACCENT : in ? T_TEXT : T_RAISE;
            cv_rrect(10 + (int32_t)w * 14, 8, 12, 50, 2, c, T_SURF);
            w++;
        }
    }
    for (k = 0, w = 0; k < 27u; k++) {
        uint32_t pc = (k + 5u) % 12u, in = 0, j;
        if (!key_black(k)) { w++; continue; }
        for (j = 0; j < 7u; j++)
            in |= SCALES[song.scale % SC_N][j] == (pc + 12u - song.root) % 12u;
        cv_rrect(10 + (int32_t)w * 14 - 5, 8, 9, 30, 2, pc == song.root ? T_ACCENT : in ? T_THEME : T_BG, T_SURF);
    }
    for (b = 0; b < 4u; b++) {                   /* the progression: a cell a bar, the one playing lit */
        int32_t x = 10 + (int32_t)b * 56;
        uint16_t f = b == cur ? T_THEME : T_RAISE;
        cv_rrect(x, 70, 52, 30, 4, f, T_SURF);
        cv_text_in(x, 70 + CAP_IN(M, 30), 52, &AF_M, N_RN[song.prog[b] % 7u], b == cur ? T_INK : T_TEXT, f);
    }
    g_label(10, 106, "BARS 1-4", T_MID);
}
static void g_mixer(void)                        /* GLO: the six strips, their pans under them */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        int32_t x = 8 + (int32_t)i * 38, h = t->s.level * 64 / 127;
        uint16_t c = t->mute ? T_DIM : TRK_COL[i], bg = i == song.sel ? T_RAISE : T_BG;
        int on = (ui.sub[PG_GLO] % 2u == 0u) == (i < 4u);   /* the strips the knobs move now */
        cv_rrect(x, 4, 35, 112, 4, bg, T_SURF);
        cv_rrect(x + 11, 72 - h, 13, h + 2, 3, on ? c : ux_mix(bg, c, 45), bg);
        cv_rect(x + 4, 82, 27, 2, T_LINE);
        cv_rrect(x + 17 + t->s.pan * 13 / 64 - 3, 79, 6, 8, 2, ui.shift && on ? T_ACCENT : T_MID, bg);
        cv_text_fit(x + 2, 92, &AF_S, t->mute ? "MUTE" : TYPE_NAME[t->type % TY_N], c, bg, 32);
    }
}
static void g_gen(const track_t *t)              /* ARP > GEN: the settings and the track's first bar */
{
    uint32_t r = trk_role(trk_i(t));
    g_label(12, 6, "STYLE", T_MID);
    cv_text_on(60, 3, &AF_M, STYLE_NAME[r][t->gen_style % STYLE_N[r]], T_TEXT, T_SURF);
    g_steps(t, 28, 1);
    g_label(12, 50, "SEQ HELD + A KEY: A NEW PART,", T_MID);
    g_label(12, 64, "RHYTHM, NOTES, OSC ... (MUTATE)", T_MID);
    cv_key_hint(12, 90, KC_ARP, "HOLD", 1, T_SURF);
    g_label(92, 92, "+ KEY: THE TYPE", T_TEXT);
}
static void g_step(const track_t *t)             /* SEQ > STEP: the cursor's bar as a piano roll / drum grid */
{
    uint32_t len = clamp(t->len, 1, NSTEP), ci = cur_idx(t), base = ci / 16u * 16u, i, k, play = song.step % len;
    uint16_t col = TRK_COL[trk_i(t)];
    char b[32], o[8];
    if (IS_DRUM(trk_i(t))) {
        static const char *const LN[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
        uint32_t l;
        for (l = 0; l < NLANE; l++) {
            int32_t y = 3 + (int32_t)l * 12;
            if (l == ui.lane)
                cv_rrect(6, y - 1, 228, 12, 2, T_RAISE, T_SURF);
            cv_text_on(10, y - 1, &AF_S, LN[l], l == ui.lane ? T_TEXT : T_MID, l == ui.lane ? T_RAISE : T_SURF);
            for (i = 0; i < 16u; i++) {
                const step_t *st = &t->step[(base + i) % NSTEP];
                int on = base + i < len && st->time == ST_NOTE && ((st->hit >> l) & 1u);
                uint16_t c = base + i >= len ? T_BG : on ? ((st->acc >> l) & 1u ? T_TEXT : col) : T_LINE;
                if (base + i == ci)
                    cv_frame(31 + (int32_t)i * 12, y - 2, 12, 12, T_ACCENT);
                cv_rrect(32 + (int32_t)i * 12, y - 1, 10, 10, 2, c, l == ui.lane ? T_RAISE : T_SURF);
            }
        }
    } else {                                     /* the notes of the bar over their range */
        int32_t lo = 127, hi = 0, span, rh, last[4] = {0, 0, 0, 0}, nl = 0;
        for (i = 0; i < 16u; i++) {
            const step_t *st = &t->step[(base + i) % NSTEP];
            if (base + i < len && st->time == ST_NOTE)
                for (k = 0; k < st->n && k < 4u; k++) {
                    lo = st->note[k] < lo ? st->note[k] : lo;
                    hi = st->note[k] > hi ? st->note[k] : hi;
                }
        }
        if (lo > hi)
            lo = hi = (int32_t)default_note(t);
        span = hi - lo + 1;
        if (span < 13) {
            lo -= (13 - span) / 2;
            span = 13;
        }
        rh = 92 / span;
        rh = rh < 2 ? 2 : rh > 8 ? 8 : rh;
        cv_rrect(8 + (int32_t)(ci % 16u) * 14, 2, 14, 96, 3, T_RAISE, T_SURF);   /* the cursor's column */
        for (i = 0; i < 16u; i++) {
            const step_t *st = &t->step[(base + i) % NSTEP];
            int32_t x = 9 + (int32_t)i * 14;
            uint16_t c = song.playing && base + i == play ? T_TEXT : col;
            if (base + i >= len) {
                cv_rect(x, 96, 12, 2, T_BG);
                nl = 0;
                continue;
            }
            if (st->time == ST_NOTE && st->n) {
                nl = st->n > 4u ? 4 : st->n;
                for (k = 0; k < (uint32_t)nl; k++) {
                    last[k] = st->note[k];
                    cv_rrect(x, 96 - (st->note[k] - lo + 1) * 92 / span, 12, rh + 1, 1,
                             st->flags & SF_ACC ? T_ACCENT : c, base + i == ci ? T_RAISE : T_SURF);
                }
            } else if (st->time == ST_TIE) {
                for (k = 0; k < (uint32_t)nl; k++)
                    cv_rect(x - 2, 96 - (last[k] - lo + 1) * 92 / span + 1, 14, rh - 1, ux_mix(T_SURF, col, 50));
            } else {
                nl = 0;
            }
            cv_rect(x, 99, 12, 1, song.playing && base + i == play ? T_ACCENT : T_LINE);
        }
    }
    str_cpy(b, "STEP ", sizeof b);
    fmt_u(b + str_len(b), ci + 1u);
    str_cat(b, "/", sizeof b);
    fmt_u(o, len);
    str_cat(b, o, sizeof b);
    g_label(10, 104, b, T_TEXT);
    if (song.rec)
        g_label(84, 104, song.playing ? "LIVE REC" : "STEP REC", C_REC);
    else if (!IS_DRUM(trk_i(t))) {
        const step_t *st = &t->step[ci];
        b[0] = 0;
        for (k = 0; st->time == ST_NOTE && k < st->n && k < 3u; k++) {
            if (IS_LOOP(trk_i(t))) { o[0] = 'S'; fmt_u(o + 1, st->note[k] + 1u); }
            else fmt_note(o, st->note[k]);
            str_cat(b, o, sizeof b);
            str_cat(b, " ", sizeof b);
        }
        g_label(84, 104, st->time == ST_TIE ? "TIE" : b[0] ? b : "REST", T_MID);
    }
    if (len > 16u) {
        str_cpy(b, "BAR ", sizeof b);
        fmt_u(b + 4, ci / 16u + 1u);
        str_cat(b, "/", sizeof b);
        fmt_u(o, (len + 15u) / 16u);
        str_cat(b, o, sizeof b);
        cv_text_r(230, 104, &AF_S, b, T_MID, T_SURF);
    }
}
static void g_euclid(const track_t *t)           /* EUCLID: the hits round the circle of LEN steps */
{
    uint32_t len = clamp(t->len, 1, NSTEP), i;
    char b[32], o[8];
    if (len > 32u) {                             /* (long parts: as rows) */
        g_steps(t, 6, 4);
        fmt_u(b, t->eu_hits);
        str_cat(b, " / ", sizeof b);
        fmt_u(o, len);
        str_cat(b, o, sizeof b);
        g_label(12, 104, b, T_TEXT);
        return;
    }
    for (i = 0; i < len; i++) {
        uint32_t ph = 0xFFFFFFFFu / len * i;
        int32_t x, y;
        const step_t *st = &t->step[i];
        int on = st->time == ST_NOTE && (IS_DRUM(trk_i(t)) ? (st->hit >> t->eu_lane) & 1u : st->n != 0);
        x = 76 + (sine(ph) * 44 >> 15);
        y = 54 - (sine(ph + 0x40000000u) * 44 >> 15);
        cv_rrect(x - 4, y - 4, 9, 9, 4, on ? TRK_COL[trk_i(t)] : T_LINE, T_SURF);
        if (song.playing && i == song.step % len)
            cv_frame(x - 6, y - 6, 13, 13, T_ACCENT);
    }
    fmt_u(b, t->eu_hits);
    str_cat(b, " / ", sizeof b);
    fmt_u(o, len);
    str_cat(b, o, sizeof b);
    cv_text_on(140, 30, &AF_M, b, T_TEXT, T_SURF);
    g_label(140, 52, IS_DRUM(trk_i(t)) ? N_LANE[t->eu_lane % NLANE] : "THE NOTES", T_MID);
    g_label(140, 66, "IN THEIR ORDER", IS_DRUM(trk_i(t)) ? T_SURF : T_MID);
}
static void g_menu(void)
{
    uint32_t i;
    for (i = 0; i < NELEM(MENU_ROWS); i++) {
        char val[16];
        const char *unit;
        int32_t y = 3 + (int32_t)i * 16;
        int sel = i == ui.menu_row;
        uint16_t bg = sel ? T_RAISE : T_SURF;
        if (sel)
            cv_rrect(6, y - 1, 228, 16, 3, T_RAISE, T_SURF);
        cv_text_on(12, y, &AF_S, PD[MENU_ROWS[i]].label, sel ? T_TEXT : T_MID, bg);
        pformat(MENU_ROWS[i], TSEL, val, &unit);
        str_cat(val, unit, sizeof val);
        cv_text_r(228, y, &AF_S, val, sel ? T_ACCENT : T_THEME, bg);
    }
}
static void g_save(void)
{
    uint32_t sv = sub_cur(PG_SAVE), i;
    char b[32];
    if (sv == SV_UNDO) {
        str_cpy(b, "UNDO ", sizeof b);
        fmt_u(b + str_len(b), undo.n_undo);
        str_cat(b, "   REDO ", sizeof b);
        fmt_u(b + str_len(b), undo.n_redo);
        cv_text_in(3, 26, GW, &AF_M, b, T_TEXT, T_SURF);
        cv_key_hint(30, 64, KC_OCTUP, "UNDO", undo.n_undo != 0, T_SURF);
        cv_key_hint(120, 64, KC_SAVE, "HOLD", 1, T_SURF);
        g_label(30, 92, "SHIFT + SAVE: REDO", T_MID);
        return;
    }
    if (sv == SV_SAMPLES) {                      /* the 8 sample slots: name, kind, length; the space left */
        for (i = 0; i < NSMP; i++) {
            const smp_t *d = &smp_dir[i];
            int32_t x = 8 + (int32_t)(i % 2u) * 114, y = 3 + (int32_t)(i / 2u) * 23;
            int sel = i == ui.mslot;
            uint16_t f = sel ? T_THEME : T_RAISE;
            cv_rrect(x, y, 110, 21, 3, f, T_SURF);
            fmt_u(b, i + 1u);
            cv_text_on(x + 3, y + 3, &AF_S, b, sel ? T_INK : T_MID, f);
            cv_text_fit(x + 14, y + 3, &AF_S, d->used ? d->name : "-", sel ? T_INK : T_TEXT, f, 62);
            if (d->used)
                cv_text_r(x + 106, y + 3, &AF_S, d->kind == SK_LOOP ? "LOOP" : "SHOT", sel ? T_INK : T_MID, f);
        }
        str_cpy(b, "FREE ", sizeof b);
        fmt_dec(b + 5, smp_free() * 10u / 22050u, 10u);
        str_cat(b, " S", sizeof b);
        cv_key_hint(8, 100, KC_OCTUP, N_MACT[ui.mact % 3u], 1, T_SURF);
        cv_text_r(228, 102, &AF_S, b, T_MID, T_SURF);
        return;
    }
    if (sv == SV_SOUND) {                        /* 16 cells: the slot's sound type */
        for (i = 0; i < NSND_SLOT; i++) {
            int32_t x = 8 + (int32_t)(i % 4u) * 57, y = 3 + (int32_t)(i / 4u) * 24;
            int sel = i == ui.sslot;
            uint16_t f = sel ? T_THEME : T_RAISE;
            cv_rrect(x, y, 54, 21, 3, f, T_SURF);
            fmt_u(b, i + 1u);
            cv_text_on(x + 3, y + 3, &AF_S, b, sel ? T_INK : T_MID, f);
            cv_text_fit(x + 18, y + 3, &AF_S, bank.s[i].used ? TYPE_NAME[bank.s[i].type % TY_N] : "-", sel ? T_INK : T_TEXT,
                        f, 34);
        }
    } else {
        for (i = 0; i < NSONG_SLOT; i++) {
            int32_t x = 8 + (int32_t)(i % 2u) * 114, y = 3 + (int32_t)(i / 2u) * 48;
            int sel = i == ui.gslot;
            uint16_t f = sel ? T_THEME : T_RAISE;
            cv_rrect(x, y, 110, 44, 4, f, T_SURF);
            str_cpy(b, "SONG ", sizeof b);
            fmt_u(b + 5, i + 1u);
            cv_text_on(x + 6, y + 4, &AF_M, b, sel ? T_INK : T_TEXT, f);
            if (song_meta[i].used) {
                str_cpy(b, NOTE_NAME[song_meta[i].root % 12u], sizeof b);
                str_cat(b, " ", sizeof b);
                str_cat(b, SCALE_NAME[song_meta[i].scale % SC_N], sizeof b);
                str_cat(b, " ", sizeof b);
                fmt_u(b + str_len(b), song_meta[i].bpm);
            } else {
                str_cpy(b, "EMPTY", sizeof b);
            }
            cv_text_fit(x + 6, y + 25, &AF_S, b, sel ? T_INK : T_MID, f, 100);
        }
    }
    cv_key_hint(8, 100, KC_OCTUP, ui.sact ? "LOAD" : "SAVE", 1, T_SURF);
    g_label(90, 102, sv == SV_SOUND ? "THE TRACK'S SOUND" : "THE WHOLE SONG", T_MID);
}
/* a layer's map: the white keys as cells (note name, action); the black keys' below */
static void g_layer(void)
{
    uint32_t i, n = 0;
    const char *nm[16];
    uint16_t on[16];
    for (i = 0; i < 16u; i++) { nm[i] = 0; on[i] = T_TEXT; }
    if (ui.layer == LY_SCL) {                    /* any key: the root (the picture as on SCL) */
        g_scale();
        return;
    }
    if (ui.layer == LY_SEQ) {
        for (i = 0; i < GT_N; i++) nm[i] = GT_NAME[i];
        n = GT_N;
    } else if (ui.layer == LY_EDIT) {
        const uint8_t *L = ROLE_ENGINES[trk_role(song.sel)];
        for (i = 0; i < 6u; i++)
            if (L[i] != 0xFF) {
                nm[i] = ENGINE_SHORT[L[i] % E_N];
                on[i] = L[i] == TSEL->s.engine ? T_ACCENT : T_TEXT;
                n = i + 1u;
            }
    } else if (ui.layer == LY_TYPE) {
        for (i = 0; i < TY_N; i++) {
            nm[i] = TYPE_NAME[i];
            on[i] = i == TSEL->type ? T_ACCENT : TYPE_ROLE[i] == ROLE_DRUM ? T_MID : T_TEXT;
        }
        n = TY_N;
    }
    for (i = 0; i < n; i++) {
        int32_t x = 6 + (int32_t)(i % 4u) * 58, y = 3 + (int32_t)(i / 4u) * 25;
        char k[3] = {W_NOTE[i], 0, 0};
        if (!nm[i])
            continue;
        cv_rrect(x, y, 56, 23, 3, T_RAISE, T_SURF);
        cv_text_on(x + 3, y + 5, &AF_S, k, T_MID, T_RAISE);
        cv_text_fit(x + 13, y + 5, &AF_S, nm[i], on[i], T_RAISE, 42);
    }
}
static int shift_map(void)                       /* SHIFT held a while, nothing done with it: its keys shown */
{
    return ui.shift && !(ui.btn_used & (1u << B_GLO)) && fm1_ms - ui.btn_t0[B_GLO] >= 600u;
}
static void g_shift(void)                        /* the black keys' functions, the white keys' mutes */
{
    static const char *const BK[SB_N] = {"F#3", "G#3", "A#3", "C#4", "D#4", "F#4", "G#4", "A#4", "C#5", "D#5", "F#5"};
    uint32_t i;
    for (i = 0; i < SB_N; i++) {
        int32_t x = 6 + (int32_t)(i % 3u) * 77, y = 2 + (int32_t)(i / 3u) * 22;
        cv_rrect(x, y, 74, 20, 3, T_RAISE, T_SURF);
        cv_text_on(x + 3, y + 3, &AF_S, BK[i], T_MID, T_RAISE);
        cv_text_fit(x + 27, y + 3, &AF_S, A_NAME[CX_KEYS[shift_cx()][i]], CX_KEYS[shift_cx()][i] == A_CLICK && song.click ? T_ACCENT : T_TEXT, T_RAISE, 46);
    }
    g_label(166, 70, "WHITE:", T_MID);
    g_label(166, 84, "F-D MUTE", T_TEXT);
    g_label(166, 98, "E-B SOLO", T_TEXT);
}
static uint32_t graph_sig(void)
{
    const track_t *t = TSEL;
    uint32_t h = ui.page * 7u + ui.sub[ui.page] * 131u + song.sel * 977u + ui.layer * 7919u + t->s.engine * 31u +
                 song.playing * 3u + (uint32_t)shift_map() * 999u + ui.ev * 2654435761u + rec.rec_writes * 40503u + ui.shift * 17u, i;
    const uint8_t *p = (const uint8_t *)&t->s;
    for (i = 0; i < sizeof(patch_t); i++)
        h = h * 33u + p[i];
    h = h * 33u + song.root + song.scale * 12u + song.prog[0] + song.prog[1] * 7u + song.prog[2] * 49u + song.prog[3] * 343u;
    for (i = 0; i < NTRK; i++)
        h = h * 33u + trk[i].mute + trk[i].s.level * 2u + trk[i].s.engine * 512u + trk[i].len * 4096u + trk[i].type * 9u +
            (uint32_t)trk[i].s.pan * 77u;
    if (song.playing)
        h = h * 33u + song.step;
    if (ui.page == PG_LFO && song.playing)
        h = h * 33u + (uint32_t)(t->lfo_v >> 11);
    for (i = 0; i < NSTEP; i++)
        h = h * 33u + t->step[i].time + t->step[i].hit * 3u + t->step[i].note[0] * 7u + t->step[i].n * 11u +
            (ui.page == PG_HOME ? trk[(i % 4u)].step[i].time * 5u : 0u);
    return h + undo.n_undo * 3u + undo.n_redo;
}
static void draw_graph(int force)
{
    const track_t *t = TSEL;
    uint32_t sig = graph_sig(), ns;
    const sub_t *sp;
    if (!force && sig == ui.sig[5])
        return;
    ui.sig[5] = sig;
    g_box();
    if (ui.layer) {
        g_layer();
    } else if (shift_map()) {
        g_shift();
    } else {
        sp = subs_of(ui.page, t, &ns);
        switch (ui.page) {
        case PG_HOME: g_overview(); break;
        case PG_EDIT: {
            const sub_t *s = &sp[ui.sub[PG_EDIT] % ns];
            if (s->p[0] == P_CUT) g_filter(&t->s);
            else if (s->p[0] == P_MONO) g_voice(&t->s);
            else g_osc(t);
            break;
        }
        case PG_ENV: if (USES_DV(song.sel)) g_osc(t); else g_env(&t->s); break;
        case PG_LFO: g_lfo(t); break;
        case PG_FX: g_fx(t); break;
        case PG_SCL: g_scale(); break;
        case PG_SEQ:
            if (sub_cur(PG_SEQ) == SQ_STEP) g_step(t);
            else if (sub_cur(PG_SEQ) == SQ_EUCLID) g_euclid(t);
            else { g_steps(t, 6, 4); g_label(12, 64 + 42, t->seq_off ? "SEQ OFF" : t->len > 16 ? "4 BARS" : "1 BAR", T_MID); }
            break;
        case PG_GEN: g_gen(t); break;
        case PG_GLO: g_mixer(); break;
        case PG_SAVE: g_save(); break;
        case PG_MENU: g_menu(); break;
        }
    }
    cv_blit(0, Y_GRAPH);
}
static void draw_foot(int force)
{
    /* (short words: the row is 228 px; holds are on the pages' pictures and in the manual) */
    static const khint_t H_HOME[3] = {{KC_REC, "ARM"}, {KC_PRESETS, "SOUND"}, {KC_GLO, "SHIFT"}};
    static const khint_t H_EDIT[3] = {{KC_EDIT, "NEXT"}, {KC_PRESETS, "SOUND"}, {KC_GLO, "SHIFT"}};
    static const khint_t H_PAGE[3] = {{KC_ALGO, "TRACK"}, {KC_PRESETS, "SOUND"}, {KC_GLO, "SHIFT"}};
    static const khint_t H_SCL[3] = {{KC_SCL, "ROOT"}, {KC_SEQ, "TOOLS"}, {KC_GLO, "SHIFT"}};
    static const khint_t H_STEP[3] = {{KC_PRESETS, "STEP"}, {KC_KEYS, "NOTE"}, {KC_REC, "ARM"}};
    static const khint_t H_SEQ[3] = {{KC_SEQ, "NEXT"}, {KC_SEQ, "TOOLS"}, {KC_GLO, "SHIFT"}};
    static const khint_t H_GLO[3] = {{KC_K14, "LEVEL"}, {KC_GLO, "PAN"}, {KC_ALGO, "TRACK"}};
    static const khint_t H_SAVE[3] = {{KC_OCTUP, "DO IT"}, {KC_PRESETS, "SLOT"}, {KC_OCTDN, "BACK"}};
    static const khint_t H_MENU[3] = {{KC_PRESETS, "ROW"}, {KC_K1, "VALUE"}, {KC_OCTDN, "BACK"}};
    static const khint_t H_SHIFT[3] = {{KC_K14, "ROW B"}, {KC_ALGO, "TYPE"}, {KC_PRESETS, "ENGINE"}};
    static const khint_t H_LAYER[3] = {{KC_KEYS, "DO IT"}, {KC_SAVE, "UNDO"}, {0, 0}};
    static const char *const LYN[5] = {"", "TOOLS", "ROOT", "ENGINE", "TYPE"};
    char b[32];
    uint32_t ns, sig, i;
    const sub_t *sp = subs_of(ui.page, TSEL, &ns);
    const khint_t *h = ui.layer ? H_LAYER : ui.shift ? H_SHIFT : ui.page == PG_HOME ? H_HOME :
                       ui.page == PG_EDIT ? H_EDIT : ui.page == PG_SCL ? H_SCL : ui.page == PG_GLO ? H_GLO :
                       ui.page == PG_SAVE ? H_SAVE : ui.page == PG_MENU ? H_MENU :
                       ui.page == PG_SEQ ? (on_step() ? H_STEP : H_SEQ) : H_PAGE;
    str_cpy(b, ui.layer ? LYN[ui.layer] : sp[ui.sub[ui.page] % ns].name, sizeof b);
    if (ui.shift && !ui.layer)
        str_cat(b, " B", sizeof b);
    sig = hsh(ui.page * 31u + ui.layer * 7u + ns * 3u + ui.sub[ui.page] * 1009u + TSEL->s.engine * 13u + ui.shift * 101u +
              (uint32_t)IS_DRUM(song.sel) * 5u, b);
    if (!force && sig == ui.sig[6])
        return;
    ui.sig[6] = sig;
    cv_begin(240, H_FOOT, T_BG);
    cv_keycap(6, 2, ui.layer == LY_SEQ ? KC_SEQ : ui.layer == LY_SCL ? KC_SCL : ui.layer == LY_EDIT ? KC_EDIT :
                    ui.layer == LY_TYPE ? KC_ARP : ui.shift ? KC_GLO : PAGE_KC[ui.page % PG_N], T_KEY, T_INK, T_BG);
    cv_text(52, CAP_IN(M, 18), &AF_M, b, ui.shift && !ui.layer ? T_ACCENT : T_TEXT);
    for (i = 0; ns > 1u && i < ns; i++)          /* the pages of this button: dots, the one shown lit */
        cv_rrect(236 - (int32_t)(ns - i) * 9, 7, 6, 6, 3, i == ui.sub[ui.page] % ns ? T_THEME : T_LINE, T_BG);
    if (ui.layer == LY_SEQ) {                    /* (the black keys: on this row) */
        cv_text(128, CAP_IN(S, 18), &AF_S, "BLK COPY PASTE X2", T_MID);
    }
    cv_key_row(6, 234, 21, h, h[2].act ? 3u : 2u, 7u, T_BG);
    cv_blit(0, Y_FOOT);
}
static void draw_cards(int force)
{
    const track_t *t = TSEL;
    uint32_t c;
    for (c = 0; c < 4u; c++)
        draw_card(c, knob_id(c), t, force);
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
    if (ui.sig[7]) {                              /* (back from it: all again) */
        ui.sig[7] = 0;
        force = 1;
    }
    if (force) {
        lcd_fill(0, H_HEAD, 240, Y_LABEL - H_HEAD, T_BG);
        lcd_fill(0, Y_LABEL + CARD_H, 240, Y_GRAPH - Y_LABEL - CARD_H, T_BG);
        for (i = 0; i < 4u; i++)                  /* (the gaps between the cards) */
            lcd_fill((uint32_t)(CARD_X(i) + CARD_W), Y_LABEL, i < 3u ? 2u : 240u - (uint32_t)(CARD_X(3) + CARD_W), CARD_H, T_BG);
        lcd_fill(0, Y_LABEL, 3, CARD_H, T_BG);
        lcd_fill(0, Y_GRAPH + H_GRAPH, 240, Y_FOOT - Y_GRAPH - H_GRAPH, T_BG);
    }
    draw_head(force);
    draw_cards(force);
    draw_graph(force);
    draw_foot(force);
}

static void ui_init(void)
{
    uint32_t i, b;
    for (i = 0; i < NELEM(PROGS) && i < 16u; i++) {   /* the progressions' names: I-VI-IV-V */
        prog_txt[i][0] = 0;
        for (b = 0; b < 4u; b++) {
            str_cat(prog_txt[i], N_RN[PROGS[i][b] % 7u], sizeof prog_txt[i]);
            if (b < 3u)
                str_cat(prog_txt[i], "-", sizeof prog_txt[i]);
        }
        N_PROG[i] = prog_txt[i];
    }
    for (i = 0; i < UI_NPALETTES; i++)
        N_THEME[i] = UI_PALETTES[i].name;
    song.bpm = 122;
    song.sel = 0;
    song.master_q12 = 2048;
    song.theme = PAL_AMBER;
    song.dly_div = 1;                             /* 3/16, as before the FX page had it */
    song.dly_fb = 61;
    song.rev_size = 70;
    song.rev_damp = 0;
    for (i = 0; i < NTRK; i++) {
        trk[i].type = TYPE_DEFAULT[i];
        trk[i].k_lvl = 8;
        trk[i].gate = 60;
        trk[i].len = 16;
        trk[i].gen_dens = 64;
        trk[i].gen_var = 64;
        trk[i].gen_mut = 64;
        trk[i].eu_hits = 4;
    }
    store_scan();                                 /* the saved sounds and songs, the settings (theme, MIDI IN ...) */
    palette_set(song.theme);
    gen_all();                                    /* a song to start with */
    for (i = 0; i < NTRK; i++)
        hist_push(i);
    undo.n_undo = undo.n_redo = 0;
    ui.page = PG_HOME;
}

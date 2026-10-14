/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the generator (from ONE CLICK SYNTH). The song has a key, a scale and a progression of four chords
 * (a bar each); each track has a role:
 *   BASS    the chords' roots and fifths on a bass rhythm (offbeats, 8ths, 3-3-2, euclidean); acid accents / slides
 *   CHORDS  triads or sevenths voiced around C4: held, house stabs, halves or a euclidean rhythm
 *   LEAD    a one-bar motif (chord tones on the beats, scale steps between) following the chords, placed where the
 *           bass rests (interlock), the last bar sometimes answered
 *   DRUMS   house, breakbeat, half-time, techno or a euclidean kit
 * Sounds are designed per role and engine from ranges that make that kind of sound (a 303 line: high resonance and
 * a short filter decay; FM bells: Risset-like inharmonic ratios; organs: drawbar partials), never a flat random.
 * gen_all: a new song; gen_track / gen_sound / gen_pattern: one track, keeping the harmony so it still fits.
 * The pattern changes happen with the audio ISR held off (gen_lock). */

static void gen_lock(void);                      /* the audio ISR held off (oc_main.c / host) */
static void gen_unlock(void);
static uint32_t rnd(uint32_t n) { return n ? rng() % n : 0u; }
static int32_t rr(int32_t lo, int32_t hi) { return lo + (int32_t)rnd((uint32_t)(hi - lo + 1)); }   /* lo..hi */
static uint32_t chance(uint32_t pct) { return rnd(100) < pct; }

/* ------------------------------------------------------------ harmony --- */
static const uint8_t PROGS[][4] = {
    {0, 5, 3, 4}, {0, 3, 4, 0}, {0, 6, 5, 6}, {0, 4, 5, 3}, {0, 2, 3, 4}, {0, 5, 6, 4}, {0, 3, 0, 4},
    {0, 0, 5, 6}, {0, 3, 5, 4}, {5, 3, 0, 4}, {0, 6, 3, 4}, {3, 4, 0, 0},
};
/* degree k (any; past the octave too) of the song's scale: semitones above the root */
static int32_t deg(int32_t k)
{
    int32_t o = k >= 0 ? k / 7 : -((-k + 6) / 7);
    return 12 * o + SCALES[song.scale % SC_N][k - 7 * o];
}
/* the note root + off nearest `around` (an octave window) */
static int32_t near(int32_t off, int32_t around)
{
    int32_t n = (int32_t)song.root + off;
    while (n < around - 6)
        n += 12;
    while (n > around + 5)
        n -= 12;
    return clamp(n, 12, 120);
}
static void gen_harmony(void)
{
    static const uint8_t W[] = {SC_MIN, SC_MIN, SC_MIN, SC_DOR, SC_DOR, SC_MAJ, SC_MAJ, SC_MIX, SC_HARM, SC_PHRY};
    song.root = (uint8_t)rnd(12);
    song.scale = W[rnd(NELEM(W))];
    song.prog_i = (uint8_t)rnd(NELEM(PROGS));
    memcpy(song.prog, PROGS[song.prog_i], 4);
}
static int in_key(int32_t n)
{
    uint32_t k, x = (uint32_t)(n - (int32_t)song.root + 120) % 12u;
    for (k = 0; k < 7u; k++)
        if (SCALES[song.scale % SC_N][k] == x)
            return 1;
    return 0;
}

/* -------------------------------------------------------------- steps --- */
static void st_clear(step_t *s) { memset(s, 0, sizeof *s); }
static void st_note(track_t *t, uint32_t i, const int32_t *n, uint32_t k, uint32_t vel, uint32_t flags)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t j;
    st_clear(s);
    s->time = ST_NOTE;
    s->n = (uint8_t)(k > 4u ? 4u : k);
    for (j = 0; j < s->n; j++)
        s->note[j] = (uint8_t)clamp(n[j], 1, 127);
    s->vel = (uint8_t)clamp((int32_t)vel, 1, 127);
    s->flags = (uint8_t)flags;
}
static void st_tie(track_t *t, uint32_t i)
{
    st_clear(&t->step[i % NSTEP]);
    t->step[i % NSTEP].time = ST_TIE;
}
static void st_hit(track_t *t, uint32_t i, uint32_t lane, uint32_t acc)
{
    step_t *s = &t->step[i % NSTEP];
    s->time = ST_NOTE;
    s->vel = 104;
    s->hit |= (uint8_t)(1u << lane);
    if (acc)
        s->acc |= (uint8_t)(1u << lane);
}
static void clear_all(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        st_clear(&t->step[i]);
}
/* k hits of 16, evenly (euclidean), rotated: bit i = step i */
static uint32_t eu16(uint32_t k, uint32_t rot)
{
    uint32_t i, m = 0;
    for (i = 0; k && i < 16u; i++)
        if ((i * k) % 16u < k)
            m |= 1u << ((i + rot) % 16u);
    return m;
}

/* ------------------------------------------------------------- sounds --- */
static void p_env(patch_t *p, int a, int d, int s, int r) { p->atk = (uint8_t)a; p->dec = (uint8_t)d; p->sus = (uint8_t)s; p->rel = (uint8_t)r; }
static void p_filter(patch_t *p, int c, int q, int env, int fdec)
{
    p->cutoff = (uint8_t)clamp(c, 0, 127);
    p->res = (uint8_t)clamp(q, 0, 127);
    p->fenv = (uint8_t)clamp(env, 0, 127);
    p->fdec = (uint8_t)clamp(fdec, 0, 127);
}
static void p_partials(patch_t *p, const uint16_t *ratio, const uint8_t *amp, uint32_t n)
{
    uint32_t k;
    p->add_n = (uint8_t)n;
    for (k = 0; k < n && k < ADD_N; k++) {
        p->add_ratio[k] = ratio[k];
        p->add_amp[k] = (uint8_t)clamp(amp[k] + rr(-12, 12), 4, 127);
    }
}

static void sound_bass(patch_t *p)
{
    uint32_t kind = rnd(4);
    p->mono = 1;
    p->keytrk = (uint8_t)rr(10, 40);
    if (kind == 0) {                             /* TB-303: one osc, high resonance, short decay sweep */
        p->engine = E_ACID;
        p->wave1 = (uint8_t)(chance(70) ? W_SAW : W_SQR);
        p_filter(p, rr(18, 42), rr(95, 122), rr(70, 115), rr(28, 50));
        p_env(p, 0, rr(40, 60), rr(80, 110), rr(8, 20));
    } else if (kind == 1) {                      /* subtractive: two oscs and a sub, a plucky filter */
        p->engine = E_SUB;
        p->wave1 = (uint8_t)(chance(60) ? W_SAW : W_SQR);
        p->wave2 = W_SQR;
        p->detune = (int8_t)rr(2, 10);
        p->mix2 = (uint8_t)rr(40, 100);
        p->sub = (uint8_t)rr(50, 110);
        p_filter(p, rr(22, 50), rr(10, 70), rr(30, 80), rr(30, 55));
        p_env(p, 0, rr(45, 70), rr(70, 115), rr(10, 25));
    } else if (kind == 2) {                      /* FM bass: ratio 1:1 or 1:2, a decaying index */
        static const uint8_t R2[] = {16, 16, 32, 8};
        p->engine = E_FM;
        p->fm_ratio[0] = 16;
        p->fm_ratio[1] = R2[rnd(4)];
        p->fm_ratio[2] = (uint8_t)(chance(50) ? 16 : 48);
        p->fm_index = (uint8_t)rr(30, 75);
        p->fm_idec = (uint8_t)rr(35, 60);
        p->fm_fb = (uint8_t)rr(0, 40);
        p->fm_algo = (uint8_t)rnd(2);
        p->cutoff = 127;
        p_env(p, 0, rr(45, 70), rr(60, 110), rr(10, 25));
    } else {                                     /* reese: three detuned saws, low-passed */
        p->engine = E_SAW;
        p->saw_n = 3;
        p->saw_spread = (uint8_t)rr(4, 12);
        p_filter(p, rr(26, 46), rr(10, 50), rr(15, 45), rr(45, 70));
        p_env(p, rr(0, 10), rr(50, 75), rr(90, 120), rr(15, 30));
    }
    p->glide = (uint8_t)(chance(30) ? rr(4, 20) : 0);
    p->level = (uint8_t)rr(96, 112);
    p->dly = 0;
    p->rev = (uint8_t)rr(0, 10);
    p->pan = 0;
}

static void sound_chords(patch_t *p)
{
    static const uint16_t ORGAN_R[8] = {128, 256, 384, 512, 768, 1024, 1280, 1536};   /* drawbars 16' 8' 5 1/3' .. */
    static const uint8_t ORGAN_A[8] = {90, 127, 70, 80, 40, 50, 20, 25};
    static const uint16_t BELL_R[8] = {143, 145, 236, 305, 435, 512, 701, 768};      /* Risset's bell partials */
    static const uint8_t BELL_A[8] = {127, 100, 110, 90, 70, 60, 45, 35};
    uint32_t kind = rnd(5);
    p->mono = 0;
    if (kind == 0) {                             /* supersaw pad / stab */
        p->engine = E_SAW;
        p->saw_n = (uint8_t)rr(5, 7);
        p->saw_spread = (uint8_t)rr(8, 24);
        p_filter(p, rr(48, 80), rr(5, 35), rr(10, 40), rr(60, 90));
        if (chance(50))
            p_env(p, rr(55, 85), 80, rr(90, 115), rr(70, 95));   /* a pad */
        else
            p_env(p, rr(0, 10), rr(45, 65), rr(20, 60), rr(40, 60));   /* a stab */
    } else if (kind == 1) {                      /* FM electric piano: 1:1 body, a 14:1 tine */
        p->engine = E_FM;
        p->fm_ratio[0] = 16;
        p->fm_ratio[1] = 16;
        p->fm_ratio[2] = (uint8_t)(chance(70) ? 224 : 112);
        p->fm_index = (uint8_t)rr(25, 55);
        p->fm_idec = (uint8_t)rr(40, 60);
        p->fm_fb = 0;
        p->fm_algo = 1;
        p->cutoff = 127;
        p_env(p, 0, rr(70, 90), rr(20, 50), rr(50, 70));
    } else if (kind == 2) {                      /* additive organ */
        p->engine = E_ADD;
        p_partials(p, ORGAN_R, ORGAN_A, (uint32_t)rr(5, 8));
        p->add_tilt = 0;
        p_env(p, rr(0, 8), 110, rr(100, 120), rr(25, 45));
    } else if (kind == 3) {                      /* inharmonic bell pad */
        p->engine = E_ADD;
        p_partials(p, BELL_R, BELL_A, 8);
        p->add_tilt = (uint8_t)rr(20, 60);
        p_env(p, rr(20, 60), rr(85, 100), rr(40, 80), rr(70, 95));
    } else {                                     /* string machine: two detuned saws, slow */
        p->engine = E_SUB;
        p->wave1 = W_SAW;
        p->wave2 = W_SAW;
        p->detune = (int8_t)rr(3, 9);
        p->mix2 = 110;
        p_filter(p, rr(55, 80), rr(0, 25), rr(5, 20), 80);
        p_env(p, rr(50, 80), 90, rr(100, 120), rr(70, 90));
    }
    p->level = (uint8_t)rr(70, 88);
    p->dly = (uint8_t)rr(0, 30);
    p->rev = (uint8_t)rr(40, 80);
    p->pan = (int8_t)rr(-20, -8);
}

static void sound_lead(patch_t *p)
{
    static const uint16_t MAR_R[3] = {256, 1024, 2560};   /* a marimba bar: 1, 4, 10 */
    static const uint8_t MAR_A[3] = {127, 55, 25};
    uint32_t kind = rnd(5);
    p->mono = (uint8_t)chance(60);
    if (kind == 0) {                             /* subtractive lead */
        p->engine = E_SUB;
        p->wave1 = W_SAW;
        p->wave2 = (uint8_t)(chance(50) ? W_SQR : W_SAW);
        p->detune = (int8_t)rr(2, 8);
        p->oct2 = (int8_t)(chance(25) ? 1 : 0);
        p->mix2 = (uint8_t)rr(50, 110);
        p_filter(p, rr(55, 90), rr(25, 70), rr(20, 55), rr(40, 60));
        p_env(p, rr(0, 15), rr(55, 75), rr(70, 110), rr(25, 45));
        p->glide = (uint8_t)(p->mono && chance(50) ? rr(6, 24) : 0);
    } else if (kind == 1) {                      /* FM bell / pluck: 1, 3.5, 1.4 */
        p->mono = 0;
        p->engine = E_FM;
        p->fm_ratio[0] = 16;
        p->fm_ratio[1] = (uint8_t)(chance(50) ? 56 : 48);
        p->fm_ratio[2] = (uint8_t)(chance(50) ? 22 : 64);
        p->fm_index = (uint8_t)rr(45, 90);
        p->fm_idec = (uint8_t)rr(25, 45);
        p->fm_fb = (uint8_t)rr(0, 20);
        p->fm_algo = (uint8_t)rnd(3);
        p->cutoff = 127;
        p_env(p, 0, rr(50, 72), 0, rr(45, 65));
    } else if (kind == 2) {                      /* additive marimba */
        p->mono = 0;
        p->engine = E_ADD;
        p_partials(p, MAR_R, MAR_A, 3);
        p->add_tilt = (uint8_t)rr(80, 127);
        p_env(p, 0, rr(45, 60), 0, rr(40, 55));
    } else if (kind == 3) {                      /* saw lead, three voices */
        p->engine = E_SAW;
        p->saw_n = 3;
        p->saw_spread = (uint8_t)rr(4, 10);
        p_filter(p, rr(60, 90), rr(15, 50), rr(15, 40), rr(45, 65));
        p_env(p, rr(0, 12), rr(55, 75), rr(80, 115), rr(30, 50));
        p->glide = (uint8_t)(p->mono && chance(40) ? rr(6, 20) : 0);
    } else {                                     /* squelchy acid lead */
        p->mono = 1;
        p->engine = E_ACID;
        p->wave1 = W_SQR;
        p_filter(p, rr(40, 65), rr(85, 115), rr(50, 90), rr(35, 55));
        p_env(p, 0, rr(50, 65), rr(70, 100), rr(15, 30));
    }
    p->keytrk = (uint8_t)rr(20, 60);
    p->level = (uint8_t)rr(78, 96);
    p->dly = (uint8_t)rr(30, 70);
    p->rev = (uint8_t)rr(20, 50);
    p->pan = (int8_t)rr(8, 20);
}

/* the instrument types the roles do not cover by themselves: their own designs */
static void sound_pad(patch_t *p)
{
    uint32_t k;
    for (k = 0; k < 30u; k++) {                  /* the chord designs, slow */
        memset(p, 0, sizeof *p);
        sound_chords(p);
        if (p->engine != E_FM)
            break;
    }
    p_env(p, rr(55, 85), rr(80, 95), rr(90, 115), rr(75, 95));
    p->fatt = (uint8_t)rr(40, 80);
    p->fsus = (uint8_t)rr(40, 90);
    p->rev = (uint8_t)rr(60, 100);
}
static void sound_atmo(patch_t *p)
{
    static const uint16_t DRONE_R[8] = {128, 256, 383, 512, 641, 768, 1022, 1280};
    static const uint8_t DRONE_A[8] = {110, 127, 60, 70, 40, 45, 25, 20};
    if (chance(50)) {                            /* wind: band-passed noise on the note, a slow sweep */
        p->engine = E_NOISE;
        p->wave1 = (uint8_t)(chance(70) ? 1 : 0);
        p_filter(p, rr(40, 75), rr(90, 120), rr(0, 20), 90);
        p->keytrk = 64;
        p->lfo_dest = LD_CUT;
        p->lfo_rate = (uint8_t)rr(5, 25);
        p->lfo_depth = (uint8_t)rr(25, 60);
    } else {                                     /* a drone of partials */
        p->engine = E_ADD;
        p_partials(p, DRONE_R, DRONE_A, 8);
        p->add_tilt = (uint8_t)rr(10, 40);
        p->lfo_dest = LD_AMP;
        p->lfo_rate = (uint8_t)rr(10, 30);
        p->lfo_depth = (uint8_t)rr(15, 40);
    }
    p_env(p, rr(80, 105), 110, rr(100, 125), rr(90, 110));
    p->level = (uint8_t)rr(66, 82);
    p->rev = (uint8_t)rr(80, 120);
    p->dly = (uint8_t)rr(20, 60);
    p->pan = (int8_t)rr(-30, 30);
}
static void sound_piano(patch_t *p)
{
    static const uint16_t PIANO_R[8] = {256, 512, 769, 1027, 1286, 1546, 1808, 2072};   /* slightly stretched */
    static const uint8_t PIANO_A[8] = {127, 80, 60, 45, 35, 25, 18, 12};
    if (chance(55)) {                            /* an electric piano: FM, 1:1 and a 14:1 tine */
        p->engine = E_FM;
        p->fm_ratio[0] = 16; p->fm_ratio[1] = 16; p->fm_ratio[2] = 224;
        p->fm_index = (uint8_t)rr(25, 50);
        p->fm_idec = (uint8_t)rr(40, 58);
        p->fm_algo = 1;
        p->cutoff = 127;
        p_env(p, 0, rr(75, 92), rr(10, 35), rr(50, 65));
    } else {                                     /* a piano of stretched partials, the high ones dying first */
        p->engine = E_ADD;
        p_partials(p, PIANO_R, PIANO_A, 8);
        p->add_tilt = (uint8_t)rr(70, 110);
        p_env(p, 0, rr(78, 92), 0, rr(48, 60));
    }
    p->level = (uint8_t)rr(78, 92);
    p->rev = (uint8_t)rr(30, 60);
    p->pan = (int8_t)rr(-12, 12);
}
static void sound_guitar(patch_t *p)
{
    p->engine = E_SUB;                           /* a plucked string: bright attack, the filter closing fast */
    p->wave1 = W_SAW;
    p->wave2 = (uint8_t)(chance(50) ? W_SQR : W_SAW);
    p->detune = (int8_t)rr(1, 5);
    p->mix2 = (uint8_t)rr(30, 80);
    p_filter(p, rr(25, 45), rr(10, 40), rr(80, 120), rr(35, 55));
    p->keytrk = (uint8_t)rr(50, 90);
    p_env(p, 0, rr(55, 72), 0, rr(40, 55));
    p->level = (uint8_t)rr(80, 94);
    p->rev = (uint8_t)rr(25, 50);
    p->dly = (uint8_t)rr(10, 40);
    p->pan = (int8_t)rr(-25, -10);
}
static void sound_brass(patch_t *p)
{
    p->engine = chance(60) ? E_SUB : E_SAW;      /* saws, the filter opening slowly as the note swells */
    p->wave1 = W_SAW;
    p->wave2 = W_SAW;
    p->detune = (int8_t)rr(3, 8);
    p->mix2 = 100;
    p->saw_n = 3;
    p->saw_spread = (uint8_t)rr(3, 8);
    p_filter(p, rr(30, 50), rr(5, 30), rr(70, 110), rr(55, 75));
    p->fatt = (uint8_t)rr(35, 55);
    p->fsus = (uint8_t)rr(50, 90);
    p_env(p, rr(25, 40), rr(60, 75), rr(90, 115), rr(30, 45));
    p->dual = (uint8_t)(chance(40) ? DU_UNISON : DU_OFF);
    p->dmix = 90;
    p->level = (uint8_t)rr(76, 90);
    p->rev = (uint8_t)rr(30, 55);
}
static void sound_flute(patch_t *p)
{
    p->engine = E_SUB;                           /* a soft tone with breath, vibrato */
    p->wave1 = (uint8_t)(chance(60) ? W_TRI : W_SINE);
    p->wave2 = W_SINE;
    p->oct2 = 1;
    p->mix2 = (uint8_t)rr(10, 35);
    p->noise = (uint8_t)rr(12, 35);
    p_filter(p, rr(60, 85), rr(0, 20), rr(10, 30), 70);
    p->keytrk = 80;
    p_env(p, rr(25, 45), 80, rr(95, 120), rr(30, 45));
    p->mono = 1;
    p->glide = (uint8_t)rr(4, 12);
    p->lfo_dest = LD_PITCH;
    p->lfo_rate = (uint8_t)rr(80, 92);
    p->lfo_depth = (uint8_t)rr(5, 10);
    p->level = (uint8_t)rr(80, 94);
    p->rev = (uint8_t)rr(35, 60);
    p->dly = (uint8_t)rr(20, 50);
}

/* the first sample slot of a kind (kind_ok: the one asked is used and of it), 0xFF: none */
static uint32_t smp_pick(uint32_t want, uint32_t kind)
{
    uint32_t k;
    if (want < NSMP && smp_dir[want].used && smp_dir[want].kind == kind)
        return want;
    for (k = 0; k < NSMP; k++)
        if (smp_dir[k].used && smp_dir[k].kind == kind)
            return k;
    return 0xFFu;
}
static void sound_sample(patch_t *p, uint32_t slot)   /* the kick's one-shot / the loop as it is */
{
    p->engine = E_SAMPLE;
    p->smp = (uint8_t)(slot % NSMP);
    p->cutoff = 127;
    p->level = (uint8_t)rr(96, 108);
    p->rev = (uint8_t)rr(0, 10);
    p->ddec = 64;
    p->dmix = 100;
}
/* an LXR kit: each voice from a few designs (kick: 909-ish boom or a short thud; toms; a rim / zap; a snare with its
 * noise; a metal cymbal; hats) and their ranges */
static void lxr_kit(patch_t *p, uint32_t type)
{
    static const uint8_t K[6][8][2] = {          /* [voice][param]: lo, hi */
        {{8, 22}, {40, 80}, {40, 110}, {20, 50}, {0, 0}, {0, 30}, {20, 90}, {0, 60}},       /* DRUM 1: the kick */
        {{30, 60}, {30, 70}, {20, 70}, {25, 60}, {0, 25}, {0, 40}, {0, 50}, {0, 30}},       /* DRUM 2: toms */
        {{60, 110}, {5, 35}, {0, 90}, {5, 40}, {0, 127}, {0, 90}, {20, 100}, {0, 70}},      /* DRUM 3: rim, zap, perc */
        {{30, 80}, {25, 60}, {50, 110}, {40, 80}, {40, 110}, {20, 90}, {10, 70}, {0, 50}},  /* SNARE */
        {{30, 90}, {50, 95}, {10, 110}, {20, 100}, {30, 100}, {10, 70}, {0, 50}, {0, 40}},  /* CYMBAL */
        {{50, 110}, {5, 30}, {35, 70}, {10, 90}, {50, 120}, {20, 90}, {0, 50}, {0, 30}},    /* HIHAT */
    };
    uint32_t v, k;
    for (v = 0; v < 6u; v++)
        for (k = 0; k < 8u; k++)
            p->lx[v][k] = (uint8_t)rr(K[v][k][0], K[v][k][1]);
    p->lx[LX_DRUM1][4] = 0;                       /* (the kick: a sine) */
    if (chance(30))
        p->lx[LX_DRUM2][4] = 26;                  /* (a triangle tom now and then) */
    p->engine = E_LXR;
    p->level = (uint8_t)rr(90, 104);
    p->rev = (uint8_t)rr(4, 16);
    p->ddec = 64;
    p->dmix = 100;
    p->dtune = (int8_t)(type == TY_KICK ? rr(-4, 4) : 0);
}
static void gen_sound_of(track_t *t, uint32_t type)
{
    patch_t *p = &t->s, old = t->s;
    uint32_t role = TYPE_ROLE[type % TY_N], k;
    if (old.engine == E_LXR && role == ROLE_DRUM) {   /* the LXR chosen stays: a new kit */
        memset(p, 0, sizeof *p);
        lxr_kit(p, type);
        return;
    }
    if (type == TY_LOOP || (type == TY_KICK && old.engine == E_SAMPLE)) {   /* the samples assigned stay */
        k = smp_pick(old.engine == E_SAMPLE ? old.smp : 0xFFu, type == TY_LOOP ? SK_LOOP : SK_ONESHOT);
        if (k != 0xFFu || type == TY_LOOP) {
            memset(p, 0, sizeof *p);
            sound_sample(p, k == 0xFFu ? 0u : k);
            p->dtune = old.engine == E_SAMPLE ? old.dtune : 0;
            return;
        }
    }
    memset(p, 0, sizeof *p);
    p->lfo_rate = 40;
    p->lfo_wave = LW_SINE;
    p->dmix = 100;
    switch (type % TY_N) {
    case TY_BASS: sound_bass(p); break;
    case TY_PAD: sound_pad(p); break;
    case TY_ATMO: sound_atmo(p); break;
    case TY_PIANO: sound_piano(p); break;
    case TY_GUITAR: sound_guitar(p); break;
    case TY_PLUCK: {
        uint32_t k;
        for (k = 0; k < 40u; k++) {              /* the lead designs that are plucked (no sustain) */
            memset(p, 0, sizeof *p);
            sound_lead(p);
            if (p->sus == 0)
                break;
        }
        break;
    }
    case TY_BRASS: sound_brass(p); break;
    case TY_FLUTE: sound_flute(p); break;
    case TY_LEAD: sound_lead(p); break;
    case TY_DRUMS: case TY_KICK: case TY_SNARE: case TY_PERC:
        p->engine = E_DRUM;
        p->kit = (uint8_t)rnd(4);
        p->level = (uint8_t)rr(86, 100);
        p->rev = (uint8_t)rr(4, 18);
        p->ddec = 64;
        p->dtune = (int8_t)(type == TY_KICK ? rr(-6, 4) : type == TY_PERC ? rr(-4, 8) : 0);
        p->pan = (int8_t)(type == TY_PERC ? rr(-24, 24) : 0);
        p->dmix = 100;
        return;
    default: sound_chords(p); break;
    }
    if (!p->dmix)
        p->dmix = 100;
    if (!p->frel)
        p->frel = p->fdec;                       /* (the filter closes as it decays: as the sounds were made) */
    if (p->lfo_dest)
        return;
    p->lfo_rate = 40;                            /* an LFO now and then, as the role would use one */
    p->lfo_wave = LW_SINE;
    if (role == ROLE_CHORD && chance(40)) {      /* a slow filter sweep under chords */
        p->lfo_dest = LD_CUT;
        p->lfo_rate = (uint8_t)rr(10, 35);
        p->lfo_depth = (uint8_t)rr(20, 60);
    } else if (role == ROLE_LEAD && chance(35)) {   /* vibrato */
        p->lfo_dest = LD_PITCH;
        p->lfo_rate = (uint8_t)rr(78, 92);
        p->lfo_depth = (uint8_t)rr(4, 12);
    } else if (role == ROLE_BASS && p->engine != E_FM && chance(15)) {   /* a wobble */
        p->lfo_dest = LD_CUT;
        p->lfo_wave = (uint8_t)(chance(50) ? LW_TRI : LW_SINE);
        p->lfo_rate = (uint8_t)rr(55, 75);
        p->lfo_depth = (uint8_t)rr(30, 70);
    }
}
/* the engines a role can have (EDIT held: a white key picks one), and a new sound of engine e for track t's type */
static const uint8_t ROLE_ENGINES[ROLE_N][6] = {
    {E_ACID, E_SUB, E_FM, E_SAW, E_NOISE, 0xFF}, {E_SAW, E_FM, E_ADD, E_SUB, E_NOISE, 0xFF},
    {E_SUB, E_FM, E_ADD, E_SAW, E_ACID, E_NOISE}, {E_DRUM, E_LXR, E_SAMPLE, 0xFF, 0xFF, 0xFF},
    {E_SAMPLE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
static int role_has_engine(uint32_t role, uint32_t e)
{
    uint32_t k;
    for (k = 0; k < 6u; k++)
        if (ROLE_ENGINES[role % ROLE_N][k] == e)
            return 1;
    return 0;
}
static void gen_sound_engine_of(track_t *t, uint32_t type, uint32_t e)
{
    uint32_t n, role = TYPE_ROLE[type % TY_N], ty;
    if (e == E_LXR && role == ROLE_DRUM) {
        memset(&t->s, 0, sizeof t->s);
        lxr_kit(&t->s, type);
        return;
    }
    if (e == E_SAMPLE && role_has_engine(role, e)) {   /* a sample: the one assigned, else the first of its kind */
        uint32_t k = smp_pick(t->s.engine == E_SAMPLE ? t->s.smp : 0xFFu, role == ROLE_LOOP ? SK_LOOP : SK_ONESHOT);
        memset(&t->s, 0, sizeof t->s);
        sound_sample(&t->s, k == 0xFFu ? 0u : k);
        return;
    }
    if (!role_has_engine(role, e)) {
        gen_sound_of(t, type);
        return;
    }
    for (n = 0; n < 300u; n++) {                 /* the type's designs, then its role's other types', until one is e */
        ty = n < 100u ? type : n % TY_N;
        if (TYPE_ROLE[ty] != role)
            continue;
        gen_sound_of(t, ty);
        if (t->s.engine == e)
            return;
    }
    if (e == E_NOISE) {                          /* (no design of that role is noise: the wind) */
        memset(&t->s, 0, sizeof t->s);
        sound_atmo(&t->s);
        t->s.engine = E_NOISE;
        t->s.wave1 = 1;
        t->s.dmix = 100;
        if (role != ROLE_CHORD)
            p_env(&t->s, 0, rr(50, 70), rr(40, 90), rr(30, 50));
    }
}

/* ----------------------------------------------------------- patterns --- */
static uint32_t bass_mask(void)                  /* the (first) bass track's gates in its first bar */
{
    uint32_t i, t, m = 0;
    for (t = 0; t < NTRK && trk_role(t) != ROLE_BASS; t++)
        ;
    for (i = 0; t < NTRK && i < 16u; i++)
        if (trk[t].step[i].time == ST_NOTE && trk[t].step[i].n)
            m |= 1u << i;
    return m;
}

/* The generator's settings per track (ARP page): DENS(ity) 0..127 sets how many notes / hits, VAR(iation) 0..127 how
 * often the line leaves the plain (leaps, fifths, accents, slides, fills), STYLE 0 = AUTO or one of the role's styles
 * (STYLE_NAME), RANGE the register in octaves. 64 / 64 / AUTO / 0 make the parts as before. */
static const char *const STYLE_NAME[ROLE_N][8] = {
    {"AUTO", "OFFBEAT", "8THS", "4THS", "3-3-2", "SYNC", "BUSY", "EUCLID"},
    {"AUTO", "HOLD", "STABS", "HALVES", "EUCLID"},
    {"AUTO", "LEGATO", "STACCATO"},
    {"AUTO", "HOUSE", "BREAK", "HALFTIME", "TECHNO", "EUCLID"},
    {"AUTO", "STRAIGHT", "SHUFFLE", "STUTTER", "HALFTIME"},
};
static const uint8_t STYLE_N[ROLE_N] = {8, 5, 3, 6, 5};
static uint32_t vchance(const track_t *t, uint32_t pct)   /* a chance scaled by VARIATION (64: as given) */
{
    return chance(pct * t->gen_var / 64u);
}
static uint32_t dens_k(const track_t *t, uint32_t lo, uint32_t hi)   /* lo..hi by DENSITY, one either way at random */
{
    int32_t k = (int32_t)lo + (int32_t)((hi - lo) * t->gen_dens / 127u) + rr(-1, 1);
    return (uint32_t)clamp(k, (int32_t)lo, (int32_t)hi);
}

static void pat_bass(track_t *t)
{
    static const uint16_t RH[] = {0x4444u, 0x5555u, 0x1111u, 0x9249u, 0x2492u, 0xB5ADu};   /* OFFBEAT .. BUSY */
    static const uint8_t BY_DENS[6] = {2, 4, 3, 0, 1, 5};   /* the rhythms sparse to dense: 4THS 3-3-2 SYNC .. BUSY */
    uint32_t st = t->gen_style % STYLE_N[ROLE_BASS], m, b, i, acid = t->s.engine == E_ACID;
    int32_t reg = 39 + 12 * t->gen_range;
    if (!st)                                     /* AUTO: a rhythm near the density, sometimes euclidean */
        st = chance(15) ? 7u : 1u + BY_DENS[dens_k(t, 0, 5)];
    m = st == 7u ? eu16(dens_k(t, 3, 9), rnd(4)) | 1u : RH[st - 1u];
    clear_all(t);
    for (b = 0; b < 4u; b++) {
        int32_t root = near(deg(song.prog[b]), reg), fifth = root + deg(song.prog[b] + 4) - deg(song.prog[b]);
        for (i = 0; i < 16u; i++) {
            int32_t n = root;
            uint32_t fl = 0, v;
            if (!((m >> i) & 1u))
                continue;
            if (i && vchance(t, 18))
                n = fifth;
            else if (i && vchance(t, 15))
                n = root + 12;
            if (acid) {                          /* the 303 way: accents and slides */
                fl |= vchance(t, 28) ? SF_ACC : 0u;
                fl |= vchance(t, 22) ? SF_SLIDE : 0u;
            }
            v = i % 4u == 0u ? 104u : (uint32_t)rr(84, 98);
            st_note(t, 16u * b + i, &n, 1, v, fl);
        }
    }
    t->gate = (uint8_t)(acid ? 55 : rr(45, 80));
}

static void pat_chords(track_t *t)
{
    uint32_t pad = t->s.atk > 40u, st = t->gen_style % STYLE_N[ROLE_CHORD], b, i, k;
    int32_t reg = 60 + 12 * t->gen_range;
    if (!st)
        st = pad ? (chance(70) ? 1u : 3u) : 1u + rnd(4);
    clear_all(t);
    for (b = 0; b < 4u; b++) {
        int32_t c[4], x;
        uint32_t nn = vchance(t, 35) ? 4u : 3u;
        for (k = 0; k < nn; k++)
            c[k] = near(deg(song.prog[b] + 2 * (int32_t)k), reg);
        for (k = 1; k < nn; k++)                 /* sorted low to high */
            for (i = k; i && c[i] < c[i - 1u]; i--) {
                x = c[i];
                c[i] = c[i - 1u];
                c[i - 1u] = x;
            }
        if (st == 1u) {                          /* held through the bar */
            st_note(t, 16u * b, c, nn, 84, 0);
            for (i = 1; i < 15u; i++)
                st_tie(t, 16u * b + i);
        } else if (st == 2u) {                   /* offbeat stabs (DENSITY: 8ths when high) */
            uint32_t every = t->gen_dens > 96u ? 2u : 4u;
            for (i = 2; i < 16u; i += every)
                st_note(t, 16u * b + i, c, nn, 92, 0);
        } else if (st == 3u) {                   /* two halves */
            st_note(t, 16u * b, c, nn, 88, 0);
            for (i = 1; i < 6u; i++)
                st_tie(t, 16u * b + i);
            st_note(t, 16u * b + 8u, c, nn, 80, 0);
            for (i = 9; i < 12u; i++)
                st_tie(t, 16u * b + i);
        } else {                                 /* euclidean */
            uint32_t m = eu16(dens_k(t, 2, 7), 0) | 1u;
            for (i = 0; i < 16u; i++)
                if ((m >> i) & 1u)
                    st_note(t, 16u * b + i, c, nn, i ? 84u : 96u, 0);
        }
    }
    t->gate = (uint8_t)(st == 2u || st == 4u ? rr(30, 60) : 95);
}

static void pat_lead(track_t *t)
{
    uint32_t bass = bass_mask(), best = 0, bo = ~0u, r, k = dens_k(t, 2, 10), i, b;
    uint32_t st = t->gen_style % STYLE_N[ROLE_LEAD], tie = st == 1u ? 85u : st == 2u ? 0u : 60u;
    int8_t mot[16];
    int32_t at = 0, base = rr(70, 76) + 12 * t->gen_range;
    for (r = 0; r < 16u; r++) {                  /* the rotation that plays least over the bass */
        uint32_t c = eu16(k, r), o = (uint32_t)__builtin_popcount(c & bass) * 4u + rnd(4);
        if (o < bo) {
            bo = o;
            best = c;
        }
    }
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u) {
            at = 2 * (int32_t)rnd(3);            /* a chord tone on the beat, now and then up an octave */
            at += vchance(t, 25) ? 7 : 0;
        } else if (vchance(t, 20)) {
            at = (int32_t)rnd(8);                /* a leap */
        } else {
            at += rr(-2, 2);                     /* a step or two */
        }
        mot[i] = (int8_t)clamp(at, -2, 9);
    }
    clear_all(t);
    for (b = 0; b < 4u; b++) {
        uint32_t mb = best;
        if (b == 3u && vchance(t, 50))           /* the last bar: an answer */
            mb = (best & 0x00FFu) | (eu16(dens_k(t, 1, 5), rnd(8)) & 0xFF00u);
        for (i = 0; i < 16u; i++) {
            int32_t n;
            uint32_t j, len, v, fl;
            if (!((mb >> i) & 1u))
                continue;
            n = near(deg(song.prog[b] + mot[i]), base);
            v = i % 4u == 0u ? 100u : (uint32_t)rr(84, 96);
            fl = t->s.mono && vchance(t, 15) ? SF_SLIDE : 0u;
            st_note(t, 16u * b + i, &n, 1, v, fl);
            for (len = 0, j = i + 1u; j < 16u && !((mb >> j) & 1u) && len < 3u && chance(tie); j++, len++)
                st_tie(t, 16u * b + j);
        }
    }
    t->gate = (uint8_t)(st == 2u ? rr(25, 45) : rr(50, 85));
}

static void pat_drums(track_t *t)
{
    uint32_t st = t->gen_style % STYLE_N[ROLE_DRUM], i, m, hat = 20u + t->gen_dens * 70u / 127u;   /* hat chance % */
    if (!st)
        st = 1u + rnd(5);
    clear_all(t);
    for (i = 0; i < 16u; i++) {
        if (st == 1u) {                          /* house */
            if (i % 4u == 0u) st_hit(t, i, LN_KICK, i == 0u);
            if (i % 8u == 4u) st_hit(t, i, LN_CLAP, 0);
            if (i % 4u == 2u) st_hit(t, i, LN_HATO, 0);
            if (i % 2u == 1u && chance(hat)) st_hit(t, i, LN_HATC, 0);
        } else if (st == 2u) {                   /* breakbeat */
            if (i == 0u || i == 10u || (i == 7u && vchance(t, 50))) st_hit(t, i, LN_KICK, i == 0u);
            if (i == 4u || i == 12u) st_hit(t, i, LN_SNARE, 1);
            if (i % 2u == 0u || chance(hat / 3u)) st_hit(t, i, LN_HATC, 0);
            if (i == 15u && vchance(t, 50)) st_hit(t, i, LN_SNARE, 0);
        } else if (st == 3u) {                   /* half-time */
            if (i == 0u || (i == 11u && vchance(t, 50)) || (i == 6u && vchance(t, 30))) st_hit(t, i, LN_KICK, i == 0u);
            if (i == 8u) st_hit(t, i, LN_SNARE, 1);
            if (chance(hat + 10u)) st_hit(t, i, LN_HATC, 0);
        } else if (st == 4u) {                   /* techno */
            if (i % 4u == 0u) st_hit(t, i, LN_KICK, 1);
            if (i % 4u == 2u) st_hit(t, i, LN_HATO, 0);
            if (chance(hat)) st_hit(t, i, LN_HATC, 0);
        }
    }
    if (st == 5u) {                              /* euclidean kit */
        uint32_t hk = dens_k(t, 5, 12);
        m = eu16(dens_k(t, 2, 6), 0);
        for (i = 0; i < 16u; i++) {
            if ((m >> i) & 1u) st_hit(t, i, LN_KICK, i == 0u);
            if ((eu16(2, 4) >> i) & 1u) st_hit(t, i, chance(50) ? LN_SNARE : LN_CLAP, 0);
            if ((eu16(hk, 0) >> i) & 1u) st_hit(t, i, LN_HATC, 0);
        }
    }
    if (vchance(t, 60)) {                        /* a percussion line on top */
        uint32_t lane = rnd(3) == 0 ? LN_BELL : chance(50) ? LN_RIM : LN_TOM, k = dens_k(t, 2, 6);
        m = eu16(k, rnd(16));
        for (i = 0; i < 16u; i++)
            if ((m >> i) & 1u)
                st_hit(t, i, lane, 0);
    }
    t->gate = 50;
}

/* the loop: a step a slice (16 a bar), as the loop was played, then varied: SHUFFLE takes some 16ths from elsewhere,
 * STUTTER repeats, HALFTIME plays every slice twice; DENSITY leaves steps out (the slice before rings on) */
static void pat_loop(track_t *t)
{
    uint32_t sl = smp_slices(t->s.smp), len = sl > NSTEP ? NSTEP : sl, st = t->gen_style % STYLE_N[ROLE_LOOP], i, x;
    if (!st)
        st = 1u + rnd(4);
    clear_all(t);
    for (i = 0; i < len; i++) {
        x = i;
        if (st == 2u && (i & 3u) >= 2u && vchance(t, 40))
            x = (i + 4u * (1u + rnd(3))) % sl;
        else if (st == 3u && (i & 7u) >= 4u && vchance(t, 50))
            x = i & ~3u;
        else if (st == 4u)
            x = (i / 2u + (i / 32u) * 16u) % sl;
        if (i && vchance(t, 6))
            x = rnd(sl);
        if (i % 4u && t->gen_dens < 50u && chance((50u - t->gen_dens) * 2u))
            continue;
        t->step[i].time = ST_NOTE;
        t->step[i].n = 1;
        t->step[i].note[0] = (uint8_t)x;
        t->step[i].vel = (uint8_t)(i % 4u ? 92 : 104);
    }
    t->len = (uint8_t)(len < 16u ? 16u : len);
    t->gate = 100;
}

/* ------------------------------------------------- key, scale, chords --- */
/* note n of the song's key moved by `shift` scale degrees, in the scale sc (its own degree found first; a note off the
 * scale goes down to the degree under it). Diatonic: C in C major +2 -> E */
static int32_t note_deg_shift(int32_t n, int32_t root, uint32_t sc, int32_t shift)
{
    int32_t d = n - root, o = d >= 0 ? d / 12 : -((-d + 11) / 12), pc = d - 12 * o, k = 6;
    while (k > 0 && SCALES[sc % SC_N][k] > pc)
        k--;
    k += shift + 7 * o;
    o = k >= 0 ? k / 7 : -((-k + 6) / 7);
    return root + 12 * o + SCALES[sc % SC_N][k - 7 * o];
}
/* the melodic parts into a new key and scale, note for note (each keeps its scale degree; the root moves the shorter
 * way round): the parts stay, they sound in the new key */
static void song_set_key(uint32_t root, uint32_t sc)
{
    int32_t mv = (int32_t)(root % 12u) - (int32_t)song.root;
    uint32_t t, i, k;
    if (mv > 6) mv -= 12;
    if (mv < -6) mv += 12;
    gen_lock();
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < NSTEP && !NO_NOTES(t); i++) {
            step_t *s = &trk[t].step[i];
            for (k = 0; k < s->n && k < 4u; k++) {
                int32_t n = note_deg_shift(s->note[k], song.root, song.scale, 0);   /* (on the old scale first) */
                int32_t d = n - (int32_t)song.root, o = d >= 0 ? d / 12 : -((-d + 11) / 12), pc = d - 12 * o, j = 0;
                while (j < 6 && SCALES[song.scale % SC_N][j] != pc)
                    j++;
                n = (int32_t)song.root + mv + 12 * o + SCALES[sc % SC_N][j];
                s->note[k] = (uint8_t)clamp(n, 12, 120);
            }
        }
    song.root = (uint8_t)(root % 12u);
    song.scale = (uint8_t)(sc % SC_N);
    gen_unlock();
}
/* progression i: each bar's melodic notes moved by the change of its chord, diatonically (the rhythm stays) */
static void song_set_prog(uint32_t pi)
{
    uint32_t t, i, k, b;
    pi %= NELEM(PROGS);
    gen_lock();
    for (b = 0; b < 4u; b++) {
        int32_t sh = (int32_t)PROGS[pi][b] - (int32_t)song.prog[b];
        if (sh > 3) sh -= 7;                     /* the nearer way: up 5 degrees is down 2 */
        if (sh < -3) sh += 7;
        for (t = 0; t < NTRK; t++)
            for (i = 16u * b; i < 16u * b + 16u && !NO_NOTES(t); i++) {
                step_t *s = &trk[t].step[i];
                for (k = 0; k < s->n && k < 4u; k++)
                    s->note[k] = (uint8_t)clamp(note_deg_shift(s->note[k], song.root, song.scale, sh), 12, 120);
            }
    }
    memcpy(song.prog, PROGS[pi], 4);
    song.prog_i = (uint8_t)pi;
    gen_unlock();
}

/* ------------------------------------------------------------ the API --- */
static void gen_lock(void);                      /* the audio ISR held off (oc_main.c / host) */
static void gen_unlock(void);
static void gen_pattern_of(track_t *t, uint32_t type)
{
    uint32_t role = TYPE_ROLE[type % TY_N], i, keep = 0xFFu;
    if (role == ROLE_BASS) pat_bass(t);
    else if (role == ROLE_CHORD) pat_chords(t);
    else if (role == ROLE_LEAD) pat_lead(t);
    else if (role == ROLE_LOOP) { pat_loop(t); return; }
    else pat_drums(t);
    t->len = (uint8_t)(role == ROLE_DRUM ? 16u : 64u);
    if (type == TY_KICK) keep = 1u << LN_KICK;   /* the drum types: their lanes of the beat */
    if (type == TY_DRUMS)                        /* a kick track plays the kick: the kit leaves it out */
        for (i = 0; i < NTRK; i++)
            if (trk[i].type == TY_KICK && &trk[i] != t)
                keep = (uint8_t)~(1u << LN_KICK);
    if (type == TY_SNARE) keep = 1u << LN_SNARE | 1u << LN_CLAP | 1u << LN_RIM;
    if (type == TY_PERC) keep = 1u << LN_HATC | 1u << LN_HATO | 1u << LN_TOM | 1u << LN_RIM | 1u << LN_BELL;
    for (i = 0; keep != 0xFFu && i < NSTEP; i++) {
        t->step[i].hit &= (uint8_t)keep;
        t->step[i].acc &= (uint8_t)keep;
        if (!t->step[i].hit)
            t->step[i].time = ST_REST;
    }
    if (type == TY_ATMO)                         /* long notes: every other chord held over two bars */
        for (i = 16; i < NSTEP; i += 32u)
            if (t->step[i].time == ST_NOTE) {
                uint32_t j;
                for (j = i - 1u; j < i + 15u; j++) {   /* (the bar before's last step too: the tie runs through) */
                    memset(&t->step[j], 0, sizeof t->step[j]);
                    t->step[j].time = ST_TIE;
                }
            }
}
static uint32_t trk_i(const track_t *t) { return (uint32_t)(t - trk); }

static void gen_sound(track_t *t)
{
    static track_t tmp;
    tmp = *t;
    gen_sound_of(&tmp, t->type);
    gen_lock();
    track_panic(trk_i(t));
    t->s = tmp.s;
    gen_unlock();
}
/* a new sound of engine e (0xFF: any of the role's) on track t */
static void gen_sound_engine(track_t *t, uint32_t e)
{
    static track_t tmp;
    tmp = *t;
    if (e == 0xFFu)
        gen_sound_of(&tmp, t->type);
    else
        gen_sound_engine_of(&tmp, t->type, e);
    gen_lock();
    track_panic(trk_i(t));
    t->s = tmp.s;
    gen_unlock();
}
/* a stored sound back on track t (PRESETS: the sound history) */
static void set_patch(track_t *t, const patch_t *p)
{
    gen_lock();
    track_panic(trk_i(t));
    t->s = *p;
    gen_unlock();
}
static void gen_pattern(track_t *t)
{
    static track_t tmp;
    tmp = *t;
    gen_pattern_of(&tmp, t->type);
    gen_lock();
    memcpy(t->step, tmp.step, sizeof t->step);
    t->len = tmp.len;
    t->gate = tmp.gate;
    gen_unlock();
}
static void gen_track(track_t *t)
{
    static track_t tmp;
    tmp = *t;
    gen_sound_of(&tmp, t->type);
    gen_pattern_of(&tmp, t->type);
    gen_lock();
    track_panic(trk_i(t));
    *t = tmp;
    t->nsnd = 0;
    t->off_at = 0;
    t->hold = 0;
    gen_unlock();
}
/* a new key: the melodic tracks' parts again (their sounds stay) */
static void gen_key(void)
{
    uint32_t i;
    gen_harmony();
    for (i = 0; i < NTRK; i++)
        if (!IS_DRUM(i))
            gen_pattern(&trk[i]);
}
static void gen_all(void)
{
    uint32_t i;
    gen_harmony();
    for (i = 0; i < NTRK; i++)
        gen_track(&trk[i]);
}

/* ------------------------------------------- partial new, MUTATE, tools --- */
/* MUTATE (gen_mut 0..127): how far a partial new goes from what is there. A value moves that part of the way to the new
 * one; a choice (a wave, an engine) changes with that chance; a step is taken from the new part with that chance */
static int32_t mix_to(int32_t a, int32_t b, uint32_t m) { return a + (b - a) * (int32_t)m / 127; }
#define MIXF(f) d->f = (__typeof__(d->f))mix_to(d->f, n->f, m)
#define PICKF(f) if (chance(m * 100u / 127u)) d->f = n->f
enum { PART_OSC, PART_FILTER, PART_ENV };
static void mutate_sound(track_t *t, uint32_t part)
{
    static track_t tmp;
    patch_t *d, *n;
    uint32_t m = t->gen_mut ? t->gen_mut : 127u, k;
    tmp = *t;
    if (part == PART_OSC && m >= 96u)
        gen_sound_of(&tmp, t->type);             /* a lot: maybe another engine */
    else
        gen_sound_engine_of(&tmp, t->type, t->s.engine);
    n = &tmp.s;
    gen_lock();
    d = &t->s;
    if (part == PART_OSC) {
        if (n->engine != d->engine) {            /* another engine: its oscillators whole, the rest stays */
            patch_t keep = *d;
            *d = *n;
            d->cutoff = keep.cutoff; d->res = keep.res; d->fenv = keep.fenv; d->fdec = keep.fdec;
            d->fatt = keep.fatt; d->fsus = keep.fsus; d->frel = keep.frel;
            d->atk = keep.atk; d->dec = keep.dec; d->sus = keep.sus; d->rel = keep.rel;
            d->level = keep.level; d->pan = keep.pan; d->dly = keep.dly; d->rev = keep.rev;
            if (d->engine == E_FM || d->engine == E_ADD || d->engine == E_DRUM)
                d->cutoff = n->cutoff;           /* (they want their own filter setting) */
        } else {
            PICKF(wave1); PICKF(wave2); MIXF(detune); MIXF(mix2); PICKF(oct2); MIXF(sub); MIXF(noise);
            for (k = 0; k < 3u; k++)
                if (chance(m * 100u / 127u))
                    d->fm_ratio[k] = n->fm_ratio[k];
            MIXF(fm_index); MIXF(fm_idec); MIXF(fm_fb); PICKF(fm_algo); MIXF(saw_n); MIXF(saw_spread);
            MIXF(add_tilt); PICKF(kit); MIXF(dtune); MIXF(ddec);
            for (k = 0; k < ADD_N; k++)
                d->add_amp[k] = (uint8_t)mix_to(d->add_amp[k], n->add_amp[k], m);
        }
    } else if (part == PART_FILTER) {
        MIXF(cutoff); MIXF(res); MIXF(fenv); MIXF(fdec); MIXF(fatt); MIXF(fsus); MIXF(frel); MIXF(keytrk);
    } else {
        MIXF(atk); MIXF(dec); MIXF(sus); MIXF(rel);
    }
    track_panic(trk_i(t));
    gen_unlock();
}
/* NOTES: the rhythm stays, the pitches are new (per step with MUTATE's chance); RHYTHM: the notes stay in their order,
 * the rhythm is new (drums: the new beat, per step) */
static void mutate_part(track_t *t, int rhythm)
{
    static track_t tmp;
    uint32_t m = t->gen_mut ? t->gen_mut : 127u, len, i, j, nn = 0;
    static step_t old[NSTEP];
    static uint8_t notes[NSTEP][5];
    tmp = *t;
    gen_pattern_of(&tmp, t->type);
    gen_lock();
    memcpy(old, t->step, sizeof old);
    len = clamp(t->len, 1, NSTEP);
    if (IS_DRUM(trk_i(t)) || !rhythm) {
        for (i = 0; i < len; i++) {
            step_t *s = &t->step[i];
            if (!chance(m * 100u / 127u))
                continue;
            if (IS_DRUM(trk_i(t))) {
                *s = tmp.step[i];
            } else if (s->time == ST_NOTE) {     /* NOTES: the new part's notes nearest before this step */
                for (j = i + 1u; j-- > 0u && tmp.step[j].time != ST_NOTE;)
                    ;
                if (j < NSTEP && tmp.step[j].time == ST_NOTE && tmp.step[j].n) {
                    uint32_t k, n = tmp.step[j].n < s->n ? tmp.step[j].n : s->n;
                    for (k = 0; k < n; k++)
                        s->note[k] = tmp.step[j].note[k];
                }
            }
        }
    } else {                                     /* RHYTHM: the old notes, in order, on the new rhythm */
        for (i = 0; i < len; i++)
            if (old[i].time == ST_NOTE && old[i].n) {
                memcpy(notes[nn], old[i].note, 4);
                notes[nn++][4] = old[i].n;
            }
        for (i = 0, j = 0; i < len; i++) {
            if (!chance(m * 100u / 127u))
                continue;
            t->step[i] = tmp.step[i];
            if (t->step[i].time == ST_NOTE && nn) {
                memcpy(t->step[i].note, notes[j % nn], 4);
                t->step[i].n = notes[j % nn][4];
                j++;
            }
        }
    }
    gen_unlock();
}
/* EUCLID: k hits over the track's LEN, rotated; melodic: the notes there, in order, on those steps (none yet: the bar's
 * chord root); a drum track: on lane `lane` */
static void euclid_apply(track_t *t, uint32_t hits, uint32_t rot, uint32_t lane)
{
    uint32_t len = clamp(t->len, 1, NSTEP), i, nn = 0, j = 0;
    static uint8_t notes[NSTEP][5];
    hits = hits > len ? len : hits;
    gen_lock();
    if (IS_DRUM(trk_i(t))) {
        for (i = 0; i < len; i++) {
            step_t *s = &t->step[i];
            uint32_t on = hits && ((i + len - rot % len) * hits) % len < hits;
            s->hit = (uint8_t)(on ? s->hit | 1u << lane : s->hit & ~(1u << lane));
            s->acc &= s->hit;
            s->time = s->hit ? ST_NOTE : ST_REST;
            s->vel = s->vel ? s->vel : 100;
        }
    } else {
        for (i = 0; i < len; i++)
            if (t->step[i].time == ST_NOTE && t->step[i].n) {
                memcpy(notes[nn], t->step[i].note, 4);
                notes[nn++][4] = t->step[i].n;
            }
        for (i = 0; i < len; i++) {
            step_t *s = &t->step[i];
            uint32_t on = hits && ((i + len - rot % len) * hits) % len < hits;
            memset(s, 0, sizeof *s);
            if (!on)
                continue;
            s->time = ST_NOTE;
            s->vel = 96;
            if (nn) {
                memcpy(s->note, notes[j % nn], 4);
                s->n = notes[j % nn][4];
                j++;
            } else if (IS_LOOP(trk_i(t))) {
                s->note[0] = (uint8_t)(i % smp_slices(t->s.smp));
            } else {
                s->note[0] = (uint8_t)near(deg(song.prog[(i / 16u) % 4u]), 48 + 12 * (int32_t)(TYPE_ROLE[t->type % TY_N] != ROLE_BASS));
                s->n = 1;
            }
        }
    }
    gen_unlock();
}
static struct { step_t step[NSTEP]; uint8_t len, gate, drum, full; } clip;   /* COPY / PASTE */
static void pattern_copy(const track_t *t)
{
    memcpy(clip.step, t->step, sizeof clip.step);
    clip.len = t->len;
    clip.gate = t->gate;
    clip.drum = (uint8_t)trk_role(trk_i(t));
    clip.full = 1;
}
static int pattern_paste(track_t *t)              /* 0: nothing copied, or a drum part onto a melodic track (and back) */
{
    if (!clip.full || (clip.drum >= ROLE_DRUM || trk_role(trk_i(t)) >= ROLE_DRUM ? clip.drum != trk_role(trk_i(t)) : 0))
        return 0;
    gen_lock();
    memcpy(t->step, clip.step, sizeof t->step);
    t->len = clip.len;
    t->gate = clip.gate;
    track_panic(trk_i(t));
    gen_unlock();
    return 1;
}
/* a track's TYPE: its new sound and part, as the type makes them (the harmony as it is) */
static void gen_type(track_t *t, uint32_t type)
{
    gen_lock();
    track_panic(trk_i(t));
    if (TYPE_ROLE[type % TY_N] >= ROLE_DRUM || TYPE_ROLE[t->type % TY_N] >= ROLE_DRUM) {
        uint32_t i;
        for (i = 0; i < NLANE; i++)
            dvoice[trk_i(t)][i].active = 0;
    }
    t->type = (uint8_t)(type % TY_N);
    gen_unlock();
    gen_track(t);
}

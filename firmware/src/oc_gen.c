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
    memcpy(song.prog, PROGS[rnd(NELEM(PROGS))], 4);
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

static void gen_sound_of(track_t *t, uint32_t role)
{
    patch_t *p = &t->s;
    memset(p, 0, sizeof *p);
    if (role == ROLE_BASS)
        sound_bass(p);
    else if (role == ROLE_CHORD)
        sound_chords(p);
    else if (role == ROLE_LEAD)
        sound_lead(p);
    else {
        p->engine = E_DRUM;
        p->kit = (uint8_t)rnd(4);
        p->level = (uint8_t)rr(86, 100);
        p->rev = (uint8_t)rr(4, 18);
        p->mono = 0;
    }
}

/* ----------------------------------------------------------- patterns --- */
static uint32_t bass_mask(void)                  /* the bass's gates in its first bar */
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        if (trk[ROLE_BASS].step[i].time == ST_NOTE && trk[ROLE_BASS].step[i].n)
            m |= 1u << i;
    return m;
}

static void pat_bass(track_t *t)
{
    static const uint16_t RH[] = {0x4444u, 0x5555u, 0x1111u, 0x9249u, 0x2492u, 0xB5ADu, 0};
    uint32_t m = RH[rnd(NELEM(RH))], b, i, acid = t->s.engine == E_ACID;
    if (!m)
        m = eu16((uint32_t)rr(5, 7), rnd(4)) | 1u;
    clear_all(t);
    for (b = 0; b < 4u; b++) {
        int32_t root = near(deg(song.prog[b]), 39), fifth = root + deg(song.prog[b] + 4) - deg(song.prog[b]);
        for (i = 0; i < 16u; i++) {
            int32_t n = root;
            uint32_t fl = 0;
            if (!((m >> i) & 1u))
                continue;
            if (i && chance(18))
                n = fifth;
            else if (i && chance(15))
                n = root + 12;
            if (acid) {                          /* the 303 way: accents and slides */
                fl |= chance(28) ? SF_ACC : 0u;
                fl |= chance(22) ? SF_SLIDE : 0u;
            }
            st_note(t, 16u * b + i, &n, 1, i % 4u == 0u ? 104u : (uint32_t)rr(84, 98), fl);
        }
    }
    t->gate = (uint8_t)(acid ? 55 : rr(45, 80));
}

static void pat_chords(track_t *t)
{
    uint32_t pad = t->s.atk > 40u, style = pad ? (chance(70) ? 0u : 2u) : rnd(4), b, i, k;
    clear_all(t);
    for (b = 0; b < 4u; b++) {
        int32_t c[4], x;
        uint32_t nn = chance(65) ? 3u : 4u;
        for (k = 0; k < nn; k++)
            c[k] = near(deg(song.prog[b] + 2 * (int32_t)k), 60);
        for (k = 1; k < nn; k++)                 /* sorted low to high */
            for (i = k; i && c[i] < c[i - 1u]; i--) {
                x = c[i];
                c[i] = c[i - 1u];
                c[i - 1u] = x;
            }
        if (style == 0) {                        /* held through the bar */
            st_note(t, 16u * b, c, nn, 84, 0);
            for (i = 1; i < 15u; i++)
                st_tie(t, 16u * b + i);
        } else if (style == 1) {                 /* offbeat stabs */
            for (i = 2; i < 16u; i += 4u)
                st_note(t, 16u * b + i, c, nn, 92, 0);
        } else if (style == 2) {                 /* two halves */
            st_note(t, 16u * b, c, nn, 88, 0);
            for (i = 1; i < 6u; i++)
                st_tie(t, 16u * b + i);
            st_note(t, 16u * b + 8u, c, nn, 80, 0);
            for (i = 9; i < 12u; i++)
                st_tie(t, 16u * b + i);
        } else {                                 /* euclidean */
            uint32_t m = eu16((uint32_t)rr(3, 5), 0) | 1u;
            for (i = 0; i < 16u; i++)
                if ((m >> i) & 1u)
                    st_note(t, 16u * b + i, c, nn, i ? 84u : 96u, 0);
        }
    }
    t->gate = (uint8_t)(style == 1 || style == 3 ? rr(30, 60) : 95);
}

static void pat_lead(track_t *t)
{
    uint32_t bass = bass_mask(), best = 0, bo = ~0u, r, k = (uint32_t)rr(4, 7), i, b;
    int8_t mot[16];
    int32_t at = 0, base = rr(70, 76);
    for (r = 0; r < 16u; r++) {                  /* the rotation that plays least over the bass */
        uint32_t c = eu16(k, r), o = (uint32_t)__builtin_popcount(c & bass) * 4u + rnd(4);
        if (o < bo) {
            bo = o;
            best = c;
        }
    }
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u)
            at = 2 * (int32_t)rnd(3) + (chance(25) ? 7 : 0);   /* a chord tone on the beat */
        else if (chance(20))
            at = (int32_t)rnd(8);                /* a leap */
        else
            at += rr(-2, 2);                     /* a step or two */
        mot[i] = (int8_t)clamp(at, -2, 9);
    }
    clear_all(t);
    for (b = 0; b < 4u; b++) {
        uint32_t mb = best;
        if (b == 3u && chance(50))               /* the last bar: an answer */
            mb = (best & 0x00FFu) | (eu16((uint32_t)rr(2, 4), rnd(8)) & 0xFF00u);
        for (i = 0; i < 16u; i++) {
            int32_t n;
            uint32_t j, len;
            if (!((mb >> i) & 1u))
                continue;
            n = near(deg(song.prog[b] + mot[i]), base);
            st_note(t, 16u * b + i, &n, 1, i % 4u == 0u ? 100u : (uint32_t)rr(84, 96),
                    t->s.mono && chance(15) ? SF_SLIDE : 0u);
            for (len = 0, j = i + 1u; j < 16u && !((mb >> j) & 1u) && len < 3u && chance(60); j++, len++)
                st_tie(t, 16u * b + j);
        }
    }
    t->gate = (uint8_t)rr(50, 85);
}

static void pat_drums(track_t *t)
{
    uint32_t style = rnd(5), i, m;
    clear_all(t);
    for (i = 0; i < 16u; i++) {
        if (style == 0) {                        /* house */
            if (i % 4u == 0u) st_hit(t, i, LN_KICK, i == 0u);
            if (i % 8u == 4u) st_hit(t, i, LN_CLAP, 0);
            if (i % 4u == 2u) st_hit(t, i, LN_HATO, 0);
            if (i % 2u == 1u && chance(60)) st_hit(t, i, LN_HATC, 0);
        } else if (style == 1) {                 /* breakbeat */
            if (i == 0u || i == 10u || (i == 7u && chance(50))) st_hit(t, i, LN_KICK, i == 0u);
            if (i == 4u || i == 12u) st_hit(t, i, LN_SNARE, 1);
            if (i % 2u == 0u) st_hit(t, i, LN_HATC, 0);
            if (i == 15u && chance(50)) st_hit(t, i, LN_SNARE, 0);
        } else if (style == 2) {                 /* half-time */
            if (i == 0u || (i == 11u && chance(50)) || (i == 6u && chance(30))) st_hit(t, i, LN_KICK, i == 0u);
            if (i == 8u) st_hit(t, i, LN_SNARE, 1);
            if (chance(75)) st_hit(t, i, LN_HATC, 0);
        } else if (style == 3) {                 /* techno */
            if (i % 4u == 0u) st_hit(t, i, LN_KICK, 1);
            if (i % 4u == 2u) st_hit(t, i, LN_HATO, 0);
            if (chance(50)) st_hit(t, i, LN_HATC, 0);
        }
    }
    if (style == 4) {                            /* euclidean kit */
        m = eu16((uint32_t)rr(3, 5), 0);
        for (i = 0; i < 16u; i++) {
            if ((m >> i) & 1u) st_hit(t, i, LN_KICK, i == 0u);
            if ((eu16(2, 4) >> i) & 1u) st_hit(t, i, chance(50) ? LN_SNARE : LN_CLAP, 0);
            if ((eu16((uint32_t)rr(7, 11), 0) >> i) & 1u) st_hit(t, i, LN_HATC, 0);
        }
    }
    if (chance(60)) {                            /* a percussion line on top */
        uint32_t lane = rnd(3) == 0 ? LN_BELL : chance(50) ? LN_RIM : LN_TOM;
        m = eu16((uint32_t)rr(3, 5), rnd(16));
        for (i = 0; i < 16u; i++)
            if ((m >> i) & 1u)
                st_hit(t, i, lane, 0);
    }
    t->gate = 50;
}

/* ------------------------------------------------------------ the API --- */
static void gen_lock(void);                      /* the audio ISR held off (oc_main.c / host) */
static void gen_unlock(void);
static void gen_pattern_of(track_t *t, uint32_t role)
{
    if (role == ROLE_BASS) pat_bass(t);
    else if (role == ROLE_CHORD) pat_chords(t);
    else if (role == ROLE_LEAD) pat_lead(t);
    else pat_drums(t);
    t->len = (uint8_t)(role == ROLE_DRUM ? 16u : 64u);
}
static uint32_t trk_i(const track_t *t) { return (uint32_t)(t - trk); }

static void gen_sound(track_t *t)
{
    static track_t tmp;
    tmp = *t;
    gen_sound_of(&tmp, trk_i(t));
    gen_lock();
    track_panic(trk_i(t));
    t->s = tmp.s;
    gen_unlock();
}
static void gen_pattern(track_t *t)
{
    static track_t tmp;
    tmp = *t;
    gen_pattern_of(&tmp, trk_i(t));
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
    gen_sound_of(&tmp, trk_i(t));
    gen_pattern_of(&tmp, trk_i(t));
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
    for (i = 0; i < ROLE_DRUM; i++)
        gen_pattern(&trk[i]);
}
static void gen_all(void)
{
    uint32_t i;
    gen_harmony();
    for (i = 0; i < NTRK; i++)
        gen_track(&trk[i]);
}

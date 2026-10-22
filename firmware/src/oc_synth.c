/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the sound. Voices (a shared pool), the six engines, the track mix, delay and reverb, the master.
 * Fixed point: audio Q15 (32767 = 1.0), envelopes Q24, phases 2^32 a cycle. Rendered in blocks of CTL samples; the
 * control values (envelopes, filter, pitch) once a block, ramped where it matters (the amplitude). */

#define VOICE_FS 20000                       /* one voice at full level, before the track and master */

/* ---------------------------------------------------------------- helpers --- */
static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536, a >= 0 up to 2^31, k up to 2^16 (no 64-bit multiply on the target) */
static inline int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((uint32_t)(a >> 16) * k) + (((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t sine(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[i + 1u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
/* polyBLEP residual (Q15) at a wrap of a phase stepping inc */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline int32_t saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc); }
static inline int32_t osc(uint32_t w, uint32_t ph, uint32_t inc)
{
    int32_t s;
    switch (w) {
    case W_SQR: return (saw(ph, inc) - saw(ph + 0x80000000u, inc)) >> 1;
    case W_TRI: s = (int32_t)(ph >> 15); return s < 65536 ? s - 32768 : 98303 - s;
    case W_SINE: return sine(ph);
    default: return saw(ph, inc);
    }
}
static inline int32_t sat(int32_t x)         /* soft clip: tanh over +-2 (Q15 in and out) */
{
    int32_t i, f;
    x = clamp(x, -65535, 65535);
    i = (x + 65536) >> 9;                    /* 0..255 */
    f = (x + 65536) & 511;
    return TANH[i] + (((TANH[i + 1] - TANH[i]) * f) >> 9);
}
static inline uint32_t xrand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
/* phase increment of a pitch in 1/16 semitones */
static uint32_t pitch_inc(int32_t p16)
{
    p16 = clamp(p16, 0, 127 * 16);
    return (uint32_t)mulq16((int32_t)NOTE_INC[p16 >> 4], FINE16[p16 & 15]);
}
/* a ratio Q4 (16 = 1.0) of an increment (wraps above Nyquist: not reached by the generated sounds) */
static inline uint32_t inc_ratio(uint32_t inc, uint32_t q4) { return (inc >> 4) * q4; }

static inline int32_t lfo_amt(const track_t *tr, uint32_t d, int32_t range);

/* ----------------------------------------------------------------- voices --- */
static uint32_t v_age;
/* what a melodic track's notes are moved by as they play: the song's TRANSPOSE and the track's OCTAVE (the notes
 * stored and the keys pressed stay as they are) */
static int32_t note_shift(uint32_t t)
{
    return NO_NOTES(t) ? 0 : song.transpose + 12 * trk[t % NTRK].octave;
}
static void voice_start(voice_t *v, uint32_t t, uint32_t note, uint32_t vel, uint32_t acc, uint32_t lane, int32_t poff)
{
    const patch_t *s = &trk[t].s;
    uint32_t keep = v->active && v->trk == t;   /* a retrigger keeps the phases: no click */
    v->trk = (uint8_t)t;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->acc = (uint8_t)acc;
    v->lane = (uint8_t)lane;
    v->gate = 1;
    v->stage = 1;
    v->poff = (int16_t)poff;
    v->pitch16 = v->pitch_to = ((int32_t)note + note_shift(t)) * 16 + poff;
    v->gstep = 0;
    v->env = keep ? v->env : 0;
    v->fenv = keep ? v->fenv : 0;
    v->fstage = 1;
    v->ienv = 32767;
    v->t = 0;
    v->age = ++v_age;
    if (!keep) {
        uint32_t i;
        v->lp = v->bp = v->z1 = v->fb1 = v->fb2 = 0;
        for (i = 0; i < 8u; i++)
            v->ph[i] = (uint32_t)(i * 0x2E8BA2E9u);   /* the saws spread from the start (no comb at onset) */
        v->amp = 0;
    }
    if (s->engine == E_ADD) {
        uint32_t k;
        for (k = 0; k < ADD_N; k++)
            v->pa[k] = k < s->add_n ? s->add_amp[k] * 258 : 0;
    }
    if (s->engine == E_DRUM)
        v->pa[0] = v->pa[1] = v->pa[2] = 0;
    v->noise ^= v->age * 2654435761u;
    v->active = 1;
}

/* steals (diagnostics, the emulator and the tests read them): [taker][victim's track], a cut that was still audible
 * (above about -36 dB), split by whether the victim's note was still held */
static struct { uint32_t held[NTRK][NTRK], tail[NTRK][NTRK], quiet, loud, lvl[6]; } vstat;   /* lvl: cuts by level, 6 dB bands from -36 dB */
static void steal_count(uint32_t t, const voice_t *v)
{
    if (!v->active)
        return;
    if (v->amp >= 512) {
        uint32_t b = 0, a = (uint32_t)v->amp >> 10;
        while (a && b < 5u) { a >>= 1; b++; }
        vstat.lvl[b]++;
        vstat.loud += v->amp >= 3277;
    }
    if (v->amp < 512)
        vstat.quiet++;
    else if (v->gate)
        vstat.held[t % NTRK][v->trk % NTRK]++;
    else
        vstat.tail[t % NTRK][v->trk % NTRK]++;
}
/* The pool: NVOICE voices for the three melodic tracks (the drums have their own, dvoice). Each track has a reserve
 * (VRES: a chord needs 4; two float); a track over its reserve gives voices up first. A voice's level is v->amp (Q15, every engine).
 * A note (track t, note n; drums: the lane) takes, in this order:
 *   1. a voice of t already on that note / lane (a retrigger: the drum machine's choke, a repeated note)
 *   2. a free voice
 *   3. a releasing voice of a track over its reserve (t's own counts as over), the quietest
 *   4. any releasing voice, the quietest, if t is under its reserve
 *   5. t's own oldest voice (a held one: t plays more notes than it has)
 *   6. the quietest voice of a track over its reserve */
static uint32_t vres(uint32_t t)                 /* a track's reserve by its role */
{
    static const uint8_t R[ROLE_N] = {1, 4, 1, 0, 0};
    return R[trk_role(t) % ROLE_N];
}
static uint8_t alloc_retrig;                     /* the last voice_alloc_ was a retrigger (rule 1) */
static voice_t *voice_alloc_(uint32_t t, uint32_t note, uint32_t lane)
{
    voice_t *best = 0, *v;
    uint32_t i, k, cnt[NTRK] = {0}, drum = trk[t % NTRK].s.engine == E_DRUM;
    for (i = 0; i < NVOICE; i++) {
        v = &voice[i];
        if (v->active && v->trk == t && (drum ? v->lane == lane : v->note == note && !v->gate)) {
            alloc_retrig = 1;                   /* (1: taken over, not a steal; voice_start keeps its phases) */
            return v;
        }
    }
    alloc_retrig = 0;
    for (i = 0; i < NVOICE; i++)
        if (!voice[i].active)
            return &voice[i];
    for (i = 0; i < NVOICE; i++)
        cnt[voice[i].trk % NTRK]++;
    for (k = 3; k <= 6; k++) {
        if (k == 4 && cnt[t] >= vres(t))
            continue;
        for (i = 0; i < NVOICE; i++) {
            uint32_t vt;
            v = &voice[i];
            vt = v->trk % NTRK;
            if (k == 3 && (v->gate || (vt != t && cnt[vt] <= vres(vt))))
                continue;
            if (k == 4 && v->gate)
                continue;
            if (k == 5 && vt != t)
                continue;
            if (k == 6 && cnt[vt] <= vres(vt))
                continue;
            if (!best || (k == 5 ? v->age < best->age : v->amp < best->amp))
                best = v;
        }
        if (best)
            return best;
    }
    for (i = 0; i < NVOICE; i++)                /* (only when every track sits at its reserve: the quietest) */
        if (!best || voice[i].amp < best->amp)
            best = &voice[i];
    return best;
}
static voice_t *voice_alloc(uint32_t t, uint32_t note, uint32_t lane)
{
    voice_t *v = voice_alloc_(t, note, lane);
    if (!alloc_retrig)
        steal_count(t, v);
    return v;
}

static uint8_t dv_turn[NTRK];
/* a note on track t (drums: note = the lane mask, acc the accented lanes; the loop: note = the slice) */
static void note_on(uint32_t t, uint32_t note, uint32_t vel, uint32_t flags)
{
    track_t *tr = &trk[t];
    voice_t *v;
    uint32_t i;
    if (USES_DV(t) && tr->s.engine == E_SAMPLE) {   /* the kick's one-shot (any lane hit), the loop's slice `note` */
        uint32_t k = tr->s.smp % NSMP, sl = smp_slices(k), len = smp_dir[k].len, a, b;
        if (!smp_dir[k].used || !smp_base || (IS_DRUM(t) && !note))
            return;
        dv_turn[t] ^= 1u;                       /* (two voices taking turns: the one before fades out in 3 ms) */
        v = &dvoice[t][dv_turn[t]];
        dvoice[t][dv_turn[t] ^ 1u].stage = 3;
        voice_start(v, t, 60, vel, flags & 1u, 0, 0);
        v->amp = 32767;
        if (IS_LOOP(t)) {
            a = len / sl * (note % sl);
            b = a + len / sl;
            v->gate = 1;                        /* (the slice sounds to its end or the next one) */
        } else {
            a = len * tr->s.sstart / 128u;
            b = len;
            v->gate = 0;
        }
        v->ph[0] = a << 12;                     /* position Q12, end */
        v->ph[1] = b;
        return;
    }
    if (tr->s.engine == E_DRUM || tr->s.engine == E_LXR) {
        for (i = 0; i < NLANE; i++) {
            if (!((note >> i) & 1u))
                continue;
            if (i == LN_HATC && dvoice[t][LN_HATO].active)   /* the closed hat chokes the open one (a 3 ms fade) */
                dvoice[t][LN_HATO].stage = 3;
            v = &dvoice[t][i];                  /* the lane's voice: a hit again restarts it */
            voice_start(v, t, 60, vel, (flags >> i) & 1u, i, 0);
            v->gate = 0;                        /* one-shot */
            v->pa[3] = 32767;                   /* (the choke fade's level) */
            if (tr->s.engine == E_LXR) {        /* LXR: its envelopes from the top */
                v->pa[0] = 32767;
                v->pa[1] = 32767;
                v->pa[2] = 32767;
                v->pa[4] = 32767;
                v->pa[5] = 32767;
            }
        }
        return;
    }
    if (tr->s.mono) {                           /* one voice (and DUAL's): legato / slide glides */
        uint32_t found = 0;
        for (i = 0; i < NVOICE; i++) {
            v = &voice[i];
            if (v->active && v->trk == t && v->stage != 3u) {
                int32_t to = ((int32_t)note + note_shift(t)) * 16 + v->poff;
                found = 1;
                if ((flags & SF_SLIDE) || tr->s.glide) {
                    uint32_t g = (flags & SF_SLIDE) ? 24u : tr->s.glide;
                    v->pitch_to = to;
                    v->gstep = (to - v->pitch16) / (int32_t)(1u + g / 2u);
                    if (!v->gstep)
                        v->pitch16 = to;
                } else {
                    v->pitch16 = v->pitch_to = to;
                }
                v->note = (uint8_t)note;
                v->vel = (uint8_t)vel;
                v->acc = (uint8_t)(flags & SF_ACC);
                v->gate = 1;
                if (!(flags & SF_SLIDE)) {     /* a new note (not slid): the envelopes again */
                    v->fstage = 1;
                    v->stage = 1;
                }
            }
        }
        if (found)
            return;
    }
    v = voice_alloc(t, note, 0);
    voice_start(v, t, note, vel, flags & SF_ACC, 0, 0);
    if (tr->s.dual) {                           /* DUAL: the second voice on the same note */
        static const int16_t OFF[DU_N] = {0, 2, 192, 112, -192};
        voice_t *w = voice_alloc(t, note, 0);
        if (w != v)
            voice_start(w, t, note, vel * (tr->s.dmix ? tr->s.dmix : 100u) / 127u, flags & SF_ACC, 0,
                        OFF[tr->s.dual % DU_N]);
    }
}
static void note_off(uint32_t t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (voice[i].active && voice[i].trk == t && voice[i].note == note && voice[i].gate) {
            voice[i].gate = 0;
            voice[i].stage = 3;
            voice[i].fstage = 3;
        }
}
static void track_panic(uint32_t t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (voice[i].trk == t && voice[i].gate) {
            voice[i].gate = 0;
            voice[i].stage = 3;
            voice[i].fstage = 3;
        }
}

/* the filter envelope, once a block: Q24 (an ADSR of its own: fatt fdec fsus frel) */
static void fenv_block(voice_t *v, const patch_t *s, int32_t kdec)
{
    int32_t sus = (int32_t)s->fsus << 17;
    if (v->fstage == 1) {
        v->fenv += (int32_t)ATK_INC[s->fatt & 127];
        if (v->fenv >= 1 << 24) {
            v->fenv = 1 << 24;
            v->fstage = 2;
        }
    } else if (v->fstage == 2) {
        v->fenv = v->fenv > sus ? sus + mulq16(v->fenv - sus, ENV_K[clamp(s->fdec + kdec, 0, 127)]) : sus;
    } else {
        v->fenv = mulq16(v->fenv, ENV_K[s->frel & 127]);
    }
}

/* the amp envelope, once a block: Q24 */
static void env_block(voice_t *v, const patch_t *s, int32_t kdec)
{
    int32_t sus = (int32_t)s->sus << 17;
    if (v->stage == 1) {
        v->env += (int32_t)ATK_INC[s->atk & 127];
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
    } else if (v->stage == 2) {
        v->env = v->env > sus ? sus + mulq16(v->env - sus, ENV_K[clamp(s->dec + kdec, 0, 127)]) : sus;
    } else {
        v->env = mulq16(v->env, ENV_K[clamp(s->rel + kdec / 2, 0, 127)]);
        if (v->env < 1 << 12)
            v->active = 0;
    }
}

/* the filter: Chamberlin state variable, run twice a sample (stable up to 16 kHz). Q15 states, clamped */
#define SVF_LIM 60000
static inline int32_t svf_lp(voice_t *v, int32_t x, int32_t f, int32_t q)
{
    int32_t k, hp;
    for (k = 0; k < 2; k++) {
        v->lp = clamp(v->lp + ((f * v->bp) >> 15), -SVF_LIM, SVF_LIM);
        hp = x - v->lp - ((q * (v->bp >> 1)) >> 14);
        v->bp = clamp(v->bp + ((f * clamp(hp, -SVF_LIM, SVF_LIM)) >> 15), -SVF_LIM, SVF_LIM);
    }
    return v->lp;
}

/* --------------------------------------------------------------- engines --- */
/* LXR: six drum voices in the manner of the Sonic Potions LXR (its published voice architecture; the code is this
 * firmware's own, fixed point). Parameters 0..127 (patch lx[voice][k]):
 *   DRUM 1-3  PITCH DECAY P.ENV P.DECAY WAVE (sine tri saw square noise) FM CLICK DRIVE
 *   SNARE     PITCH DECAY NOISE N.DECAY TONE (the noise's filter) SNAP CLICK DRIVE
 *   CYMBAL    PITCH DECAY RATIO FM FILTER (high-pass) RESO NOISE DRIVE
 *   HIHAT     PITCH CLOSED OPEN FM FILTER NOISE CLICK DRIVE
 * State in the voice: pa[0] amp, pa[1] pitch env, pa[2] click, pa[3] choke, pa[4] noise env, pa[5] snare tone (Q15) */
static int32_t lx_k(uint32_t idx)                /* a per-sample decay factor (Q16 taken off) for TIME_MS[idx] to -60 dB */
{
    uint32_t ns = TIME_MS[idx & 127u] * 44u + 1u;
    return clamp((int32_t)(452000u / ns), 1, 65535);
}
/* one sample of an exponential decay, e (0..32767) by k (Q16 taken off), rounded up: at least 1 a sample, so it
 * reaches 0 (rounded down it stalled where e * k < 65536: a level held for ever) */
static inline int32_t lx_dec(int32_t e, int32_t k)
{
    int32_t d = (int32_t)(((uint32_t)e * (uint32_t)k + 65535u) >> 16);
    return e > d ? e - d : 0;
}
static inline int32_t lx_wave(uint32_t w, uint32_t ph, uint32_t *nz)
{
    switch (w) {
    case 1: { int32_t v = (int32_t)(ph >> 15); return v < 65536 ? v - 32768 : 98303 - v; }
    case 2: return (int32_t)(ph >> 16) - 32768;
    case 3: return ph & 0x80000000u ? -26000 : 26000;
    case 4: return (int32_t)(xrand(nz) >> 17) - 16384;
    default: return sine(ph);
    }
}
static void lxr_render(voice_t *v, const track_t *tr, int32_t *out, uint32_t n)
{
    const patch_t *s = &tr->s;
    uint32_t lane = v->lane & 7u, vx = LX_OF_LANE[lane], kind = LX_KIND[vx], i;
    const uint8_t *p = s->lx[vx];
    int32_t tune = clamp(s->dtune, -24, 24) * 16, gain = (v->vel + (v->acc ? 30 : 0)), drive = 16 + p[7] / 4;
    /* the decays as TIME_MS indices: drums 6 ms .. 3 s, hats 10 .. 150 ms closed / 50 ms .. 1.5 s open, cymbal 0.4 .. 6 s */
    int32_t ka = lx_k(kind == LK_HIHAT ? (lane == LN_HATO ? 55u + p[2] * 45u / 127u : 30u + p[1] * 40u / 127u)
                      : kind == LK_CYMBAL ? 80u + p[1] * 40u / 127u : 25u + p[1] * 85u / 127u);
    int32_t kp = lx_k(kind == LK_SNARE ? 25u : 15u + p[3] * 60u / 127u), kc = lx_k(12), kn = lx_k(30u + p[3] * 70u / 127u);
    if (kind == LK_SNARE && kn < ka)             /* (the snare: its body lasts as long as the longer of tone and noise; the
                                                  * tone's own decay then shapes the tone alone) */
        ka = kn;
    int32_t kt = lx_k(25u + p[1] * 85u / 127u);
    uint32_t inc, minc;
    int32_t f = 0, q = 50000;
    if (s->ddec && s->ddec != 64)                /* the kit's DECAY knob: all voices longer / shorter */
        ka = clamp(ka * 64 / s->ddec, 1, 65535);
    ka = clamp(ka + tr->k_dec * ka / 48, 1, 65535);
    switch (kind) {
    case LK_DRUM: inc = pitch_inc((24 + p[0] * 60 / 127) * 16 + tune); break;
    case LK_SNARE: inc = pitch_inc((43 + p[0] * 26 / 127) * 16 + tune); break;
    default: inc = pitch_inc((60 + p[0] * 40 / 127) * 16 + tune); break;
    }
    minc = kind == LK_CYMBAL ? (inc >> 6) * (64u + p[2]) : inc * 2u;   /* the modulator: x1 .. x3 (cymbal), x2 */
    if (kind == LK_SNARE) f = SVF_F[clamp(60 + p[4] * 3 / 2 + tr->k_cut, 0, 255)];
    if (kind >= LK_CYMBAL) {
        f = SVF_F[clamp(120 + p[4] + tr->k_cut, 0, 255)];
        q = 65534 - clamp((kind == LK_CYMBAL ? p[5] : 20) + tr->k_res, 0, 127) * 480;
    }
    for (i = 0; i < n; i++) {
        int32_t a = v->pa[0], y, nz = (int32_t)(xrand(&v->noise) >> 17) - 16384;
        uint32_t tt = v->t++;
        if (lane == LN_CLAP && tt < 1323u)       /* the clap: three bursts 10 ms apart, then the tail */
            a = (tt % 441u) < 160u ? 32767 : 6000;
        switch (kind) {
        case LK_DRUM: {
            uint32_t pi = inc + (inc >> 4) * (uint32_t)((v->pa[1] * p[2]) >> 15);
            int32_t m = p[5] ? (sine(v->ph[1]) * p[5]) >> 4 : 0;
            v->ph[1] += minc;
            v->ph[0] += pi;
            y = lx_wave(p[4] * 5u / 128u, v->ph[0] + ((uint32_t)m << 10), &v->noise);
            v->pa[1] = lx_dec(v->pa[1], kp);
            break;
        }
        case LK_SNARE: {
            int32_t tone, nn;
            v->ph[0] += inc + (inc >> 3) * (uint32_t)((v->pa[1] * p[5]) >> 15);
            tone = sine(v->ph[0]);
            nn = nz * 2 - svf_lp(v, nz * 2, f, 40000);   /* the noise above TONE, its band at TONE a little louder */
            nn = (nn + v->bp) >> 1;
            y = (((tone * v->pa[5]) >> 15) * (127 - p[2] / 2) >> 7) + (((nn * v->pa[4]) >> 15) * p[2] >> 6);
            v->pa[5] = lx_dec(v->pa[5], kt);
            if (lane == LN_CLAP)
                y = nn;
            v->pa[1] = lx_dec(v->pa[1], kp);
            v->pa[4] = lx_dec(v->pa[4], kn);
            break;
        }
        default: {                               /* metal: three squares, the second and third FM'd, high-passed */
            int32_t m = (int32_t)((v->ph[1] += minc) >> 31) * 2 - 1, x;
            uint32_t fm = p[3];
            v->ph[0] += inc + (uint32_t)(m * (int32_t)(fm << 17));
            v->ph[2] += (inc >> 6) * 87u + (uint32_t)(m * (int32_t)(fm << 16));
            x = ((int32_t)(v->ph[0] >> 31) + (int32_t)(v->ph[1] >> 31) + (int32_t)(v->ph[2] >> 31)) * 9000 - 13500;
            x += (nz * (kind == LK_CYMBAL ? p[6] : p[5])) >> 6;
            y = x - svf_lp(v, x, f, q);
            break;
        }
        }
        if (v->pa[2] > 64) {                     /* the click: a short noisy transient */
            int32_t c = kind == LK_CYMBAL ? 0 : p[6];
            y += (nz * ((v->pa[2] * c) >> 7)) >> 15;
            v->pa[2] = lx_dec(v->pa[2], kc);
        }
        if (p[7])
            y = sat((y * drive) >> 4);
        if (v->stage == 3) {                     /* choked (the closed hat over the open): ~3 ms */
            v->pa[3] -= 248;
            if (v->pa[3] <= 0) { v->active = 0; break; }
            a = (a * v->pa[3]) >> 15;
        }
        out[i] += (((y * a) >> 15) * gain) >> 7;
        v->pa[0] = lx_dec(v->pa[0], ka);
        if (v->pa[0] < 8 && tt > 1400u) {
            v->active = 0;
            break;
        }
    }
    v->amp = v->pa[0];
}


/* one voice, n samples, added to out (Q15 x VOICE_FS / 32768 scale); a0 -> a1 the amplitude ramp (Q15) */
static void render_voice(voice_t *v, int32_t *out, uint32_t n)
{
    track_t *tr = &trk[v->trk];
    const patch_t *s = &tr->s;
    uint32_t i, inc;
    int32_t a0 = v->amp, a1, da, f = 0, q = 0, c, x;

    if (s->engine == E_SAMPLE) {                 /* a sample from the store: mu-law, linearly interpolated, through the filter */
        const smp_t *d = &smp_dir[s->smp % NSMP];
        const uint8_t *p = smp_base ? smp_base + d->off : 0;
        uint32_t pos = v->ph[0], end = v->ph[1], step, e;
        int32_t y, fs, qs;
        if (!p || !d->used || end > d->len) {
            v->active = 0;
            return;
        }
        step = (uint32_t)d->rate * 4096u / FS;  /* native speed, Q12 */
        if (IS_DRUM(v->trk))                    /* the kick: TUNE in semitones (2^(1/12) ~ 1 + 1/17) */
            step = (uint32_t)((int32_t)step * (100 + clamp(s->dtune, -24, 24) * 6) / 100);
        fs = SVF_F[clamp(s->cutoff * 2 + tr->k_cut * 2, 0, 255)];
        qs = 65534 - clamp(s->res + tr->k_res, 0, 127) * 500;
        e = (uint32_t)clamp(v->amp, 0, 32767);   /* (3: fading out, ~3 ms) */
        for (i = 0; i < n; i++) {
            uint32_t idx = pos >> 12, fr = pos & 4095u;
            if (idx + 1u >= end) {
                v->active = 0;
                break;
            }
            y = ULAW[p[idx]];
            y += ((ULAW[p[idx + 1u]] - y) * (int32_t)fr) >> 12;
            if (s->cutoff < 127 || tr->k_cut < 0)
                y = svf_lp(v, y >> 1, fs, qs) << 1;
            out[i] += (((y * (int32_t)e) >> 15) * (v->vel + (v->acc ? 24 : 0))) >> 8;
            if (v->stage == 3u)
                e = (e * 31u) >> 5;
            pos += step;
        }
        v->ph[0] = pos;
        v->amp = (int32_t)e;
        if (v->stage == 3u && e < 64u)
            v->active = 0;
        return;
    }
    if (s->engine == E_LXR) {
        lxr_render(v, tr, out, n);
        return;
    }
    if (s->engine == E_DRUM) {                   /* drums keep their own envelopes */
        static const uint16_t DEC[8] = {9000, 4500, 5200, 1200, 9000, 7000, 900, 5000};   /* ms x 0.1 / ~1/e */
        static const uint16_t F0[8] = {52, 185, 1100, 7000, 6000, 98, 820, 560};
        uint32_t kit = s->kit & 3u, lane = v->lane & 7u,
                 tune = (100u + kit * 12u - 12u) * (uint32_t)(100 + clamp(s->dtune, -24, 24) * 3) / 100u;
        int32_t vel = (v->vel + (v->acc ? 30 : 0)) * 200;   /* Q15-ish */
        uint32_t dec = DEC[lane] * (8u + kit) / 8u * (32u + (s->ddec ? s->ddec : 64u) / 2u) / 64u + 1u, kd = (32767u << 16) / (dec * 4u + 1u);   /* (divisions: */
        uint32_t ik = pitch_inc(16 * 31) / 100u * tune, f0 = F0[lane] * tune / 100u * 97391u;   /* once a block) */
        for (i = 0; i < n; i++) {
            int32_t e, y = 0, nz = (int32_t)(xrand(&v->noise) >> 17) - 16384;
            uint32_t tt = v->t++;
            e = tt < 40u ? (int32_t)(tt + 1u) * 819 :
                tt - 40u > dec * 4u ? 0 : 32767 - (int32_t)(((tt - 40u) * kd) >> 16);
            if (e <= 0) {
                v->active = 0;
                break;
            }
            e = (e * e) >> 15;                   /* a curved decay */
            if (v->stage == 3) {                 /* choked: down in ~3 ms */
                v->pa[3] -= 248;
                if (v->pa[3] <= 0) {
                    v->active = 0;
                    break;
                }
                e = (e * v->pa[3]) >> 15;
            }
            switch (lane) {
            case LN_KICK: {
                int32_t sweep = 32767 - (int32_t)(tt < 6000u ? tt * 5u : 30000u);
                v->ph[0] += ik + (uint32_t)((sweep * sweep) >> 15) * 2600u;
                y = sine(v->ph[0]) + (tt < 60u ? nz : 0);
                break;
            }
            case LN_TOM:
                v->ph[0] += f0 + (tt < 3000u ? (3000u - tt) * 300u : 0u);
                y = sine(v->ph[0]);
                break;
            case LN_SNARE:
                v->ph[0] += f0;
                y = ((sine(v->ph[0]) * (tt < 2000u ? 2000 - (int32_t)tt : 0)) >> 12) +
                    ((svf_lp(v, nz * 2, 22000, 16000) + nz) >> 1);
                break;
            case LN_CLAP: {
                int32_t g = tt >= 1323u || tt < 200u || (tt >= 441u && tt < 641u) || (tt >= 882u && tt < 1082u)
                            ? 32767 : 4000;         /* three bursts 10 ms apart, then the tail */
                y = (svf_lp(v, nz * 2, 9000, 9000) * g) >> 15;
                break;
            }
            case LN_HATC:
            case LN_HATO: {
                int32_t m = ((int32_t)((v->ph[0] += 18700000u) >> 31) + (int32_t)((v->ph[1] += 26100000u) >> 31) +
                             (int32_t)((v->ph[2] += 33900000u) >> 31)) * 8000 - 12000;
                int32_t hx = (nz + m) >> 1;
                y = hx - svf_lp(v, hx, 26000, 20000);   /* high-passed */
                break;
            }
            case LN_RIM:
                v->ph[0] += 820u * 97391u;
                y = (int32_t)((v->ph[0] >> 15) & 0xFFFF) - 32768;
                y = (y + nz) >> 1;
                break;
            default:                             /* cowbell: two squares, band-limited a little */
                v->ph[0] += 560u * 97391u;
                v->ph[1] += 845u * 97391u;
                y = ((int32_t)(v->ph[0] >> 31) + (int32_t)(v->ph[1] >> 31)) * 16000 - 16000;
                y = svf_lp(v, y, 14000, 26000);
                break;
            }
            out[i] += (((y * e) >> 15) * (vel >> 3)) >> 12;
            v->amp = e;
        }
        return;
    }

    /* the control block: envelopes, glide, the filter */
    int32_t pkeep;
    env_block(v, s, tr->k_dec);
    if (v->gstep) {
        v->pitch16 += v->gstep;
        if ((v->gstep > 0 && v->pitch16 >= v->pitch_to) || (v->gstep < 0 && v->pitch16 <= v->pitch_to)) {
            v->pitch16 = v->pitch_to;
            v->gstep = 0;
        }
    }
    fenv_block(v, s, tr->k_dec);
    pkeep = v->pitch16;
    v->pitch16 += lfo_amt(tr, LD_PITCH, 48) + s->tune * 16 + s->fine * 16 / 100;   /* (vibrato, PITCH, FINE: back after the render) */
    c = s->cutoff * 2 + tr->k_cut * 2 + lfo_amt(tr, LD_CUT, 120) + ((s->fenv * (v->fenv >> 12)) >> 11) + (v->acc ? 30 : 0) +
        ((int32_t)s->keytrk * (v->pitch16 - 60 * 16) >> 9);
    f = SVF_F[clamp(c, 0, 255)];
    q = 65534 - clamp(s->res + tr->k_res, 0, 127) * 500 - (v->acc ? 3000 : 0);   /* res 127: q ~ 0.06 */
    a1 = ((v->env >> 9) * (v->vel + (v->acc ? 24 : 0))) / 151;
    a1 = clamp(a1, 0, 32767);
    if (s->lfo_dest == LD_AMP)                   /* tremolo: down from full, never up */
        a1 = (a1 * (32767 - ((32767 - tr->lfo_v) >> 1) * s->lfo_depth / 127)) >> 15;
    da = (a1 - a0) / (int32_t)n;
    inc = pitch_inc(v->pitch16);

    switch (s->engine) {
    case E_SUB: {
        uint32_t i2 = pitch_inc(v->pitch16 + s->detune + s->oct2 * 192), isub = inc >> 1;
        int32_t m2 = s->mix2 * 258, ms = s->sub * 200, mn = s->noise * 150;
        for (i = 0; i < n; i++) {
            x = osc(s->wave1, v->ph[0], inc) + ((osc(s->wave2, v->ph[1], i2) * m2) >> 15) +
                ((v->ph[2] & 0x80000000u ? -ms : ms)) + ((((int32_t)(xrand(&v->noise) >> 17) - 16384) * mn) >> 14);
            v->ph[0] += inc;
            v->ph[1] += i2;
            v->ph[2] += isub;
            x = svf_lp(v, x >> 1, f, q);
            out[i] += (x * (a0 >> 2)) >> 13;
            a0 += da;
        }
        break;
    }
    case E_ACID: {                               /* one oscillator, driven into a resonant 3-pole low-pass */
        int32_t fz = f > 30000 ? 30000 : f;
        for (i = 0; i < n; i++) {
            x = osc(s->wave1 == W_SQR ? W_SQR : W_SAW, v->ph[0], inc);
            v->ph[0] += inc;
            x = sat((svf_lp(v, x, f, q) * 3) >> 1);
            v->z1 += (fz * (x - v->z1)) >> 15;
            out[i] += (v->z1 * (a0 >> 2)) >> 13;
            a0 += da;
        }
        break;
    }
    case E_FM: {                                 /* three sine operators; op 1 carries */
        uint32_t i1 = inc_ratio(inc, s->fm_ratio[0]), i2 = inc_ratio(inc, s->fm_ratio[1]),
                 i3 = inc_ratio(inc, s->fm_ratio[2]);
        int32_t idx, fb = s->fm_fb * 40, m2, m3;
        v->ienv = mulq16(v->ienv << 8, ENV_K[s->fm_idec & 127]) >> 8;
        idx = (s->fm_index * 6 * (v->ienv + 3000)) >> 15;   /* ~0 .. 8 rad */
        for (i = 0; i < n; i++) {
            m3 = sine(v->ph[2] + (uint32_t)(((v->fb1 + v->fb2) >> 1) * fb));
            v->fb2 = v->fb1;
            v->fb1 = m3;
            if (s->fm_algo == 0) {               /* 3 -> 2 -> 1 */
                m2 = sine(v->ph[1] + ((uint32_t)(m3 * idx) << 8));
                x = sine(v->ph[0] + ((uint32_t)(m2 * idx) << 8));
            } else if (s->fm_algo == 1) {        /* 2 + 3 -> 1 */
                m2 = sine(v->ph[1]);
                x = sine(v->ph[0] + ((uint32_t)(((m2 + m3) >> 1) * idx) << 8));
            } else {                             /* 2 -> 1, 3 sounds too (a second carrier) */
                m2 = sine(v->ph[1]);
                x = ((sine(v->ph[0] + ((uint32_t)(m2 * idx) << 8)) + (m3 >> 1)) * 21845) >> 15;
            }
            v->ph[0] += i1;
            v->ph[1] += i2;
            v->ph[2] += i3;
            if (s->cutoff < 127)
                x = svf_lp(v, x, f, q);
            out[i] += (x * (a0 >> 2)) >> 13;
            a0 += da;
        }
        break;
    }
    case E_SAW: {                                /* detuned saws around the note */
        uint32_t k, ns = clamp(s->saw_n, 1, 7), si[7];
        int32_t sg = 3 * 16384 / (int32_t)(ns + 2u);  /* (no division in the sample loop) */
        for (k = 0; k < ns; k++)
            si[k] = pitch_inc(v->pitch16 + ((int32_t)k * 2 - (int32_t)ns + 1) * s->saw_spread / 8 + (k & 1u ? s->detune : 0));
        for (i = 0; i < n; i++) {
            x = 0;
            for (k = 0; k < ns; k++) {
                x += saw(v->ph[k], si[k]);
                v->ph[k] += si[k];
            }
            x = svf_lp(v, ((x >> 2) * sg) >> 12, f, q);
            out[i] += (x * (a0 >> 2)) >> 13;
            a0 += da;
        }
        break;
    }
    case E_NOISE: {                             /* noise through the filter: COLOR (wave1) LP / BP / HP, pitched by
                                                 * KEYTRK and RESO (high resonance: a whistle on the note) */
        uint32_t col = s->wave1 % 3u;
        int32_t k;
        for (i = 0; i < n; i++) {
            int32_t nz = (int32_t)(xrand(&v->noise) >> 17) - 16384, hp = 0, y;
            for (k = 0; k < 2; k++) {
                v->lp = clamp(v->lp + ((f * v->bp) >> 15), -SVF_LIM, SVF_LIM);
                hp = clamp(nz - v->lp - ((q * (v->bp >> 1)) >> 14), -SVF_LIM, SVF_LIM);
                v->bp = clamp(v->bp + ((f * hp) >> 15), -SVF_LIM, SVF_LIM);
            }
            y = col == 0u ? v->lp : col == 1u ? v->bp : hp;
            out[i] += (y * (a0 >> 2)) >> 13;
            a0 += da;
        }
        break;
    }
    case E_ADD: {                                /* sine partials, the higher ones dying sooner */
        uint32_t k, np = clamp(s->add_n, 1, ADD_N), pi[ADD_N];
        int32_t ag = 2 * 16384 / (int32_t)(np + 1u);
        for (k = 0; k < np; k++) {
            pi[k] = (inc >> 8) * s->add_ratio[k];
            if (pi[k] > 0x3F000000u)             /* (above ~10 kHz: silent) */
                v->pa[k] = 0;
            v->pa[k] = mulq16(v->pa[k] << 4, ENV_K[clamp(s->dec + 20 - (int32_t)(s->add_tilt * k) / 16, 0, 127)]) >> 4;
        }
        for (i = 0; i < n; i++) {
            x = 0;
            for (k = 0; k < np; k++) {
                x += (sine(v->ph[k]) * v->pa[k]) >> 15;
                v->ph[k] += pi[k];
            }
            x = (x * ag) >> 14;
            out[i] += (x * (a0 >> 2)) >> 13;
            a0 += da;
        }
        break;
    }
    }
    v->amp = a1;
    v->pitch16 = pkeep;
}

/* -------------------------------------------------------------- effects --- */
#define DLY_N 32768u                             /* 0.74 s */
#define RV_C 4
static int16_t dly_buf[DLY_N] __attribute__((section(".pool")));
static int16_t rv_comb[RV_C][1700] __attribute__((section(".pool")));
static int16_t rv_ap[4][560] __attribute__((section(".pool")));
static const uint16_t RV_CLEN[RV_C] = {1557, 1617, 1491, 1422};
static const uint16_t RV_APLEN[4] = {556, 441, 341, 225};
static uint32_t dly_w;
static uint16_t rv_ci[RV_C], rv_ai[4];
static int32_t rv_lp[RV_C], dly_lp;

static inline int32_t s16(int32_t x) { return clamp(x, -32767, 32767); }
static void fx_block(int32_t *L, int32_t *R, const int32_t *dsend, const int32_t *rsend, uint32_t n)
{
    static const uint8_t DIV16[6] = {2, 3, 4, 6, 8, 12};   /* the delay in 16ths: 1/8 3/16 1/4 3/8 1/2 3/4 */
    uint32_t i, k, dt = (uint32_t)(FS * 15u * DIV16[song.dly_div % 6u] / (song.bpm ? song.bpm : 120u));
    int32_t dfb = song.dly_fb * 27 / 127, rfb = 22 + song.rev_size * 9 / 127, rd = 1 + song.rev_damp / 43;
    if (dt >= DLY_N)
        dt = DLY_N - 1u;
    for (i = 0; i < n; i++) {
        int32_t d = dly_buf[(dly_w - dt) & (DLY_N - 1u)], r = 0, a, in = rsend[i] >> 2, ap;
        dly_lp += (d - dly_lp) >> 2;             /* darker repeats */
        dly_buf[dly_w++ & (DLY_N - 1u)] = (int16_t)s16(dsend[i] + ((dly_lp * dfb) >> 5));   /* FEEDBACK up to 0.85 */
        for (k = 0; k < RV_C; k++) {             /* parallel combs with damping */
            int16_t *b = rv_comb[k];
            int32_t y = b[rv_ci[k]];
            rv_lp[k] += (y - rv_lp[k]) >> rd;    /* DAMP: darker tails */
            b[rv_ci[k]] = (int16_t)s16(in + ((rv_lp[k] * rfb) >> 5));   /* SIZE: 0.69 .. 0.97 */
            if (++rv_ci[k] >= RV_CLEN[k])
                rv_ci[k] = 0;
            r += y;
        }
        r >>= 1;
        for (k = 0; k < 2u; k++) {               /* left: allpasses 0 1, right: 2 3 */
            int16_t *b = rv_ap[k];
            a = b[rv_ai[k]];
            ap = r - (a >> 1);
            b[rv_ai[k]] = (int16_t)s16(ap);
            r = a + (ap >> 1);
            if (++rv_ai[k] >= RV_APLEN[k])
                rv_ai[k] = 0;
        }
        L[i] += r + d;
        {
            int32_t rr = r;
            for (k = 2; k < 4u; k++) {
                int16_t *b = rv_ap[k];
                a = b[rv_ai[k]];
                ap = rr - (a >> 1);
                b[rv_ai[k]] = (int16_t)s16(ap);
                rr = a + (ap >> 1);
                if (++rv_ai[k] >= RV_APLEN[k])
                    rv_ai[k] = 0;
            }
            R[i] += rr + ((d * 7) >> 3);
        }
    }
}

/* ------------------------------------------------------------------ mix --- */
static void seq_block(uint32_t n);               /* oc_seq.c: the steps of this block */
static int32_t scope[64];
/* n samples (n <= CTL) of the master into out: stereo interleaved, Q15 */
/* the tracks' LFOs, once a block: lfo_v Q15 (-32767..32767) */
static void lfo_block(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        track_t *tr = &trk[t];
        const patch_t *s = &tr->s;
        uint32_t was = tr->lfo_ph, ph;
        int32_t v;
        if (s->lfo_dest == LD_OFF || !s->lfo_depth) {
            tr->lfo_v = 0;
            continue;
        }
        ph = tr->lfo_ph += LFO_INC[s->lfo_rate & 127];
        switch (s->lfo_wave) {
        case LW_TRI: v = (int32_t)(ph >> 15); v = v < 65536 ? v - 32768 : 98303 - v; break;
        case LW_SAW: v = (int32_t)(ph >> 16) - 32768; break;
        case LW_SQR: v = ph & 0x80000000u ? -32767 : 32767; break;
        case LW_SH:
            if (ph < was)                        /* a new cycle: a new value */
                tr->lfo_sh = (int32_t)(rng() >> 17) - 16384;
            v = tr->lfo_sh * 2;
            break;
        default: v = sine(ph); break;
        }
        tr->lfo_v = clamp(v, -32767, 32767);
    }
}
/* the LFO's amount on track t for dest d, scaled to +-range at full depth */
static inline int32_t lfo_amt(const track_t *tr, uint32_t d, int32_t range)
{
    return tr->s.lfo_dest == d ? ((tr->lfo_v >> 7) * tr->s.lfo_depth >> 7) * range >> 8 : 0;
}

static void mix_block(int32_t *out, uint32_t n)
{
    int32_t tb[NTRK][CTL], L[CTL], R[CTL], ds[CTL], rs[CTL];
    uint32_t i, t, m = song.master_q12;
    seq_block(n);
    lfo_block();
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < n; i++)
            tb[t][i] = 0;
    for (i = 0; i < NVOICE; i++)
        if (voice[i].active)
            render_voice(&voice[i], tb[voice[i].trk % NTRK], n);
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < NLANE; i++)
            if (dvoice[t][i].active)
                render_voice(&dvoice[t][i], tb[t], n);
    for (i = 0; i < n; i++)
        L[i] = R[i] = ds[i] = rs[i] = 0;
    for (t = 0; t < NTRK; t++) {
        const track_t *tr = &trk[t];
        int32_t g = tr->mute ? 0 : (tr->s.level * tr->k_lvl) >> 3;   /* 0..254, 127 at the default knob */
        int32_t pan = clamp(tr->s.pan + lfo_amt(tr, LD_PAN, 64), -64, 64), pl = 64 - pan, pr = 64 + pan;   /* 0..128 */
        int32_t dg = tr->s.dly, rg = tr->s.rev;
        for (i = 0; i < n; i++) {
            int32_t x = (tb[t][i] >> 2) * g >> 4;   /* g 127: x 2 */
            L[i] += (x * pl) >> 7;
            R[i] += (x * pr) >> 7;
            ds[i] += (x * dg) >> 8;
            rs[i] += (x * rg) >> 7;
        }
    }
    fx_block(L, R, ds, rs, n);
    if (song.playing && (song.click == CLICK_ON || (song.click == CLICK_REC && song.rec))) {   /* the metronome */
        uint32_t beat = song.step % 4u == 0u, bar = song.step % 16u == 0u, p0 = song.pos >= n ? song.pos - n : 0u;
        for (i = 0; beat && i < n; i++) {
            uint32_t p = p0 + i;
            if (p < 1800u) {
                int32_t c = (sine(p * (bar ? 146100u : 97400u)) * (int32_t)(1800u - p) / 1800) >> 2;
                L[i] += c;
                R[i] += c;
            }
        }
    }
    for (i = 0; i < n; i++) {                    /* DC blocker (~7 Hz) on both sides */
        static int32_t dcl, dcr;
        dcl += (L[i] * 1024 - dcl) >> 10;
        dcr += (R[i] * 1024 - dcr) >> 10;
        L[i] -= dcl >> 10;
        R[i] -= dcr >> 10;
    }
    for (i = 0; i < n; i++) {
        out[2u * i] = sat(((L[i] >> 2) * (int32_t)m) >> 9) >> 1;
        out[2u * i + 1u] = sat(((R[i] >> 2) * (int32_t)m) >> 9) >> 1;
    }
    scope[(song.pos >> 5) & 63u] = out[0];
}

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

/* ----------------------------------------------------------------- voices --- */
static uint32_t v_age;
static void voice_start(voice_t *v, uint32_t t, uint32_t note, uint32_t vel, uint32_t acc, uint32_t lane)
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
    v->pitch16 = v->pitch_to = (int32_t)note * 16;
    v->gstep = 0;
    v->env = keep ? v->env : 0;
    v->fenv = 1 << 24;
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

static voice_t *voice_alloc(uint32_t t)
{
    voice_t *best = 0;
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (!voice[i].active)
            return &voice[i];
    for (i = 0; i < NVOICE; i++)                /* a released one, the quietest */
        if (!voice[i].gate && (!best || voice[i].env < best->env))
            best = &voice[i];
    if (best)
        return best;
    for (i = 0; i < NVOICE; i++)                /* the oldest, of this track if it has one */
        if (!best || (voice[i].trk == t && best->trk != t) ||
            ((voice[i].trk == t) == (best->trk == t) && voice[i].age < best->age))
            best = &voice[i];
    return best;
}

/* a note on track t (drums: note = the lane mask, acc the accented lanes) */
static void note_on(uint32_t t, uint32_t note, uint32_t vel, uint32_t flags)
{
    track_t *tr = &trk[t];
    voice_t *v;
    uint32_t i;
    if (tr->s.engine == E_DRUM) {
        for (i = 0; i < NLANE; i++) {
            uint32_t j;
            if (!((note >> i) & 1u))
                continue;
            if (i == LN_HATC)                   /* the closed hat chokes the open one */
                for (j = 0; j < NVOICE; j++)
                    if (voice[j].active && voice[j].trk == t && voice[j].lane == LN_HATO)
                        voice[j].active = 0;
            v = voice_alloc(t);
            voice_start(v, t, 60, vel, (flags >> i) & 1u, i);
            v->gate = 0;                        /* one-shot */
        }
        return;
    }
    if (tr->s.mono) {                           /* one voice: legato / slide glides */
        for (i = 0; i < NVOICE; i++) {
            v = &voice[i];
            if (v->active && v->trk == t && v->stage != 3u) {
                int32_t to = (int32_t)note * 16;
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
                if (!(flags & SF_SLIDE)) {     /* a new note (not slid): the filter envelope again */
                    v->fenv = 1 << 24;
                    v->stage = 1;
                }
                return;
            }
        }
    }
    v = voice_alloc(t);
    voice_start(v, t, note, vel, flags & SF_ACC, 0);
}
static void note_off(uint32_t t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (voice[i].active && voice[i].trk == t && voice[i].note == note && voice[i].gate) {
            voice[i].gate = 0;
            voice[i].stage = 3;
        }
}
static void track_panic(uint32_t t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (voice[i].trk == t && voice[i].gate) {
            voice[i].gate = 0;
            voice[i].stage = 3;
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
/* one voice, n samples, added to out (Q15 x VOICE_FS / 32768 scale); a0 -> a1 the amplitude ramp (Q15) */
static void render_voice(voice_t *v, int32_t *out, uint32_t n)
{
    track_t *tr = &trk[v->trk];
    const patch_t *s = &tr->s;
    uint32_t i, inc;
    int32_t a0 = v->amp, a1, da, f = 0, q = 0, c, x;

    if (s->engine == E_DRUM) {                   /* drums keep their own envelopes */
        static const uint16_t DEC[8] = {9000, 4500, 5200, 1200, 9000, 7000, 900, 5000};   /* ms x 0.1 / ~1/e */
        static const uint16_t F0[8] = {52, 185, 1100, 7000, 6000, 98, 820, 560};
        uint32_t kit = s->kit & 3u, lane = v->lane & 7u, tune = 100u + kit * 12u - 12u;
        int32_t vel = (v->vel + (v->acc ? 30 : 0)) * 200;   /* Q15-ish */
        uint32_t dec = DEC[lane] * (8u + kit) / 8u + 1u, kd = (32767u << 16) / (dec * 4u + 1u);   /* (divisions: */
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
        }
        return;
    }

    /* the control block: envelopes, glide, the filter */
    env_block(v, s, tr->k_dec);
    if (v->gstep) {
        v->pitch16 += v->gstep;
        if ((v->gstep > 0 && v->pitch16 >= v->pitch_to) || (v->gstep < 0 && v->pitch16 <= v->pitch_to)) {
            v->pitch16 = v->pitch_to;
            v->gstep = 0;
        }
    }
    v->fenv = mulq16(v->fenv, ENV_K[clamp(s->fdec + tr->k_dec, 0, 127)]);
    c = s->cutoff * 2 + tr->k_cut * 2 + ((s->fenv * (v->fenv >> 12)) >> 11) + (v->acc ? 30 : 0) +
        ((int32_t)s->keytrk * (v->pitch16 - 60 * 16) >> 9);
    f = SVF_F[clamp(c, 0, 255)];
    q = 65534 - clamp(s->res + tr->k_res, 0, 127) * 500 - (v->acc ? 3000 : 0);   /* res 127: q ~ 0.06 */
    a1 = ((v->env >> 9) * (v->vel + (v->acc ? 24 : 0))) / 151;
    a1 = clamp(a1, 0, 32767);
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
    uint32_t i, k, dt = (uint32_t)(FS * 60u * 3u / (4u * (song.bpm ? song.bpm : 120u)));   /* a dotted 8th */
    if (dt >= DLY_N)
        dt = DLY_N - 1u;
    for (i = 0; i < n; i++) {
        int32_t d = dly_buf[(dly_w - dt) & (DLY_N - 1u)], r = 0, a, in = rsend[i] >> 2, ap;
        dly_lp += (d - dly_lp) >> 2;             /* darker repeats */
        dly_buf[dly_w++ & (DLY_N - 1u)] = (int16_t)s16(dsend[i] + ((dly_lp * 13) >> 5));
        for (k = 0; k < RV_C; k++) {             /* parallel combs with damping */
            int16_t *b = rv_comb[k];
            int32_t y = b[rv_ci[k]];
            rv_lp[k] += (y - rv_lp[k]) >> 1;
            b[rv_ci[k]] = (int16_t)s16(in + ((rv_lp[k] * 27) >> 5));
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
static void mix_block(int32_t *out, uint32_t n)
{
    int32_t tb[NTRK][CTL], L[CTL], R[CTL], ds[CTL], rs[CTL];
    uint32_t i, t, m = song.master_q12;
    seq_block(n);
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < n; i++)
            tb[t][i] = 0;
    for (i = 0; i < NVOICE; i++)
        if (voice[i].active)
            render_voice(&voice[i], tb[voice[i].trk % NTRK], n);
    for (i = 0; i < n; i++)
        L[i] = R[i] = ds[i] = rs[i] = 0;
    for (t = 0; t < NTRK; t++) {
        const track_t *tr = &trk[t];
        int32_t g = tr->mute ? 0 : (tr->s.level * tr->k_lvl) >> 3;   /* 0..254, 127 at the default knob */
        int32_t pl = 64 - tr->s.pan, pr = 64 + tr->s.pan;   /* 0..128 */
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

/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: the sound. Eight voices, one a track, each two layers as on the Sonic Potions LXR:
 *   the synth layer   in the manner of the LXR's voices (oc_synth.c's LXR engine, here a voice per track): DRUM (an
 *                     oscillator with pitch envelope, FM, click, drive), SNARE (tone + noise), CLAP (noise bursts),
 *                     HAT (three FM'd squares through a high-pass, closed / open) and CYMBAL;
 *   the sample layer  the LXR's transient samples (lxr_transients.h, its own licence), played from the start of each hit
 *                     at PITCH (0.25 .. 1.75 as on the LXR), mixed in before the amp envelope as the LXR does; or SNAP,
 *                     the LXR's pitch snap of the synth layer.
 * Then the track mix (level, pan, delay and reverb sends), delay, reverb, the metronome, the master.
 * Fixed point: audio Q15, phases 2^32 a cycle; rendered in blocks of CTL samples. */

/* ---------------------------------------------------------------- helpers --- */
static inline int32_t sine(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[i + 1u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline int32_t mulq16(int32_t a, uint32_t k)   /* a * k / 65536, a >= 0 (no 64-bit multiply on the target) */
{
    return (int32_t)(((uint32_t)(a >> 16) * k) + (((uint32_t)(a & 0xFFFF) * k) >> 16));
}
static inline int32_t sat(int32_t x)         /* soft clip: tanh over +-2 (Q15 in and out) */
{
    int32_t i, f;
    x = clamp(x, -65535, 65535);
    i = (x + 65536) >> 9;
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
static uint32_t pitch_inc(int32_t p16)        /* the phase increment of a pitch in 1/16 semitones */
{
    p16 = clamp(p16, 0, 127 * 16);
    return (uint32_t)mulq16((int32_t)NOTE_INC[p16 >> 4], FINE16[p16 & 15]);
}
static int32_t lx_k(uint32_t idx)             /* a per-sample decay factor (Q16 taken off) for TIME_MS[idx] to -60 dB */
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
#define SVF_LIM 60000
static inline int32_t svf_lp(voice_t *v, int32_t x, int32_t f, int32_t q)   /* (v->bp: the band) */
{
    int32_t k, hp;
    for (k = 0; k < 2; k++) {
        v->lp = clamp(v->lp + ((f * v->bp) >> 15), -SVF_LIM, SVF_LIM);
        hp = x - v->lp - ((q * (v->bp >> 1)) >> 14);
        v->bp = clamp(v->bp + ((f * clamp(hp, -SVF_LIM, SVF_LIM)) >> 15), -SVF_LIM, SVF_LIM);
    }
    return v->lp;
}

/* ------------------------------------------------------------------ LFOs --- */
/* two LFOs a sound, in time with the tempo (their cycle LR_*: 1/16 .. 8 bars, from the bar PLAY began), bipolar depth */
static uint32_t lfo_clk;                         /* samples since PLAY (stopped: free running) */
static uint32_t lfo_was[NTRK][2];
static int32_t lfo_v[NTRK][2], lfo_sh[NTRK][2];
static uint32_t step_base(void) { return (uint32_t)FS * 15u / (song.bpm ? song.bpm : 120u); }
static void lfo_block(uint32_t n)                /* once a block: lfo_v Q15 */
{
    uint32_t t, k, base = step_base();
    lfo_clk += n;
    for (t = 0; t < NTRK; t++)
        for (k = 0; k < 2u; k++) {
            const lfo_t *l = &snd[t].lfo[k];
            uint32_t per, ph;
            int32_t v;
            if (l->dest == LD_OFF || !l->depth) {
                lfo_v[t][k] = 0;
                continue;
            }
            per = base * LR_STEPS[l->rate % LR_N];
            ph = ((lfo_clk % per) / ((per >> 16) + 1u)) << 16;   /* (the phase, 2^32 a cycle, no 64-bit divide) */
            switch (l->wave) {
            case LW_TRI: v = (int32_t)(ph >> 15); v = v < 65536 ? v - 32768 : 98303 - v; break;
            case LW_SAW: v = (int32_t)(ph >> 16) - 32768; break;
            case LW_SQR: v = ph & 0x80000000u ? -32767 : 32767; break;
            case LW_SH:
                if (ph < lfo_was[t][k])          /* a new cycle: a new value */
                    lfo_sh[t][k] = (int32_t)(rng() >> 17) - 16384;
                v = lfo_sh[t][k] * 2;
                break;
            default: v = sine(ph); break;
            }
            lfo_was[t][k] = ph;
            lfo_v[t][k] = clamp(v, -32767, 32767);
        }
}
static inline int32_t lfo_amt(uint32_t t, uint32_t d, int32_t range)   /* both LFOs on dest d: +-range at full depth */
{
    int32_t a = 0, k;
    for (k = 0; k < 2; k++)
        if (snd[t].lfo[k].dest == d)
            a += ((lfo_v[t][k] >> 7) * snd[t].lfo[k].depth >> 6) * range >> 8;
    return a;
}

/* ----------------------------------------------------------------- voices --- */
/* the synth layer's parameters by kind (the knobs of EDIT: 1..4, with SHIFT 5..8) */
static const char *const LX_LABEL[K_N][8] = {
    {"PITCH", "DECAY", "P.ENV", "P.DEC", "WAVE", "FM", "CLICK", "DRIVE"},      /* DRUM */
    {"PITCH", "DECAY", "NOISE", "N.DEC", "TONE", "SNAP", "CLICK", "DRIVE"},    /* SNARE */
    {"TONE", "DECAY", "SPREAD", "BURSTS", "COLOR", "RESO", "CLICK", "DRIVE"},  /* CLAP */
    {"PITCH", "DECAY", "FM", "FILTER", "NOISE", "CLICK", "RESO", "DRIVE"},     /* CL HAT */
    {"PITCH", "DECAY", "FM", "FILTER", "NOISE", "CLICK", "RESO", "DRIVE"},     /* OP HAT */
    {"PITCH", "DECAY", "RATIO", "FM", "FILTER", "RESO", "NOISE", "DRIVE"},     /* CYMBAL */
};

/* a hit on track t (the audio ISR: the sequencer, the pads, MIDI) */
static void voice_hit(uint32_t t, uint32_t vel, uint32_t acc)
{
    voice_t *v = &voice[t % NTRK];
    const sound_t *s = &snd[t % NTRK];
    uint32_t i;
    if (TRK_KIND[t % NTRK] == K_HATC)           /* the closed hat chokes the open one (a 3 ms fade) */
        for (i = 0; i < NTRK; i++)
            if (TRK_KIND[i] == K_HATO && voice[i].active)
                voice[i].choke = 1;
    if (!v->active) {
        v->lp = v->bp = 0;
        for (i = 0; i < 3u; i++)
            v->ph[i] = i * 0x2E8BA2E9u;
    }
    v->vel = (uint8_t)clamp((int32_t)vel, 1, 127);
    v->acc = (uint8_t)(acc != 0);
    v->choke = 0;
    v->t = 0;
    v->amp = v->penv = v->click = v->cfade = v->nenv = v->tone = v->snap = v->tenv = 32767;
    /* the sample layer from START (its phase in 44.1 kHz samples, Q20: a 22.05 kHz machine sample spans twice its length) */
    v->tph = (((s->smp >= TR_MACH0 ? 2u * MACH_TRANS_LEN : LXR_TRANS_LEN) * s->sstart) >> 7) << 20;
    v->tinc = (1u << 18) + s->spitch * ((3u << 19) / 127u);   /* Q20: 0.25 .. 1.75 */
    v->tlp = 0;
    v->benv = 32767;
    v->noise ^= (t + 1u) * 2654435761u + fm1_ms;
    v->active = 1;
}

static void lx_render(uint32_t t, voice_t *v, int32_t *out, uint32_t n)
{
    const sound_t *s = &snd[t];
    const uint8_t *p = s->p;
    uint32_t kind = TRK_KIND[t], i, inc = 0, minc = 0, fm = 0, spacing = 441, bursts = 3;
    int32_t gain = v->vel + (v->acc ? 30 : 0), drive = 16 + p[7] / 4, syn = s->syn;
    int32_t tune = clamp(song.tune, -24, 24) * 16 + lfo_amt(t, LD_PITCH, 12 * 16), lcut = lfo_amt(t, LD_FILTER, 64);
    int32_t ka, kp = lx_k(15), kc = lx_k(12), kn = 0, kt = 0, ks, kd, f = 0, q = 50000, click = 0, noise = 0, color = 0;
    int32_t slvl = clamp(s->slvl + lfo_amt(t, LD_SAMPLE, 127), 0, 127);
    const int8_t *tr = s->smp >= TR_MACH0 ? MACH_TRANS[t][(s->smp - TR_MACH0) % MACH_TRANS_N]   /* the sample layer's */
                     : s->smp >= TR_OWN0 ? OWN_TRANS[t][(s->smp - TR_OWN0) % OWN_TRANS_N]
                     : s->smp >= TR_SMP0 ? LXR_TRANS[(s->smp - TR_SMP0) % LXR_TRANS_N] : 0;
    uint32_t mach = s->smp >= TR_MACH0, tn = mach ? MACH_TRANS_LEN : LXR_TRANS_LEN, tend = (mach ? 2u * tn : tn) << 20;
    int32_t tk = s->stone < 0 ? 4095 + s->stone * 60 : s->stone * 40, kb = lx_k(18);   /* TONE (Q12), BEND's envelope */
    uint32_t didx;
    switch (kind) {
    case K_DRUM:
        didx = 25u + p[1] * 85u / 127u;
        kp = lx_k(15u + p[3] * 60u / 127u);
        inc = pitch_inc((24 + p[0] * 60 / 127) * 16 + tune);
        minc = inc * 2u;
        click = p[6];
        break;
    case K_SNARE:
        didx = 25u + p[1] * 85u / 127u;
        kp = lx_k(25);
        kn = lx_k(30u + p[3] * 70u / 127u);
        kt = lx_k(didx);
        inc = pitch_inc((43 + p[0] * 26 / 127) * 16 + tune);
        f = SVF_F[clamp(60 + p[4] * 3 / 2 + lcut, 0, 255)];
        click = p[6];
        break;
    case K_CLAP:
        didx = 30u + p[1] * 80u / 127u;
        f = SVF_F[clamp(80 + p[0] * 4 / 3 + lcut, 0, 255)];
        q = 65534 - p[5] * 400;
        spacing = 220u + p[2] * 660u / 127u;     /* 5 .. 20 ms apart */
        bursts = 2u + p[3] * 3u / 127u;          /* 2 .. 5 */
        color = p[4];
        click = p[6];
        break;
    case K_HATC:
    case K_HATO:
        didx = kind == K_HATC ? 30u + p[1] * 40u / 127u : 55u + p[1] * 45u / 127u;
        inc = pitch_inc((60 + p[0] * 40 / 127) * 16 + tune);
        minc = inc * 2u;
        fm = p[2];
        f = SVF_F[clamp(120 + p[3] + lcut, 0, 255)];
        noise = p[4];
        click = p[5];
        q = 65534 - p[6] * 480;
        break;
    default:                                     /* K_CYM */
        didx = 80u + p[1] * 40u / 127u;
        inc = pitch_inc((60 + p[0] * 40 / 127) * 16 + tune);
        minc = (inc >> 6) * (64u + p[2]);
        fm = p[3];
        f = SVF_F[clamp(120 + p[4] + lcut, 0, 255)];
        q = 65534 - p[5] * 480;
        noise = p[6];
        break;
    }
    ka = lx_k((uint32_t)clamp((int32_t)didx + lfo_amt(t, LD_DECAY, 30), 0, 127));
    if (kind == K_SNARE && kn < ka)              /* (the snare's body lasts as long as the longer of tone and noise) */
        ka = kn;
    if (song.ddec && song.ddec != 64)            /* the kit's DECAY: all sounds longer / shorter */
        ka = clamp(ka * 64 / song.ddec, 1, 65535);
    ks = lx_k(4u + (127u - s->spitch) * 20u / 127u);   /* SNAP: faster at a higher PITCH */
    kd = lx_k(20u + s->sdec * 50u / 127u);       /* the sample layer's DECAY (127: the whole sample) */
    for (i = 0; i < n; i++) {
        int32_t a = v->amp, y = 0, nz = (int32_t)(xrand(&v->noise) >> 17) - 16384, layer = 0;
        uint32_t tt = v->t++, snap = 0;
        if (s->smp == TR_SNAP && slvl)            /* SNAP: up to 4x the pitch at the hit, gone in a few ms */
            snap = (uint32_t)((v->snap * slvl) >> 15);
        switch (kind) {
        case K_DRUM: {
            uint32_t pi = inc + (inc >> 4) * (uint32_t)((v->penv * p[2]) >> 15) + (inc >> 5) * snap;
            int32_t m = p[5] ? (sine(v->ph[1]) * p[5]) >> 4 : 0;
            v->ph[1] += minc;
            v->ph[0] += pi;
            y = lx_wave(p[4] * 5u / 128u, v->ph[0] + ((uint32_t)m << 10), &v->noise);
            v->penv = lx_dec(v->penv, kp);
            break;
        }
        case K_SNARE: {
            int32_t tone, nn;
            v->ph[0] += inc + (inc >> 3) * (uint32_t)((v->penv * p[5]) >> 15) + (inc >> 5) * snap;
            tone = sine(v->ph[0]);
            nn = nz * 2 - svf_lp(v, nz * 2, f, 40000);   /* the noise above TONE, its band at TONE a little louder */
            nn = (nn + v->bp) >> 1;
            y = (((tone * v->tone) >> 15) * (127 - p[2] / 2) >> 7) + (((nn * v->nenv) >> 15) * p[2] >> 6);
            v->tone = lx_dec(v->tone, kt);
            v->penv = lx_dec(v->penv, kp);
            v->nenv = lx_dec(v->nenv, kn);
            break;
        }
        case K_CLAP: {
            int32_t lp = svf_lp(v, nz * 2, f, q), hp = nz * 2 - lp;
            y = (v->bp * (127 - color) + hp * color) >> 7;
            if (tt < spacing * bursts)           /* the bursts, then the tail */
                a = (tt % spacing) < 160u ? 32767 : 6000;
            break;
        }
        default: {                               /* metal: three squares, the second and third FM'd, high-passed */
            int32_t m = (int32_t)((v->ph[1] += minc) >> 31) * 2 - 1, x;
            v->ph[0] += inc + (inc >> 5) * snap + (uint32_t)(m * (int32_t)(fm << 17));
            v->ph[2] += (inc >> 6) * 87u + (uint32_t)(m * (int32_t)(fm << 16));
            x = ((int32_t)(v->ph[0] >> 31) + (int32_t)(v->ph[1] >> 31) + (int32_t)(v->ph[2] >> 31)) * 9000 - 13500;
            x += (nz * noise) >> 6;
            y = x - svf_lp(v, x, f, q);
            break;
        }
        }
        if (v->click > 64) {                     /* the click: a short noisy transient */
            y += (nz * ((v->click * click) >> 7)) >> 15;
            v->click = lx_dec(v->click, kc);
        }
        if (snap)
            v->snap = lx_dec(v->snap, ks);
        y = (y * syn) >> 7;
        if (tr && v->tph < tend) {               /* the sample layer */
            uint32_t k = mach ? v->tph >> 21 : v->tph >> 20, inc = v->tinc;
            int32_t x;
            if (mach) {                          /* (22.05 kHz: between two samples, linearly) */
                int32_t a0 = tr[k], a1 = k + 1u < tn ? tr[k + 1u] : 0;
                x = a0 * 256 + (a1 - a0) * (int32_t)((v->tph >> 13) & 0xFFu);
            } else {
                x = tr[k] * 256;
            }
            if (k > tn - (mach ? 128u : 256u))   /* (the samples end abruptly: a 6 ms fade) */
                x = x * (int32_t)(tn - k) >> (mach ? 7 : 8);
            if (s->stone) {                      /* TONE: a one-pole low-pass (-) or high-pass (+) */
                v->tlp += ((x - v->tlp) * tk) >> 12;
                x = s->stone < 0 ? v->tlp : x - v->tlp;
            }
            layer = (((x * v->tenv) >> 15) * slvl) >> 6;   /* (twice the LXR's: heard over a full synth layer) */
            if (s->sdec < 127u)
                v->tenv = lx_dec(v->tenv, kd);
            if (s->sbend) {                      /* BEND: from up to 2x the pitch down (+), from far below up (-) */
                int32_t f = (s->sbend * (v->benv >> 7)) >> 8;
                inc = (uint32_t)clamp((int32_t)v->tinc + (int32_t)(v->tinc >> 6) * f, (int32_t)(v->tinc >> 2), 0x7FFFFFFF);
                v->benv = lx_dec(v->benv, kb);
            }
            v->tph += inc;
            y += layer;
        }
        if (p[7])
            y = sat((y * drive) >> 4);
        else
            y = clamp(y, -65535, 65535);         /* (synth + sample: y * a below stays in 32 bits) */
        if (v->choke) {                          /* choked (the closed hat over the open): ~3 ms */
            v->cfade -= 248;
            if (v->cfade <= 0) { v->active = 0; break; }
            a = (a * v->cfade) >> 15;
        }
        out[i] += (((y * a) >> 15) * gain) >> 7;
        v->amp = lx_dec(v->amp, ka);
        if (v->amp < 8 && tt > 1400u && (!tr || v->tph >= tend || !slvl)) {
            v->active = 0;
            break;
        }
    }
}

/* the sounds at power-on (preset 0) and the ranges PRESETS rolls the others in (lo, hi per parameter) */
static const uint8_t LX_FACTORY[NTRK][8] = {
    {14, 80, 80, 34, 0, 10, 50, 20},             /* KICK */
    {54, 44, 80, 58, 76, 50, 40, 10},            /* SNARE */
    {60, 56, 40, 60, 30, 30, 0, 10},             /* CLAP */
    {80, 20, 50, 90, 50, 20, 20, 0},             /* CL HAT */
    {80, 60, 50, 90, 50, 10, 20, 0},             /* OP HAT */
    {44, 50, 40, 40, 0, 10, 20, 0},              /* TOM */
    {90, 14, 40, 20, 26, 30, 80, 30},            /* RIM */
    {60, 70, 60, 60, 70, 40, 20, 0},             /* CYMBAL */
};
static const uint8_t LX_RANGE[NTRK][8][2] = {
    {{8, 22}, {55, 100}, {40, 110}, {20, 50}, {0, 0}, {0, 30}, {20, 90}, {0, 60}},
    {{30, 80}, {25, 60}, {50, 110}, {40, 80}, {40, 110}, {20, 90}, {10, 70}, {0, 50}},
    {{30, 100}, {30, 80}, {10, 80}, {30, 100}, {0, 80}, {0, 60}, {0, 30}, {0, 40}},
    {{50, 110}, {5, 40}, {10, 90}, {50, 120}, {20, 90}, {0, 50}, {0, 40}, {0, 30}},
    {{50, 110}, {35, 90}, {10, 90}, {50, 120}, {20, 90}, {0, 50}, {0, 40}, {0, 30}},
    {{30, 60}, {30, 70}, {20, 70}, {25, 60}, {0, 25}, {0, 40}, {0, 50}, {0, 30}},
    {{60, 110}, {5, 35}, {0, 90}, {5, 40}, {0, 127}, {0, 90}, {20, 100}, {0, 70}},
    {{30, 90}, {50, 95}, {10, 110}, {20, 100}, {30, 100}, {10, 70}, {0, 50}, {0, 40}},
};
static const uint8_t TR_FACTORY[NTRK][2] = {     /* the sample layer: one of the sound's own, its level */
    {TR_OWN0 + 0, 90}, {TR_OWN0 + 0, 90}, {TR_OWN0 + 0, 90}, {TR_OWN0 + 0, 80},
    {TR_OWN0 + 0, 80}, {TR_OWN0 + 0, 90}, {TR_OWN0 + 0, 100}, {TR_OWN0 + 0, 80},
};
static const uint8_t TR_LXR_FIT[NTRK][3] = {     /* the LXR transients that suit each sound (CLK CK2 TIK KIK RIM DRP HAT CLP KK2 SNR TOM SP2) */
    {0, 3, 8}, {9, 11, 2}, {7, 9, 2}, {6, 2, 1}, {6, 11, 1}, {10, 2, 5}, {4, 5, 1}, {6, 11, 2},
};
static void sound_preset(uint32_t t, uint32_t k)   /* track t's sound k: 0 the factory one, else rolled (k the seed) */
{
    sound_t *s = &snd[t % NTRK];
    uint32_t i, x = k * 2654435761u + t * 40503u + 1u;
    for (i = 0; i < 8u; i++) {
        uint32_t lo = LX_RANGE[t][i][0], hi = LX_RANGE[t][i][1];
        s->p[i] = k ? (uint8_t)(lo + xrand(&x) % (hi - lo + 1u)) : LX_FACTORY[t][i];
    }
    if (k && TRK_KIND[t] == K_DRUM && t != 6u && (xrand(&x) & 3u) == 0u)
        s->p[4] = 26;                            /* a triangle drum now and then */
    /* the sample layer always plays: one of the sound's own sixteen, of its sixteen after the classic machines, or a
     * fitting LXR one */
    if (!k) {
        s->smp = TR_FACTORY[t][0];
    } else {
        uint32_t r = xrand(&x) % 10u;
        s->smp = (uint8_t)(r < 4u ? TR_OWN0 + xrand(&x) % 16u : r < 8u ? TR_MACH0 + xrand(&x) % 16u
                                  : TR_SMP0 + TR_LXR_FIT[t][xrand(&x) % 3u]);
    }
    s->sstart = 0;
    s->stone = 0;
    s->sbend = k && xrand(&x) % 5u == 0u ? (int8_t)((int32_t)(xrand(&x) % 81u) - 40) : 0;
    s->slvl = k ? (uint8_t)(60u + xrand(&x) % 68u) : TR_FACTORY[t][1];
    s->spitch = k ? (uint8_t)(40u + xrand(&x) % 50u) : 64;
    s->sdec = k && xrand(&x) % 3u == 0u ? (uint8_t)(50u + xrand(&x) % 70u) : 127;
    s->preset = (uint8_t)k;
}
static void sound_init(uint32_t t)               /* the whole sound: the factory one, centred, the LFOs off */
{
    sound_t *s = &snd[t % NTRK];
    uint32_t k;
    sound_preset(t, 0);
    s->syn = 127;
    s->level = 100;
    s->pan = (int8_t)((const int8_t[NTRK]){0, 0, 6, -12, -12, 14, 10, 16})[t % NTRK];
    s->dly = 0;
    s->rev = t == 2u || t == 7u ? 30 : 8;
    s->drv = 0;
    for (k = 0; k < 2u; k++) {
        s->lfo[k].dest = LD_OFF;
        s->lfo[k].wave = LW_SINE;
        s->lfo[k].rate = LR_1BAR;
        s->lfo[k].depth = 32;
    }
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
    int32_t dk = 16 + song.dly_tone * 240 / 127, dl = song.dly_lvl, rl = song.rev_lvl;   /* TONE: the repeats' brightness */
    if (dt >= DLY_N)
        dt = DLY_N - 1u;
    for (i = 0; i < n; i++) {
        int32_t d = dly_buf[(dly_w - dt) & (DLY_N - 1u)], r = 0, a, in = rsend[i] >> 2, ap;
        dly_lp += ((d - dly_lp) * dk) >> 8;      /* darker repeats */
        dly_buf[dly_w++ & (DLY_N - 1u)] = (int16_t)s16(dsend[i] + ((dly_lp * dfb) >> 5));
        for (k = 0; k < RV_C; k++) {             /* parallel combs with damping */
            int16_t *b = rv_comb[k];
            int32_t y = b[rv_ci[k]];
            rv_lp[k] += (y - rv_lp[k]) >> rd;
            b[rv_ci[k]] = (int16_t)s16(in + ((rv_lp[k] * rfb) >> 5));
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
        L[i] += ((r * rl) >> 7) + ((d * dl) >> 7);   /* the returns' LEVEL */
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
            R[i] += ((rr * rl) >> 7) + ((d * 7 * dl) >> 10);
        }
    }
}

/* the drive bus: the sounds' DRIVE sends through SOFT (tanh), HARD (a clip) or FOLD (a wave folder), its TONE (a low-pass),
 * back at LEVEL in the middle */
static int32_t drv_lp;
static void drive_block(int32_t *L, int32_t *R, const int32_t *dv, uint32_t n)
{
    int32_t g = 16 + song.drv_amt * 2, tk = 8 + song.drv_tone * 248 / 127, lvl = song.drv_lvl, T = 20000;
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        int32_t x = clamp((dv[i] * g) >> 4, -262143, 262143), y;
        switch (song.drv_mode) {
        case DM_HARD: y = clamp(x, -T, T); break;
        case DM_FOLD:
            y = x;
            for (k = 0; k < 6u && (y > T || y < -T); k++)   /* folded back at +-T */
                y = y > T ? 2 * T - y : -2 * T - y;
            y = clamp(y, -T, T);
            break;
        default: y = sat(x); break;
        }
        drv_lp += ((y - drv_lp) * tk) >> 8;
        y = (drv_lp * lvl) >> 7;
        L[i] += y;
        R[i] += y;
    }
}

/* ------------------------------------------------------------------ mix --- */
static void seq_block(uint32_t n);               /* lx_seq.c: the steps of this block */
static int32_t peak[NTRK];                       /* the mixer's meters (Q15, decaying in the UI) */
static void mix_block(int32_t *out, uint32_t n)  /* n samples (n <= CTL) of the master into out: stereo, Q15 */
{
    int32_t tb[CTL], L[CTL], R[CTL], ds[CTL], rs[CTL], dv[CTL];
    uint32_t i, t, m = song.master_q12;
    seq_block(n);
    lfo_block(n);
    for (i = 0; i < n; i++)
        L[i] = R[i] = ds[i] = rs[i] = dv[i] = 0;
    for (t = 0; t < NTRK; t++) {
        const sound_t *s = &snd[t];
        int32_t g, pan, pl, pr, dg = s->dly, rg = s->rev, vg = s->drv, pk = 0;
        if (!voice[t].active)
            continue;
        for (i = 0; i < n; i++)
            tb[i] = 0;
        lx_render(t, &voice[t], tb, n);
        if (!trk_audible(t))
            continue;
        g = s->level * 2;                        /* 0..254, 200 at the default */
        g = clamp(g * (256 + lfo_amt(t, LD_LEVEL, 256)) >> 8, 0, 254);
        pan = clamp(s->pan + lfo_amt(t, LD_PAN, 64), -64, 64);
        pl = 64 - pan;
        pr = 64 + pan;
        for (i = 0; i < n; i++) {
            int32_t x = (tb[i] >> 2) * g >> 4;
            L[i] += (x * pl) >> 7;
            R[i] += (x * pr) >> 7;
            ds[i] += (x * dg) >> 8;
            rs[i] += (x * rg) >> 7;
            dv[i] += (x * vg) >> 7;
            if (x > pk) pk = x;
            else if (-x > pk) pk = -x;
        }
        if (pk > peak[t])
            peak[t] = pk;
    }
    fx_block(L, R, ds, rs, n);
    drive_block(L, R, dv, n);
    if (song.playing && (song.click == CLICK_ON || (song.click == CLICK_REC && song.rec)) && song.step % 4u == 0u) {
        uint32_t bar = song.pstep % 16u == 0u, p0 = song.pos >= n ? song.pos - n : 0u;
        for (i = 0; i < n; i++) {
            uint32_t pp = p0 + i;
            if (pp < 1800u) {
                int32_t c = (sine(pp * (bar ? 146100u : 97400u)) * (int32_t)(1800u - pp) / 1800) >> 2;
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
}

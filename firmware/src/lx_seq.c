/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: the sequencer, run by the audio ISR once a block (mix_block -> seq_block). A global 16th clock (BPM,
 * SWING); each track plays its step of the pattern (16ths since the pattern began % its LEN). A pattern ends after
 * its longest track; then the cued pattern follows, or the next one of the CHAIN.
 * Note repeat (ARP): the pads repeat at RATE while held (LATCH: until pressed again), on the clock's grid while
 * playing. With REC armed while playing every hit (the pads, MIDI IN) is written to the nearest step of its track;
 * in ERASE (SHIFT + REC) holding a pad clears its track's steps as the playhead passes them. */

/* main loop -> audio ISR */
enum { EV_HIT = 1, EV_PAD_ON, EV_PAD_OFF, EV_STOP, EV_PANIC, EV_UNLATCH };
#define EVQ 32u
static uint32_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
static void ev_push(uint32_t type, uint32_t t, uint32_t vel)
{
    if (ev_w - ev_r >= EVQ)
        return;
    evq[ev_w % EVQ] = type | t << 8 | vel << 16;
    RING_PUBLISH();
    ev_w++;
}
static volatile uint8_t seq_start;               /* PLAY pressed: step 1 at the next block */
static uint32_t song_abs;                        /* samples since PLAY (the note repeat's grid) */
static volatile uint8_t hit_flash;               /* bit t: track t was hit (the UI clears it) */

static uint32_t step_len(uint32_t step)          /* samples of 16th `step`, swung */
{
    uint32_t base = step_base(), sw = base * song.swing / 200u;
    return step & 1u ? base - sw : base + sw;
}
static uint32_t pat_len(const pattern_t *p)      /* a pattern's length: its longest track */
{
    uint32_t t, n = 1;
    for (t = 0; t < NTRK; t++)
        if (p->len[t] > n)
            n = p->len[t];
    return n > NSTEP ? NSTEP : n;
}

/* ------------------------------------------------------------ recording --- */
static uint8_t rec_skip[NTRK];                   /* step + 1 written just ahead of the playhead: it does not play again */
static uint32_t rec_writes;                      /* (the tests and the screen) */
static void rec_hit(uint32_t t, uint32_t vel, uint32_t acc)
{
    pattern_t *p = PAT;
    uint32_t len = clamp(p->len[t], 1, NSTEP), g = song.pstep, q = Q_STEPS[song.qgrid % Q_N], r = g % q, at = g - r;
    uint32_t sl = step_len(song.step);
    /* the grid point: the nearest one (QUANTIZE ROUND NEAREST), or the one the hit came after (BEFORE) */
    if (!song.qround && 2u * (r * sl + song.pos) > q * sl)
        at += q;
    if (at > g)                                  /* written ahead of the playhead: it sounded already, it does not play again */
        rec_skip[t] = (uint8_t)(at % len + 1u);
    p->s[t][at % len] = (uint8_t)(clamp((int32_t)vel, 1, 127) | (acc ? S_ACC : 0u));
    p->x[t][at % len] = 0;                       /* (always, the sound's pan) */
    rec_writes++;
}
static int erasing(void) { return song.rec && song.erase && song.playing; }
/* a live hit (a pad, MIDI IN, AUDITION): played, and written when REC is armed while playing */
static void live_hit(uint32_t t, uint32_t vel, uint32_t acc)
{
    t %= NTRK;
    if (erasing())
        return;
    voice_hit(t, vel, acc, PAN_SOUND);
    hit_flash |= (uint8_t)(1u << t);
    if (song.rec && song.playing)
        rec_hit(t, vel, acc);
}

/* ---------------------------------------------------------- note repeat --- */
static struct {
    uint8_t rate, vel, vmode, latch, human;      /* the ARP page */
    uint8_t held, latched;                       /* bit t: pad t held / repeating (LATCH) */
    uint8_t n[NTRK];                             /* repeats since the pad went down (the velocity ramps) */
    uint32_t clk;                                /* the free clock (stopped) */
    uint32_t at[NTRK];                           /* the clock at the pad's press */
} nr = {NR_16, 100, NV_FIXED, 0, 0, 0, 0, {0}, 0, {0}};
static uint32_t nr_vel(uint32_t t)
{
    uint32_t v = nr.vel, k = nr.n[t] < 6u ? nr.n[t] : 6u;
    switch (nr.vmode) {
    case NV_UP: v = v * (2u + k) / 8u; break;
    case NV_DOWN: v = v * (8u - k) / 8u; break;
    case NV_RANDOM: v = v - rng() % (v / 2u + 1u); break;
    }
    if (nr.human)
        v = (uint32_t)clamp((int32_t)v + (int32_t)(rng() % (nr.human / 2u + 1u)) - nr.human / 4, 1, 127);
    if (nr.n[t] < 255u)
        nr.n[t]++;
    return v ? v : 1u;
}
static void pad_on(uint32_t t)
{
    uint32_t b = 1u << (t % NTRK);
    t %= NTRK;
    nr.n[t] = 0;
    if (nr.latch && nr.rate != NR_OFF) {
        if (nr.latched & b) {                    /* LATCH: pressed again, it stops */
            nr.latched &= (uint8_t)~b;
            return;
        }
        nr.latched |= (uint8_t)b;
    }
    nr.held |= (uint8_t)b;
    nr.at[t] = nr.clk;
    live_hit(t, nr_vel(t), 0);
}
static void pad_off(uint32_t t) { nr.held &= (uint8_t)~(1u << (t % NTRK)); }
static void nr_block(uint32_t n)                 /* the repeats in this block: on the grid of RATE */
{
    uint32_t act = (uint32_t)(nr.held | nr.latched) & 0xFFu, per, gp, t;
    nr.clk += n;
    if (nr.rate == NR_OFF || nr.rate >= NR_N || !act)
        return;
    per = step_base() * NR_SIXTHS[nr.rate] / 6u;
    gp = song.playing ? song_abs : nr.clk - n;
    if (!per || gp / per == (gp + n) / per)
        return;
    for (t = 0; t < NTRK; t++)
        if ((act >> t) & 1u) {
            if (nr.n[t] == 1u && nr.clk - nr.at[t] < per / 2u)   /* (just hit by its press: no double) */
                continue;
            live_hit(t, nr_vel(t), 0);
        }
}

/* --------------------------------------------------------------- MIDI IN --- */
/* General MIDI drum notes play their sounds; C4..G4 (60..67) the eight sounds in order */
static int32_t midi_track(uint32_t n)
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
    return n >= 60u && n < 68u ? (int32_t)n - 60 : -1;
}
static void midi_note(uint32_t st, uint32_t d1, uint32_t d2)
{
    int32_t t = midi_track(d1);
    if (song.midi_ch && (st & 15u) + 1u != song.midi_ch)
        return;
    if (t >= 0 && (st & 0xF0u) == 0x90u && d2)
        live_hit((uint32_t)t, d2, d2 > 110u);
}

/* ------------------------------------------------------------ the steps --- */
static void track_step(uint32_t t)
{
    pattern_t *p = PAT;
    uint32_t len = clamp(p->len[t], 1, NSTEP), idx = song.pstep % len;
    uint8_t s = p->s[t][idx];
    if (rec_skip[t] == idx + 1u) {               /* written ahead: it sounded already (the pad) */
        rec_skip[t] = 0;
        return;
    }
    if (erasing() && (((nr.held | nr.latched) >> t) & 1u)) {   /* ERASE: the held pads' steps go */
        p->s[t][idx] = 0;
        p->x[t][idx] = 0;
        return;
    }
    if (s && trk_audible(t)) {
        uint32_t x = p->x[t][idx];
        if (X_PROB(x) && (rng() & 15u) >= X_PROB(x))   /* PROBABILITY: k/16 of the passes */
            return;
        voice_hit(t, S_VEL(s), s & S_ACC, X_PAN(x) ? ((int32_t)X_PAN(x) - 8) * 8 : PAN_SOUND);
        hit_flash |= (uint8_t)(1u << t);
    }
}
static void pattern_end(void)                    /* the cued pattern, or the chain's next */
{
    if (song.pat_next < NPAT) {
        song.pat = song.chain0 = song.pat_next;
    } else if (song.chain > 1u) {
        uint32_t i = (uint32_t)(song.pat + NPAT - song.chain0) % NPAT;
        song.pat = (uint8_t)((song.chain0 + (i + 1u) % song.chain) % NPAT);
    }
    song.pat_next = 0xFF;
}

static void midi_in_drain(void);                  /* USB MIDI IN (oc_main.c; the host's) */
static void seq_block(uint32_t n)
{
    uint32_t t;
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % EVQ], ty = e & 0xFFu, tt = (e >> 8) & 7u, vel = (e >> 16) & 0xFFu;
        ev_r++;
        if (ty == EV_HIT)
            live_hit(tt, vel & 0x7Fu, vel & 0x80u);
        else if (ty == EV_PAD_ON)
            pad_on(tt);
        else if (ty == EV_PAD_OFF)
            pad_off(tt);
        else if (ty == EV_UNLATCH)
            nr.latched = 0;
        else if (ty == EV_PANIC)                 /* (EV_STOP: the hits ring out) */
            for (t = 0; t < NTRK; t++)
                if (voice[t].active)
                    voice[t].choke = 1;
    }
    midi_in_drain();
    if (seq_start) {
        seq_start = 0;
        if (song.pat_next < NPAT)
            song.pat = song.chain0 = song.pat_next;
        song.pat_next = 0xFF;
        song.step = song.pos = song.pstep = 0;
        song_abs = 0;
        lfo_clk = 0;                             /* (the LFOs from the bar PLAY began) */
        song.playing = 1;
        for (t = 0; t < NTRK; t++) {
            rec_skip[t] = 0;
            track_step(t);
        }
    }
    nr_block(n);
    if (!song.playing)
        return;
    song.pos += n;
    song_abs += n;
    if (song.pos >= step_len(song.step)) {
        song.pos -= step_len(song.step);
        song.step++;
        if (++song.pstep >= pat_len(PAT)) {
            song.pstep = 0;
            pattern_end();
        }
        for (t = 0; t < NTRK; t++)
            track_step(t);
    }
}

static void seq_play(int on)
{
    if (on && !song.playing) {
        seq_start = 1;
    } else if (!on && song.playing) {
        song.playing = 0;
        ev_push(EV_STOP, 0, 0);
    }
}
static void seq_restart(void) { seq_start = 1; }   /* SHIFT + PLAY: from the top (playing or not) */

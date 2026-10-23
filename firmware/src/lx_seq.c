/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: the sequencer, run by the audio ISR once a block (mix_block -> seq_block). A global 16th clock (BPM,
 * SWING; or MIDI clock, CK_EXT: a 16th every 6 ticks); each track steps at its SPEED (a step every d / n 16ths) through
 * its LEN steps. A pattern ends after its longest track; then the cued pattern follows, the next row of the SONG, or
 * the next one of the CHAIN.
 * A step's hit carries its parameter locks (lx_core.h lock_t) and lands NUDGE 32nds of the step late or early (an early
 * one is played from the step before it); its substeps follow over the step.
 * Note repeat (ARP): the pads repeat at RATE while held (LATCH: until pressed again), on the clock's grid while
 * playing. With REC armed while playing every hit (the pads, MIDI IN) is written to the nearest step of its track;
 * in ERASE (SHIFT + REC) holding a pad clears its track's steps as the playhead passes them.
 * MIDI OUT (USB): the hits of a sound with a channel as notes; MIDI clock (24 a quarter, start / stop) when CLK OUT is on
 * (with CK_EXT: the clock received, passed on). MIDI IN: notes play the sounds, controllers go to the UI (CC learn). */

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
/* audio ISR -> main loop: MIDI IN controllers (channel, number, value), for the CC map (lx_ui.c) */
#define CCQ 32u
static uint32_t ccq[CCQ];
static volatile uint32_t cc_w, cc_r;
static volatile uint8_t seq_start;               /* PLAY pressed: step 1 at the next block (CK_EXT: at the next tick) */
static uint32_t song_abs;                        /* samples since PLAY (the hits' times, the note repeat's grid) */
static uint32_t clk_now;                         /* samples since power-on (MIDI: note lengths, the clock's ticks) */
static volatile uint8_t hit_flash;               /* bit t: track t was hit (the UI clears it) */

static uint32_t step_len(uint32_t step)          /* samples of 16th `step`, swung (CK_EXT: straight) */
{
    uint32_t base = step_base(), sw = song.clk_src == CK_EXT ? 0u : base * song.swing / 200u;
    return step & 1u ? base - sw : base + sw;
}
static uint32_t trk_n16(const pattern_t *p, uint32_t t)   /* 16ths a pass of track t takes (rounded up) */
{
    uint32_t i = SPD_I(p->spd[t]), len = (uint32_t)clamp(p->len[t], 1, NSTEP);
    return (len * SPD_D[i] + SPD_N[i] - 1u) / SPD_N[i];
}
static uint32_t pat_len(const pattern_t *p)      /* a pattern's length in 16ths: its longest track */
{
    uint32_t t, n = 1;
    for (t = 0; t < NTRK; t++)
        if (trk_n16(p, t) > n)
            n = trk_n16(p, t);
    return n;
}
static uint32_t song_fine(void)                  /* where the pattern is: 16ths since it began, Q10 */
{
    uint32_t sl = step_len(song.step), fr = sl ? song.pos * 1024u / sl : 0u;
    return song.pstep * 1024u + (fr > 1023u ? 1023u : fr);
}
static uint32_t trk_at(uint32_t t, uint32_t g)   /* .. and track t at its speed: its steps since then, Q10 */
{
    uint32_t i = SPD_I(PAT->spd[t]);
    return SPD_N[i] == SPD_D[i] ? g : g * SPD_N[i] / SPD_D[i];
}
static uint32_t trk_fine(uint32_t t) { return trk_at(t, song_fine()); }
static uint32_t trk_slen(uint32_t t)             /* samples a step of track t lasts */
{
    uint32_t i = SPD_I(PAT->spd[t]);
    return step_len(song.step) * SPD_D[i] / SPD_N[i];
}

/* ------------------------------------------------------------- MIDI OUT --- */
static void midi_out_event(uint32_t pkt);        /* usb.c (the host's: lx_host.c) */
static void mo_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >= 0xF8u ? 0xFu : st >> 4;
    midi_out_event(cin | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}
static struct { uint8_t on, ch, note; uint32_t off_at; } mnote[NTRK];   /* each sound's note sounding on MIDI OUT */
static void note_off(uint32_t t)
{
    if (mnote[t].on) {
        mo_send(0x80u | ((mnote[t].ch - 1u) & 15u), mnote[t].note, 0);
        mnote[t].on = 0;
    }
}
static void note_out(uint32_t t, uint32_t vel, uint32_t acc, uint32_t gate)
{
    const sound_t *s = &snd[t];
    if (!s->och || s->och > 16u || !trk_audible(t))
        return;
    note_off(t);
    mnote[t].ch = s->och;
    mnote[t].note = s->onote & 0x7Fu;
    mnote[t].on = 1;
    mnote[t].off_at = clk_now + (gate ? gate : step_base() / 2u);
    mo_send(0x90u | ((s->och - 1u) & 15u), s->onote, (uint32_t)clamp((int32_t)vel + (acc ? 20 : 0), 1, 127));
}
/* a hit: the voice, the pads' lights, MIDI OUT (echo 0: a hit from MIDI IN, not sent back) */
static void trk_hit(uint32_t t, uint32_t vel, uint32_t acc, int32_t pan, uint32_t nlk, const uint8_t (*lk)[2], int echo)
{
    voice_hit_lk(t, vel, acc, pan, nlk, lk);
    hit_flash |= (uint8_t)(1u << t);
    if (echo)
        note_out(t, vel, acc, 0);
}

/* ------------------------------------------------------------ recording --- */
static uint8_t rec_skip[NTRK];                   /* step + 1 written just ahead of the playhead: it does not play again */
static uint32_t rec_writes;                      /* (the tests and the screen) */
static uint16_t trk_k[NTRK];                     /* each track's steps since the pattern began */
static void rec_hit(uint32_t t, uint32_t vel, uint32_t acc)
{
    pattern_t *p = PAT;
    uint32_t len = clamp(p->len[t], 1, NSTEP), f = trk_fine(t), g = f >> 10, q = Q_STEPS[song.qgrid % Q_N];
    uint32_t in = (g % q) * 1024u + (f & 1023u), at = g - g % q;
    /* the grid point: the nearest one (QUANTIZE ROUND NEAREST), or the one the hit came after (BEFORE) */
    if (!song.qround && 2u * in > q * 1024u)
        at += q;
    if (at > trk_k[t])                           /* written ahead of the playhead: it sounded already, it does not play again */
        rec_skip[t] = (uint8_t)(at % len + 1u);
    p->s[t][at % len] = (uint8_t)(clamp((int32_t)vel, 1, 127) | (acc ? S_ACC : 0u));
    p->x[t][at % len] = 0;                       /* (always, the sound's pan, one hit, on time) */
    p->r[t][at % len] = 0;
    rec_writes++;
}
static int erasing(void) { return song.rec && song.erase && song.playing; }
/* a live hit (a pad, MIDI IN, AUDITION): played, and written when REC is armed while playing */
static void live_hit(uint32_t t, uint32_t vel, uint32_t acc, int echo)
{
    t %= NTRK;
    if (erasing())
        return;
    trk_hit(t, vel, acc, PAN_SOUND, 0, 0, echo);
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
    live_hit(t, nr_vel(t), 0, 1);
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
            live_hit(t, nr_vel(t), 0, 1);
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
/* MIDI clock in (CK_EXT): a 16th every 6 ticks, the tempo from the last 24 */
static struct {
    uint8_t arm;                                 /* 1 start at the next tick, 2 continue at the next tick */
    uint8_t tc;                                  /* ticks into the 16th */
    uint8_t due;                                 /* 16ths ended by ticks, for seq_block */
    uint8_t n;                                   /* ticks measured (up to 24) */
    uint32_t last, gap[24], sum, w, seen;        /* the last tick (clk_now), the gaps, their sum; when one came */
} xc;
static void seq_go(void);
static void ext_tick(void)
{
    uint32_t d = clk_now - xc.last;
    if (xc.last && d < FS) {                     /* the tempo: a quarter of ticks */
        xc.sum += d - xc.gap[xc.w % 24u];
        xc.gap[xc.w++ % 24u] = d;
        if (xc.n < 24u)
            xc.n++;
        if (xc.n == 24u && xc.sum)
            song.bpm = (uint8_t)clamp((int32_t)((FS * 60u + xc.sum / 2u) / xc.sum), 60, 200);
    } else {
        xc.n = 0;
        xc.sum = 0;
        memset(xc.gap, 0, sizeof xc.gap);
    }
    xc.last = clk_now;
    xc.seen = clk_now | 1u;
    if (song.clk_src != CK_EXT)
        return;
    if (song.clk_out)
        mo_send(0xF8u, 0, 0);                    /* (passed on) */
    if (xc.arm == 1u) {                          /* START: this tick is step 1 */
        xc.arm = 0;
        xc.tc = 0;
        seq_go();
        return;
    }
    if (xc.arm == 2u) {                          /* CONTINUE: from where it stopped */
        xc.arm = 0;
        xc.tc = 0;
        song.playing = 1;
        return;
    }
    if (song.playing && ++xc.tc >= 6u) {
        xc.tc = 0;
        if (xc.due < 8u)
            xc.due++;
    }
}
#define MIDI_ROOT 60                             /* tonal: the note that plays a sound at its own pitch */
static void midi_note(uint32_t st, uint32_t d1, uint32_t d2)
{
    int32_t t;
    switch (st) {
    case 0xF8u: ext_tick(); return;
    case 0xFAu: case 0xFBu:                      /* START / CONTINUE (CK_EXT: at the next tick; passed on) */
        if (song.clk_src != CK_EXT) return;
        if (song.clk_out) mo_send(st, 0, 0);
        xc.arm = st == 0xFAu ? 1u : 2u;
        return;
    case 0xFCu:
        if (song.clk_src != CK_EXT) return;
        if (song.clk_out) mo_send(st, 0, 0);
        xc.arm = 0;
        song.playing = 0;
        return;
    }
    if ((st & 0xF0u) == 0xB0u) {                 /* a controller: for the CC map (the main loop) */
        if (cc_w - cc_r < CCQ) {
            ccq[cc_w % CCQ] = (st & 15u) | d1 << 8 | d2 << 16;
            RING_PUBLISH();
            cc_w++;
        }
        return;
    }
    if ((st & 0xF0u) == 0x90u && d2) {            /* a channel only one sound has (its MIDI CH): that sound, tonal */
        uint32_t k, n = 0, ch = (st & 15u) + 1u;
        for (k = 0, t = -1; k < NTRK; k++)
            if (snd[k].och == ch) {
                t = (int32_t)k;
                n++;
            }
        if (n == 1u && ch != song.midi_ch) {   /* C3 (60) its own pitch, a semitone a key, three octaves each way */
            hit_semi = (int8_t)clamp((int32_t)d1 - MIDI_ROOT, -36, 36);
            live_hit((uint32_t)t, d2, d2 > 110u, 0);
            hit_semi = 0;
            return;
        }
    }
    if (song.midi_ch && (st & 15u) + 1u != song.midi_ch)
        return;
    t = midi_track(d1);
    if (t >= 0 && (st & 0xF0u) == 0x90u && d2)
        live_hit((uint32_t)t, d2, d2 > 110u, 0);
}

/* ------------------------------------------------------------ the steps --- */
/* a hit due: when, its substeps to come and their gap, the hit itself (its locks with it) */
typedef struct { uint8_t on, left, vel, acc, nlk; int8_t pan; uint8_t lk[NLK_V][2]; uint32_t at, gap; } hit_t;
static hit_t pend[NTRK][2];
static uint16_t trk_pre[NTRK];                   /* step + 1 played already, early (a negative NUDGE) */
static void pend_fire(uint32_t t, hit_t *h)      /* the hits of h due by now */
{
    while (h->on && (int32_t)(song_abs - h->at) >= 0) {
        if (trk_audible(t))
            trk_hit(t, h->vel, h->acc, h->pan, h->nlk, (const uint8_t (*)[2])h->lk, 1);
        if (h->left) {
            h->left--;
            h->at += h->gap;
        } else {
            h->on = 0;
        }
    }
}
static void pend_add(uint32_t t, const hit_t *h)
{
    hit_t *d = !pend[t][0].on ? &pend[t][0] : !pend[t][1].on ? &pend[t][1] :
               (int32_t)(pend[t][0].at - pend[t][1].at) < 0 ? &pend[t][0] : &pend[t][1];
    *d = *h;
    d->on = 1;
    pend_fire(t, d);
}
/* the hit of step idx of track t (its k-th since the pattern began), if it plays: 1 */
static int step_trig(uint32_t t, uint32_t idx, uint32_t k, hit_t *h)
{
    pattern_t *p = PAT;
    uint32_t len = clamp(p->len[t], 1, NSTEP), s = p->s[t][idx], x = p->x[t][idx], c = X_COND(x), i, key;
    const lock_t *l = plk[song.pat % NPAT];
    if (!s || !trk_audible(t))
        return 0;
    if (c && c < C_PROB_N && (rng() & 15u) >= c)   /* PROBABILITY: k/16 of the passes */
        return 0;
    if (c >= C_PROB_N && c < C_FILL) {           /* A:B: the A-th of every B passes of the track */
        uint32_t tsp = (pat_len(p) * SPD_N[SPD_I(p->spd[t])] + SPD_D[SPD_I(p->spd[t])] - 1u) / SPD_D[SPD_I(p->spd[t])];
        uint32_t pass = song.loops * (tsp / len) + k / len;
        if (pass % COND_B[c - C_PROB_N] != COND_A[c - C_PROB_N] - 1u)
            return 0;
    }
    if ((c == C_FILL && !song.fill) || (c == C_NFILL && song.fill))
        return 0;
    h->vel = (uint8_t)S_VEL(s);
    h->acc = (uint8_t)(s & S_ACC);
    h->pan = (int8_t)(X_PAN(x) ? X_PANV(x) : PAN_SOUND);
    h->left = (uint8_t)(R_SUB(p->r[t][idx]) < 6u ? R_SUB(p->r[t][idx]) : 0u);
    h->nlk = 0;
    key = t << 6 | idx;
    for (i = 0; i < NLOCK && h->nlk < NLK_V; i++)
        if (l[i].pid && l[i].key == key) {
            h->lk[h->nlk][0] = l[i].pid;
            h->lk[h->nlk][1] = l[i].val;
            h->nlk++;
        }
    return 1;
}
static void track_step(uint32_t t, uint32_t k)   /* track t reaches its k-th step since the pattern began */
{
    pattern_t *p = PAT;
    uint32_t len = clamp(p->len[t], 1, NSTEP), idx = k % len, sl = trk_slen(t);
    hit_t h;
    trk_k[t] = (uint16_t)k;
    if (trk_pre[t] == k + 1u) {                  /* played already (early) */
        trk_pre[t] = 0;
        return;
    }
    trk_pre[t] = 0;
    if (rec_skip[t] == idx + 1u) {               /* written ahead: it sounded already (the pad) */
        rec_skip[t] = 0;
        return;
    }
    if (erasing() && (((nr.held | nr.latched) >> t) & 1u)) {   /* ERASE: the held pads' steps go */
        p->s[t][idx] = 0;
        p->x[t][idx] = 0;
        p->r[t][idx] = 0;
        {
            uint32_t i;
            for (i = 0; i < NLOCK; i++)
                if (plk[song.pat % NPAT][i].pid && plk[song.pat % NPAT][i].key == (t << 6 | idx))
                    plk[song.pat % NPAT][i].pid = 0;
        }
        return;
    }
    if (step_trig(t, idx, k, &h)) {
        int32_t m = R_MICRO(p->r[t][idx]);
        h.gap = sl / (h.left + 1u);
        h.at = song_abs + (m > 0 ? (uint32_t)m * sl / 32u : 0u);
        pend_add(t, &h);
    }
    /* the next step early (a negative NUDGE): played from here, if it is still in this pattern */
    if ((k + 1u) * SPD_D[SPD_I(p->spd[t])] < pat_len(p) * SPD_N[SPD_I(p->spd[t])]) {
        uint32_t i2 = (k + 1u) % len;
        int32_t m2 = R_MICRO(p->r[t][i2]);
        if (m2 < 0 && p->s[t][i2] && step_trig(t, i2, k + 1u, &h)) {
            h.gap = sl / (h.left + 1u);
            h.at = song_abs + sl - (uint32_t)(-m2) * sl / 32u;
            pend_add(t, &h);
            trk_pre[t] = (uint16_t)(k + 2u);
        }
    }
}
static void pattern_end(void)                    /* the cued pattern, the song's next, or the chain's next */
{
    if (song.pat_next < NPAT) {
        song.pat = song.chain0 = song.pat_next;
    } else if (song.smode && song.slen) {
        if (++song.srep >= (srows[song.srow % NSONG].rep ? srows[song.srow % NSONG].rep : 1u)) {
            song.srep = 0;
            if (++song.srow >= song.slen) {
                song.srow = 0;
                if (song.send)                   /* (STOP at the end) */
                    song.playing = 0;
            }
        }
        song.pat = srows[song.srow % NSONG].pat % NPAT;
    } else if (song.chain > 1u) {
        uint32_t i = (uint32_t)(song.pat + NPAT - song.chain0) % NPAT;
        song.pat = (uint8_t)((song.chain0 + (i + 1u) % song.chain) % NPAT);
    }
    song.pat_next = 0xFF;
}

static uint32_t clk_acc;                          /* MIDI clock out: Q8 samples toward the next tick */
static void seq_go(void)                         /* step 1 of the pattern (the song's first row) */
{
    uint32_t t;
    if (song.clk_out && song.clk_src == CK_INT) {   /* MIDI OUT: START and the downbeat's tick before its notes */
        mo_send(0xFAu, 0, 0);
        mo_send(0xF8u, 0, 0);
        clk_acc = 0;
    }
    if (song.smode && song.slen) {
        song.srow = song.srep = 0;
        song.pat = srows[0].pat % NPAT;
        song.pat_next = 0xFF;
    } else if (song.pat_next < NPAT) {
        song.pat = song.chain0 = song.pat_next;
    }
    song.pat_next = 0xFF;
    song.step = song.pos = song.pstep = 0;
    song.loops = 0;
    song_abs = 0;
    lfo_clk = 0;                                 /* (the LFOs from the bar PLAY began) */
    song.playing = 1;
    memset(pend, 0, sizeof pend);
    for (t = 0; t < NTRK; t++) {
        rec_skip[t] = 0;
        trk_pre[t] = 0;
        track_step(t, 0);
    }
}
static void step_next(void)                      /* the global 16th ends */
{
    uint32_t t;
    song.step++;
    if (++song.pstep >= pat_len(PAT)) {
        song.pstep = 0;
        song.loops++;
        pattern_end();
        for (t = 0; t < NTRK; t++) {
            trk_pre[t] = 0;
            if (song.playing)
                track_step(t, 0);
        }
    }
}

static void midi_in_drain(void);                  /* USB MIDI IN (oc_main.c; the host's) */
static uint8_t was_playing;
static void seq_block(uint32_t n)
{
    uint32_t t, g, tick = step_base() * 256u / 6u;
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % EVQ], ty = e & 0xFFu, tt = (e >> 8) & 7u, vel = (e >> 16) & 0xFFu;
        ev_r++;
        if (ty == EV_HIT)
            live_hit(tt, vel & 0x7Fu, vel & 0x80u, 1);
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
    clk_now += n;
    midi_in_drain();
    for (t = 0; t < NTRK; t++)                   /* MIDI OUT: the notes' ends */
        if (mnote[t].on && (int32_t)(clk_now - mnote[t].off_at) >= 0)
            note_off(t);
    if (seq_start) {
        seq_start = 0;
        if (song.clk_src == CK_EXT)
            xc.arm = 1;                          /* (at the next tick) */
        else
            seq_go();
    }
    if (song.playing != was_playing) {           /* MIDI OUT: STOP; stopped, the notes end (START: seq_go) */
        was_playing = song.playing;
        if (!song.playing) {
            if (song.clk_out && song.clk_src == CK_INT)
                mo_send(0xFCu, 0, 0);
            for (t = 0; t < NTRK; t++)
                note_off(t);
            memset(pend, 0, sizeof pend);
        }
    }
    if (song.clk_out && song.clk_src == CK_INT && tick) {   /* MIDI clock out: always, at the tempo (straight) */
        clk_acc += n * 256u;
        while (clk_acc >= tick) {
            clk_acc -= tick;
            mo_send(0xF8u, 0, 0);
        }
    }
    nr_block(n);
    if (!song.playing)
        return;
    song.pos += n;
    song_abs += n;
    if (song.clk_src == CK_EXT) {                /* the 16ths end on the clock's ticks */
        uint32_t sl = step_len(song.step);
        if (song.pos >= sl)
            song.pos = sl - 1u;                  /* (a slower clock: wait at the step's end) */
        while (xc.due && song.playing) {
            xc.due--;
            song.pos = 0;
            step_next();
        }
    } else {
        while (song.playing && song.pos >= step_len(song.step)) {
            song.pos -= step_len(song.step);
            step_next();
        }
    }
    for (t = 0, g = song_fine(); t < NTRK && song.playing; t++) {   /* each track at its speed: its steps reached */
        uint32_t k = trk_at(t, g) >> 10;
        while (trk_k[t] < k && song.playing)
            track_step(t, trk_k[t] + 1u);
    }
    for (t = 0; t < NTRK; t++) {                 /* the hits due: late (NUDGE), substeps */
        pend_fire(t, &pend[t][0]);
        pend_fire(t, &pend[t][1]);
    }
}

static void seq_reset(void)                     /* (power-on; the host's tests boot again and again) */
{
    memset(pend, 0, sizeof pend);
    memset(trk_k, 0, sizeof trk_k);
    memset(trk_pre, 0, sizeof trk_pre);
    memset(mnote, 0, sizeof mnote);
    memset(&xc, 0, sizeof xc);
    memset(rec_skip, 0, sizeof rec_skip);
    clk_acc = 0;
    was_playing = 0;
    seq_start = 0;
    cc_r = cc_w;
}
static void seq_play(int on)
{
    if (on && !song.playing) {
        seq_start = 1;
    } else if (!on) {
        xc.arm = 0;
        if (song.playing) {
            song.playing = 0;
            ev_push(EV_STOP, 0, 0);
        }
    }
}
static void seq_restart(void) { seq_start = 1; }   /* SHIFT + PLAY: from the top (playing or not) */

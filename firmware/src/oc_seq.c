/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the sequencer, run by the audio ISR once a block (mix_block -> seq_block). A global 16th clock
 * (BPM, SWING); each track plays its step (global step % its LEN): NOTE steps start their notes, TIE steps hold the
 * last ones, slides glide into the next note; GATE sets when a note ends. SEQ OFF: the track's pattern rests.
 * Live notes (the keys through a ring from the main loop, MIDI IN from the USB ring) play here too, and when REC is
 * armed while playing they are written into the selected track: on the nearest step (a note in the second half of a
 * step goes to the next one, which then does not play it again), held notes as ties; OVERDUB adds, REPLACE clears each
 * step of the track as the playhead reaches it in the passes recorded. */

/* main loop -> audio ISR: key notes, panic */
enum { EV_ON = 1, EV_OFF, EV_PANIC, EV_STOP };
#define EVQ 32u
static uint32_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
static void ev_push(uint32_t type, uint32_t t, uint32_t note, uint32_t vel)
{
    if (ev_w - ev_r >= EVQ)
        return;
    evq[ev_w % EVQ] = type | t << 8 | note << 16 | vel << 24;
    RING_PUBLISH();
    ev_w++;
}
static volatile uint8_t seq_start;               /* PLAY pressed: step 1 at the next block */

static uint32_t step_len(uint32_t step)          /* samples of 16th `step`, swung */
{
    uint32_t base = (uint32_t)FS * 15u / (song.bpm ? song.bpm : 120u);
    uint32_t sw = base * song.swing / 200u;
    return step & 1u ? base - sw : base + sw;
}

static void track_release(uint32_t t)
{
    track_t *tr = &trk[t];
    uint32_t k;
    for (k = 0; k < tr->nsnd; k++)
        note_off(t, tr->snd[k]);
    tr->nsnd = 0;
    tr->off_at = 0;
}

/* ------------------------------------------------------------ recording --- */
static struct {
    uint64_t done;                               /* steps of the armed track written (REPLACE: cleared) in this pass */
    uint8_t skip;                                /* step + 1 written just ahead of the playhead: it does not play again */
    uint8_t at[128];                             /* a held note: the step + 1 it was written on */
    uint32_t g0[128];                            /* .. and the global step then */
    uint32_t rec_writes;                         /* (the tests and the screen: notes written) */
} rec;
static int rec_live(uint32_t t) { return song.rec && song.playing && t == song.sel; }
static void rec_note_on(uint32_t t, uint32_t note, uint32_t vel)
{
    track_t *tr = &trk[t];
    uint32_t len = clamp(tr->len, 1, NSTEP), g = song.step, idx, k;
    step_t *s;
    if (song.pos > step_len(g) / 2u) {           /* nearer the next step */
        g++;
        rec.skip = (uint8_t)(g % len + 1u);
    }
    idx = g % len;
    s = &tr->step[idx];
    if (song.rec_mode == REC_REPLACE && !((rec.done >> idx) & 1u)) {
        memset(s, 0, sizeof *s);
    }
    rec.done |= (uint64_t)1 << idx;
    if (IS_DRUM(t)) {                            /* (note: the lanes as a mask) */
        s->time = ST_NOTE;
        s->hit |= (uint8_t)note;
        if (vel > 110u)
            s->acc |= (uint8_t)note;
        s->vel = s->vel ? s->vel : 104;
        rec.rec_writes++;
        return;
    }
    if (s->time != ST_NOTE || !s->n) {
        memset(s, 0, sizeof *s);
        s->time = ST_NOTE;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n && s->n < 4u)                  /* a chord: the notes of one step */
        s->note[s->n++] = (uint8_t)note;
    s->vel = (uint8_t)(vel ? vel : 96);
    rec.at[note & 127u] = (uint8_t)(idx + 1u);
    rec.g0[note & 127u] = g;
    rec.rec_writes++;
}
static void rec_note_off(uint32_t t, uint32_t note)
{
    track_t *tr = &trk[t];
    uint32_t len = clamp(tr->len, 1, NSTEP), at = rec.at[note & 127u], g = song.step, n, i;
    if (!at || IS_DRUM(t))
        return;
    rec.at[note & 127u] = 0;
    if (song.pos > step_len(g) / 2u)
        g++;
    n = g > rec.g0[note & 127u] ? g - rec.g0[note & 127u] : 0u;   /* steps held */
    for (i = 1; i < n && i < 16u && i < len; i++) {   /* held: ties after it (not over a note) */
        step_t *s = &tr->step[(at - 1u + i) % len];
        if (s->time == ST_NOTE && !((rec.done >> ((at - 1u + i) % len)) & 1u))
            break;
        memset(s, 0, sizeof *s);
        s->time = ST_TIE;
        rec.done |= (uint64_t)1 << ((at - 1u + i) % len);
    }
}

/* a live note (the keys, MIDI): played, and recorded when REC is armed while playing */
static void live_on(uint32_t t, uint32_t note, uint32_t vel)
{
    note_on(t, note, vel, 0);
    if (rec_live(t))
        rec_note_on(t, note, vel);
}
static void live_off(uint32_t t, uint32_t note)
{
    note_off(t, note);
    if (rec_live(t))
        rec_note_off(t, note);
}
/* a MIDI note message: channels 1..4 play tracks 1..4 (MIDI IN CH1-4), or every channel the selected track (SEL);
 * on a drum track the General MIDI drum notes play their lanes */
static void midi_note(uint32_t st, uint32_t d1, uint32_t d2)
{
    static const int8_t GM[16] = {LN_KICK, LN_RIM, LN_SNARE, LN_CLAP, LN_SNARE, LN_TOM, LN_HATC, LN_TOM,
                                  LN_HATC, LN_TOM, LN_HATO, LN_TOM, LN_TOM, LN_BELL, LN_TOM, LN_BELL};   /* 36 .. 51 */
    uint32_t ch = st & 15u, t = song.midi_in == MI_SEL ? song.sel : ch, n = d1;
    if (t >= NTRK)
        return;
    if (IS_DRUM(t))
        n = 1u << (d1 >= 36u && d1 < 52u ? (uint32_t)GM[d1 - 36u] : (d1 + 4u) % NLANE);
    if ((st & 0xF0u) == 0x90u && d2)
        live_on(t, n, d2);
    else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
        live_off(t, n);
}

/* ------------------------------------------------------------ the steps --- */
static void track_step(uint32_t t, uint32_t g)
{
    track_t *tr = &trk[t];
    uint32_t len = clamp(tr->len, 1, NSTEP), idx = g % len, k;
    step_t *s = &tr->step[idx];
    const step_t *nx = &tr->step[(idx + 1u) % len];
    if (t == song.sel && song.rec && song.playing) {
        if (idx == 0u)
            rec.done &= 1u;                      /* a new pass (step 1 may be written just ahead of it) */
        if (song.rec_mode == REC_REPLACE && !((rec.done >> idx) & 1u)) {
            memset(s, 0, sizeof *s);             /* REPLACE: what the pass reaches before a note goes */
            rec.done |= (uint64_t)1 << idx;
        }
        if (rec.skip == idx + 1u) {              /* written just before: it sounds already (the key) */
            rec.skip = 0;
            track_release(t);
            tr->hold = 0;
            return;
        }
    }
    if (tr->seq_off) {
        track_release(t);
        tr->hold = 0;
        return;
    }
    if (tr->s.engine == E_DRUM) {
        if (s->time == ST_NOTE && s->hit && !tr->mute)
            note_on(t, s->hit, s->vel ? s->vel : 100, s->acc);
        return;
    }
    if (s->time == ST_TIE) {
        if (!tr->nsnd)
            return;
    } else if (s->time == ST_NOTE && s->n) {
        uint32_t slide = tr->hold && tr->nsnd && tr->s.mono;
        if (!slide)
            track_release(t);
        if (!tr->mute) {
            for (k = 0; k < s->n && k < 4u; k++)
                note_on(t, s->note[k], s->vel ? s->vel : 96, (s->flags & SF_ACC) | (slide ? SF_SLIDE : 0u));
            if (slide)                           /* (the old note glided into the new one: its voice) */
                for (k = 0; k < tr->nsnd; k++)
                    if (tr->snd[k] != s->note[0])
                        note_off(t, tr->snd[k]);
            for (k = 0; k < s->n && k < 4u; k++)
                tr->snd[k] = s->note[k];
            tr->nsnd = s->n > 4u ? 4u : s->n;
        }
    } else {
        track_release(t);
        tr->hold = 0;
        return;
    }
    tr->hold = (uint8_t)((s->time == ST_NOTE && (s->flags & SF_SLIDE) && nx->time == ST_NOTE) ||
                         (s->time == ST_TIE && tr->hold));
    if (nx->time == ST_TIE || tr->hold)
        tr->off_at = 0;
    else
        tr->off_at = 1u + step_len(g) * clamp(tr->gate, 1, 100) / 100u;
}

static void midi_in_drain(void);                  /* USB MIDI IN (oc_main.c; the emulator's) */
static void seq_block(uint32_t n)
{
    uint32_t t;
    while (ev_r != ev_w) {                        /* the keys */
        uint32_t e = evq[ev_r % EVQ], ty = e & 0xFFu, tt = (e >> 8) & 3u;
        ev_r++;
        if (ty == EV_ON)
            live_on(tt, (e >> 16) & 0xFFu, e >> 24);
        else if (ty == EV_OFF)
            live_off(tt, (e >> 16) & 0xFFu);
        else if (ty == EV_PANIC)
            for (t = 0; t < NTRK; t++)
                track_panic(t);
        else if (ty == EV_STOP)
            for (t = 0; t < NTRK; t++) {
                track_release(t);
                trk[t].hold = 0;
                track_panic(t);
            }
    }
    midi_in_drain();
    if (seq_start) {
        seq_start = 0;
        for (t = 0; t < NTRK; t++) {
            track_release(t);
            trk[t].hold = 0;
        }
        song.step = 0;
        song.pos = 0;
        song.playing = 1;
        rec.done = 0;
        rec.skip = 0;
        for (t = 0; t < NTRK; t++)
            track_step(t, 0);
    }
    if (!song.playing)
        return;
    for (t = 0; t < NTRK; t++)                    /* gates ending in this block */
        if (trk[t].off_at && song.pos + n >= trk[t].off_at)
            track_release(t);
    song.pos += n;
    if (song.pos >= step_len(song.step)) {
        song.pos -= step_len(song.step);
        song.step++;
        for (t = 0; t < NTRK; t++)
            track_step(t, song.step);
    }
}

static void seq_play(int on)
{
    if (on && !song.playing) {
        seq_start = 1;
    } else if (!on && song.playing) {
        song.playing = 0;
        ev_push(EV_STOP, 0, 0, 0);
    }
}
static void seq_restart(void) { seq_start = 1; }   /* SHIFT + PLAY: from the top (playing or not) */

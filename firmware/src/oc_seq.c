/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the sequencer, run by the audio ISR once a block (mix_block -> seq_block). A global 16th clock
 * (BPM, SWING); each track plays its step (global step % its LEN): NOTE steps start their notes, TIE steps hold the
 * last ones, slides glide into the next note; GATE sets when a note ends. Notes from the keys and from MIDI IN come
 * through a ring (ev_*) so only the audio ISR touches the voices. */

/* main loop -> audio ISR: key / MIDI notes, panic */
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

static void track_step(uint32_t t, uint32_t g)
{
    track_t *tr = &trk[t];
    uint32_t len = clamp(tr->len, 1, NSTEP), idx = g % len, k;
    const step_t *s = &tr->step[idx], *nx = &tr->step[(idx + 1u) % len];
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

static void midi_in_drain(void);                  /* oc_midi: USB MIDI IN (main build) */
static void seq_block(uint32_t n)
{
    uint32_t t;
    while (ev_r != ev_w) {                        /* the keys, MIDI */
        uint32_t e = evq[ev_r % EVQ], ty = e & 0xFFu, tt = (e >> 8) & 3u;
        ev_r++;
        if (ty == EV_ON)
            note_on(tt, (e >> 16) & 0xFFu, e >> 24, 0);
        else if (ty == EV_OFF)
            note_off(tt, (e >> 16) & 0xFFu);
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
        song.step = 0;
        song.pos = 0;
        song.playing = 1;
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

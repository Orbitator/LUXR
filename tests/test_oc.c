/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1 host test (ASan / UBSan in tests/run.sh): the panel as on the device. Every page and sub-page with
 * its knobs (both rows: SHIFT) turned both ways on every track; ONE CLICK (REC held) and the tools while playing; the
 * parts stay valid (types, notes in the key, chords); key / scale / chords changes keep the rhythm; holds and taps;
 * SHIFT's functions; the step editor, step and live recording (OVERDUB, REPLACE), the drum grid; EUCLID; the TYPE;
 * undo / redo over several levels; saving and loading sounds and songs; the MENU and its settings; MIDI IN; the
 * companion protocol; the UBOOT countdown. */
#include "../host/host.c"
#include <math.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double peak;
static void meter(const float *l, const float *r, void *u)
{
    uint32_t i;
    (void)u;
    for (i = 0; i < CTL; i++) {
        CHECK(isfinite(l[i]) && isfinite(r[i]), "not finite");
        if (fabs(l[i]) > peak) peak = fabs(l[i]);
        if (fabs(r[i]) > peak) peak = fabs(r[i]);
    }
}
static void check_parts(const char *where, int key)
{
    uint32_t i, j, k;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        CHECK(role_has_engine(trk_role(i), t->s.engine), "%s: track %u (%s) engine %u", where, i, TYPE_NAME[t->type], t->s.engine);
        CHECK(t->len >= 1 && t->len <= NSTEP, "%s: len %u", where, t->len);
        for (j = 0; j < t->len; j++) {
            const step_t *s = &t->step[j];
            if (s->time != ST_NOTE || NO_NOTES(i)) continue;
            CHECK(s->n >= 1 && s->n <= 4, "%s: track %u step %u n %u", where, i, j, s->n);
            for (k = 0; key && k < s->n; k++) CHECK(in_key(s->note[k]), "%s: track %u note %u off key", where, i, s->note[k]);
        }
    }
}
static uint32_t rhythm_sig(void)
{
    uint32_t i, j, h = 7;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < NSTEP && !NO_NOTES(i); j++)
            h = h * 33u + trk[i].step[j].time * 3u + trk[i].step[j].n;
    return h;
}
static uint32_t key_of(uint32_t w, int black)    /* the key of white (black) place w */
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == w)
            return k;
    return 0;
}
static uint32_t white(uint32_t w) { return 1u << key_of(w, 0); }
static uint32_t black(uint32_t w) { return 1u << key_of(w, 1); }
static void turn(uint32_t enc, int32_t n) { h_enc[enc] = n; host_run(30, 0, 0); }
static void keys_tap(uint32_t keys, uint32_t ms) { h_keys = keys; host_run(ms, 0, 0); h_keys = 0; host_run(30, 0, 0); }
static void open_page(uint32_t pg, uint32_t sub) { ui.page = (uint8_t)pg; ui.sub[pg] = (uint8_t)sub; ui.eu_snap = 0; }
static void shift_on(void) { h_held |= 1u << B_GLO; h_pressed |= 1u << B_GLO; host_run(30, 0, 0); }
static void shift_off(void) { h_held &= ~(1u << B_GLO); host_run(30, 0, 0); }
static uint32_t notes_in(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < t->len; i++)
        n += t->step[i].time == ST_NOTE;
    return n;
}

static uint8_t ulaw_enc(int32_t x)               /* G.711 */
{
    int32_t sign = x < 0 ? 0x80 : 0, e, m;
    x = x < -32635 ? 32635 : x < 0 ? -x : x;
    if (x > 32635) x = 32635;
    x += 0x84;
    for (e = 7; e > 0 && !(x & (0x4000 >> (7 - e))); e--)
        ;
    m = (x >> (e + 3)) & 15;
    return (uint8_t)~(sign | e << 4 | m);
}
static int smp_frame(uint32_t cmd, const uint8_t *raw, uint32_t n)   /* a companion frame; the device's answer code */
{
    static uint8_t f[2048];
    uint32_t w = 4;
    f[0] = 0x7D; f[1] = 'O'; f[2] = 'C'; f[3] = (uint8_t)cmd;
    if (raw && n) w += pack7(raw, n, f + 4);
    h_sent_n = 0;
    proto_frame(f, w);
    return h_sent_n >= 8u && h_sent[4] == 0x2F && h_sent[5] == cmd ? h_sent[6] : 99;
}
static int smp_upload(uint32_t slot, uint32_t kind, uint32_t bars, uint32_t target, const char *name, const uint8_t *d, uint32_t len)
{
    uint8_t h[23] = {(uint8_t)slot, (uint8_t)kind, 60, (uint8_t)bars, 22050 & 255, 22050 >> 8,
                     (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24), (uint8_t)target};
    uint32_t o;
    int rc;
    memcpy(h + 11, name, strlen(name) < 11 ? strlen(name) : 11);
    if ((rc = smp_frame(0x20, h, 23)) != 0) return 100 + rc;
    for (o = 0; o < len; o += 256u) {
        uint8_t c[260];
        uint32_t n = len - o < 256u ? len - o : 256u;
        c[0] = (uint8_t)o; c[1] = (uint8_t)(o >> 8); c[2] = (uint8_t)(o >> 16); c[3] = (uint8_t)(o >> 24);
        memcpy(c + 4, d + o, n);
        if ((rc = smp_frame(0x21, c, 4 + n)) != 0) return 200 + rc;
    }
    return smp_frame(0x22, 0, 0);
}

int main(void)
{
    uint32_t r, i, pg, c, ns, row, pasted = 0;
    host_boot(7);
    check_parts("boot", 1);
    for (i = 0; i < NTRK; i++) CHECK(trk[i].type == TYPE_DEFAULT[i], "type %u", i);
    host_tap(B_PLAY);
    CHECK(song.playing, "PLAY");
    /* a tap opens a page when let go, again the next of its pages */
    host_tap(B_EDIT);
    CHECK(ui.page == PG_EDIT && !ui.layer, "EDIT tap -> page %u layer %u", ui.page, ui.layer);
    { uint32_t s0 = ui.sub[PG_EDIT]; subs_of(PG_EDIT, TSEL, &ns); host_tap(B_EDIT);
      CHECK(ns < 2 || ui.sub[PG_EDIT] == (s0 + 1) % ns, "EDIT again: next page"); }
    /* every page, every sub-page, every knob of both rows (SHIFT: GLO held) both ways, on every track */
    for (song.sel = 0; song.sel < NTRK; song.sel++)
        for (pg = 0; pg < PG_N; pg++) {
            const sub_t *sp = subs_of(pg, TSEL, &ns);
            for (i = 0; i < ns; i++)
                for (row = 0; row < 2; row++) {
                    open_page(pg, i);
                    if (row) shift_on();
                    for (c = 0; c < 4; c++) {
                        uint32_t id = pg == PG_MENU ? (c ? 0u : MENU_ROWS[0]) : sp[i].p[c + 4 * row];
                        int32_t before = id ? pget(id, TSEL) : 0, dir = id && before >= pdesc_of(id, TSEL).max ? -1 : 1;
                        CHECK(knob_id(c) == id, "page %u/%u row %u knob %u: id %u, not %u", pg, i, row, c, knob_id(c), id);
                        if (id == P_TYPE)
                            continue;                /* (below: a new sound and part) */
                        turn(EN_K1 + c, dir);
                        turn(EN_K1 + c, -dir);
                        if (id && id != P_SHIFT && id != P_PROG && id != P_ROOT && id != P_SCALE)
                            CHECK(pget(id, TSEL) == before, "page %u/%u row %u knob %u (%s): %d -> %d", pg, i, row, c,
                                  PD[id].label, before, pget(id, TSEL));
                        turn(EN_K1 + c, (int32_t)(rng() % 19) - 9);   /* a fast turn either way */
                    }
                    if (row) shift_off();
                    CHECK(ui.page == pg, "SHIFT used: no mixer (page %u, not %u)", ui.page, pg);
                    check_parts("knobs", 0);
                }
            ui.sub[pg] = 0;
        }
    song.sel = 0;
    open_page(PG_HOME, 0);
    host_hold(B_REC, 400, 0);                    /* (a clean song after the sweep) */
    song.transpose = 0;
    check_parts("ONE CLICK", 1);
    /* key, scale, chords: the rhythm stays, the notes in the new key */
    { uint32_t rs = rhythm_sig();
      song_set_key((song.root + 5u) % 12u, (song.scale + 1u) % SC_N); check_parts("new key", 1);
      CHECK(rs == rhythm_sig(), "key change kept the rhythm");
      song_set_prog((song.prog_i + 3u) % NELEM(PROGS)); check_parts("new chords", 1);
      CHECK(rs == rhythm_sig(), "chords change kept the rhythm"); }
    /* ONE CLICK (REC held) and the tools layer (SEQ held + a key), playing */
    for (r = 0; r < 40; r++) {
        peak = 0;
        host_hold(B_REC, 400, 0);
        CHECK(!song.rec, "REC held: no arm");
        check_parts("ONE CLICK", 1);
        host_run(r < 6 ? 3000 : 400, meter, 0);
        CHECK(peak < 1.0, "round %u peak %.3f", r, peak);
        turn(EN_ALGO, (int32_t)(rng() % 7) - 3);
        { uint32_t a = rng() % (GT_N + GB_N); char w[24];
          if (a >= GT_N) host_hold(B_SEQ, 400, black(a - GT_N)); else host_hold(B_SEQ, 400, white(a));
          snprintf(w, sizeof w, "SEQ layer %s", a >= GT_N ? GB_NAME[a - GT_N] : GT_NAME[a]);
          pasted = a == GT_N + GB_PASTE;           /* (a part copied before a key change: its notes as they were) */
          check_parts(w, !pasted); }
        CHECK(!ui.layer && ui.page != PG_SEQ, "SEQ held: no page after it (page %u layer %u)", ui.page, ui.layer);
        turn(EN_PRESET, (int32_t)(rng() % 3) - 1);
        check_parts("PRESETS", !pasted);
        for (i = 0; i < NVOICE; i++) CHECK(!voice[i].active || !IS_DRUM(voice[i].trk), "synth voice on drums %u", voice[i].trk);
    }
    /* SHIFT + white keys: mute, solo; GLO tapped alone: the mixer */
    { uint32_t m = trk[1].mute; host_hold(B_GLO, 60, white(1)); CHECK(trk[1].mute != m, "SHIFT + key mutes");
      CHECK(ui.page != PG_GLO, "SHIFT used: no mixer"); host_hold(B_GLO, 60, white(1)); CHECK(trk[1].mute == m, "unmute");
      host_hold(B_GLO, 60, white(NTRK + 2)); CHECK(!trk[2].mute && trk[0].mute && trk[1].mute && trk[3].mute, "solo 3");
      host_hold(B_GLO, 60, white(NTRK + 2)); CHECK(!trk[0].mute && !trk[1].mute && !trk[3].mute, "solo off");
      host_tap(B_GLO); CHECK(ui.page == PG_GLO, "GLO tap: the mixer"); }
    /* EDIT held: a white key picks the engine; SCL held: a key sets the root; ARP held: a white key the TYPE */
    song.sel = 2; host_hold(B_EDIT, 400, white(1));
    CHECK(trk[2].s.engine == ROLE_ENGINES[trk_role(2)][1], "EDIT + key engine %u", trk[2].s.engine);
    host_hold(B_SCL, 400, 1u << 2);               /* G3 */
    CHECK(song.root == 7, "SCL + G: root %u", song.root); check_parts("root by key", 1);
    host_hold(B_ARP, 400, white(TY_PAD)); CHECK(trk[2].type == TY_PAD && trk_role(2) == ROLE_CHORD, "ARP + key: PAD");
    check_parts("type PAD", 1);
    host_hold(B_ARP, 400, white(TY_PERC)); CHECK(trk[2].type == TY_PERC && IS_DRUM(2), "a melodic track to PERC");
    check_parts("type PERC", 1);
    host_run(1000, meter, 0);
    for (i = 0; i < NVOICE; i++) CHECK(!voice[i].active || voice[i].trk != 2 || !voice[i].gate, "PERC: no synth voice");
    shift_on(); turn(EN_ALGO, -1); shift_off();
    CHECK(trk[2].type == TY_SNARE, "SHIFT + ALGO: the type before (%s)", TYPE_NAME[trk[2].type]);
    host_hold(B_ARP, 400, white(TY_LEAD)); check_parts("back to LEAD", 1);
    /* SHIFT + PRESETS: the engine; SHIFT + SELECT: swing; SHIFT + OCT: transpose */
    song.octave = 0;
    { uint32_t e = trk[2].s.engine, sw = song.swing, pg0 = ui.page;
      shift_on(); turn(EN_PRESET, 1); turn(EN_SELECT, 3);
      h_pressed |= 1u << B_OCTUP; h_held |= 1u << B_OCTUP; host_run(30, 0, 0); h_held &= ~(1u << B_OCTUP); host_run(30, 0, 0);
      shift_off();
      CHECK(trk[2].s.engine != e, "SHIFT + PRESETS: engine");
      CHECK(song.swing == sw + 3, "SHIFT + SELECT: swing %u", song.swing);
      CHECK(song.transpose == 1, "SHIFT + OCT+: transpose %d", song.transpose);
      CHECK(song.octave == 0, "SHIFT + OCT+: not the octave");
      CHECK(ui.page == pg0, "(no page change: %u)", ui.page);
      song.transpose = 0; }
    /* undo / redo over several levels: ONE CLICKs back and forth */
    { static track_t a[4][NTRK]; uint32_t k;
      host_tap(B_HOME);
      for (k = 0; k < 4; k++) { memcpy(a[k], trk, sizeof trk); host_hold(B_REC, 400, 0); }
      for (k = 4; k-- > 0;) { host_hold(B_SAVE, 400, 0); CHECK(!memcmp(a[k][1].step, trk[1].step, sizeof a[k][1].step), "undo %u", k); }
      shift_on(); host_tap(B_SAVE); shift_off();
      CHECK(!memcmp(a[1][1].step, trk[1].step, sizeof a[1][1].step), "SHIFT + SAVE: redo");
      CHECK(ui.page == PG_HOME, "redo: no page (%u)", ui.page); }
    /* PRESETS: back returns the sound before */
    { patch_t p0 = trk[0].s; song.sel = 0; turn(EN_PRESET, 1); turn(EN_PRESET, -1);
      CHECK(!memcmp(&p0, &trk[0].s, sizeof p0), "PRESETS back: the sound before"); }
    /* the keys play the selected track */
    song.sel = 2; h_keys = 1u << 7; host_run(100, 0, 0);
    { int on = 0; for (i = 0; i < NVOICE; i++) on |= voice[i].active && voice[i].trk == 2 && voice[i].gate; CHECK(on, "key note"); }
    h_keys = 0; host_run(100, 0, 0);
    /* EUCLID: HITS on a melodic track and on a drum lane */
    song.sel = 1; open_page(PG_SEQ, SQ_EUCLID); trk[1].len = 16;
    turn(EN_K2, 5 - (int32_t)trk[1].eu_hits);
    CHECK(trk[1].eu_hits == 5 && notes_in(&trk[1]) == 5, "EUCLID 5: %u notes", notes_in(&trk[1]));
    turn(EN_K2, 2); CHECK(notes_in(&trk[1]) == 7, "EUCLID 7: %u", notes_in(&trk[1]));
    check_parts("euclid", 1);
    song.sel = 3; open_page(PG_SEQ, SQ_EUCLID); trk[3].len = 16;
    turn(EN_K4, 2 - (int32_t)trk[3].eu_lane); turn(EN_K2, 3 - (int32_t)trk[3].eu_hits);
    { uint32_t n = 0; for (i = 0; i < 16; i++) n += (trk[3].step[i].hit >> 2) & 1u; CHECK(n == 3, "EUCLID clap 3: %u", n); }
    /* the step editor: the cursor (PRESETS), a chord of keys written at it (no advance), its columns */
    seq_play(0); host_run(50, 0, 0);
    song.sel = 1; open_page(PG_SEQ, SQ_STEP); ui.cur = 0;
    turn(EN_PRESET, 3); CHECK(ui.cur == 3, "cursor %u", ui.cur);
    h_keys = 1u << 7; host_run(40, 0, 0); h_keys |= 1u << 11; host_run(40, 0, 0); h_keys = 0; host_run(40, 0, 0);
    { const step_t *s = &trk[1].step[3];
      CHECK(s->time == ST_NOTE && s->n == 2 && s->note[0] == key_note(7) && s->note[1] == key_note(11), "chord at the cursor: n %u", s->n);
      CHECK(ui.cur == 3, "STEP page, not armed: no advance (%u)", ui.cur); }
    turn(EN_K1, 1); turn(EN_K1, 1); CHECK(trk[1].step[3].note[0] == key_note(7) + 2 && trk[1].step[3].note[1] == key_note(11) + 2, "NOTE moves the chord");
    turn(EN_K3, 2); CHECK(trk[1].step[4].time == ST_TIE && trk[1].step[5].time == ST_TIE, "LENGTH 3: ties");
    turn(EN_K3, -2); CHECK(trk[1].step[4].time == ST_REST, "LENGTH 1: the ties gone");
    shift_on(); turn(EN_K3, 1); shift_off();
    CHECK(trk[1].step[3].n == 3, "CHORD triad: n %u", trk[1].step[3].n);
    turn(EN_K4, -1); CHECK(trk[1].step[3].time == ST_REST, "STEP type: REST");
    /* SHIFT + black keys: NOTE < > moves the cursor's note with its ties past its neighbour; TAP TEMPO; CLICK */
    { step_t *st = trk[1].step; uint32_t k;
      gen_lock(); memset(st, 0, sizeof trk[1].step); trk[1].len = 16;
      st[4].time = ST_NOTE; st[4].n = 1; st[4].note[0] = 60; st[4].vel = 90; st[5].time = ST_TIE;     /* C4, 2 steps */
      st[6].time = ST_NOTE; st[6].n = 1; st[6].note[0] = 64; st[6].vel = 90;                           /* E4, 1 step */
      gen_unlock(); ui.cur = 4;
      shift_on(); keys_tap(black(5 + 1), 40); shift_off();
      CHECK(st[4].time == ST_NOTE && st[4].note[0] == 64 && st[5].time == ST_NOTE && st[5].note[0] == 60 && st[6].time == ST_TIE,
            "NOTE >: E4 C4~ (%u %u %u)", st[4].time, st[5].time, st[6].time);
      CHECK(ui.cur == 5, "cursor follows (%u)", ui.cur);
      shift_on(); keys_tap(black(5), 40); keys_tap(black(5), 40); shift_off();
      CHECK(st[3].time == ST_NOTE && st[3].note[0] == 60 && st[4].time == ST_TIE && st[5].time == ST_REST && st[6].note[0] == 64,
            "NOTE < <: C4~ at 4, E4 at 6");
      CHECK(ui.page == PG_SEQ, "SHIFT used: no mixer");
      shift_on(); keys_tap(black(7), 40); shift_off();      /* STEP: INSERT at the cursor */
      CHECK(st[3].time == ST_REST && st[4].time == ST_NOTE && st[4].note[0] == 60, "INSERT");
      shift_on(); keys_tap(black(8), 40); shift_off();      /* DELETE */
      CHECK(st[3].time == ST_NOTE && st[3].note[0] == 60, "DELETE");
      { uint8_t r = song.root; open_page(PG_SCL, 0); shift_on(); keys_tap(black(6), 40); shift_off();   /* SCL: ROOT + */
        CHECK(song.root == (r + 1u) % 12u, "SCL: ROOT +"); }
      { patch_t p0 = trk[1].s; open_page(PG_ENV, 0); shift_on(); keys_tap(black(2), 40); shift_off();  /* sound: COPY SD */
        gen_sound(&trk[1]); shift_on(); keys_tap(black(3), 40); shift_off();
        CHECK(!memcmp(&p0, &trk[1].s, sizeof p0), "COPY SD / PASTE"); }
      open_page(PG_HOME, 0);
      shift_on(); for (k = 0; k < 4; k++) { keys_tap(black(10), 40); host_run(430, 0, 0); } shift_off();
      CHECK(song.bpm >= 115 && song.bpm <= 125, "TAP TEMPO: %u BPM", song.bpm);
      { uint8_t c0 = song.click; shift_on(); keys_tap(black(9), 40); shift_off(); CHECK(song.click != c0, "CLICK toggles");
        shift_on(); keys_tap(black(9), 40); shift_off(); }
      shift_on(); host_run(800, 0, 0); CHECK(shift_map(), "SHIFT held: the key map"); shift_off(); }
    /* step recording: armed while stopped, a key writes and the cursor moves on */
    open_page(PG_HOME, 0); ui.cur = 0;
    host_tap(B_REC); CHECK(song.rec, "REC tap arms");
    for (i = 0; i < 4; i++) keys_tap(1u << (i * 2), 60);
    CHECK(ui.cur == 4, "step record: cursor %u", ui.cur);
    for (i = 0; i < 4; i++) CHECK(trk[1].step[i].time == ST_NOTE && trk[1].step[i].note[0] == key_note(i * 2), "step %u", i);
    turn(EN_PRESET, 1); keys_tap(1u << 4, 60);
    CHECK(ui.cur == 6 && trk[1].step[5].note[0] == key_note(4), "PRESETS skips a step");
    host_tap(B_REC); CHECK(!song.rec, "REC tap disarms");
    /* drum grid: white keys toggle the lane's steps, black keys pick the lane */
    song.sel = 3; open_page(PG_SEQ, SQ_STEP); ui.cur = 0;
    keys_tap(black(1), 40); CHECK(ui.lane == 1, "black key: lane SNARE");
    { uint32_t was = (trk[3].step[6].hit >> 1) & 1u; keys_tap(white(6), 40);
      CHECK(((trk[3].step[6].hit >> 1) & 1u) != was, "white key toggles step 7");
      keys_tap(white(6), 40); CHECK(((trk[3].step[6].hit >> 1) & 1u) == was, "and back"); }
    /* live recording while playing: OVERDUB adds, REPLACE clears the pass */
    song.sel = 1; open_page(PG_HOME, 0);
    gen_lock(); memset(trk[1].step, 0, sizeof trk[1].step); trk[1].len = 16; gen_unlock();
    host_tap(B_PLAY); host_tap(B_REC);
    CHECK(song.rec && song.playing, "armed while playing");
    { uint32_t w0 = rec.rec_writes; for (i = 0; i < 6; i++) { keys_tap(1u << 9, 90); host_run(150, 0, 0); }
      CHECK(rec.rec_writes >= w0 + 6, "live: %u notes written", rec.rec_writes - w0);
      CHECK(notes_in(&trk[1]) >= 4, "live: %u steps", notes_in(&trk[1])); }
    shift_on(); host_tap(B_REC); shift_off();
    CHECK(song.rec_mode == REC_REPLACE && song.rec, "SHIFT + REC: REPLACE (%u)", song.rec_mode);
    host_run(2 * 16 * 125, 0, 0);                /* two passes without a key: all cleared */
    CHECK(notes_in(&trk[1]) == 0, "REPLACE: %u left", notes_in(&trk[1]));
    host_tap(B_REC); song.rec_mode = REC_OVERDUB;
    /* MIDI IN: the selected track (MENU: MIDI IN SELECTED); a note on a drum track plays its lane */
    song.midi_in = MI_SEL; song.sel = 3; midi_note(0x95, 38, 100); host_run(20, 0, 0);
    CHECK(dvoice[3][LN_SNARE].active, "MIDI 38: the snare");
    song.sel = 1; midi_note(0x90, 64, 90); host_run(20, 0, 0);
    { int on = 0; for (i = 0; i < NVOICE; i++) on |= voice[i].active && voice[i].trk == 1 && voice[i].note == 64; CHECK(on, "MIDI to SEL"); }
    midi_note(0x80, 64, 0); song.midi_in = MI_CH14;
    seq_play(0); host_run(50, 0, 0);
    /* SAVE: a sound into slot 3, a song into slot 2; changes; loaded back */
    { patch_t p0; static track_t t0[NTRK]; uint8_t root;
      song.sel = 1; p0 = trk[1].s; ui.sslot = 0; ui.gslot = 0; ui.sact = 0; ui.sub[PG_SAVE] = 0;
      open_page(PG_HOME, 0); host_tap(B_SAVE); CHECK(ui.page == PG_SAVE && sub_cur(PG_SAVE) == SV_SOUND, "SAVE: SOUND");
      turn(EN_PRESET, 2); CHECK(ui.sslot == 2, "slot %u", ui.sslot);
      host_tap(B_OCTUP); CHECK(bank.s[2].used && !strcmp(ui.msg, "SOUND SAVED"), "sound saved (%s)", ui.msg);
      host_tap(B_SAVE); CHECK(sub_cur(PG_SAVE) == SV_SONG, "SAVE again: SONG");
      turn(EN_K1, 1); memcpy(t0, trk, sizeof trk); root = song.root;
      host_tap(B_OCTUP); CHECK(song_meta[1].used, "song saved (%s)", ui.msg);
      host_hold(B_REC, 400, 0); gen_key();
      turn(EN_K2, 1); host_tap(B_OCTUP); CHECK(!strcmp(ui.msg, "SONG LOADED"), "song loaded (%s)", ui.msg);
      CHECK(song.root == root && !memcmp(t0[2].step, trk[2].step, sizeof t0[2].step) && !memcmp(&t0[0].s, &trk[0].s, sizeof(patch_t)), "song back");
      host_tap(B_SAVE); host_tap(B_SAVE); host_tap(B_SAVE); CHECK(sub_cur(PG_SAVE) == SV_SOUND, "SAVE: SOUND again");
      gen_sound(&trk[1]); CHECK(ui.sact == 1, "LOAD still");
      host_tap(B_OCTUP); CHECK(!memcmp(&p0, &trk[1].s, sizeof p0), "sound back (%s)", ui.msg);
      song.sel = 3; host_tap(B_OCTUP); CHECK(!strcmp(ui.msg, "NOT A DRUM SOUND"), "no bass on drums (%s)", ui.msg);
      h_store_fail = 1; turn(EN_K2, -1); host_tap(B_OCTUP); CHECK(!strcmp(ui.msg, "SAVE FAILED"), "a failing store (%s)", ui.msg);
      h_store_fail = 0; host_tap(B_OCTDN); CHECK(ui.page == PG_HOME, "OCT-: back"); }
    /* samples: a kick one-shot and a 2-bar loop uploaded as the companion does, assigned to tracks 5 and 6 */
    { static uint8_t kick[4410], loop[2 * 2 * 22050 * 60 / 122]; uint32_t n, lp = sizeof loop, i2, on = 0;
      for (n = 0; n < sizeof kick; n++) kick[n] = ulaw_enc((int32_t)(28000.0 * exp(-n / 900.0) * sin(6.283 * 55.0 * n / 22050.0 * (1.0 + 3.0 * exp(-n / 300.0)))));
      for (n = 0; n < lp; n++) loop[n] = ulaw_enc(n % 2756u < 300u ? (int32_t)(20000.0 * sin(n * 0.3)) : 0);
      CHECK(trk[4].type == TY_KICK && trk[5].type == TY_LOOP, "tracks 5 and 6: KICK, LOOP");
      CHECK(smp_upload(0, SK_ONESHOT, 1, 4, "KICK 909", kick, sizeof kick) == 0, "kick upload");
      CHECK(smp_upload(1, SK_LOOP, 2, 5, "BREAK", loop, lp) == 0, "loop upload");
      CHECK(smp_dir[0].used && smp_dir[1].used && smp_dir[1].bars == 2, "the directory");
      CHECK(trk[4].s.engine == E_SAMPLE && trk[4].s.smp == 0, "kick assigned");
      CHECK(trk[5].s.engine == E_SAMPLE && trk[5].s.smp == 1 && trk[5].len == 32, "loop assigned: 32 slices (%u)", trk[5].len);
      for (i2 = 0; i2 < trk[5].len; i2++) CHECK(trk[5].step[i2].time != ST_NOTE || trk[5].step[i2].note[0] < 32, "slice %u", i2);
      CHECK(smp_frame(0x21, (const uint8_t *)"\0\0\0\0x", 5) == 3, "a chunk with no upload: out of order");
      { uint32_t big = smp_free() + 1u; uint8_t h[23] = {2, 0, 60, 1, 0x22, 0x56, (uint8_t)big, (uint8_t)(big >> 8), (uint8_t)(big >> 16), 0};
        CHECK(smp_frame(0x20, h, 23) == 1, "too big: no space"); }
      peak = 0; seq_play(1); host_run(50, 0, 0);
      for (n = 0; n < 40; n++) { host_run(25, meter, 0); on |= dvoice[5][0].active | dvoice[5][1].active; }
      CHECK(on, "the loop plays its slices"); CHECK(peak > 0.02 && peak < 1.0, "loop audio %.3f", peak);
      seq_play(0); host_run(50, 0, 0);
      smp_load(); CHECK(smp_dir[1].used && !strcmp(smp_dir[1].name, "BREAK"), "the directory kept");
      ui.sub[PG_SAVE] = SV_SAMPLES; ui.page = PG_SAVE; ui.mslot = 1; ui.mact = 1; song.sel = 5;
      host_tap(B_OCTUP); CHECK(!smp_dir[1].used, "SAVE > SAMPLES: DELETE (%s)", ui.msg);
      CHECK(smp_upload(1, SK_LOOP, 2, 0x7F, "BREAK2", loop, lp) == 0 && smp_dir[1].off == trk[0].len * 0 + smp_dir[1].off, "re-upload");
      ui.mact = 0; host_tap(B_OCTUP); CHECK(!strcmp(ui.msg, "ASSIGNED") && trk[5].s.smp == 1, "SAVE > SAMPLES: ASSIGN (%s)", ui.msg);
      song.sel = 1; host_tap(B_OCTUP); CHECK(!strcmp(ui.msg, "LOOPS: LOOP TRACK"), "a loop not on a synth track");
      ui.page = PG_HOME; }
    /* the MENU (HOME held): a row, its value, kept in the store */
    host_hold(B_HOME, 400, 0); CHECK(ui.page == PG_MENU, "HOME held: MENU");
    turn(EN_PRESET, 4); CHECK(MENU_ROWS[ui.menu_row] == P_THEME, "row THEME");
    turn(EN_K1, 3); CHECK(song.theme == PAL_AMBER + 3, "theme %u", song.theme);
    turn(EN_PRESET, -2); turn(EN_K1, 1); CHECK(song.click == CLICK_REC, "click REC");
    host_tap(B_OCTDN); CHECK(ui.page != PG_MENU, "OCT-: out of the menu");
    song.theme = 0; song.click = 0; store_scan(); CHECK(song.theme == PAL_AMBER + 3 && song.click == CLICK_REC, "settings kept");
    host_tap(B_PLAY); song.rec = 1; host_run(600, meter, 0); song.rec = 0; host_tap(B_PLAY);   /* (the click) */
    /* the companion protocol */
    { uint8_t f[8] = {0x7D, 'O', 'C', 0x10}; CHECK(proto_frame(f, 4), "dump"); CHECK(h_sent_n > 2000 && h_sent[4] == 0x11, "dump %u", h_sent_n); }
    { uint8_t f[4] = {0x7D, 'X', 'C', 1}; CHECK(!proto_frame(f, 4), "foreign frame taken"); }
    /* UBOOT countdown: both OCT held 5 s */
    h_held = 3u << B_OCTDN; h_pressed = 3u << B_OCTDN; host_run(5200, 0, 0); CHECK(ui.uboot == 9, "uboot %u", ui.uboot);
    h_held = 0; host_frame(); CHECK(ui.uboot == 0, "uboot cancel");
    printf("test_oc: %s, %d failures\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}

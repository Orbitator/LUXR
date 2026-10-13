/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the panel and the screen.
 *   REC   ONE CLICK: a new song (key, chords, four sounds and parts)
 *   ARP   the selected track again (sound and part)   EDIT  its sound only   SEQ  its part only
 *   SCL   a new key and chords (the melodic parts follow, the sounds stay)
 *   HOME  undo the last generation (again: redo)       GLO   mute / unmute the track
 *   PLAY  play / stop      FX  swing 0 / 12 / 25 %      OCT- OCT+  the keys' octave (both held 5 s: UBOOT)
 *   SELECT tempo   ALGORITHM the track   KNOB 1..4 the track's CUTOFF RESONANCE DECAY LEVEL (live, not saved)
 *   keys: play the selected track (DRUMS: the white keys F3..C4 are the kit's lanes)
 * The screen is drawn in bands (BAND_H rows) into one buffer and sent only where something changed (a signature
 * per region). Platform hooks (oc_main.c on the FM-1, host/ for the tests): io_pressed io_held io_keys io_enc
 * io_led_button io_led_key io_blit. */

static uint32_t io_pressed(void);                /* B_* labels pressed since the last call */
static uint32_t io_held(void);                   /* B_* labels held */
static uint32_t io_keys(void);                   /* note keys held, bit k = key k (0 = F3 .. 26 = G5) */
static int32_t io_enc(uint32_t role);            /* detents since the last call, + clockwise */
static void io_led_button(uint32_t b, int on);
static void io_led_key(uint32_t k, int on);
static void io_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);

/* -------------------------------------------------------------- drawing --- */
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BG RGB(14, 14, 18)
#define C_SURF RGB(34, 34, 42)
#define C_SEL RGB(52, 48, 70)
#define C_TEXT RGB(232, 230, 240)
#define C_DIM RGB(120, 118, 135)
#define C_ACC RGB(255, 170, 40)
#define C_HOT RGB(255, 70, 90)
static const uint16_t ROLE_COL[NTRK] = {RGB(90, 200, 255), RGB(170, 130, 255), RGB(120, 230, 140), RGB(255, 170, 40)};

#define BAND_H 40u
static uint16_t band[240u * BAND_H] __attribute__((section(".pool")));
static uint32_t band_h;
static inline uint16_t px_swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }   /* the panel: big endian */
static void b_begin(uint32_t h, uint16_t bg)
{
    uint32_t i;
    band_h = h;
    for (i = 0; i < 240u * h; i++)
        band[i] = px_swap(bg);
}
static void b_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    uint16_t s = px_swap(c);
    for (j = y < 0 ? 0 : y; j < y + h && j < (int32_t)band_h; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < 240; i++)
            band[j * 240 + i] = s;
}
static int32_t b_text(int32_t x, int32_t y, const char *str, uint32_t sc, uint16_t c)
{
    uint16_t s = px_swap(c);
    for (; *str; str++) {
        uint32_t ch = (uint8_t)*str, col, row, a, b;
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        ch = ch < 32u || ch > 95u ? 0u : ch - 32u;
        for (col = 0; col < 5u; col++)
            for (row = 0; row < 7u; row++)
                if ((FONT5[ch][col] >> row) & 1u)
                    for (a = 0; a < sc; a++)
                        for (b = 0; b < sc; b++) {
                            int32_t px = x + (int32_t)(col * sc + a), py = y + (int32_t)(row * sc + b);
                            if (px >= 0 && px < 240 && py >= 0 && py < (int32_t)band_h)
                                band[py * 240 + px] = s;
                        }
        x += (int32_t)(6u * sc);
    }
    return x;
}
static int32_t text_w(const char *s, uint32_t sc) { uint32_t n = 0; while (s[n]) n++; return (int32_t)(n * 6u * sc) - (int32_t)sc; }
static void b_send(uint32_t y) { io_blit(0, y, 240, band_h, band); }

static void fmt_u(char *b, uint32_t v)            /* decimal */
{
    char t[12];
    uint32_t n = 0, i;
    do t[n++] = (char)('0' + v % 10u); while ((v /= 10u) && n < 11u);
    for (i = 0; i < n; i++)
        b[i] = t[n - 1u - i];
    b[n] = 0;
}
static void cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)) ; }

/* ------------------------------------------------------------------ ui --- */
static struct {
    char msg[28];
    uint32_t msg_t;
    uint32_t sig[8];
    uint32_t keys;               /* the note keys last seen */
    uint8_t keynote[27];         /* the note each held key started */
    uint8_t has_undo, uboot;
    uint32_t both_t;
} ui;
static struct { track_t t[NTRK]; uint8_t root, scale, prog[4]; } undo_buf __attribute__((section(".pool")));

static void ui_say(const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < sizeof ui.msg - 1u; i++)
        ui.msg[i] = s[i];
    ui.msg[i] = 0;
    ui.msg_t = fm1_ms;
}
static void undo_take(void)
{
    gen_lock();
    memcpy(undo_buf.t, trk, sizeof trk);
    gen_unlock();
    undo_buf.root = song.root;
    undo_buf.scale = song.scale;
    memcpy(undo_buf.prog, song.prog, 4);
    ui.has_undo = 1;
}
static void undo_swap(void)
{
    static track_t tmp[NTRK];
    uint8_t r = song.root, s = song.scale, p[4];
    uint32_t i;
    memcpy(p, song.prog, 4);
    memcpy(tmp, undo_buf.t, sizeof tmp);
    gen_lock();
    memcpy(undo_buf.t, trk, sizeof trk);
    for (i = 0; i < NTRK; i++) {
        track_panic(i);
        tmp[i].nsnd = 0;
        tmp[i].off_at = 0;
        tmp[i].hold = 0;
    }
    memcpy(trk, tmp, sizeof trk);
    gen_unlock();
    song.root = undo_buf.root;
    song.scale = undo_buf.scale;
    memcpy(song.prog, undo_buf.prog, 4);
    undo_buf.root = r;
    undo_buf.scale = s;
    memcpy(undo_buf.prog, p, 4);
}
static void say_key(const char *pre)
{
    char b[28];
    b[0] = 0;
    cat(b, pre);
    cat(b, NOTE_NAME[song.root % 12u]);
    cat(b, " ");
    cat(b, SCALE_NAME[song.scale % SC_N]);
    ui_say(b);
}

static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t white_index(uint32_t k)          /* white key k's place 0..15 */
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += !key_black(i);
    return n;
}

static void ui_input(void)
{
    uint32_t p = io_pressed(), held = io_held(), keys = io_keys(), k, t = song.sel;
    int32_t e;
    if (p) {
        static uint8_t seeded;
        if (!seeded) {                            /* the first press seeds the dice (its moment is random enough) */
            seeded = 1;
            rng_state ^= fm1_ms * 2654435761u + 1u;
        }
    }
    if (p & (1u << B_PLAY))
        seq_play(!song.playing);
    if (p & (1u << B_REC)) {
        undo_take();
        gen_all();
        say_key("ONE CLICK: ");
    }
    if (p & (1u << B_ARP)) {
        undo_take();
        gen_track(&trk[t]);
        ui_say("NEW TRACK");
    }
    if (p & (1u << B_EDIT)) {
        undo_take();
        gen_sound(&trk[t]);
        ui_say(ENGINE_NAME[trk[t].s.engine % E_N]);
    }
    if (p & (1u << B_SEQ)) {
        undo_take();
        gen_pattern(&trk[t]);
        ui_say("NEW PART");
    }
    if (p & (1u << B_SCL)) {
        undo_take();
        gen_key();
        say_key("NEW KEY: ");
    }
    if (p & (1u << B_HOME)) {
        if (ui.has_undo) {
            undo_swap();
            ui_say("UNDO / REDO");
        } else {
            ui_say("NOTHING TO UNDO");
        }
    }
    if (p & (1u << B_GLO)) {
        trk[t].mute ^= 1u;
        ui_say(trk[t].mute ? "MUTED" : "UNMUTED");
    }
    if (p & (1u << B_FX)) {
        song.swing = (uint8_t)(song.swing >= 25u ? 0u : song.swing + 12u + (song.swing ? 1u : 0u));
        ui_say(song.swing ? (song.swing > 12u ? "SWING 25%" : "SWING 12%") : "SWING OFF");
    }
    if (p & (1u << B_SAVE))
        ui_say("SAVE: NOT YET");
    if ((p & (1u << B_OCTDN)) && !(held & (1u << B_OCTUP)) && song.octave > -2)
        song.octave--;
    if ((p & (1u << B_OCTUP)) && !(held & (1u << B_OCTDN)) && song.octave < 2)
        song.octave++;
    if ((held & (3u << B_OCTDN)) == (3u << B_OCTDN)) {   /* both held: UBOOT after 5 s (oc_main.c acts) */
        uint32_t s = fm1_ms - ui.both_t;
        ui.uboot = (uint8_t)(s > 2000u ? (s >= 5000u ? 9u : 1u + (5000u - s) / 1000u) : 0u);
    } else {
        if (ui.uboot && ui.uboot < 9u)
            ui_say("UPDATE CANCELLED");
        ui.uboot = 0;
        ui.both_t = fm1_ms;
    }

    if ((e = io_enc(EN_SELECT)) != 0)
        song.bpm = (uint8_t)clamp(song.bpm + e, 60, 200);
    if ((e = io_enc(EN_ALGO)) != 0)
        song.sel = (uint8_t)clamp((int32_t)song.sel + e, 0, NTRK - 1);
    if ((e = io_enc(EN_PRESET)) != 0)
        song.sel = (uint8_t)clamp((int32_t)song.sel + e, 0, NTRK - 1);
    t = song.sel;
    if ((e = io_enc(EN_K1)) != 0)
        trk[t].k_cut = (int8_t)clamp(trk[t].k_cut + e * 2, -64, 63);
    if ((e = io_enc(EN_K2)) != 0)
        trk[t].k_res = (int8_t)clamp(trk[t].k_res + e * 2, -64, 63);
    if ((e = io_enc(EN_K3)) != 0)
        trk[t].k_dec = (int8_t)clamp(trk[t].k_dec + e * 2, -40, 40);
    if ((e = io_enc(EN_K4)) != 0)
        trk[t].k_lvl = (uint8_t)clamp(trk[t].k_lvl + e, 0, 16);

    for (k = 0; k < 27u; k++) {                   /* the keys: the selected track */
        uint32_t now = (keys >> k) & 1u, was = (ui.keys >> k) & 1u;
        if (now == was)
            continue;
        if (trk[t].s.engine == E_DRUM) {
            if (now && !key_black(k) && white_index(k) < NLANE)
                ev_push(EV_ON, t, 1u << white_index(k), 110);
            continue;
        }
        if (now) {
            uint32_t n = (uint32_t)clamp(53 + (int32_t)k + 12 * song.octave, 12, 120);
            ui.keynote[k] = (uint8_t)n;
            ev_push(EV_ON, t, n, 100);
        } else {
            ev_push(EV_OFF, t, ui.keynote[k], 0);
        }
    }
    ui.keys = keys;
}

static void ui_leds(void)
{
    uint32_t k, bar = song.step % 16u;
    io_led_button(B_PLAY, song.playing);
    io_led_button(B_REC, 1);
    io_led_button(B_GLO, trk[song.sel].mute);
    io_led_button(B_HOME, ui.has_undo);
    for (k = 0; k < 27u; k++)                     /* the white keys: the bar's 16th now */
        io_led_key(k, !key_black(k) && song.playing && white_index(k) == bar);
}

/* the screen: header, key line, four tracks, footer */
static void draw_track(uint32_t i, uint32_t y)
{
    const track_t *t = &trk[i];
    uint32_t sel = i == song.sel, len = t->len ? t->len : 16u, cur = song.step % len, base = cur / 16u * 16u, s;
    char b[24];
    b_begin(BAND_H, C_BG);
    b_rect(4, 1, 232, 38, sel ? C_SEL : C_SURF);
    b_rect(4, 1, 4, 38, ROLE_COL[i]);
    b_text(14, 5, ROLE_NAME[i], 2, t->mute ? C_DIM : C_TEXT);
    b[0] = 0;
    cat(b, ENGINE_NAME[t->s.engine % E_N]);
    b_text(236 - text_w(b, 1) - 4, 7, b, 1, t->mute ? C_DIM : ROLE_COL[i]);
    for (s = 0; s < 16u; s++) {                   /* the bar playing: its steps */
        const step_t *st = &t->step[(base + s) % NSTEP];
        int32_t x = 14 + (int32_t)s * 13;
        uint16_t c = st->time == ST_NOTE ? ROLE_COL[i] : st->time == ST_TIE ? C_DIM : C_BG;
        if (song.playing && s == cur % 16u)
            c = st->time == ST_NOTE ? C_TEXT : C_ACC;
        b_rect(x, 25, 11, 10, c);
    }
}
static uint32_t track_sig(uint32_t i)
{
    const track_t *t = &trk[i];
    uint32_t h = t->s.engine * 7u + t->mute * 13u + (i == song.sel) * 31u + t->len, s;
    uint32_t len = t->len ? t->len : 16u, cur = song.step % len;
    for (s = 0; s < 16u; s++)
        h = h * 33u + t->step[(cur / 16u * 16u + s) % NSTEP].time;
    return h * 2654435761u + (song.playing ? cur % 16u + 1u : 0u);
}

static void ui_draw(int force)
{
    uint32_t i, sg;
    char b[32], n[8];
    if (ui.uboot) {                               /* the UBOOT countdown over everything */
        sg = 0xB00700u + ui.uboot;
        if (force || ui.sig[0] != sg) {
            ui.sig[0] = sg;
            for (i = 0; i < 6u; i++) {
                b_begin(BAND_H, C_BG);
                if (i == 2u) {
                    b[0] = 0;
                    cat(b, ui.uboot >= 9u ? "UBOOT" : "UPDATE MODE IN ");
                    if (ui.uboot < 9u) {
                        fmt_u(n, ui.uboot);
                        cat(b, n);
                    }
                    b_text(120 - text_w(b, 2) / 2, 12, b, 2, C_HOT);
                }
                b_send(i * BAND_H);
            }
            for (i = 1; i < 8u; i++)
                ui.sig[i] = 0;
        }
        return;
    }
    /* header */
    sg = song.playing * 1000003u + song.bpm * 31u + song.swing + 7u;
    if (force || ui.sig[0] != sg) {
        ui.sig[0] = sg;
        b_begin(BAND_H, C_BG);
        b_text(8, 10, "ONE CLICK", 3, C_ACC);
        fmt_u(b, song.bpm);
        b_text(232 - text_w(b, 2), 6, b, 2, C_TEXT);
        b_text(232 - text_w(song.playing ? "PLAY" : "STOP", 1), 26, song.playing ? "PLAY" : "STOP", 1,
               song.playing ? C_ACC : C_DIM);
        b_send(0);
    }
    /* key line */
    sg = song.root * 131u + song.scale * 7u + song.prog[0] + song.prog[1] * 8u + song.prog[2] * 64u + song.prog[3] * 512u +
         (uint32_t)(song.octave + 3) * 99991u;
    if (force || ui.sig[1] != sg) {
        static const char *const RN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        ui.sig[1] = sg;
        b_begin(22, C_BG);
        b[0] = 0;
        cat(b, NOTE_NAME[song.root % 12u]);
        cat(b, " ");
        cat(b, SCALE_NAME[song.scale % SC_N]);
        b_text(8, 4, b, 2, C_TEXT);
        b[0] = 0;
        for (i = 0; i < 4u; i++) {
            cat(b, RN[song.prog[i] % 7u]);
            if (i < 3u)
                cat(b, "-");
        }
        b_text(232 - text_w(b, 1), 8, b, 1, C_DIM);
        b_send(BAND_H);
    }
    for (i = 0; i < NTRK; i++) {
        sg = track_sig(i);
        if (force || ui.sig[2u + i] != sg) {
            ui.sig[2u + i] = sg;
            draw_track(i, 0);
            b_send(62u + i * BAND_H);
        }
    }
    /* footer: the last message for 2 s, else the knob values */
    if (fm1_ms - ui.msg_t < 2000u && ui.msg[0]) {
        b[0] = 0;
        cat(b, ui.msg);
        sg = 0x5A5A0000u ^ (ui.msg_t * 7u);
    } else {
        const track_t *t = &trk[song.sel];
        b[0] = 0;
        cat(b, "CUT ");
        fmt_u(n, (uint32_t)(t->k_cut + 64));
        cat(b, n);
        cat(b, " RES ");
        fmt_u(n, (uint32_t)(t->k_res + 64));
        cat(b, n);
        cat(b, " LVL ");
        fmt_u(n, t->k_lvl);
        cat(b, n);
        sg = (uint32_t)(t->k_cut + 64) * 7u + (uint32_t)(t->k_res + 64) * 1031u + t->k_lvl * 77777u + song.sel * 3u;
    }
    if (force || ui.sig[6] != sg) {
        ui.sig[6] = sg;
        b_begin(18, C_BG);
        b_text(120 - text_w(b, 1) / 2, 6, b, 1, fm1_ms - ui.msg_t < 2000u ? C_ACC : C_DIM);
        b_send(222);
    }
}

static void ui_init(void)
{
    uint32_t i;
    song.bpm = 122;
    song.sel = 0;
    song.master_q12 = 2048;
    for (i = 0; i < NTRK; i++) {
        trk[i].k_lvl = 8;
        trk[i].gate = 60;
        trk[i].len = 16;
    }
    gen_all();                                    /* a song to start with */
    ui.has_undo = 0;
}

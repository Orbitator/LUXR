/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: the companion protocol (the browser app over Web MIDI; the same frames could ride BLE-MIDI later).
 * SysEx, manufacturer id 7D (non-commercial), then 'O' 'C', a command, its data (8-bit data packed 7 in 8):
 *   01           ONE CLICK (a new song)          02 t   a new track t (sound and part)
 *   03 t         a new sound on t                04 t   a new part on t
 *   05           a new key                       06 p   play (1) / stop (0)
 *   07 b         tempo (b + 60)                  08     undo / redo
 *   10           dump: the device answers 11 + the packed state: 2 (the version), sizeof patch_t, sizeof step_t,
 *                the tracks (6), root, scale, prog[4], bpm, playing; then each track: patch_t (engine first), LEN, GATE,
 *                64 x step_t (step_t: note[4] n time flags vel hit acc); then each track's TYPE and OCTAVE, the song's
 *                TRANSPOSE, SWING
 *   20 + packed  sample upload begin: slot, kind (0 one-shot, 1 loop), root, bars, rate (2, LE), length (4, LE),
 *                the track to give it to (0x7F none), name (12)
 *   21 + packed  a chunk: its offset (4, LE), up to 256 bytes (8-bit mu-law), in order
 *   22           commit (the directory written; the track assigned)       23 s   clear slot s (0x7F: all)
 *   24           list: the device answers 25 + packed: free bytes (4), the area (4), 8 x (used kind root bars
 *                rate(2) length(4) name(12))
 *                20 .. 23 are answered 2F cmd code (0 ok, 1 no space, 2 bad request, 3 out of order, 4 flash): the
 *                companion sends the next chunk after it
 *   12 t h + packed  steps of t, half h (0: steps 1..32, 1: 33..64): LEN, GATE, 32 steps (a frame stays < 640 B)   13 t + packed  the sound of t   14 + packed  root scale prog[4]
 * proto_frame(p, n): one frame without F0 / F7; replies through proto_send. Unknown frames are left alone. */

static int proto_send(const uint8_t *p, uint32_t n);   /* a whole F0..F7 message (platform) */

/* 8-bit bytes <-> 7-bit groups: each 7 bytes become a byte of their high bits and the 7 low halves */
static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t i, o = 0;
    for (i = 0; i < n; i += 7u) {
        uint32_t k, hi = 0, m = n - i < 7u ? n - i : 7u, h = o++;
        for (k = 0; k < m; k++) {
            hi |= ((in[i + k] >> 7) & 1u) << k;
            out[o++] = in[i + k] & 0x7Fu;
        }
        out[h] = (uint8_t)hi;
    }
    return o;
}
static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t hi = in[i++], k;
        for (k = 0; k < 7u && i < n && o < max; k++)
            out[o++] = (uint8_t)(in[i++] | ((hi >> k) & 1u) << 7);
    }
    return o;
}

#define TRK_WIRE (sizeof(patch_t) + 2u + NSTEP * sizeof(step_t))
static uint8_t px_raw[12u + NTRK * TRK_WIRE + 2u * NTRK + 2u] __attribute__((section(".pool")));
static uint8_t px_wire[sizeof px_raw * 8u / 7u + 16u] __attribute__((section(".pool")));

static void proto_dump(void)
{
    uint32_t i, n = 0, w;
    px_raw[n++] = 2;                              /* the dump's version, then the sizes it uses */
    px_raw[n++] = (uint8_t)sizeof(patch_t);
    px_raw[n++] = (uint8_t)sizeof(step_t);
    px_raw[n++] = NTRK;
    px_raw[n++] = song.root;
    px_raw[n++] = song.scale;
    memcpy(px_raw + n, song.prog, 4);
    n += 4;
    px_raw[n++] = song.bpm;
    px_raw[n++] = song.playing;
    for (i = 0; i < NTRK; i++) {
        memcpy(px_raw + n, &trk[i].s, sizeof(patch_t));
        n += sizeof(patch_t);
        px_raw[n++] = trk[i].len;
        px_raw[n++] = trk[i].gate;
        memcpy(px_raw + n, trk[i].step, sizeof trk[i].step);
        n += sizeof trk[i].step;
    }
    for (i = 0; i < NTRK; i++)
        px_raw[n++] = trk[i].type;
    for (i = 0; i < NTRK; i++)
        px_raw[n++] = (uint8_t)trk[i].octave;
    px_raw[n++] = (uint8_t)song.transpose;
    px_raw[n++] = song.swing;
    px_wire[0] = 0xF0;
    px_wire[1] = 0x7D;
    px_wire[2] = 'O';
    px_wire[3] = 'C';
    px_wire[4] = 0x11;
    w = 5u + pack7(px_raw, n, px_wire + 5);
    px_wire[w++] = 0xF7;
    (void)proto_send(px_wire, w);
}

static void ui_say(const char *s);
static void undo_take(void);
static void undo_do(int redo);
static void proto_status(uint32_t cmd, int rc)    /* 2F cmd code */
{
    uint8_t f[8] = {0xF0, 0x7D, 'O', 'C', 0x2F, (uint8_t)(cmd & 0x7Fu), (uint8_t)(rc < 0 ? -rc : 0), 0xF7};
    (void)proto_send(f, sizeof f);
}
static uint32_t le32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void proto_smp_list(void)
{
    uint32_t n = 0, k, w, fr = smp_free(), ar = SMP_AREA;
    for (k = 0; k < 4u; k++) px_raw[n++] = (uint8_t)(fr >> (8u * k));
    for (k = 0; k < 4u; k++) px_raw[n++] = (uint8_t)(ar >> (8u * k));
    for (k = 0; k < NSMP; k++) {
        const smp_t *m = &smp_dir[k];
        uint32_t j;
        px_raw[n++] = m->used;
        px_raw[n++] = m->kind;
        px_raw[n++] = m->root;
        px_raw[n++] = m->bars;
        px_raw[n++] = (uint8_t)m->rate;
        px_raw[n++] = (uint8_t)(m->rate >> 8);
        for (j = 0; j < 4u; j++) px_raw[n++] = (uint8_t)(m->len >> (8u * j));
        memcpy(px_raw + n, m->name, 12);
        n += 12;
    }
    px_wire[0] = 0xF0; px_wire[1] = 0x7D; px_wire[2] = 'O'; px_wire[3] = 'C'; px_wire[4] = 0x25;
    w = 5u + pack7(px_raw, n, px_wire + 5);
    px_wire[w++] = 0xF7;
    (void)proto_send(px_wire, w);
}
/* 1 = the frame was ours */
static int proto_frame(const uint8_t *p, uint32_t n)
{
    uint32_t t, k;
    if (n < 4u || p[0] != 0x7Du || p[1] != 'O' || p[2] != 'C')
        return 0;
    t = n > 4u ? p[4] % NTRK : song.sel;
    switch (p[3]) {
    case 0x01: undo_take(); gen_all(); ui_say("ONE CLICK (APP)"); break;
    case 0x02: undo_take(); gen_track(&trk[t]); break;
    case 0x03: undo_take(); gen_sound(&trk[t]); break;
    case 0x04: undo_take(); gen_pattern(&trk[t]); break;
    case 0x05: undo_take(); gen_key(); break;
    case 0x06: seq_play(n > 4u && p[4]); break;
    case 0x07: if (n > 4u) song.bpm = (uint8_t)clamp(p[4] + 60, 60, 200); break;
    case 0x08: undo_do(n > 4u && p[4]); break;   /* 0: undo, 1: redo */
    case 0x20: {                                  /* sample upload: begin */
        smp_t m;
        memset(&m, 0, sizeof m);
        k = unpack7(p + 4, n - 4u, px_raw, 23);
        if (k < 23u) { proto_status(0x20, -2); break; }
        m.kind = px_raw[1] ? SK_LOOP : SK_ONESHOT;
        m.root = px_raw[2] & 127u;
        m.bars = (uint8_t)clamp(px_raw[3], 1, 4);
        m.rate = (uint16_t)(px_raw[4] | px_raw[5] << 8);
        m.len = le32(px_raw + 6);
        memcpy(m.name, px_raw + 11, 11);
        proto_status(0x20, smp_begin(px_raw[0], &m, px_raw[10] < NTRK ? px_raw[10] : 0xFFu));
        break;
    }
    case 0x21:
        k = unpack7(p + 4, n - 4u, px_raw, 4u + 256u);
        proto_status(0x21, k > 4u ? smp_chunk(le32(px_raw), px_raw + 4, k - 4u) : -2);
        break;
    case 0x22: proto_status(0x22, smp_commit()); break;
    case 0x23: proto_status(0x23, smp_clear(n > 4u && p[4] < NSMP ? (int)p[4] : -1)); break;
    case 0x24: proto_smp_list(); break;
    case 0x10: proto_dump(); break;
    case 0x12: {                                  /* steps of t */
        uint32_t h = n > 5u ? p[5] & 1u : 0u, half = NSTEP / 2u * sizeof(step_t);
        k = n > 6u ? unpack7(p + 6, n - 6u, px_raw, 2u + half) : 0u;
        if (k == 2u + half) {
            gen_lock();
            trk[t].len = (uint8_t)clamp(px_raw[0], 1, NSTEP);
            trk[t].gate = (uint8_t)clamp(px_raw[1], 1, 100);
            memcpy(&trk[t].step[h * NSTEP / 2u], px_raw + 2, half);
            gen_unlock();
        }
        break;
    }
    case 0x13: {                                  /* the sound of t (the engine number checked) */
        k = unpack7(p + 5, n - 5u, px_raw, sizeof(patch_t));
        if (k == sizeof(patch_t) && px_raw[0] < E_N && role_has_engine(trk_role(t), px_raw[0])) {
            gen_lock();
            track_panic(t);
            memcpy(&trk[t].s, px_raw, sizeof(patch_t));
            gen_unlock();
        }
        break;
    }
    case 0x14: {
        k = unpack7(p + 4, n - 4u, px_raw, 6);
        if (k == 6u && px_raw[1] < SC_N) {
            song.root = (uint8_t)(px_raw[0] % 12u);
            song.scale = px_raw[1];
            for (k = 0; k < 4u; k++)
                song.prog[k] = (uint8_t)(px_raw[2u + k] % 7u);
        }
        break;
    }
    default: break;
    }
    return 1;
}

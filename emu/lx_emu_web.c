/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR FM-1 emulator: the firmware's app (lx_*.c: sound, sequencer, UI, store, the backup protocol) compiled to
 * WebAssembly and run against a simulated FM-1, as host/lx_host.c does for the tests:
 *   - time: the audio the AudioWorklet asks for drives the clock; the UI frame runs every 15 ms of it, as the
 *     device's main loop does;
 *   - the panel: buttons by their printed labels (B_*), the 7 encoders, the 27 keys, the MASTER pot, the LEDs;
 *   - the screen: 240 x 240 RGB565 in the panel's byte order, a dirty flag; and the same drawn sharp at HD_X x
 *     (hd_scr, emu/hd.c through build/emu/gfx_hd.c), the rows changed for the page;
 *   - the store: the patch flash in RAM (the page keeps it in the browser); a .luxr backup is imported through
 *     the same staging and conversion the USB restore uses (proto 0x44 / 0x46);
 *   - MIDI: notes / CC in (Web MIDI or the page), MIDI OUT packets out.
 * Not here: the JieLi CPU, the boot / update / UBOOT paths, real USB.   (emu/build_lx.sh) */
#include <stdint.h>
#define OC_HOST 1
#include "oc_tables.h"
#include "lxr_transients.h"
#include "own_transients.h"
#include "machine_transients.h"
#include "libc.c"
#define RING_PUBLISH() ((void)0)
#include "lx_core.h"
#include "ui_hd.h"
static uint16_t e_screen[240 * 240];
static uint32_t e_dirty;
#define HW (240u * HD_X)
static uint16_t hd_scr[HW * HW];                  /* the sharp screen (big endian, as the panel) */
static uint32_t hd_y0 = HW, hd_y1;                /* its rows changed since the page took them */
static void hd_dirty(uint32_t y0, uint32_t y1)
{
    if (y1 > HW) y1 = HW;
    if (y0 < hd_y0) hd_y0 = y0;
    if (y1 > hd_y1) hd_y1 = y1;
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* (big endian, as the panel) */
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            e_screen[(y + j) * 240u + x + i] = px[j * w + i];
    e_dirty = 1;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t s = (uint16_t)((c >> 8) | (c << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            e_screen[j * 240u + i] = s;
    for (j = y * HD_X; j < (y + h) * HD_X && j < HW; j++)
        for (i = x * HD_X; i < (x + w) * HD_X && i < HW; i++)
            hd_scr[j * HW + i] = s;
    hd_dirty(y * HD_X, (y + h) * HD_X);
    e_dirty = 1;
}
#include "gfx_hd.c"
static void midi_out_event(uint32_t pkt);
#include "lx_synth.c"
#include "lx_seq.c"
#include "lx_ui.c"

#define EXPORT __attribute__((visibility("default")))

/* ------------------------------------------------------------ the platform --- */
#define MIQ 256u
static uint32_t miq[MIQ], mi_w, mi_r;
static void midi_in_drain(void)
{
    while (mi_r != mi_w) {
        uint32_t m = miq[mi_r++ % MIQ];
        midi_note(m & 0xFFu, (m >> 8) & 0x7Fu, (m >> 16) & 0x7Fu);
    }
}
#define MOQ 512u
static uint32_t moq[MOQ], mo_n;
static void midi_out_event(uint32_t pkt)
{
    if (mo_n < MOQ)
        moq[mo_n++] = pkt;
}
static uint8_t e_store[0x49000];                  /* the store (oc_main.c: the flash from 0x97000) */
static uint32_t e_store_gen;
static int io_store_read(uint32_t off, void *d, uint32_t n)
{
    if (off + n > sizeof e_store)
        return -1;
    memcpy(d, e_store + off, n);
    return 0;
}
static int io_store_erase(uint32_t off)
{
    if ((off & 0xFFFu) || off + 0x1000u > sizeof e_store)
        return -1;
    memset(e_store + off, 0xFF, 0x1000u);
    e_store_gen++;
    return 0;
}
static int io_store_prog(uint32_t off, const void *s, uint32_t n)   /* (NOR: bits only go 1 -> 0) */
{
    uint32_t i;
    const uint8_t *b = s;
    if (off + n > sizeof e_store)
        return -1;
    for (i = 0; i < n; i++)
        e_store[off + i] &= b[i];
    e_store_gen++;
    return 0;
}
static int32_t e_batt = 612;
static int32_t io_batt(void) { return e_batt; }
static uint8_t e_out[1024];
static uint32_t e_out_n;
static int proto_send(const uint8_t *p, uint32_t n)
{
    if (n > sizeof e_out)
        n = sizeof e_out;
    memcpy(e_out, p, n);
    e_out_n = n;
    return 0;
}
static uint32_t e_pressed, e_held, e_keys;
static int32_t e_enc[NE];
static uint8_t e_led_btn[NB], e_led_key[27];
static uint32_t io_pressed(void) { uint32_t p = e_pressed; e_pressed = 0; return p; }
static uint32_t io_held(void) { return e_held; }
static uint32_t io_keys(void) { return e_keys; }
static int32_t io_enc(uint32_t r) { int32_t s = e_enc[r]; e_enc[r] = 0; return s; }
static void io_led_button(uint32_t b, int on) { e_led_btn[b % NB] = (uint8_t)on; }
static void io_led_key(uint32_t k, int on) { e_led_key[k % 27u] = (uint8_t)on; }

/* ---------------------------------------------------------------- exports --- */
static float e_l[HALF_FRAMES], e_r[HALF_FRAMES];
static uint8_t e_in[2048];
static uint32_t e_samples, e_next_frame;

EXPORT float *emu_out_l(void) { return e_l; }
EXPORT float *emu_out_r(void) { return e_r; }
EXPORT uint16_t *emu_screen(void) { return e_screen; }
EXPORT uint8_t *emu_leds_btn(void) { return e_led_btn; }
EXPORT uint8_t *emu_leds_key(void) { return e_led_key; }
EXPORT uint32_t emu_dirty(void) { uint32_t d = e_dirty; e_dirty = 0; return d; }
EXPORT uint8_t *emu_store(void) { return e_store; }
EXPORT uint32_t emu_store_len(void) { return sizeof e_store; }
EXPORT uint32_t emu_store_gen(void) { return e_store_gen; }
EXPORT uint8_t *emu_in_buf(void) { return e_in; }
EXPORT uint8_t *emu_out_buf(void) { return e_out; }
EXPORT uint32_t emu_out_take(void) { uint32_t n = e_out_n; e_out_n = 0; return n; }
EXPORT uint32_t *emu_mout(void) { return moq; }
EXPORT uint32_t emu_mout_take(void) { uint32_t n = mo_n; mo_n = 0; return n; }

EXPORT void emu_init(uint32_t seed)
{
    rng_state = seed ? seed : 1u;
    ui_init();
    ui_draw(1);
}
EXPORT void emu_buttons(uint32_t held)            /* buttons held (bit = B_*): the presses are the new ones */
{
    e_pressed |= held & ~e_held;
    e_held = held;
}
EXPORT void emu_keys(uint32_t keys) { e_keys = keys; }
EXPORT void emu_enc(uint32_t role, int32_t steps) { if (role < NE) e_enc[role] += steps; }
EXPORT void emu_master(uint32_t pot)              /* 0..1023, the square law of master.c */
{
    uint32_t a = pot > 1023u ? 1023u : pot;
    song.master_q12 = (uint16_t)(a * a >> 8);
}
EXPORT void emu_batt(int32_t raw) { e_batt = raw; }
EXPORT void emu_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (mi_w - mi_r < MIQ)
        miq[mi_w++ % MIQ] = (status & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16;
}
EXPORT int emu_sysex(uint32_t n) { return n <= sizeof e_in ? proto_frame(e_in, n) : 0; }

/* the backup import: one slot's bytes (as a .luxr file holds them) into the stage, converted and stored,
 * as the USB restore does (0x44 / 0x46). 0 ok, 2 not a patch, 4 flash error. */
EXPORT uint8_t *emu_stage(void) { return stage; }
EXPORT uint32_t emu_stage_len(void) { return sizeof stage; }
EXPORT uint32_t emu_nslot(void) { return NSLOT; }
EXPORT uint32_t emu_import(uint32_t slot, uint32_t len)
{
    if (slot >= NSLOT || len > sizeof stage)
        return 2;
    seq_play(0);
    if (proj_convert(stage, len))
        return 2;
    if (store_write(ST_PROJ(slot), &proj_buf, sizeof proj_buf))
        return 4;
    ui.used[slot] = 1;
    return 0;
}
EXPORT void emu_load(uint32_t slot)               /* a patch, kit and patterns (as SHIFT + PRESET) */
{
    if (slot < NSLOT && ui.used[slot])
        proj_load(slot, SW_ALL);
    ui_draw(1);
}

EXPORT void emu_render(uint32_t n)                /* n samples (<= HALF_FRAMES); the UI frames in them first */
{
    int32_t o[2 * CTL];
    uint32_t b, i;
    if (n > HALF_FRAMES)
        n = HALF_FRAMES;
    for (b = 0; b < n; b += CTL) {
        uint32_t m = n - b < CTL ? n - b : CTL;
        if ((int32_t)(e_samples - e_next_frame) >= 0) {
            e_next_frame += 15u * 441u / 10u;      /* 15 ms */
            ui_input();
            ui_leds();
            ui_draw(0);
        }
        mix_block(o, m);
        for (i = 0; i < m; i++) {
            e_l[b + i] = (float)o[2u * i] * (1.0f / 32768.0f);
            e_r[b + i] = (float)o[2u * i + 1u] * (1.0f / 32768.0f);
        }
        e_samples += m;
        fm1_ms = e_samples / 44u;
    }
}
EXPORT uint32_t emu_playing(void) { return song.playing; }
EXPORT uint32_t emu_bpm(void) { return song.bpm; }
EXPORT uint32_t emu_page(void) { return ui.page; }
EXPORT void emu_save(uint32_t slot) { if (slot < NSLOT) proj_save(slot, SW_ALL); }   /* (the tests) */
EXPORT void emu_set_bpm(uint32_t b) { song.bpm = (uint16_t)b; }                       /* (the tests) */
EXPORT char *emu_msg(void) { return ui.msg; }                                     /* (the tests) */
EXPORT uint16_t *emu_hd(void) { return hd_scr; }
EXPORT uint32_t emu_hd_size(void) { return HW; }
EXPORT uint32_t emu_hd_rows(void)                 /* the rows changed: y0 | y1 << 16 (0: none); then cleared */
{
    uint32_t r = hd_y1 > hd_y0 ? hd_y0 | hd_y1 << 16 : 0u;
    hd_y0 = HW;
    hd_y1 = 0;
    return r;
}

/* ---- the page's fine slider (a knob held): what a knob sets now, and its value as the screen shows it.
 * role: EN_SELECT, EN_PRESET, EN_K1 .. EN_K4. A turn is pset(id, pget(id) + d), so the page reaches a value
 * by turning the knob value - (now + the steps not taken yet); parameters a turn acts on (copy, load, clear)
 * get no slider (kind 0) */
static struct { int32_t kind, id, min, max, val, pending; char label[24], text[24]; } e_knob;
EXPORT void *emu_knob(uint32_t role)
{
    const char *unit = "";
    e_knob.kind = 0;
    e_knob.pending = role < NE ? e_enc[role] : 0;
    e_knob.label[0] = e_knob.text[0] = 0;
    if (role == EN_SELECT && !ui.shift && ui.page != PG_MIX && ui.page != PG_COMP && ui.page != PG_LIM &&
        ui.page != PG_OUT && ui.page != PG_SAVE) {
        e_knob.kind = 2; e_knob.id = 0; e_knob.min = 1; e_knob.max = NTRK; e_knob.val = song.sel % NTRK + 1;
        str_cpy(e_knob.label, "SOUND", sizeof e_knob.label);
        str_cpy(e_knob.text, TRK_NAME[song.sel % NTRK], sizeof e_knob.text);
    } else if (role == EN_PRESET && !ui.shift) {
        e_knob.kind = 2; e_knob.id = 0; e_knob.min = 1; e_knob.max = NSLOT; e_knob.val = ui.slot + 1;
        str_cpy(e_knob.label, "SLOT", sizeof e_knob.label);
        fmt_u(e_knob.text, ui.slot + 1u);
        str_cat(e_knob.text, ui.used[ui.slot] ? " IN USE" : " EMPTY", sizeof e_knob.text);
    } else if (role >= EN_K1 && role <= EN_K4) {
        uint32_t id = knob_id(role - EN_K1);
        pdesc_t d = pdesc_of(id);
        switch (id) {
        case P_NONE: case P_PCOPY: case P_PPASTE: case P_LOAD: case P_NGO: case P_TCLR: case P_TALL: case P_PCLR:
        case P_LDEL: case P_PLEN: case P_VERSION:
            return &e_knob;
        }
        if (d.max <= d.min)
            return &e_knob;
        e_knob.kind = 1; e_knob.id = (int32_t)id; e_knob.min = d.min; e_knob.max = d.max; e_knob.val = pget(id);
        str_cpy(e_knob.label, d.label ? d.label : "", sizeof e_knob.label);
        pformat(id, e_knob.text, &unit);
        if (unit[0]) { str_cat(e_knob.text, " ", sizeof e_knob.text); str_cat(e_knob.text, unit, sizeof e_knob.text); }
    }
    return &e_knob;
}
EXPORT uint32_t emu_pg_midi(void) { return PG_MIDI; }

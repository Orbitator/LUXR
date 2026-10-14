/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1 emulator: the firmware's app (oc_*.c: sound, sequencer, generator, UI, companion protocol) compiled to
 * WebAssembly and run against a simulated FM-1 (after the idea of Felucca's / X0X's browser builds):
 *   - time: the audio the AudioWorklet asks for drives the clock; the UI frame runs every 15 ms of it, as the main
 *     loop on the device;
 *   - the panel: buttons by their printed labels (B_*), the 7 encoders, the 27 keys, the MASTER pot, the LEDs read back;
 *   - the screen: io_blit into a 240 x 240 RGB565 frame (the panel's byte order), a dirty flag;
 *   - USB: the companion's SysEx frames in and out (a virtual cable: emu.html connects the companion to it), and
 *     MIDI notes on channels 1..4 as the device takes them.
 * Not here: the JieLi CPU (its speed: see emu_load), the boot / update / UBOOT paths, the flash, real USB.
 *   clang --target=wasm32 -O2 -nostdlib -ffreestanding -Wl,--no-entry -Wl,--export-dynamic ... (emu/build.sh) */
#include <stdint.h>
#include "oc_tables.h"
#include "libc.c"
#define RING_PUBLISH() ((void)0)
#include "oc_core.h"
#include "oc_font.c"
#include "oc_synth.c"
#include "oc_seq.c"
#include "oc_gen.c"
#include "oc_ui.c"
#include "oc_proto.c"

#define EXPORT __attribute__((visibility("default")))

/* ------------------------------------------------------------ the panel --- */
static uint32_t e_held, e_pressed, e_keys;
static int32_t e_enc[NE];
static uint8_t e_led_btn[NB], e_led_key[27];
static uint32_t io_pressed(void) { uint32_t p = e_pressed; e_pressed = 0; return p; }
static uint32_t io_held(void) { return e_held; }
static uint32_t io_keys(void) { return e_keys; }
static int32_t io_enc(uint32_t r) { int32_t s = e_enc[r]; e_enc[r] = 0; return s; }
static void io_led_button(uint32_t b, int on) { e_led_btn[b % NB] = (uint8_t)on; }
static void io_led_key(uint32_t k, int on) { e_led_key[k % 27u] = (uint8_t)on; }

/* ----------------------------------------------------------- the screen --- */
static uint16_t e_screen[240 * 240];
static uint32_t e_dirty;
static void io_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            e_screen[(y + j) * 240u + x + i] = px[j * w + i];   /* (big endian, as the panel takes it) */
    e_dirty = 1;
}

/* -------------------------------------------------------------- USB MIDI --- */
static uint8_t e_out[4096];                       /* SysEx the device sent, F0 .. F7, until emu_out_take */
static uint32_t e_out_n;
static int proto_send(const uint8_t *p, uint32_t n)
{
    if (n > sizeof e_out)
        return -1;
    memcpy(e_out, p, n);
    e_out_n = n;
    return 0;
}
#define MIQ 64u
static uint32_t miq[MIQ];
static uint32_t mi_w, mi_r;
static void midi_in_drain(void)                   /* as oc_main.c: notes on channels 1..4 play tracks 1..4 */
{
    while (mi_r != mi_w) {
        uint32_t m = miq[mi_r++ % MIQ], st = m & 0xFFu, ch = st & 15u, d1 = (m >> 8) & 0x7Fu, d2 = (m >> 16) & 0x7Fu;
        if (ch >= NTRK)
            continue;
        if ((st & 0xF0u) == 0x90u && d2)
            note_on(ch, trk[ch].s.engine == E_DRUM ? 1u << ((d1 - 36u) & 7u) : d1, d2, 0);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            note_off(ch, d1);
    }
}
static void gen_lock(void) {}                     /* (one thread: the render and the UI never overlap) */
static void gen_unlock(void) {}

/* ---------------------------------------------------------------- exports --- */
static float e_l[HALF_FRAMES], e_r[HALF_FRAMES];
static uint8_t e_in[2048];                        /* the page writes a SysEx frame here, then emu_sysex(n) */
static uint32_t e_samples, e_next_frame, e_load_ops;

EXPORT float *emu_out_l(void) { return e_l; }
EXPORT float *emu_out_r(void) { return e_r; }
EXPORT uint16_t *emu_screen(void) { return e_screen; }
EXPORT uint8_t *emu_leds_btn(void) { return e_led_btn; }
EXPORT uint8_t *emu_leds_key(void) { return e_led_key; }
EXPORT uint8_t *emu_in_buf(void) { return e_in; }
EXPORT uint8_t *emu_out_buf(void) { return e_out; }
EXPORT uint32_t emu_out_take(void) { uint32_t n = e_out_n; e_out_n = 0; return n; }
EXPORT uint32_t emu_dirty(void) { uint32_t d = e_dirty; e_dirty = 0; return d; }

EXPORT void emu_init(uint32_t seed)
{
    rng_state = seed ? seed : 1u;
    ui_init();
    ui_draw(1);
}
/* buttons held (bit = B_* label): the presses are the new ones */
EXPORT void emu_buttons(uint32_t held)
{
    e_pressed |= held & ~e_held;
    e_held = held;
}
EXPORT void emu_keys(uint32_t keys) { e_keys = keys; }
EXPORT void emu_enc(uint32_t role, int32_t steps) { if (role < NE) e_enc[role] += steps; }
EXPORT void emu_master(uint32_t pot) { song.master_q12 = (uint16_t)((pot > 1023u ? 1023u : pot) * (pot > 1023u ? 1023u : pot) >> 8); }
EXPORT void emu_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (mi_w - mi_r < MIQ)
        miq[mi_w++ % MIQ] = (status & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16;
}
/* a SysEx frame in e_in: n bytes without F0 / F7 (the device answers through emu_out_take) */
EXPORT int emu_sysex(uint32_t n) { return n <= sizeof e_in ? proto_frame(e_in, n) : 0; }

/* n samples (<= HALF_FRAMES) into e_l / e_r; the UI frames that fall in them run first */
EXPORT void emu_render(uint32_t n)
{
    int32_t o[2 * CTL];
    uint32_t b, i;
    if (n > HALF_FRAMES)
        n = HALF_FRAMES;
    for (b = 0; b < n; b += CTL) {
        uint32_t m = n - b < CTL ? n - b : CTL;
        if (e_samples >= e_next_frame) {
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
/* the song's state for the page */
EXPORT uint32_t emu_playing(void) { return song.playing; }
EXPORT uint32_t emu_step(void) { return song.step; }
EXPORT uint32_t emu_voices(void)                  /* synth voices sounding (of NVOICE) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += voice[i].active;
    (void)e_load_ops;
    return n;
}
EXPORT uint32_t emu_sel(void) { return song.sel; }
EXPORT uint32_t emu_bpm(void) { return song.bpm; }
EXPORT uint32_t emu_root(void) { return song.root; }
EXPORT uint32_t emu_scale(void) { return song.scale; }
EXPORT uint32_t emu_engine(uint32_t t) { return trk[t % NTRK].s.engine; }
EXPORT uint32_t emu_muted(uint32_t t) { return trk[t % NTRK].mute; }
EXPORT uint32_t emu_dvoices(void)                 /* drum lanes sounding (of NLANE) */
{
    uint32_t i, n = 0;
    for (i = 0; i < NLANE; i++)
        n += dvoice[i].active;
    return n;
}
/* hearable cuts since the start (oc_synth.c vstat: a voice taken while above about -36 dB) */
EXPORT uint32_t emu_cuts(void)
{
    uint32_t a, b, n = 0;
    for (a = 0; a < NTRK; a++)
        for (b = 0; b < NTRK; b++)
            n += vstat.held[a][b] + vstat.tail[a][b];
    return n;
}

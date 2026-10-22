/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR on a computer: the app's sources (sound, sequencer, UI) against a simulated panel and screen, for the tests
 * and the demo renders. No hardware code here.
 *   cc -O2 -Ibuild/gen -Ifirmware/src -o build/X host/X.c   (build/gen: tools/build.py --app lxr --gen) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_HOST 1
#ifdef __APPLE__
#define section(x) aligned(4)                    /* (Mach-O wants "segment,section": the host needs no sections) */
#endif
#include "oc_tables.h"
#include "lxr_transients.h"
#include "own_transients.h"
#include "machine_transients.h"

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
#define RING_PUBLISH() ((void)0)
#include "lx_core.h"
static uint16_t screen[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* (panel order: big endian) */
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            screen[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            screen[j * 240u + i] = c;
}
static unsigned str_len(const char *s)           /* (as firmware/src/libc.c, which the host build leaves out) */
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}
static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    if (!max)
        return;
    while (s[i] && i + 1u < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
#include "gfx.c"
#include "lx_synth.c"
#include "lx_seq.c"
#include "lx_ui.c"

/* ------------------------------------------------------- the platform --- */
static void midi_in_drain(void) {}
static uint8_t h_store[0x49000];                    /* the store in RAM (oc_main.c: the flash) */
static int io_store_read(uint32_t off, void *d, uint32_t n)
{
    if (off + n > sizeof h_store)
        return -1;
    memcpy(d, h_store + off, n);
    return 0;
}
static int io_store_erase(uint32_t off)
{
    if ((off & 0xFFFu) || off + 0x1000u > sizeof h_store)
        return -1;
    memset(h_store + off, 0xFF, 0x1000u);
    return 0;
}
static int io_store_prog(uint32_t off, const void *s, uint32_t n)   /* (NOR: bits only go 1 -> 0) */
{
    uint32_t i;
    const uint8_t *b = s;
    if (off + n > sizeof h_store)
        return -1;
    for (i = 0; i < n; i++)
        h_store[off + i] &= b[i];
    return 0;
}
static uint32_t h_pressed, h_held, h_keys;
static int32_t h_enc[NE];
static uint8_t h_led_btn[NB], h_led_key[27];
static uint32_t io_pressed(void) { uint32_t p = h_pressed; h_pressed = 0; return p; }
static uint32_t io_held(void) { return h_held; }
static uint32_t io_keys(void) { return h_keys; }
static int32_t io_enc(uint32_t r) { int32_t s = h_enc[r]; h_enc[r] = 0; return s; }
static void io_led_button(uint32_t b, int on) { h_led_btn[b % NB] = (uint8_t)on; }
static void io_led_key(uint32_t k, int on) { h_led_key[k % 27u] = (uint8_t)on; }

/* time: audio in CTL blocks; the UI every 15 ms */
static float out_l[CTL], out_r[CTL];
static uint32_t h_samples;
static void host_block(void)
{
    int32_t o[2 * CTL];
    uint32_t i;
    mix_block(o, CTL);
    for (i = 0; i < CTL; i++) {
        out_l[i] = (float)o[2 * i] / 32768.0f;
        out_r[i] = (float)o[2 * i + 1] / 32768.0f;
    }
    h_samples += CTL;
    fm1_ms = h_samples / 44u;
}
static void host_run(uint32_t ms, void (*fn)(const float *, const float *, void *), void *user)
{
    uint32_t end = h_samples + ms * 44u + ms / 10u, next = h_samples;
    while (h_samples < end) {
        host_block();
        if (fn)
            fn(out_l, out_r, user);
        if (h_samples >= next) {
            ui_input();
            ui_leds();
            ui_draw(0);
            next += 15u * 44u;
        }
    }
}
static void host_boot(uint32_t seed)
{
    memset(voice, 0, sizeof voice);
    memset(&song, 0, sizeof song);
    memset(&ui, 0, sizeof ui);
    memset(&undo, 0, sizeof undo);
    rng_state = seed ? seed : 1u;
    ui_init();
    ui_draw(1);
}
static void host_tap(uint32_t b)                    /* a button: down 40 ms, up */
{
    h_pressed |= 1u << b;
    h_held |= 1u << b;
    host_run(40, 0, 0);
    h_held &= ~(1u << b);
    host_run(30, 0, 0);
}
static void host_hold(uint32_t b, uint32_t ms, uint32_t keys)   /* held ms, then keys (a mask) pressed meanwhile */
{
    h_pressed |= 1u << b;
    h_held |= 1u << b;
    host_run(ms, 0, 0);
    if (keys) {
        h_keys = keys;
        host_run(60, 0, 0);
        h_keys = 0;
        host_run(30, 0, 0);
    }
    h_held &= ~(1u << b);
    host_run(30, 0, 0);
}
static uint32_t key_white(uint32_t w)               /* the key bit of white key w (0 = F3) / black key b (0 = F#3) */
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (!key_black(k) && key_place(k) == w)
            return 1u << k;
    return 0;
}
static uint32_t key_blk(uint32_t b)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && key_place(k) == b)
            return 1u << k;
    return 0;
}
static void host_key(uint32_t mask)                 /* keys down 40 ms, up */
{
    h_keys |= mask;
    host_run(40, 0, 0);
    h_keys &= ~mask;
    host_run(30, 0, 0);
}
static void host_turn(uint32_t enc, int32_t d)
{
    h_enc[enc] += d;
    host_run(20, 0, 0);
}
static void host_ppm(const char *path)              /* the screen as a PPM */
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = screen[i];
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(px, 3, 1, f);
    }
    fclose(f);
}

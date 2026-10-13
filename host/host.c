/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1 on a computer: the app's sources (sound, sequencer, generator, UI, protocol) against a simulated
 * panel and screen, for the tests and the demo renders. No hardware code here.
 *   cc -O2 -Ibuild/gen -Ifirmware/src -o build/X host/X.c */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_HOST 1
#include "oc_tables.h"

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
#define RING_PUBLISH() ((void)0)
#include "oc_core.h"
#include "oc_font.c"
#include "oc_synth.c"
#include "oc_seq.c"
#include "oc_gen.c"
#include "oc_ui.c"
#include "oc_proto.c"

/* ------------------------------------------------------- the platform --- */
static void gen_lock(void) {}
static void gen_unlock(void) {}
static void midi_in_drain(void) {}
static uint32_t h_pressed, h_held, h_keys;
static int32_t h_enc[NE];
static uint8_t h_led_btn[NB], h_led_key[27];
static uint16_t screen[240 * 240];
static uint8_t h_sent[4096];
static uint32_t h_sent_n;
static uint32_t io_pressed(void) { uint32_t p = h_pressed; h_pressed = 0; return p; }
static uint32_t io_held(void) { return h_held; }
static uint32_t io_keys(void) { return h_keys; }
static int32_t io_enc(uint32_t r) { int32_t s = h_enc[r]; h_enc[r] = 0; return s; }
static void io_led_button(uint32_t b, int on) { h_led_btn[b % NB] = (uint8_t)on; }
static void io_led_key(uint32_t k, int on) { h_led_key[k % 27u] = (uint8_t)on; }
static void io_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            screen[(y + j) * 240u + x + i] = (uint16_t)((px[j * w + i] >> 8) | (px[j * w + i] << 8));
}
static int proto_send(const uint8_t *p, uint32_t n)
{
    if (n > sizeof h_sent)
        n = sizeof h_sent;
    memcpy(h_sent, p, n);
    h_sent_n = n;
    return 0;
}

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
static void host_frame(void)                       /* a UI frame (input, LEDs, screen) */
{
    ui_input();
    ui_leds();
    ui_draw(0);
}
static void host_boot(uint32_t seed)
{
    memset(trk, 0, sizeof trk);
    memset(voice, 0, sizeof voice);
    memset(&song, 0, sizeof song);
    rng_state = seed ? seed : 1u;
    ui_init();
    ui_draw(1);
}
static void host_press(uint32_t b) { h_pressed |= 1u << b; host_frame(); }

/* run ms of time, frames every 15 ms; fn(l, r, user) per block */
static void host_run(uint32_t ms, void (*fn)(const float *, const float *, void *), void *user)
{
    uint32_t end = h_samples + ms * 44u + ms / 10u, next = h_samples;
    while (h_samples < end) {
        host_block();
        if (fn)
            fn(out_l, out_r, user);
        if (h_samples >= next) {
            host_frame();
            next += 15u * 44u;
        }
    }
}

static void host_png_ppm(const char *path)          /* the screen as a PPM */
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = screen[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(rgb, 3, 1, f);
    }
    fclose(f);
}

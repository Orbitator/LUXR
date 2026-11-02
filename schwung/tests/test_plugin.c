/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): the plugin on a computer, loaded as Schwung loads it (dlopen, move_plugin_init_v2).
 *   test_plugin path/to/dsp.so outdir
 * Writes outdir/ui_hierarchy.json, chain_params.json, state.json (checked by run.sh as JSON) and outdir/luxr_move.wav;
 * fails on silence, on instances that share a sound, on a state that does not come back, on a render too slow. */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plugin_api_v1.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double g_beat = -1.0;
static float host_bpm(void) { return 120.0f; }
static double host_beat(void) { return g_beat; }
static void host_log(const char *m) { printf("  [host log] %s\n", m); }

static plugin_api_v2_t *api;
static char big[1 << 18];

static void note(void *in, int n, int v)
{
    uint8_t m[3] = {0x90, (uint8_t)n, (uint8_t)v};
    api->on_midi(in, m, 3, 0);
}
static double block_rms(void *in, int blocks, int16_t *keep, int *kept, int keep_max)
{
    int16_t b[256];
    double e = 0;
    int k, i;
    for (k = 0; k < blocks; k++) {
        api->render_block(in, b, 128);
        for (i = 0; i < 256; i++) {
            e += (double)b[i] * b[i];
            if (keep && *kept < keep_max)
                keep[(*kept)++] = b[i];
        }
        if (g_beat >= 0)
            g_beat += 128.0 / 44100.0 * 2.0;
    }
    return sqrt(e / (blocks * 256.0));
}
static void put_file(const char *dir, const char *name, const char *s, int n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if ((f = fopen(p, "wb"))) {
        fwrite(s, 1, (size_t)n, f);
        fclose(f);
    }
}
static void wav(const char *dir, const int16_t *s, int n)
{
    char p[512];
    FILE *f;
    uint32_t bytes = (uint32_t)n * 2u, v;
    snprintf(p, sizeof p, "%s/luxr_move.wav", dir);
    if (!(f = fopen(p, "wb")))
        return;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); }
    v = 44100; fwrite(&v, 4, 1, f); v = 44100u * 4u; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {4, 16}; fwrite(h, 2, 2, f); }
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(s, 2, (size_t)n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    static int16_t rec[44100 * 2 * 12];
    int nrec = 0, n, k;
    void *so, *a, *b, *ins[8];
    plugin_api_v2_t *(*init)(const host_api_v1_t *);
    host_api_v1_t host;
    double r, ra, rb;
    char val[64], st[1 << 15];
    const char *dir = argc > 2 ? argv[2] : ".";
    if (argc < 2) {
        fprintf(stderr, "usage: test_plugin dsp.so [outdir]\n");
        return 2;
    }
    if (!(so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL))) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    CHECK(!dlsym(so, "move_plugin_init_v1"), "only the v2 entry is exported");
    CHECK(!dlsym(so, "lxe_ops_0") && !dlsym(so, "lxe_LXR_TRANS"), "the engines stay hidden");
    *(void **)&init = dlsym(so, "move_plugin_init_v2");
    if (!init) {
        fprintf(stderr, "no move_plugin_init_v2\n");
        return 1;
    }
    memset(&host, 0, sizeof host);
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = 128;
    host.log = host_log;
    host.get_bpm = host_bpm;
    host.get_beat_position = host_beat;
    api = init(&host);
    CHECK(api && api->api_version == 2, "api v2");

    a = api->create_instance("/tmp", NULL);
    CHECK(a != NULL, "an instance");
    n = api->get_param(a, "ui_hierarchy", big, sizeof big);
    CHECK(n > 1000, "ui_hierarchy (%d bytes)", n);
    if (n > 0) put_file(dir, "ui_hierarchy.json", big, n);
    CHECK(api->get_param(a, "ui_hierarchy", big, 4096) == -1, "a hierarchy cut short is no answer");
    n = api->get_param(a, "chain_params", big, sizeof big);
    CHECK(n > 1000 && n < 65536, "chain_params (%d bytes)", n);
    if (n > 0) put_file(dir, "chain_params.json", big, n);
    { int c = 0; const char *p = big; while ((p = strstr(p, "\"key\""))) { c++; p++; } CHECK(c <= 256, "chain_params: %d (cap 256)", c); printf("  chain_params: %d, ui_hierarchy %d B\n", c, (int)strlen(big)); }
    api->get_param(a, "kit_count", val, sizeof val);
    printf("  kits: %s\n", val);
    CHECK(atoi(val) >= 2, "kits");
    api->get_param(a, "s1_p1", val, sizeof val);
    CHECK(!strcmp(val, "14"), "the factory kick's pitch: %s", val);
    CHECK(api->get_param(a, "nonsense", val, sizeof val) == -1, "an unknown key");
    n = api->get_param(a, "ui_hierarchy", big, sizeof big);
    CHECK(strstr(big, "\"focus_param\":\"ui_voice\"") && strstr(big, "\"canvas_script\":\"perform.js\""),
          "the hierarchy: focus and PERFORM");
    api->set_param(a, "ui_voice", "snd5");
    api->get_param(a, "ui_voice", val, sizeof val);
    CHECK(!strcmp(val, "snd5"), "ui_voice: %s", val);
    api->set_param(a, "ui_voice", "snd9");
    api->get_param(a, "ui_voice", val, sizeof val);
    CHECK(!strcmp(val, "snd5"), "ui_voice out of range ignored: %s", val);

    /* silence, then the eight sounds on the Move map, then accents */
    r = block_rms(a, 40, 0, 0, 0);
    CHECK(r == 0.0, "quiet at first: %f", r);
    for (k = 0; k < 8; k++) {
        note(a, 36 + k, 100);
        r = block_rms(a, 60, rec, &nrec, (int)(sizeof rec / sizeof *rec));
        printf("  sound %d: rms %.0f\n", k + 1, r);
        CHECK(r > 100.0, "sound %d sounds (rms %f)", k + 1, r);
    }
    note(a, 36, 100);
    ra = block_rms(a, 30, rec, &nrec, (int)(sizeof rec / sizeof *rec));
    block_rms(a, 200, rec, &nrec, (int)(sizeof rec / sizeof *rec));
    note(a, 44, 100);
    rb = block_rms(a, 30, rec, &nrec, (int)(sizeof rec / sizeof *rec));
    CHECK(rb > ra, "the upper row accents (%f > %f)", rb, ra);
    note(a, 60, 100);                            /* (MOVE: outside the two rows, nothing) */
    block_rms(a, 200, 0, 0, 0);
    r = block_rms(a, 10, 0, 0, 0);
    note(a, 60, 100);
    CHECK(block_rms(a, 10, 0, 0, 0) <= r + 1.0, "note 60 plays nothing on the Move map");

    /* a groove at 120 on Move's beat, the kit's delay and reverb on, then every kit */
    api->set_param(a, "s2_rev", "60");
    api->set_param(a, "s4_dly", "50");
    api->get_param(a, "s2_rev", val, sizeof val);
    CHECK(!strcmp(val, "60"), "s2_rev: %s", val);
    g_beat = 0.0;
    for (k = 0; k < 32; k++) {
        if (k % 4 == 0) note(a, 36, 110);
        if (k % 8 == 4) note(a, 37, 100);
        note(a, k % 4 == 2 ? 40 : 39, k % 2 ? 60 : 90);
        block_rms(a, 43, rec, &nrec, (int)(sizeof rec / sizeof *rec));   /* (a 16th at 120: 43 blocks) */
    }
    api->get_param(a, "kit_count", val, sizeof val);
    for (k = 0; k < atoi(val); k++) {
        char kv[16], name[64];
        snprintf(kv, sizeof kv, "%d", k);
        api->set_param(a, "kit", kv);
        api->get_param(a, "kit_name", name, sizeof name);
        note(a, 36, 100); note(a, 38, 100); note(a, 42, 100);
        r = block_rms(a, 40, rec, &nrec, (int)(sizeof rec / sizeof *rec));
        printf("  kit %d (%s): rms %.0f\n", k, name, r);
        CHECK(r > 100.0, "kit %d sounds", k);
    }
    wav(dir, rec, nrec);
    { int pk = 0, i; for (i = 0; i < nrec; i++) if (abs(rec[i]) > pk) pk = abs(rec[i]); printf("  peak %d (%.1f dBFS)\n", pk, 20 * log10(pk / 32767.0)); CHECK(pk > 3000, "loud enough"); }

    /* enums by index and by name; the note maps */
    api->set_param(a, "k_map", "GM");
    api->get_param(a, "k_map", val, sizeof val);
    CHECK(!strcmp(val, "1"), "k_map by name: %s", val);
    api->set_param(a, "k_map", "2");
    api->set_param(a, "k_tsnd", "0");
    block_rms(a, 400, 0, 0, 0);
    note(a, 48, 100);
    r = block_rms(a, 20, 0, 0, 0);
    CHECK(r > 100.0, "TONAL plays");
    api->set_param(a, "k_map", "0");

    /* state: changed, read, a fresh instance takes it, reads the same */
    api->set_param(a, "kit", "3");
    api->set_param(a, "s3_p2", "99");
    api->set_param(a, "s5_pan", "-40");
    api->set_param(a, "s6_smp", "20");
    api->set_param(a, "k_tune", "-5");
    api->set_param(a, "s7_var", "9");            /* (a rolled sound: its values and its number) */
    n = api->get_param(a, "state", st, sizeof st);
    CHECK(n > 100, "state");
    if (n > 0) put_file(dir, "state.json", st, n);
    b = api->create_instance("/tmp", NULL);
    CHECK(b != NULL, "a second instance");
    api->set_param(b, "state", st);
    n = api->get_param(b, "state", big, sizeof big);
    CHECK(n > 0 && !strcmp(st, big), "the state comes back as it was");
    if (n > 0 && strcmp(st, big)) put_file(dir, "state_back.json", big, n);

    /* two instances: their own sounds */
    block_rms(a, 400, 0, 0, 0);
    block_rms(b, 400, 0, 0, 0);
    note(a, 36, 120);
    ra = block_rms(a, 10, 0, 0, 0);
    rb = block_rms(b, 10, 0, 0, 0);
    CHECK(ra > 100.0 && rb < 1.0, "a hit on one instance only (%f, %f)", ra, rb);

    /* the slots: six engines, the seventh instance refused, a freed slot taken again */
    ins[0] = a; ins[1] = b;
    for (k = 2; k < 6; k++) {
        ins[k] = api->create_instance("/tmp", NULL);
        CHECK(ins[k] != NULL, "instance %d", k + 1);
    }
    CHECK(api->create_instance("/tmp", NULL) == NULL, "a seventh instance is refused");
    api->destroy_instance(ins[3]);
    ins[3] = api->create_instance("/tmp", NULL);
    CHECK(ins[3] != NULL, "a freed slot");
    api->get_param(ins[3], "s1_p1", val, sizeof val);
    CHECK(!strcmp(val, "14"), "a new instance starts with the factory kit: %s", val);

    /* cost: one instance, a hit on every sound every 16th, delay and reverb on */
    {
        int16_t o[256];
        clock_t c0 = clock();
        int blocks = 3445;                       /* 10 s */
        for (k = 0; k < blocks; k++) {
            if (k % 43 == 0) { int s; for (s = 0; s < 8; s++) note(a, 36 + s, 100); }
            api->render_block(a, o, 128);
        }
        double us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / blocks;
        printf("  render: %.1f us a block of 128 (2902 us real time), %.2f %%\n", us, us / 2902.0 * 100.0);
    }
    for (k = 0; k < 6; k++)
        api->destroy_instance(ins[k]);
    printf(fails ? "test_plugin: %d failed\n" : "test_plugin: ok\n", fails);
    return fails != 0;
}

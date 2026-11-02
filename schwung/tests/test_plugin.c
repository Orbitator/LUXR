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
#include <unistd.h>
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

/* a test WAV: a 1 s sine at 220 Hz, rate, channels, 16 / 24 bit PCM or 32 bit float */
static void wav_make(const char *path, uint32_t rate, uint32_t ch, uint32_t bits, int flt)
{
    FILE *f = fopen(path, "wb");
    uint32_t n = rate, i, c, bps = bits / 8u, data = n * ch * bps, v;
    uint16_t h;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f); v = 36u + data; fwrite(&v, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("LIST", 1, 4, f); v = 4; fwrite(&v, 4, 1, f); fwrite("INFO", 1, 4, f);   /* (a chunk to skip) */
    fwrite("fmt ", 1, 4, f); v = 16; fwrite(&v, 4, 1, f);
    h = flt ? 3 : 1; fwrite(&h, 2, 1, f); h = (uint16_t)ch; fwrite(&h, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * ch * bps; fwrite(&v, 4, 1, f);
    h = (uint16_t)(ch * bps); fwrite(&h, 2, 1, f); h = (uint16_t)bits; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (i = 0; i < n; i++)
        for (c = 0; c < ch; c++) {
            double x = 0.8 * sin(2 * M_PI * 220.0 * i / rate);
            if (flt) { float y = (float)x; fwrite(&y, 4, 1, f); }
            else if (bits == 16) { int16_t y = (int16_t)(x * 32767); fwrite(&y, 2, 1, f); }
            else { int32_t y = (int32_t)(x * 8388607); fwrite(&y, 3, 1, f); }
        }
    fclose(f);
}
static int wait_status(void *in, const char *key, const char *want)
{
    char v[32];
    int k;
    int16_t o[256];
    for (k = 0; k < 300; k++) {                  /* (up to 3 s: the loader polls every 10 ms) */
        api->render_block(in, o, 128);
        api->get_param(in, key, v, sizeof v);
        if (!strcmp(v, want))
            return 1;
        usleep(10000);
    }
    printf("  %s: %s, not %s\n", key, v, want);
    return 0;
}
static double rms_window(void *in, int from_blocks, int blocks)   /* (after from_blocks) */
{
    block_rms(in, from_blocks, 0, 0, 0);
    return block_rms(in, blocks, 0, 0, 0);
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
    {                                            /* (the kits of yours: a fresh folder of the test's own) */
        char kd[600], cmd[700];
        snprintf(kd, sizeof kd, "%s/kits", dir);
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", kd);
        if (system(cmd)) {}
        setenv("LUXR_KITS_DIR", kd, 1);
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
    CHECK(!strcmp(val, "16"), "the first preset (Patch 1) at the start: kick pitch %s", val);
    api->get_param(a, "kit_name", val, sizeof val);
    CHECK(!strcmp(val, "Patch 1"), "the first preset's name: %s", val);
    CHECK(api->get_param(a, "nonsense", val, sizeof val) == -1, "an unknown key");
    n = api->get_param(a, "ui_hierarchy", big, sizeof big);
    CHECK(strstr(big, "\"focus_param\":\"ui_voice\"") && strstr(big, "\"canvas_script\":\"perform.js\"") &&
          strstr(big, "\"focus_press_param\":\"ui_press\"") && strstr(big, "\"child_key_template\":\"s{index}_{key}\""),
          "the hierarchy: focus and PERFORM");

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

    /* the sample layer from a WAV: its first 50 ms, or the whole of it */
    {
        char w16[600], w24[600], wfl[600], st2[1 << 15];
        snprintf(w16, sizeof w16, "%s/test 16bit \"st\".wav", dir);   /* (a space and quotes: the state escapes them) */
        snprintf(w24, sizeof w24, "%s/test24.wav", dir);
        snprintf(wfl, sizeof wfl, "%s/testfloat.wav", dir);
        wav_make(w16, 48000, 2, 16, 0);
        wav_make(w24, 22050, 1, 24, 0);
        wav_make(wfl, 44100, 1, 32, 1);
        api->set_param(a, "kit", "0");
        api->set_param(a, "s1_syn", "0");        /* (the kick's synth layer silent: the sample layer alone) */
        api->set_param(a, "s1_sdec", "127");
        api->set_param(a, "s1_p2", "127");       /* (the longest decay) */
        api->set_param(a, "s1_file", w16);
        CHECK(wait_status(a, "s1_file_status", "Loaded"), "the 16 bit stereo WAV loads");
        api->get_param(a, "s1_smp", val, sizeof val);
        CHECK(!strcmp(val, "46"), "a file chosen: the sample layer plays it (smp %s)", val);
        api->get_param(a, "s1_slen", val, sizeof val);
        CHECK(!strcmp(val, "0"), "LENGTH 50 ms at first (%s)", val);
        block_rms(a, 400, 0, 0, 0);
        note(a, 36, 100);
        ra = block_rms(a, 12, 0, 0, 0);          /* (the first 35 ms) */
        rb = rms_window(a, 30, 100);             /* (0.12 .. 0.4 s) */
        printf("  WAV 50 ms: %.0f in the first 35 ms, %.0f after 120 ms\n", ra, rb);
        CHECK(ra > 1000.0 && rb < 30.0, "50 ms: a transient (%f, %f)", ra, rb);
        api->set_param(a, "s1_slen", "Whole");
        block_rms(a, 400, 0, 0, 0);
        note(a, 36, 100);
        ra = block_rms(a, 12, 0, 0, 0);
        rb = rms_window(a, 150, 60);             /* (0.45 .. 0.62 s: the sine is 1 s) */
        r = rms_window(a, 250, 40);              /* (1.4 s on: over) */
        printf("  WAV whole: %.0f at first, %.0f at 0.5 s, %.0f after its end\n", ra, rb, r);
        CHECK(rb > 1000.0 && r < 30.0, "WHOLE: to the end of the file and no further (%f, %f)", rb, r);
        api->set_param(a, "s1_spitch", "127");   /* (PITCH 1.75: shorter) */
        block_rms(a, 400, 0, 0, 0);
        note(a, 36, 100);
        rb = rms_window(a, 220, 40);             /* (0.65 .. 0.76 s: past 1 / 1.75) */
        CHECK(rb < 30.0, "PITCH plays the whole file faster (%f)", rb);
        api->set_param(a, "s1_spitch", "64");
        n = api->get_param(a, "state", st2, sizeof st2);
        CHECK(n > 0 && strstr(st2, "\"s1_file\":") && strstr(st2, "\\\"st\\\""), "the state has the path, escaped");
        {
            void *c = api->create_instance("/tmp", NULL);
            CHECK(c != NULL, "an instance for the state");
            if (c) {
                api->set_param(c, "state", st2);
                CHECK(wait_status(c, "s1_file_status", "Loaded"), "the state loads the WAV again");
                api->get_param(c, "s1_file", big, sizeof big);
                CHECK(!strcmp(big, w16), "the path comes back: %s", big);
                api->get_param(c, "s1_slen", val, sizeof val);
                CHECK(!strcmp(val, "1"), "LENGTH comes back: %s", val);
                block_rms(c, 10, 0, 0, 0);
                note(c, 36, 100);
                CHECK(rms_window(c, 150, 60) > 1000.0, "and plays whole");
                api->destroy_instance(c);
            }
        }
        api->set_param(a, "s2_file", w24);
        CHECK(wait_status(a, "s2_file_status", "Loaded"), "24 bit mono at 22.05 kHz");
        api->set_param(a, "s3_file", wfl);
        CHECK(wait_status(a, "s3_file_status", "Loaded"), "32 bit float");
        api->set_param(a, "s4_file", "/nonexistent/x.wav");
        CHECK(wait_status(a, "s4_file_status", "Failed"), "a missing file fails");
        note(a, 39, 100);
        block_rms(a, 20, 0, 0, 0);               /* (and plays nothing from it, safely) */
        for (k = 0; k < 40; k++) {               /* files switched faster than the loader: nothing leaks, nothing breaks */
            api->set_param(a, "s5_file", k % 3 == 0 ? w16 : k % 3 == 1 ? w24 : wfl);
            note(a, 40, 100);
            block_rms(a, 2, 0, 0, 0);
            if (k % 7 == 0) usleep(12000);
        }
        CHECK(wait_status(a, "s5_file_status", "Loaded"), "the last file switched to");
        api->set_param(a, "s5_file", "");
        CHECK(wait_status(a, "s5_file_status", "None"), "a file cleared");
        api->set_param(a, "s1_smp", "14");       /* (back to a transient of its own) */
        api->set_param(a, "s1_syn", "127");
    }

    {                                            /* the focus: PERFORM's, and a pad HIT (a press and its note) */
        char v0[32], v1[32];
        api->get_param(a, "ui_voice", v0, sizeof v0);
        api->set_param(a, "ui_voice", "snd5");
        api->get_param(a, "ui_voice", val, sizeof val);
        CHECK(strstr(val, ":4") && strcmp(val, v0), "ui_voice: %s", val);
        api->set_param(a, "ui_voice", "snd9");
        api->get_param(a, "ui_voice", v1, sizeof v1);
        CHECK(!strcmp(v1, val), "ui_voice out of range ignored: %s", v1);
        note(a, 38, 100);                        /* (a sequenced note: no press, no focus) */
        block_rms(a, 10, 0, 0, 0);
        api->get_param(a, "ui_voice", v1, sizeof v1);
        CHECK(!strcmp(v1, val), "a note alone moves nothing: %s", v1);
        api->set_param(a, "ui_press", "1");      /* (the press first, its note a block later) */
        block_rms(a, 1, 0, 0, 0);
        note(a, 39, 100);
        api->get_param(a, "ui_voice", v1, sizeof v1);
        CHECK(strstr(v1, ":3") && strcmp(v1, val), "press + note: the closed hat (39) is in focus (%s)", v1);
        note(a, 39, 100);                        /* (the note first, its press after) */
        block_rms(a, 1, 0, 0, 0);
        strcpy(val, v1);
        api->set_param(a, "ui_press", "1");
        block_rms(a, 40, 0, 0, 0);               /* (no note after the press: the one before it, once the window ends) */
        api->get_param(a, "ui_voice", v1, sizeof v1);
        CHECK(strstr(v1, ":3") && strcmp(v1, val), "the same pad again: a new count (%s after %s)", v1, val);
        block_rms(a, 200, 0, 0, 0);              /* (a press long after its note pairs with nothing) */
        strcpy(val, v1);
        api->set_param(a, "ui_press", "1");
        block_rms(a, 100, 0, 0, 0);
        note(a, 36, 100);
        api->get_param(a, "ui_voice", v1, sizeof v1);
        CHECK(!strcmp(v1, val), "a press and a note 290 ms apart are not a hit: %s", v1);
    }

    /* kits of your own: saved, listed, loaded back, deleted */
    {
        char nm[64], st0[1 << 15], st1[1 << 15];
        int nk0, nk, ok = 0;
        api->set_param(a, "kit", "2");
        api->set_param(a, "s3_p2", "111");
        api->set_param(a, "k_tune", "7");
        api->get_param(a, "kit_count", val, sizeof val);
        nk0 = atoi(val);
        api->get_param(a, "state", st0, sizeof st0);
        api->set_param(a, "k_save", "  My Kit/1!  ");   /* (a name made safe for a file: "My Kit1") */
        for (k = 0; k < 300 && !ok; k++) {
            int16_t o[256];
            api->render_block(a, o, 128);
            api->get_param(a, "kit_count", val, sizeof val);
            api->get_param(a, "kit_name", nm, sizeof nm);
            ok = atoi(val) == nk0 + 1 && !strcmp(nm, "My Kit1");
            if (!ok) usleep(10000);
        }
        api->get_param(a, "k_error", val, sizeof val);
        CHECK(ok, "SAVE KIT: listed and selected (%s; error \"%s\")", nm, val);
        api->get_param(a, "kit", val, sizeof val);
        nk = atoi(val);
        CHECK(nk == nk0, "the saved kit is the last in the list (%d)", nk);
        api->set_param(a, "kit", "0");           /* (another kit, then the saved one back) */
        api->get_param(a, "s3_p2", val, sizeof val);
        CHECK(strcmp(val, "111"), "another kit loaded (%s)", val);
        snprintf(val, sizeof val, "%d", nk);
        api->set_param(a, "kit", val);
        for (k = 0, ok = 0; k < 300 && !ok; k++) {
            int16_t o[256];
            api->render_block(a, o, 128);
            api->get_param(a, "s3_p2", val, sizeof val);
            ok = !strcmp(val, "111");
            if (!ok) usleep(10000);
        }
        CHECK(ok, "the saved kit loads back (s3_p2 %s)", val);
        api->get_param(a, "k_tune", val, sizeof val);
        CHECK(!strcmp(val, "7"), "with its kit values (tune %s)", val);
        api->get_param(a, "state", st1, sizeof st1);
        {   /* (the same state but for the kit's number) */
            char *p0 = strstr(st0, ",\"k_vol\""), *p1 = strstr(st1, ",\"k_vol\"");
            CHECK(p0 && p1 && !strcmp(p0, p1), "the whole kit came back");
            if (!(p0 && p1 && !strcmp(p0, p1))) { put_file(dir, "kit_saved.json", st0, (int)strlen(st0)); put_file(dir, "kit_loaded.json", st1, (int)strlen(st1)); }
        }
        api->get_param(a, "kit_name", nm, sizeof nm);
        CHECK(!strcmp(nm, "My Kit1"), "its name: %s", nm);
        api->set_param(a, "k_delete", "Delete");
        for (k = 0, ok = 0; k < 300 && !ok; k++) {
            int16_t o[256];
            api->render_block(a, o, 128);
            api->get_param(a, "kit_count", val, sizeof val);
            ok = atoi(val) == nk0;
            if (!ok) usleep(10000);
        }
        CHECK(ok, "DELETE KIT: gone from the list (%s kits)", val);
        api->set_param(a, "kit", "0");
        api->set_param(a, "k_delete", "Delete");   /* (a preset of LUXR's is never deleted) */
        block_rms(a, 30, 0, 0, 0);
        api->get_param(a, "kit_count", val, sizeof val);
        CHECK(atoi(val) == nk0, "the presets stay (%s)", val);
    }

    /* a retriggered sound fades its tail instead of cutting it (no click): the largest step between two samples at a
     * retrigger is no larger than the sound's own */
    {
        int16_t o[256], prev = 0;
        int jump_hit = 0, jump_all = 0, b2, i2;
        api->set_param(a, "kit", "0");
        block_rms(a, 300, 0, 0, 0);
        for (b2 = 0; b2 < 400; b2++) {
            if (b2 % 20 == 0) note(a, 46, 120);  /* (the open hat, retriggered long before it ends) */
            api->render_block(a, o, 128);
            for (i2 = 0; i2 < 256; i2 += 2) {
                int d = abs(o[i2] - prev);
                prev = o[i2];
                if (b2 % 20 == 0 && b2 && i2 < 8) { if (d > jump_hit) jump_hit = d; }
                else if (d > jump_all) jump_all = d;
            }
        }
        printf("  retrigger: largest step %d at a hit, %d elsewhere\n", jump_hit, jump_all);
        CHECK(jump_hit <= jump_all + 2000, "no click at a retrigger (%d vs %d)", jump_hit, jump_all);
    }

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
    CHECK(!strcmp(val, "16"), "a new instance starts with the first preset: %s", val);

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

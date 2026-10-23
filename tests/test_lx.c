/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR: the app on the computer (sound, sequencer, panel, store), screenshots of its pages and a demo WAV.
 *   test_lx OUTDIR          (OUTDIR/lxr_demo.wav, OUTDIR/lx_<page>.ppm) */
#include "../host/lx_host.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* hits a track got: its voice started again (t back to the start) */
static uint32_t hits[NTRK], last_t[NTRK];
static float peak_out;
static double sum_sq;
static uint32_t n_out;
static FILE *wav;
static void watch(const float *l, const float *r, void *u)
{
    uint32_t t, i;
    (void)u;
    for (t = 0; t < NTRK; t++) {
        if (voice[t].active && voice[t].t < last_t[t])
            hits[t]++;
        last_t[t] = voice[t].active ? voice[t].t : 0xFFFFFFFFu;
    }
    for (i = 0; i < CTL; i++) {
        float a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
        if (a > peak_out) peak_out = a;
        if (b > peak_out) peak_out = b;
        sum_sq += (double)l[i] * l[i] + (double)r[i] * r[i];
        n_out += 2;
        if (wav) {
            float x = l[i] * 1.6f, y = r[i] * 1.6f;
            int16_t s[2] = {(int16_t)(32767 * (x > 1 ? 1 : x < -1 ? -1 : x)), (int16_t)(32767 * (y > 1 ? 1 : y < -1 ? -1 : y))};
            fwrite(s, 4, 1, wav);
        }
    }
}
static void reset_count(void)
{
    uint32_t t;
    memset(hits, 0, sizeof hits);
    for (t = 0; t < NTRK; t++)                   /* (a voice ringing now is no new hit) */
        last_t[t] = voice[t].active ? voice[t].t : 0xFFFFFFFFu;
    peak_out = 0;
    sum_sq = 0;
    n_out = 0;
}
static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t d = frames * 4u, v;
    uint16_t a;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = 36 + d; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    a = 1; fwrite(&a, 2, 1, f); a = 2; fwrite(&a, 2, 1, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    a = 4; fwrite(&a, 2, 1, f); a = 16; fwrite(&a, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
}
static uint32_t steps_on(uint32_t p, uint32_t t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NSTEP; i++)
        n += pat[p].s[t][i] != 0u;
    return n;
}
#define BAR_MS 2000u                             /* one bar at 120 BPM */
/* hit times: the sample (h_samples) at the block a track's voice started again */
static uint32_t hat_n[NTRK], hat[NTRK][64];
static void watch_at(const float *l, const float *r, void *u)
{
    uint32_t t;
    (void)l; (void)r; (void)u;
    for (t = 0; t < NTRK; t++) {
        if (voice[t].active && voice[t].t < last_t[t]) {
            if (hat_n[t] < 64u) hat[t][hat_n[t]] = h_samples - voice[t].t;
            hat_n[t]++;
            hits[t]++;
        }
        last_t[t] = voice[t].active ? voice[t].t : 0xFFFFFFFFu;
    }
}
static void reset_at(void) { memset(hat_n, 0, sizeof hat_n); reset_count(); }
static uint32_t mout_count(uint32_t mask, uint32_t want, uint32_t from, uint32_t to)   /* MIDI OUT packets matching */
{
    uint32_t i, n = 0;
    for (i = from; i < to && i < h_mout_n; i++)
        n += (h_mout[i] & mask) == want;
    return n;
}
static uint32_t mout_find(uint32_t mask, uint32_t want, uint32_t from)
{
    uint32_t i;
    for (i = from; i < h_mout_n; i++)
        if ((h_mout[i] & mask) == want)
            return i;
    return 0xFFFFFFFFu;
}
static void run_samples(uint32_t n, void (*fn)(const float *, const float *, void *))   /* audio only, no UI */
{
    uint32_t end = h_samples + n;
    while ((int32_t)(h_samples - end) < 0) {
        host_block();
        if (fn) fn(out_l, out_r, 0);
    }
}
static uint32_t locks_of(uint32_t pn, uint32_t t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NLOCK; i++)
        n += plk[pn][i].pid && (plk[pn][i].key >> 6) == t;
    return n;
}

static void shift_key(uint32_t mask)             /* a key with GLO held (SHIFT) */
{
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(300, 0, 0);
    host_key(mask);
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build";
    char path[512];
    uint32_t t, frames0;

    /* 1. the demo pattern: sound, the hits, the level */
    host_boot(1);
    snprintf(path, sizeof path, "%s/lxr_demo.wav", dir);
    wav = fopen(path, "wb");
    if (wav) fseek(wav, 44, SEEK_SET);
    reset_count();
    host_tap(B_PLAY);
    CHECK(song.playing, "PLAY starts");
    host_run(4 * BAR_MS, watch, 0);
    CHECK(hits[0] >= 19u && hits[0] <= 21u, "kick: %u hits in 4 bars (20)", hits[0]);
    CHECK(hits[1] >= 7u && hits[1] <= 9u, "snare: %u hits (8)", hits[1]);
    CHECK(hits[3] >= 31u && hits[3] <= 33u, "closed hat: %u hits (32)", hits[3]);
    CHECK(peak_out > 0.05f && peak_out < 0.999f, "peak %.3f", peak_out);
    printf("demo: peak %.3f rms %.4f, hits BD %u SD %u CP %u CH %u OH %u TM %u RS %u CY %u\n", peak_out,
           n_out ? __builtin_sqrt(sum_sq / n_out) : 0.0, hits[0], hits[1], hits[2], hits[3], hits[4], hits[5], hits[6], hits[7]);
    /* every sound alone, its layers: the synth layer, then the sample layer alone */
    for (t = 0; t < NTRK; t++) {
        reset_count();
        ev_push(EV_HIT, t, 120);
        host_run(250, watch, 0);
    }
    for (t = 0; t < NTRK; t++) {
        snd[t].syn = 0;
        snd[t].smp = (uint8_t)(TR_SMP0 + t);
        snd[t].slvl = 120;
        reset_count();
        ev_push(EV_HIT, t, 120);
        host_run(150, watch, 0);
        CHECK(peak_out > 0.01f, "the sample layer of %s sounds alone (peak %.4f)", TRK_NAME[t], peak_out);
        snd[t].syn = 127;
    }
    /* sixteen own transients a sound, every one of them sounds (the synth layer off) */
    {
        uint32_t k, quiet = 0;
        for (t = 0; t < NTRK; t++) {
            snd[t].syn = 0;
            snd[t].slvl = 127;
            for (k = 0; k < 16u; k++) {
                snd[t].smp = (uint8_t)(TR_OWN0 + k);
                reset_count();
                ev_push(EV_HIT, t, 120);
                host_run(60, watch, 0);
                quiet += peak_out < 0.01f;
            }
            sound_init(t);
        }
        CHECK(!quiet && TR_MACH0 - TR_OWN0 == 16, "16 own transients for each sound, all sound (%u quiet)", quiet);
        quiet = 0;
        for (t = 0; t < NTRK; t++) {             /* and 16 after the classic machines (22.05 kHz) */
            snd[t].syn = 0;
            snd[t].slvl = 127;
            for (k = 0; k < 16u; k++) {
                snd[t].smp = (uint8_t)(TR_MACH0 + k);
                reset_count();
                ev_push(EV_HIT, t, 120);
                host_run(60, watch, 0);
                quiet += peak_out < 0.01f;
            }
            sound_init(t);
        }
        CHECK(!quiet && TR_N - TR_MACH0 == 16, "16 machine transients for each sound, all sound (%u quiet)", quiet);
    }
    /* the sample layer's START, TONE, BEND change what it plays */
    {
        double base, e2;
        snd[1].syn = 0;
        snd[1].slvl = 127;
        snd[1].smp = TR_MACH0 + 3;               /* 909 A */
        reset_count(); ev_push(EV_HIT, 1, 120); host_run(60, watch, 0); base = sum_sq;
        snd[1].sstart = 100;
        reset_count(); ev_push(EV_HIT, 1, 120); host_run(60, watch, 0); e2 = sum_sq;
        CHECK(e2 < base * 0.7, "START late in the sample: less of it (%.2f -> %.2f)", base, e2);
        snd[1].sstart = 0;
        snd[1].stone = -60;
        reset_count(); ev_push(EV_HIT, 1, 120); host_run(60, watch, 0); e2 = sum_sq;
        CHECK(e2 < base * 0.8, "TONE low-pass: darker, less energy (%.2f -> %.2f)", base, e2);
        snd[1].stone = 0;
        snd[1].sbend = 63;
        reset_count(); ev_push(EV_HIT, 1, 120); host_run(60, watch, 0); e2 = sum_sq;
        CHECK(e2 < base * 0.95 || e2 > base * 1.05, "BEND changes it (%.2f -> %.2f)", base, e2);
        sound_init(1);
    }
    /* ALGORITHM rolls a new sound for the selected one: always with a transient sample */
    for (t = 0; t < 40u; t++) {
        sel_track((int32_t)(t % NTRK));
        host_turn(EN_ALGO, 1);
        CHECK(snd[song.sel].smp >= TR_SMP0 && snd[song.sel].slvl > 0, "ALGORITHM: %s without a sample (%u)", TRK_NAME[song.sel],
              snd[song.sel].smp);
    }
    sel_track(0);
    host_run(2 * BAR_MS, watch, 0);
    if (wav) {
        frames0 = (uint32_t)((ftell(wav) - 44) / 4);
        wav_header(wav, frames0);
        fclose(wav);
        wav = 0;
    }
    host_tap(B_PLAY);
    CHECK(!song.playing, "PLAY stops");
    /* every sound dies away: hit at full level (accented, longest decays), then silence */
    for (t = 0; t < NTRK; t++) {
        snd[t].p[1] = 127;
        snd[t].sdec = 127;
        ev_push(EV_HIT, t, 127 | S_ACC);
    }
    host_run(15000, 0, 0);
    for (t = 0; t < NTRK; t++)
        CHECK(!voice[t].active, "%s still sounding 15 s after its hit (amp %d)", TRK_NAME[t], voice[t].amp);
    reset_count();
    host_run(200, watch, 0);
    CHECK(peak_out < 0.002f, "silence after the hits died away (peak %.4f)", peak_out);

    /* 2. GLO: tapped the mixer, held SHIFT (not the mixer) */
    host_boot(2);
    CHECK(ui.page == PG_SEQ, "starts on SEQ");
    host_tap(B_GLO);
    CHECK(ui.page == PG_MIX, "GLO tapped: the mixer (page %u)", ui.page);
    host_tap(B_SEQ);
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(100, 0, 0);
    CHECK(!ui.shift, "GLO 100 ms: not yet SHIFT");
    host_run(300, 0, 0);
    CHECK(ui.shift, "GLO 400 ms: SHIFT");
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(!ui.shift && ui.page == PG_SEQ, "GLO let go after a hold: SHIFT off, no mixer");
    /* GLO + PLAY at once: SHIFT + PLAY (from the top), no mixer */
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(30, 0, 0);
    host_tap(B_PLAY);
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(song.playing && ui.page == PG_SEQ, "GLO + PLAY: from the top (playing %u, page %u)", song.playing, ui.page);
    host_tap(B_PLAY);

    /* 3. the mixer: KNOB 1..4 the levels of 1-4, OCT+ 5-8, SHIFT the pans */
    host_tap(B_GLO);
    host_turn(EN_K1, 10);
    CHECK(snd[0].level == 110u, "mixer K1: kick level %u", snd[0].level);
    host_tap(B_OCTUP);
    CHECK(ui.bank == 1u, "OCT+: bank 5-8");
    host_turn(EN_K4, -20);
    CHECK(snd[7].level == 80u, "mixer K4 in bank 2: cymbal level %u", snd[7].level);
    host_tap(B_OCTDN);
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(300, 0, 0);
    host_turn(EN_K2, 8);
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(snd[1].pan == 8, "mixer SHIFT + K2: snare pan %d", snd[1].pan);
    CHECK(ui.page == PG_MIX, "mixer still");
    snprintf(path, sizeof path, "%s/lx_mixer.ppm", dir);
    host_ppm(path);
    /* GLO tapped again: the mixer's page 2, KNOB 1..4 the pans (OCT+ bank 5-8), again: back to the levels */
    host_tap(B_GLO);
    CHECK(ui.page == PG_MIX && ui.mixpan, "GLO again: mixer page 2 (pan)");
    host_turn(EN_K1, -20);
    CHECK(snd[0].pan == -20 && snd[0].level == 110u, "page 2 K1: kick pan %d (level %u kept)", snd[0].pan, snd[0].level);
    host_tap(B_OCTUP);
    host_turn(EN_K3, 30);
    CHECK(snd[6].pan == 40, "page 2, bank 5-8, K3: rim pan %d", snd[6].pan);
    snprintf(path, sizeof path, "%s/lx_mixer_pan.ppm", dir);
    host_ppm(path);
    host_tap(B_OCTDN);
    host_tap(B_GLO);
    CHECK(ui.page == PG_MIX && !ui.mixpan, "GLO again: back to the levels");
    host_turn(EN_K1, -10);
    CHECK(snd[0].level == 100u, "page 1 K1: kick level %u", snd[0].level);

    /* 4. SEQ: white keys the steps of the bar, OCT the bars, SHIFT + OCT+ a bar more */
    host_tap(B_SEQ);
    sel_track(5);                                /* the tom: step 15 */
    host_key(key_white(2));
    CHECK(PAT->s[5][2] == 100u, "white key 3: step 3 on");
    host_key(key_white(2));
    CHECK(PAT->s[5][2] == 0u, "again: off");
    host_tap(B_OCTUP);
    CHECK(ui.bar == 0u, "OCT+ on a one-bar track stays");
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(300, 0, 0);
    host_tap(B_OCTUP);
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(PAT->len[5] == 32u && ui.bar == 1u, "SHIFT + OCT+: a second bar (len %u, bar %u)", PAT->len[5], ui.bar);
    CHECK(PAT->s[5][31] == PAT->s[5][15], "the new bar a copy of the last");
    host_tap(B_OCTDN);
    CHECK(ui.bar == 0u, "OCT-: bar 1");
    host_tap(B_OCTUP);
    host_key(key_white(0));
    CHECK(PAT->s[5][16] == 100u, "white key 1 in bar 2: step 17");
    shift_key(key_blk(BK_PLOCK));                /* (SHIFT + C#5: the accent mode) */
    host_key(key_white(0));
    CHECK(PAT->s[5][16] == (100u | S_ACC), "accent edit: the step accented");
    shift_key(key_blk(BK_PLOCK));
    snprintf(path, sizeof path, "%s/lx_seq.ppm", dir);
    host_ppm(path);
    /* black keys: sound, mute, copy / paste, clear, undo */
    host_key(key_blk(BK_NEXT));
    CHECK(song.sel == 6u, "G#3: the next sound");
    host_key(key_blk(BK_PREV));
    host_key(key_blk(BK_MUTE));
    CHECK((song.mute >> 5) & 1u, "A#3: mute");
    host_key(key_blk(BK_MUTE));
    host_tap(B_OCTUP);                           /* (another sound showed bar 1: back to bar 2) */
    CHECK(ui.bar == 1u, "bar 2 again");
    host_key(key_blk(BK_COPY));
    host_tap(B_OCTDN);
    host_key(key_blk(BK_PASTE));
    CHECK(PAT->s[5][0] == (100u | S_ACC), "copy bar 2, paste into bar 1");
    host_key(key_blk(BK_CLEAR));
    CHECK(!steps_on(0, 5), "A#4: the track cleared");
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(300, 0, 0);
    host_key(key_blk(BK_PREV));
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(steps_on(0, 5) >= 2u, "SHIFT + F#3: undo (%u steps back)", steps_on(0, 5));

    /* 5. NOTE REPEAT: ARP tapped, a pad held a bar at 1/16 */
    host_boot(3);
    memset(pat[0].s, 0, sizeof pat[0].s);
    host_tap(B_ARP);
    CHECK(ui.page == PG_NR, "ARP: note repeat");
    host_key(key_white(8 + NR_16));
    CHECK(nr.rate == NR_16, "white key 13: the rate 1/16");
    host_tap(B_PLAY);
    host_run(200, 0, 0);
    reset_count();
    h_keys |= key_white(2);
    host_run(BAR_MS, watch, 0);
    h_keys &= ~key_white(2);
    host_run(40, watch, 0);
    CHECK(hits[2] >= 15u && hits[2] <= 17u, "a pad held a bar at 1/16: %u hits (16)", hits[2]);
    /* recorded with REC armed: 1/8 a bar -> 8 steps */
    host_key(key_white(8 + NR_8));
    host_tap(B_REC);
    CHECK(song.rec, "REC armed");
    h_keys |= key_white(1);
    host_run(BAR_MS + 100u, 0, 0);
    h_keys &= ~key_white(1);
    host_run(40, 0, 0);
    CHECK(steps_on(0, 1) == 8u, "recorded at 1/8: %u steps (8)", steps_on(0, 1));
    snprintf(path, sizeof path, "%s/lx_repeat.ppm", dir);
    host_ppm(path);
    /* ERASE (SHIFT + REC): the pad held a bar clears its steps */
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(300, 0, 0);
    host_tap(B_REC);
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(song.erase && song.rec, "SHIFT + REC: ERASE armed");
    h_keys |= key_white(1);
    host_run(BAR_MS + 100u, 0, 0);
    h_keys &= ~key_white(1);
    host_run(40, 0, 0);
    CHECK(steps_on(0, 1) == 0u, "erased: %u steps left", steps_on(0, 1));
    host_tap(B_REC);
    CHECK(!song.rec && !song.erase, "REC off");
    /* LATCH: a pad repeats until pressed again */
    host_turn(EN_K4, 1);
    CHECK(nr.latch, "K4: latch");
    host_key(key_white(7));
    reset_count();
    host_run(BAR_MS, watch, 0);
    CHECK(hits[7] >= 7u, "latched: %u hits a bar at 1/8", hits[7]);
    host_key(key_white(7));
    reset_count();
    host_run(BAR_MS / 2u, watch, 0);
    CHECK(hits[7] == 0u, "pressed again: stopped (%u)", hits[7]);
    /* ARP tapped again: off, back to the page before */
    host_tap(B_ARP);
    CHECK(ui.page == PG_SEQ, "ARP again: back (page %u)", ui.page);
    /* ARP held: note repeat while held */
    h_pressed |= 1u << B_ARP; h_held |= 1u << B_ARP;
    host_run(500, 0, 0);
    CHECK(ui.page == PG_NR, "ARP held: note repeat");
    h_held &= ~(1u << B_ARP);
    host_run(40, 0, 0);
    CHECK(ui.page == PG_SEQ, "ARP let go: back");
    host_tap(B_PLAY);

    /* 6. patterns: cued at the end, the chain */
    host_boot(4);
    demo_pattern(&pat[1]);
    host_tap(B_PLAY);
    host_run(300, 0, 0);
    host_tap(B_SCL);
    host_turn(EN_K1, 1);
    CHECK(song.pat == 0u && song.pat_next == 1u, "pattern 2 cued (%u %u)", song.pat, song.pat_next);
    host_run(BAR_MS, 0, 0);
    CHECK(song.pat == 1u, "at the end of the bar: pattern 2 (%u)", song.pat);
    host_turn(EN_K1, -1);
    host_run(BAR_MS, 0, 0);
    host_turn(EN_K2, 1);
    CHECK(song.chain == 2u, "chain 2");
    host_run(BAR_MS, 0, 0);
    CHECK(song.pat == 1u, "chained: pattern 2 (%u)", song.pat);
    host_run(BAR_MS, 0, 0);
    CHECK(song.pat == 0u, "chained: pattern 1 again (%u)", song.pat);
    snprintf(path, sizeof path, "%s/lx_patterns.ppm", dir);
    host_ppm(path);
    host_tap(B_PLAY);

    /* 7. the store: SAVE writes kit + patterns (K2: or one of them), PRESETS loads the patches (SHIFT: patterns only) */
    host_boot(5);
    snd[2].level = 33;
    pat[3].s[4][9] = 77;
    host_tap(B_SAVE);
    host_turn(EN_K1, 2);                         /* slot 3 */
    host_tap(B_OCTUP);
    CHECK(ui.used[2], "saved into slot 3");
    snprintf(path, sizeof path, "%s/lx_store.ppm", dir);
    host_ppm(path);
    snd[2].level = 44;
    pat[3].s[4][9] = 55;
    host_turn(EN_K1, 2);                         /* slot 5: the kit only (its patterns: the ones now, none stored) */
    host_turn(EN_K2, 1);
    CHECK(ui.swhat == SW_KIT, "K2: KIT");
    host_tap(B_OCTUP);
    CHECK(ui.used[4], "kit saved into slot 5");
    snd[2].level = 99;
    pat[3].s[4][9] = 0;
    ui.patch = 3;                                /* PRESETS from slot 4 on: the next patch in use is 5 */
    host_turn(EN_PRESET, 1);
    CHECK(ui.patch == 4u && snd[2].level == 44u, "PRESETS: patch 5 loaded (kit %u)", snd[2].level);
    host_turn(EN_PRESET, -1);
    CHECK(ui.patch == 2u && snd[2].level == 33u && pat[3].s[4][9] == 77u, "PRESETS back: patch 3 (%u %u)", snd[2].level,
          pat[3].s[4][9]);
    snd[2].level = 99;
    pat[3].s[4][9] = 0;
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;   /* SHIFT + PRESETS: the patterns only */
    host_run(300, 0, 0);
    host_turn(EN_PRESET, 1);                     /* patch 5's patterns (55), the kit as it is (99) */
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);
    CHECK(ui.patch == 4u && pat[3].s[4][9] == 55u && snd[2].level == 99u, "SHIFT + PRESETS: patterns only (%u %u %u)",
          ui.patch, pat[3].s[4][9], snd[2].level);
    if (ui.page != PG_SAVE) host_tap(B_SAVE);    /* (SAVE again would show NEW PATCH) */
    host_turn(EN_K1, -2);                        /* slot 3 again, the patterns only: its kit stays */
    host_turn(EN_K2, 1);
    pat[3].s[4][9] = 66;
    host_tap(B_OCTUP);
    snd[2].level = 1;
    proj_load(2, SW_ALL);
    CHECK(snd[2].level == 33u && pat[3].s[4][9] == 66u, "SAVE PATTERN kept the slot's kit (%u %u)", snd[2].level, pat[3].s[4][9]);
    /* STORE, KNOB 3: what is loaded, KNOB 4: a turn loads the slot */
    if (ui.page != PG_SAVE) host_tap(B_SAVE);
    snd[2].level = 5;
    pat[3].s[4][9] = 1;
    host_turn(EN_K3, 1);                         /* LOADS: KIT */
    host_turn(EN_K4, 1);
    CHECK(snd[2].level == 33u && pat[3].s[4][9] == 1u, "K3 KIT, K4: the kit of slot 3 loaded, the patterns kept (%u %u)",
          snd[2].level, pat[3].s[4][9]);
    host_turn(EN_K3, 1);                         /* LOADS: PATTERN */
    snd[2].level = 5;
    host_turn(EN_K4, -1);
    CHECK(snd[2].level == 5u && pat[3].s[4][9] == 66u, "K3 PATTERN, K4: the patterns loaded, the kit kept (%u %u)",
          snd[2].level, pat[3].s[4][9]);
    host_turn(EN_K3, -2);                        /* LOADS: KIT+PAT */
    host_turn(EN_K4, 1);
    CHECK(snd[2].level == 33u && pat[3].s[4][9] == 66u, "K3 KIT+PAT, K4: both loaded");
    snprintf(path, sizeof path, "%s/lx_store.ppm", dir);
    host_ppm(path);
    host_tap(B_OCTDN);
    CHECK(ui.page != PG_SAVE, "OCT- leaves the store");
    host_boot(6);                                /* a new boot sees the slots */
    CHECK(ui.used[2] && ui.used[4] && ui.slot == 2u, "after a boot: slots 3 and 5 in use, 3 selected");

    /* 8. the other pages' screens */
    host_tap(B_EDIT);
    CHECK(ui.page == PG_VOICE, "EDIT: the synth layer");
    snprintf(path, sizeof path, "%s/lx_synth.ppm", dir);
    host_ppm(path);
    host_tap(B_EDIT);
    CHECK(ui.page == PG_VOICE2, "EDIT again: the synth layer's parameters 5-8 (no SHIFT needed)");
    host_turn(EN_K1, 5);
    CHECK(snd[song.sel].p[4] == LX_FACTORY[song.sel][4] + 5u, "SYNTH 5-8, K1: parameter 5 (%u)", snd[song.sel].p[4]);
    snprintf(path, sizeof path, "%s/lx_synth2.ppm", dir);
    host_ppm(path);
    host_tap(B_EDIT);
    CHECK(ui.page == PG_SAMPLE, "EDIT a third time: the sample layer");
    host_turn(EN_K1, 30);                        /* into the machines' bank */
    host_turn(EN_K3, -20);
    CHECK(snd[song.sel].smp >= TR_MACH0 && snd[song.sel].spitch == 44u, "SAMPLE: K1 sample (%u), K3 its pitch (%u)",
          snd[song.sel].smp, snd[song.sel].spitch);
    snprintf(path, sizeof path, "%s/lx_sample.ppm", dir);
    host_ppm(path);
    host_tap(B_EDIT);
    CHECK(ui.page == PG_SAMPLE2, "EDIT a fourth time: SAMPLE 2");
    host_turn(EN_K1, 40);
    host_turn(EN_K2, -30);
    host_turn(EN_K3, 20);
    host_turn(EN_K4, -127);
    CHECK(snd[song.sel].sstart == 40u && snd[song.sel].stone == -30 && snd[song.sel].sbend == 20 && !snd[song.sel].syn,
          "SAMPLE 2: start, tone, bend, synth level");
    snprintf(path, sizeof path, "%s/lx_sample2.ppm", dir);
    host_ppm(path);
    host_run(3000, 0, 0);                        /* (all quiet) */
    reset_count();
    host_turn(EN_K1, 5);
    host_turn(EN_K3, 5);
    host_run(100, watch, 0);
    CHECK(!voice[song.sel].active && peak_out < 0.001f, "turning a knob does not play the sound");
    host_turn(EN_K4, 127);
    host_tap(B_EDIT);
    CHECK(ui.page == PG_SND, "EDIT a fifth time: the sound's mix and sends");
    host_tap(B_EDIT);
    CHECK(ui.page == PG_VOICE, "EDIT again: back to the synth layer");
    host_tap(B_HOME);
    snprintf(path, sizeof path, "%s/lx_kit.ppm", dir);
    host_ppm(path);
    host_tap(B_FX);
    host_tap(B_OCTUP);
    CHECK(song.sel == 1u, "OCT+ on FX: the next sound");

    /* 10. FX: sends, DRIVE (3 modes), DELAY, REVERB pages; the drive bus sounds */
    host_boot(7);
    host_tap(B_FX);
    CHECK(ui.page == PG_FXS, "FX: the sends");
    host_turn(EN_K1, 100);
    CHECK(snd[0].drv == 100u, "FX sends K1: kick drive send %u", snd[0].drv);
    host_tap(B_FX);
    CHECK(ui.page == PG_FXD, "FX again: DRIVE");
    host_turn(EN_K1, 2);
    CHECK(song.drv_mode == DM_FOLD, "DRIVE MODE: FOLD");
    snprintf(path, sizeof path, "%s/lx_drive.ppm", dir);
    host_ppm(path);
    host_tap(B_FX);
    CHECK(ui.page == PG_FXDL, "FX again: DELAY");
    host_turn(EN_K1, 2);
    CHECK(song.dly_mode == DL_PINGPONG, "DELAY MODE: PING PONG");
    host_tap(B_FX);
    CHECK(ui.page == PG_FXDL2, "FX again: DELAY 2 (tone, level)");
    host_tap(B_FX);
    CHECK(ui.page == PG_FXR, "FX again: REVERB");
    host_tap(B_FX);
    CHECK(ui.page == PG_MFX, "FX again: MASTER (the filter)");
    host_tap(B_FX);
    CHECK(ui.page == PG_FXS, "FX again: back to the sends");
    {
        float dry, wet;
        uint32_t m;
        snd[0].drv = 0;
        for (m = 0; m < 2u; m++) {
            if (m) snd[0].drv = 127;
            reset_count();
            ev_push(EV_HIT, 0, 120);
            host_run(300, watch, 0);
            if (m) wet = (float)sum_sq; else dry = (float)sum_sq;
        }
        CHECK(wet > dry * 1.05f, "the drive send adds the drive bus (%.1f -> %.1f)", dry, wet);
        snd[0].drv = 0;
    }
    {                                            /* the delay's modes: ping pong alternates the sides, stereo has two times */
        static float el[64], er[64];
        uint32_t k, mode;
        for (mode = 0; mode < 3u; mode++) {
            uint32_t tbase = h_samples;
            float rl = 0, rr = 0;
            song.dly_mode = (uint8_t)mode;
            song.dly_div = 2;                    /* 1/4: 0.5 s at 120 */
            song.dly_div2 = 0;                   /* 1/8 (stereo's right) */
            song.dly_fb = 90;
            sound_init(2);                       /* (the clap as it comes: loud and short) */
            snd[2].dly = 127;
            snd[2].rev = 0;
            snd[2].pan = 0;
            ev_push(EV_HIT, 2, 127);
            memset(el, 0, sizeof el); memset(er, 0, sizeof er);
            for (k = 0; k < 64u; k++) {          /* 64 windows of 31 ms */
                uint32_t j;
                host_run(31, 0, 0);
                for (j = 0; j < CTL; j++) { el[k] += out_l[j] * out_l[j]; er[k] += out_r[j] * out_r[j]; }
            }
            (void)tbase;
            for (k = 14; k < 18u; k++) { rl += el[k]; rr += er[k]; }   /* (around 0.5 s: the first repeat) */
            if (mode == DL_PINGPONG)
                CHECK(rl > rr * 4.0f, "PING PONG: the first repeat on the left (%.4f %.4f)", rl, rr);
            if (mode == DL_STEREO) {
                float r8 = 0, l8 = 0;
                for (k = 6; k < 10u; k++) { r8 += er[k]; l8 += el[k]; }   /* (around 0.25 s: the right's 1/8) */
                CHECK(r8 > l8 * 2.0f, "STEREO: the right side repeats at its own time (%.4f %.4f)", r8, l8);
            }
            host_run(3000, 0, 0);
        }
        song.dly_mode = DL_MONO;
        snd[6].dly = 0;
    }

    /* 11. LFO: two a sound, in time with the tempo */
    host_tap(B_LFO);
    CHECK(ui.page == PG_LFO1, "LFO: LFO 1");
    host_turn(EN_K1, LD_PITCH);
    host_turn(EN_K3, -2);                        /* 1 BAR -> 1/4 */
    host_turn(EN_K4, 20);
    CHECK(snd[0].lfo[0].dest == LD_PITCH && snd[0].lfo[0].rate == LR_4 && snd[0].lfo[0].depth == 52, "LFO 1: %u %u %d",
          snd[0].lfo[0].dest, snd[0].lfo[0].rate, snd[0].lfo[0].depth);
    snprintf(path, sizeof path, "%s/lx_lfo.ppm", dir);
    host_ppm(path);
    host_tap(B_LFO);
    CHECK(ui.page == PG_LFO2, "LFO again: LFO 2");
    host_turn(EN_K1, LD_PAN);
    host_turn(EN_K4, -96);
    CHECK(snd[0].lfo[1].dest == LD_PAN && snd[0].lfo[1].depth == -64, "LFO 2: PAN, depth %d", snd[0].lfo[1].depth);

    /* 12. SEQ + SHIFT: KNOB 1 doubles the pattern (COPY x2: the notes too) or halves it */
    host_boot(8);                                /* (it starts on SEQ) */
    h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
    host_run(300, 0, 0);
    host_turn(EN_K1, 1);
    CHECK(pat_len(PAT) == 32u && PAT->s[0][16] == PAT->s[0][0] && PAT->s[0][16], "x2 with COPY: 32 steps, bar 2 a copy");
    host_turn(EN_K2, -1);                        /* COPY off */
    host_turn(EN_K1, 1);
    CHECK(pat_len(PAT) == 64u && !PAT->s[0][32] && PAT->s[0][16], "x2 without COPY: 64 steps, bars 3-4 empty");
    host_turn(EN_K1, 1);
    CHECK(pat_len(PAT) == 64u, "64 steps at most");
    host_turn(EN_K1, -1);
    CHECK(pat_len(PAT) == 32u, "turned back: half (%u)", pat_len(PAT));
    h_held &= ~(1u << B_GLO);
    host_run(40, 0, 0);

    /* 13. SEQ again: LIVE, the eight sounds at once; with REC + PLAY written, quantized; REC held: QUANTIZE */
    host_boot(9);
    memset(pat[0].s, 0, sizeof pat[0].s);
    host_tap(B_SEQ);                             /* (it starts on SEQ: a tap is the second press) */
    CHECK(ui.page == PG_LIVE, "SEQ again: LIVE");
    reset_count();
    h_keys |= key_white(0) | key_white(1) | key_white(3) | key_white(15);
    host_run(60, watch, 0);
    h_keys = 0;
    host_run(40, watch, 0);
    CHECK(hits[0] == 1u && hits[1] == 1u && hits[3] == 1u && hits[7] == 1u, "four pads at once: four sounds (%u %u %u %u)",
          hits[0], hits[1], hits[3], hits[7]);
    snprintf(path, sizeof path, "%s/lx_live.ppm", dir);
    host_ppm(path);
    h_pressed |= 1u << B_REC; h_held |= 1u << B_REC;   /* REC held: QUANTIZE (and not armed) */
    host_run(500, 0, 0);
    h_held &= ~(1u << B_REC);
    host_run(40, 0, 0);
    CHECK(ui.page == PG_REC && !song.rec, "REC held: QUANTIZE, not armed (page %u rec %u)", ui.page, song.rec);
    host_turn(EN_K1, 2);
    CHECK(song.qgrid == Q_4, "QUANTIZE GRID 1/4");
    snprintf(path, sizeof path, "%s/lx_quant.ppm", dir);
    host_ppm(path);
    host_tap(B_SEQ);
    host_tap(B_SEQ);
    host_tap(B_REC);
    CHECK(song.rec, "REC tapped: armed");
    host_tap(B_PLAY);
    host_run(BAR_MS / 16u * 5u + 30u, 0, 0);     /* step 6 (index 5): to the beat before it (BEFORE) or after (NEAREST) */
    host_key(key_white(1));
    host_run(BAR_MS, 0, 0);
    CHECK(pat[0].s[1][4] && !pat[0].s[1][5], "the live hit on step 6 quantized to 1/4: step 5 (%u %u)", pat[0].s[1][4],
          pat[0].s[1][5]);
    host_tap(B_PLAY);
    host_tap(B_REC);

    /* 14. off SEQ the white keys play the sounds (and only SEQ shows the steps); PATTERNS: K3 copy, K4 paste */
    host_boot(10);
    host_tap(B_FX);
    reset_count();
    host_key(key_white(2));
    host_run(40, watch, 0);
    CHECK(hits[2] == 1u && !pat[0].s[song.sel][2], "FX page, white key 3: the clap played, no step set");
    h_keys |= key_white(5);
    host_run(20, 0, 0);
    CHECK(h_led_key[__builtin_ctz(key_white(5))], "a pad lights as it is hit");
    h_keys = 0;
    host_run(400, 0, 0);
    CHECK(!h_led_key[__builtin_ctz(key_white(0))], "off SEQ the steps are not lit (the kick on step 1)");
    CHECK(song.sel == 5u, "on a sound's page (FX sends) the sound played is the one shown (%u)", song.sel);
    sel_track(0);
    host_tap(B_SEQ);
    host_run(30, 0, 0);
    CHECK(h_led_key[__builtin_ctz(key_white(0))], "on SEQ the steps are lit (the kick on step 1)");
    host_tap(B_SCL);
    host_turn(EN_K3, 1);
    CHECK(pget(P_PCOPY) == 1, "PATTERNS K3: pattern 1 copied");
    host_turn(EN_K1, 2);                         /* pattern 3 (stopped: shown at once) */
    host_turn(EN_K4, 1);
    CHECK(song.pat == 2u && !memcmp(pat[2].s, pat[0].s, sizeof pat[0].s), "PATTERNS K4: pasted into pattern 3");
    snprintf(path, sizeof path, "%s/lx_patterns.ppm", dir);
    host_ppm(path);

    /* 15. STEP EDIT: a set step held long, KNOB 1..4 its velocity, probability, pan, accent; a tap still clears */
    host_boot(11);
    sel_track(1);                                /* the snare: steps 5 and 13 */
    h_keys |= key_white(4);
    host_run(500, 0, 0);
    CHECK(ui.sedit == (1u << 4), "held 0.5 s: STEP EDIT on step 5 (%x)", ui.sedit);
    host_turn(EN_K1, -40);
    host_turn(EN_K2, -8);                        /* 100 % -> 8/16: 50 % */
    host_turn(EN_K3, 2);                         /* the sound's -> L32 */
    host_turn(EN_K4, 2);                         /* SUBSTEPS: 1 -> 3 */
    snprintf(path, sizeof path, "%s/lx_stepedit.ppm", dir);
    host_ppm(path);
    h_keys = 0;
    host_run(40, 0, 0);
    CHECK(PAT->s[1][4] == 60u && X_COND(PAT->x[1][4]) == 8u && X_PAN(PAT->x[1][4]) == 2u && PAT->r[1][4] == 2u && !ui.sedit,
          "STEP EDIT: velo 60, 50 %%, L32, 3 substeps; let go: kept (%u %u %u %u)", PAT->s[1][4], X_COND(PAT->x[1][4]),
          X_PAN(PAT->x[1][4]), PAT->r[1][4]);
    host_key(key_white(12));
    CHECK(!PAT->s[1][12], "a set step tapped: cleared when let go");
    host_key(key_white(12));
    CHECK(PAT->s[1][12] == 100u, "an empty step tapped: set");
    {                                            /* probability 50 %: about half the passes; the step's pan heard */
        uint32_t pass, played = 0;
        memset(pat[0].s, 0, sizeof pat[0].s);
        memset(pat[0].x, 0, sizeof pat[0].x);
        pat[0].s[1][0] = 100;
        pat[0].x[1][0] = 8u << 3;
        host_tap(B_PLAY);
        for (pass = 0; pass < 40u; pass++) {
            reset_count();
            host_run(BAR_MS, watch, 0);
            played += hits[1] > 0u;
        }
        host_tap(B_PLAY);
        CHECK(played > 10u && played < 30u, "PROB 50 %%: played in %u of 40 bars", played);
        pat[0].x[1][0] = 1u;                     /* hard left */
        ev_push(EV_HIT, 1, 1);                   /* (any hit first: the step's own pan comes with the sequencer) */
        voice_hit(1, 120, 0, X_PANV(1u));
        CHECK(voice[1].span == -48, "the step's pan reaches the voice (%d)", voice[1].span);
        {                                        /* A:B: 1:2 the odd passes, 2:4 the 2nd of 4, 8:8 the 8th of 8 */
            static const uint8_t CODE[3] = {16, 19, 29};
            static const uint8_t WANT[3][8] = {{1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 0, 0, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}};
            uint32_t c, bar, ok;
            for (c = 0; c < 3u; c++) {
                pat[0].x[1][0] = (uint8_t)(CODE[c] << 3);
                host_tap(B_PLAY);
                host_run(BAR_MS / 2u, 0, 0);     /* (mid bar 1: each window from here holds the next bar's step 1) */
                for (bar = 0, ok = 1; bar < 8u; bar++) {
                    reset_count();
                    host_run(BAR_MS, watch, 0);
                    ok &= (hits[1] > 0u) == WANT[c][(bar + 1u) % 8u];
                }
                host_tap(B_PLAY);
                CHECK(ok, "condition %u:%u plays the right bars", COND_A[CODE[c] - 16u], COND_B[CODE[c] - 16u]);
            }
            pat[0].x[1][0] = 0;

            ui.sedit = 1;                        /* (the knob: past 100 % the conditions) */
            ui.bar = 0;
            pat[0].s[1][0] = 100;
            pset(P_EPROB, 17);
            CHECK(X_COND(pat[0].x[1][0]) == 16u, "STEP EDIT PROB past 100 %%: 1:2");
            ui.sedit = 0;
        }
        pat[0].x[1][0] = 0;                      /* SUBSTEPS: 4 hits on step 1, 6 on step 9: 10 a bar */
        pat[0].r[1][0] = 3;
        pat[0].s[1][8] = 100;
        pat[0].r[1][8] = 5;
        host_tap(B_PLAY);
        host_run(100, 0, 0);
        reset_count();
        host_run(BAR_MS * 2u, watch, 0);
        host_tap(B_PLAY);
        CHECK(hits[1] >= 19u && hits[1] <= 21u, "substeps 4 + 6 a bar: %u hits in 2 bars (20)", hits[1]);
    }

    /* 16. the store: 14 slots; the formats before (LXP1..3) moved at the first boot; backup / restore over USB */
    {
        static uint8_t img[9000];
        uint32_t i, t, ver, n, sum;
        for (ver = 1; ver <= 3u; ver++) {
            uint32_t ss = ver == 1u ? 22u : ver == 2u ? 27u : 30u, at = ver == 1u ? 28u : 12u, body;
            memset(h_store, 0xFF, sizeof h_store);
            memset(img, 0, sizeof img);
            for (t = 0; t < NTRK; t++) {         /* a sound: p[8] = 10 + t, its level 77 */
                for (i = 0; i < 8u; i++) img[at + t * ss + i] = (uint8_t)(10u + t);
                img[at + t * ss + 8] = TR_OWN0 + 3;
                img[at + t * ss + (ver == 3u ? 16u : 13u)] = 77;   /* level (after syn) */
            }
            n = at + NTRK * ss + (ver == 1u ? 0u : 13u);
            for (i = 0; i < NPAT; i++) {         /* pattern i: track 2, step i at velocity 90, 16 steps */
                img[n + i * 520u + 2u * NSTEP + i] = 90;
                memset(img + n + i * 520u + NTRK * NSTEP, 16, NTRK);
            }
            if (ver >= 2u) img[n + NPAT * 520u] = 133;          /* bpm */
            else img[12] = 133;
            body = ver == 1u ? NTRK * ss + NPAT * 520u : NTRK * ss + 13u + NPAT * 520u + 6u;
            sum = fnv(img + at, body);
            img[0] = 'L'; img[1] = 'X'; img[2] = 'P'; img[3] = (uint8_t)('0' + ver);
            img[4] = 0; img[8] = (uint8_t)sum; img[9] = (uint8_t)(sum >> 8); img[10] = (uint8_t)(sum >> 16); img[11] = (uint8_t)(sum >> 24);
            memcpy(h_store + ST_OLD(5), img, sizeof img);       /* old slot 6 and slot 1 */
            memcpy(h_store + ST_OLD(0), img, sizeof img);
            {
                set_rec_t st = {ver == 1u ? MAGIC_SET1 : MAGIC_SET, PAL_AMBER, 0, 0, 5, 0, 0, 0, 0};
                memcpy(h_store + ST_SET, &st, sizeof st);
            }
            host_boot(12);
            CHECK(ui.used[5] && ui.used[0], "LXP%u: slots 1 and 6 moved to the new layout", ver);
            CHECK(!proj_load(5, SW_ALL) && snd[3].p[0] == 13u && snd[3].level == 77u && snd[3].smp == TR_OWN0 + 3 &&
                  pat[7].s[2][7] == 90u && !pat[7].x[2][7] && song.bpm == 133u,
                  "LXP%u: kit and patterns as they were (%u %u %u %u)", ver, snd[3].p[0], snd[3].level, pat[7].s[2][7], song.bpm);
        }
        /* LXP4 (the steps' extras as nibbles, no substeps): converted, probability kept, pan to its nearest place */
        {
            static uint8_t raw[16784];
            kit6_t k;
            uint32_t sum4;
            memset(raw, 0, sizeof raw);
            memset(&k, 0, sizeof k);
            k.s[2].level = 55;
            memcpy(raw + 12, &k, KIT_OLD);
            raw[12 + KIT_OLD + 3u * NSTEP + 5u] = 77;                    /* pattern 1, track 4, step 6 */
            raw[12 + KIT_OLD + NTRK * NSTEP + 3u * NSTEP + 5u] = 8u << 4 | 4u;   /* 50 %, L32 */
            memset(raw + 12 + KIT_OLD + 2u * NTRK * NSTEP, 16, NTRK);
            sum4 = fnv(raw + 12, KIT_OLD + NPAT * 1032u + 6u);
            memcpy(raw, "LXP4", 4);
            raw[8] = (uint8_t)sum4; raw[9] = (uint8_t)(sum4 >> 8); raw[10] = (uint8_t)(sum4 >> 16); raw[11] = (uint8_t)(sum4 >> 24);
            CHECK(!proj_convert(raw, sizeof raw) && proj_buf.k.s[2].level == 55u && !dec_blob(&proj_buf, 1) && pat[0].s[3][5] == 77u &&
                  X_COND(pat[0].x[3][5]) == 8u && X_PAN(pat[0].x[3][5]) == 2u,
                  "LXP4 converted: kit, step, 50 %%, L32 (%u %u)", X_COND(pat[0].x[3][5]), X_PAN(pat[0].x[3][5]));
        }
        /* USB: info, a slot read back whole, restored into another slot */
        {
            uint8_t f[400], raw[300];
            uint32_t k, off;
            static uint8_t got[sizeof(proj_t)];
            f[0] = 0x7D; f[1] = 'O'; f[2] = 'C'; f[3] = 0x40;
            proto_frame(f, 4);
            k = unpack7(h_sent + 5, h_sent_n - 6, raw, sizeof raw);
            CHECK(h_sent[4] == 0x41 && !memcmp(raw, "LUXR", 4) && raw[4 + str_len((char *)raw + 4) + 1] == NSLOT,
                  "USB info: LUXR, %u slots", NSLOT);
            for (off = 0; off < sizeof(proj_t); off += 256u) {
                uint8_t q[5] = {5, (uint8_t)off, (uint8_t)(off >> 8), 0, 0};
                f[3] = 0x42;
                k = 4u + pack7(q, 5, f + 4);
                proto_frame(f, k);
                k = unpack7(h_sent + 5, h_sent_n - 6, raw, sizeof raw);
                memcpy(got + off, raw + 5, k - 5u);
            }
            CHECK(rd32(got) == MAGIC_PROJ, "USB: slot 6 read back (LXP4)");
            for (off = 0; off < sizeof(proj_t); off += 256u) {
                uint8_t q[261];
                uint32_t m = sizeof(proj_t) - off < 256u ? sizeof(proj_t) - off : 256u;
                q[0] = 11; wr32(q + 1, off);
                memcpy(q + 5, got + off, m);
                f[3] = 0x44;
                k = 4u + pack7(q, 5u + m, f + 4);
                proto_frame(f, k);
            }
            {
                uint8_t q[5] = {11, 0, 0, 0, 0};
                wr32(q + 1, sizeof(proj_t));
                f[3] = 0x46;
                k = 4u + pack7(q, 5, f + 4);
                proto_frame(f, k);
                unpack7(h_sent + 5, h_sent_n - 6, raw, sizeof raw);
            }
            CHECK(h_sent[4] == 0x47 && raw[0] == 0 && ui.used[11], "USB restore into slot 12: ok");
            snd[3].p[0] = 0;
            CHECK(!proj_load(11, SW_ALL) && snd[3].p[0] == 13u, "slot 12 loads what slot 6 had");
            memcpy(stage, img, sizeof img);      /* an old-format backup restored: converted on the way in */
            {
                uint8_t q[5] = {12, 0, 0, 0, 0};
                wr32(q + 1, sizeof img);
                f[3] = 0x46;
                k = 4u + pack7(q, 5, f + 4);
                proto_frame(f, k);
                unpack7(h_sent + 5, h_sent_n - 6, raw, sizeof raw);
            }
            CHECK(raw[0] == 0 && ui.used[12], "an LXP3 backup restored into slot 13, converted");
            CHECK(ui.restoring && ui.rs_done == 2u, "restoring: the loading screen, 2 patches in (%u)", ui.rs_done);
            ui_draw(0);
            snprintf(path, sizeof path, "%s/lx_restore.ppm", dir);
            host_ppm(path);
            snd[3].p[0] = 0;
            host_run(1000, 0, 0);
            CHECK(ui.restoring, "still restoring a second later");
            host_run(2000, 0, 0);
            CHECK(!ui.restoring && !strncmp(ui.msg, "PATCHES RESTORED", 16) && snd[3].p[0] == 13u,
                  "2.5 s quiet: the restore is over, the patch loaded again (%s)", ui.msg);
            boot_screen(65, "STARTING THE SOUND");
            snprintf(path, sizeof path, "%s/lx_boot.ppm", dir);
            host_ppm(path);
        }
    }

    /* 18. HOME: tapped the KIT, held SYSTEM; at power-on patch 1 is loaded */
    host_boot(14);
    host_tap(B_HOME);
    CHECK(ui.page == PG_KIT, "HOME tapped: KIT");
    host_hold(B_HOME, 500, 0);
    CHECK(ui.page == PG_INFO, "HOME held: SYSTEM");
    snprintf(path, sizeof path, "%s/lx_info.ppm", dir);
    host_ppm(path);
    snd[4].level = 21;
    pat[0].s[6][3] = 99;
    pat[0].r[6][3] = 4;
    proj_save(0, SW_ALL);
    host_boot(15);
    CHECK(snd[4].level == 21u && pat[0].s[6][3] == 99u && pat[0].r[6][3] == 4u, "power-on: patch 1 loaded, substeps kept (%u %u %u)",
          snd[4].level, pat[0].s[6][3], pat[0].r[6][3]);

    /* 17. SHIFT + ALGORITHM: the sample layer's samples in turn, the synth layer kept (off SEQ; on SEQ: a random
     * pattern for the track, turned back: the track doubled) */
    host_boot(13);
    {
        uint32_t n0;
        h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
        host_run(300, 0, 0);
        memset(PAT->s[0], 0, NSTEP);
        host_turn(EN_ALGO, 1);
        n0 = steps_on(song.pat, 0);
        CHECK(n0 >= 4u, "SEQ, SHIFT + ALGORITHM: a random kick pattern (%u steps)", n0);
        host_turn(EN_ALGO, -1);
        CHECK(PAT->len[0] == 32u && steps_on(song.pat, 0) == 2u * n0, "turned back: the track doubled (len %u)", PAT->len[0]);
        h_held &= ~(1u << B_GLO);
        host_run(40, 0, 0);
    }
    host_tap(B_EDIT);
    {
        uint8_t p0[8], s0 = snd[0].smp;
        memcpy(p0, snd[0].p, 8);
        h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
        host_run(300, 0, 0);
        host_turn(EN_ALGO, 1);
        CHECK(snd[0].smp == s0 + 1u && !memcmp(snd[0].p, p0, 8), "SHIFT + ALGORITHM: the next sample (%u -> %u), synth kept",
              s0, snd[0].smp);
        host_turn(EN_ALGO, -2);
        CHECK(snd[0].smp == s0 - 1u, "turned back: the one before (%u)", snd[0].smp);
        snd[0].smp = TR_N - 1;
        host_turn(EN_ALGO, 1);
        CHECK(snd[0].smp == TR_SMP0, "past the last: the first sample again (%u)", snd[0].smp);
        h_held &= ~(1u << B_GLO);
        host_run(40, 0, 0);
    }


    /* 19. LXP7: P-LOCK, motion rec, NUDGE, FILL, track speed, SONG, MIDI clock out / in, note out, CC learn, the
     * master filter, the store (from an empty store: the demo pattern) */
    memset(h_store, 0xFF, sizeof h_store);
    host_boot(20);
    {                                            /* P-LOCK: C#5, a step held, a knob: the step's lock */
        const lock_t *l;
        host_tap(B_EDIT);
        CHECK(ui.page == PG_VOICE, "EDIT: SYNTH 1-4");
        host_key(key_blk(BK_PLOCK));
        CHECK(ui.lockmode == LM_LOCK, "C#5: P-LOCK on");
        h_keys |= key_white(0);
        host_run(60, 0, 0);
        host_turn(EN_K2, 20);
        snprintf(path, sizeof path, "%s/lx_plock.ppm", dir);
        host_ppm(path);
        h_keys &= ~key_white(0);
        host_run(40, 0, 0);
        l = lock_find(plk[0], 0, 0, P_V1);
        CHECK(l && l->val == 100u && snd[0].p[1] == 80u && pat[0].s[0][0], "a lock on step 1: DECAY 100, the sound kept at 80 (%u)",
              l ? l->val : 0u);
        reset_at();
        seq_play(1);
        host_run(60, watch_at, 0);
        CHECK(voice[0].nlk == 1u && voice[0].lk[0][0] == P_V1 && voice[0].lk[0][1] == 100u, "the hit of step 1 carries its lock");
        host_run(500, watch_at, 0);
        CHECK(hits[0] >= 2u && voice[0].nlk == 0u, "step 5: no lock (%u hits)", hits[0]);
        seq_play(0);
        host_run(40, 0, 0);
        h_keys |= key_white(0);                  /* held + C#5: its locks go, the step stays */
        host_run(60, 0, 0);
        host_key(key_blk(BK_PLOCK));
        h_keys &= ~key_white(0);
        host_run(40, 0, 0);
        CHECK(!lock_find(plk[0], 0, 0, P_V1) && pat[0].s[0][0] && ui.lockmode == LM_LOCK, "step held + C#5: its locks cleared, step kept");
        /* motion rec: P-LOCK, REC armed, playing: the knob writes the steps playing (with a hit) */
        song.rec = 1;
        seq_play(1);
        {
            uint32_t k;
            for (k = 0; k < 90u; k++)
                host_turn(EN_K3, 1);             /* (P.ENV, 20 ms a turn: a bar and a bit) */
        }
        seq_play(0);
        song.rec = 0;
        host_run(40, 0, 0);
        CHECK(locks_of(0, 0) >= 4u && snd[0].p[2] == 80u, "motion rec: %u locks on the kick, the sound kept", locks_of(0, 0));
        host_key(key_blk(BK_PLOCK));
        CHECK(ui.lockmode == LM_OFF, "C#5 again: P-LOCK off");
    }
    host_boot(21);
    {                                            /* NUDGE: D#5, a step held, KNOB 1: its timing in 32nds */
        uint32_t d0, d1, sl = FS * 15u / 120u;
        song.sel = 1;                            /* (the snare) */
        host_key(key_blk(BK_NUDGE));
        CHECK(ui.lockmode == LM_NUDGE, "D#5: NUDGE on");
        h_keys |= key_white(4);
        host_run(60, 0, 0);
        host_turn(EN_K1, 8);
        h_keys &= ~key_white(4);
        host_run(40, 0, 0);
        CHECK(R_MICRO(pat[0].r[1][4]) == 8 && pat[0].s[1][4], "snare step 5 nudged +8/32 (%d)", R_MICRO(pat[0].r[1][4]));
        snprintf(path, sizeof path, "%s/lx_nudge.ppm", dir);
        host_ppm(path);
        reset_at();
        seq_play(1);
        host_run(1000, watch_at, 0);
        seq_play(0);
        host_run(300, 0, 0);
        d0 = hat_n[1] && hat_n[0] ? hat[1][0] - hat[0][0] : 0u;
        CHECK(d0 + 64u > 4u * sl + sl / 4u && d0 < 4u * sl + sl / 4u + 64u, "late: %u samples after the downbeat (%u)", d0,
              4u * sl + sl / 4u);
        pat[0].r[1][4] = R_MAKE(0, -8);
        reset_at();
        seq_play(1);
        host_run(1000, watch_at, 0);
        seq_play(0);
        host_run(300, 0, 0);
        d1 = hat_n[1] && hat_n[0] ? hat[1][0] - hat[0][0] : 0u;
        CHECK(d1 + 64u > 4u * sl - sl / 4u && d1 < 4u * sl - sl / 4u + 64u && hits[1] == 1u, "early: %u samples (%u), played once (%u)",
              d1, 4u * sl - sl / 4u, hits[1]);
        host_key(key_blk(BK_NUDGE));
    }
    host_boot(22);
    {                                            /* FILL: a step that plays only while F#5 is held (and NOT FILL) */
        pat[0].x[2][12] = (uint8_t)(C_FILL << 3);
        pat[0].s[7][8] = 100;
        pat[0].x[7][8] = (uint8_t)(C_NFILL << 3);
        reset_count();
        seq_play(1);
        host_run(2000, watch, 0);
        CHECK(hits[2] == 0u && hits[7] >= 1u, "no FILL: the FILL step quiet, the NOT FILL one plays (%u %u)", hits[2], hits[7]);
        h_keys |= key_blk(BK_FILL);
        reset_count();
        host_run(2000, watch, 0);
        CHECK(song.fill && hits[2] == 1u && hits[7] == 1u, "F#5 held: FILL (%u %u)", hits[2], hits[7]);   /* (the cymbal: its step 1) */
        h_keys &= ~key_blk(BK_FILL);
        host_run(40, 0, 0);
        CHECK(!song.fill, "F#5 let go: FILL off");
        seq_play(0);
        host_run(40, 0, 0);
        ui.sedit = 1u << 4;                      /* STEP EDIT's PROB card: .. 8:8, FILL, !FILL */
        song.sel = 1;
        pset(P_EPROB, 31);
        CHECK(X_COND(pat[0].x[1][4]) == C_FILL, "PROB 31: FILL");
        {
            char v[16]; const char *u;
            pformat(P_EPROB, v, &u);
            CHECK(!strcmp(v, "FILL"), "its card: %s", v);
        }
        ui.sedit = 0;
    }
    host_boot(23);
    {                                            /* SPEED: the closed hat (8 a bar) at 2x, then 1/2x */
        reset_count();
        song.sel = 3;
        pset(P_SPEED, 2);
        CHECK(pat[0].spd[3] == 2 && pat_len(&pat[0]) == 16u, "SPEED 2X (the pattern stays a bar)");
        seq_play(1);
        host_run(4000, watch, 0);
        CHECK(hits[3] >= 31u && hits[3] <= 33u && hits[0] >= 9u && hits[0] <= 11u, "2X: %u hats in 2 bars (32), kicks %u", hits[3], hits[0]);
        seq_play(0);
        host_run(40, 0, 0);
        pset(P_SPEED, -2);
        CHECK(pat_len(&pat[0]) == 32u, "1/2X: the pattern two bars");
        reset_count();
        seq_play(1);
        host_run(4000, watch, 0);
        CHECK(hits[3] >= 7u && hits[3] <= 9u, "1/2X: %u hats in 2 bars (8)", hits[3]);
        seq_play(0);
        host_run(40, 0, 0);
        {
            char v[16]; const char *u;
            pformat(P_SPEED, v, &u);
            CHECK(!strcmp(v, "1/2X"), "SPEED card: %s", v);
        }
    }
    host_boot(24);
    {                                            /* SONG: P1 x2, P3 x1, loop; then STOP at the end */
        host_tap(B_SCL);
        host_tap(B_SCL);
        CHECK(ui.page == PG_SONG, "SCL twice: SONG");
        pset(P_SLEN, 2);
        pset(P_SROW, 1); pset(P_SPAT, 1); pset(P_SREP, 2);
        pset(P_SROW, 2); pset(P_SPAT, 3); pset(P_SREP, 1);
        pset(P_SMODE, 1);
        memcpy(&pat[2], &pat[0], sizeof pat[0]);
        snprintf(path, sizeof path, "%s/lx_song.ppm", dir);
        ui_draw(1);
        host_ppm(path);
        seq_play(1);
        host_run(1000, 0, 0);
        CHECK(song.pat == 0u, "bar 1: P1");
        host_run(2000, 0, 0);
        CHECK(song.pat == 0u && song.srep == 1u, "bar 2: P1 again");
        host_run(2000, 0, 0);
        CHECK(song.pat == 2u && song.srow == 1u, "bar 3: P3 (%u)", song.pat + 1u);
        host_run(2000, 0, 0);
        CHECK(song.pat == 0u && song.playing, "bar 4: P1, looped");
        seq_play(0);
        host_run(40, 0, 0);
        pset(P_SEND, 1);
        seq_play(1);
        host_run(6200, 0, 0);
        CHECK(!song.playing, "AT END STOP: stopped after 3 bars");
    }
    host_boot(25);
    {                                            /* MIDI clock out: START, 24 a quarter, STOP; note out on channel 10 */
        uint32_t a, b;
        song.clk_out = 1;
        snd[0].och = 10;
        host_run(100, 0, 0);
        h_mout_n = 0;
        seq_play(1);
        host_run(2000, 0, 0);
        seq_play(0);
        host_run(100, 0, 0);
        a = mout_find(0xFFFFu, 0xFA0Fu, 0);
        b = mout_find(0xFFFFu, 0xFC0Fu, 0);
        CHECK(a != 0xFFFFFFFFu && b != 0xFFFFFFFFu && b > a, "clock out: START, STOP");
        CHECK(a != 0xFFFFFFFFu && mout_count(0xFFFFu, 0xF80Fu, a, b) >= 95u && mout_count(0xFFFFu, 0xF80Fu, a, b) <= 98u,
              "clock out: %u ticks in a bar (96)", a != 0xFFFFFFFFu ? mout_count(0xFFFFu, 0xF80Fu, a, b) : 0u);
        CHECK(mout_count(0xFFFFFFu, 0x9u | 0x99u << 8 | 36u << 16, 0, h_mout_n) == 6u &&
              mout_count(0xFFFFFFu, 0x8u | 0x89u << 8 | 36u << 16, 0, h_mout_n) == 6u,
              "note out: 6 kicks (a bar and the next downbeat), note 36 on channel 10, on and off (%u %u)",
              mout_count(0xFFFFFFu, 0x9u | 0x99u << 8 | 36u << 16, 0, h_mout_n), mout_count(0xFFFFFFu, 0x8u | 0x89u << 8 | 36u << 16, 0, h_mout_n));
        CHECK(mout_count(0xF00Fu, 0x9009u, 0, h_mout_n) == 6u, "only the sound with a channel is sent (%u note-ons)", mout_count(0xF00Fu, 0x9009u, 0, h_mout_n));
    }
    host_boot(26);
    {                                            /* MIDI clock in: EXT follows START and the ticks, the tempo from them */
        uint32_t k, t0;
        double per = 44100.0 * 60.0 / (140.0 * 24.0);
        song.clk_src = CK_EXT;
        reset_count();
        host_midi_in(0xFA, 0, 0);
        t0 = h_samples;
        for (k = 0; k <= 192u; k++) {            /* two bars at 140 (and the tick of the third's downbeat) */
            host_midi_in(0xF8, 0, 0);
            run_samples((uint32_t)(t0 + (k + 1) * per) - h_samples, watch);
        }
        CHECK(song.playing && song.loops == 2u && song.pstep == 0u, "EXT: two bars of ticks, two passes (%u %u)", song.loops, song.pstep);
        CHECK(song.bpm >= 139u && song.bpm <= 141u, "EXT: the tempo from the clock (%u)", song.bpm);
        CHECK(hits[0] == 11u, "EXT: the kicks as the ticks say (%u: 5 a bar, then the downbeat)", hits[0]);
        host_midi_in(0xFC, 0, 0);
        run_samples(200, 0);
        CHECK(!song.playing, "EXT: STOP");
        seq_play(1);                             /* PLAY in EXT: waits for the clock */
        run_samples(4000, 0);
        CHECK(!song.playing && xc.arm == 1u, "EXT: PLAY waits for a tick");
        host_midi_in(0xF8, 0, 0);
        run_samples(100, 0);
        CHECK(song.playing, "EXT: the tick starts it");
        seq_play(0);
        run_samples(100, 0);
    }
    host_boot(27);
    {                                            /* CC learn: the MIDI page, LEARN, a knob, a controller */
        host_tap(B_HOME);
        host_tap(B_HOME);
        CHECK(ui.page == PG_MIDI, "HOME twice: MIDI");
        snprintf(path, sizeof path, "%s/lx_midi.ppm", dir);
        host_ppm(path);
        pset(P_LEARN, 1);
        host_tap(B_EDIT);
        host_turn(EN_K2, 1);
        CHECK(ui.learn == 2u && ui.lpid == P_V1 && ui.ltrk == 0u, "LEARN: the kick's DECAY armed");
        host_midi_in(0xB0, 21, 64);
        host_run(40, 0, 0);
        CHECK(ccmap[0].pid == P_V1 && ccmap[0].cc == 21u && ccmap[0].ch == 0u && ccmap[0].trk == 0u, "CC 21 mapped");
        pset(P_LEARN, 0);
        song.sel = 3;
        host_midi_in(0xB0, 21, 127);
        host_run(40, 0, 0);
        CHECK(snd[0].p[1] == 127u && song.sel == 3u, "CC 21 = 127: the kick's DECAY 127 (the hat selected)");
        host_midi_in(0xB0, 21, 0);
        host_midi_in(0xB1, 21, 64);              /* (another channel: not mapped) */
        host_run(40, 0, 0);
        CHECK(snd[0].p[1] == 0u, "CC 21 = 0: DECAY 0 (%u)", snd[0].p[1]);
        pset(P_LEARN, 1);                        /* the master filter, learnt too */
        host_tap(B_FX); host_tap(B_FX); host_tap(B_FX); host_tap(B_FX); host_tap(B_FX); host_tap(B_FX);
        CHECK(ui.page == PG_MFX, "FX six times: MASTER");
        host_turn(EN_K1, -1);
        host_midi_in(0xB0, 74, 0);
        host_run(40, 0, 0);
        pset(P_LEARN, 0);
        host_midi_in(0xB0, 74, 32);
        host_run(40, 0, 0);
        CHECK(song.mflt < -20, "CC 74 moves the master filter (%d)", song.mflt);
        snprintf(path, sizeof path, "%s/lx_master.ppm", dir);
        host_ppm(path);
        host_boot(28);                           /* (the map is kept with the settings) */
        CHECK(ccmap[0].pid == P_V1 && ccmap[1].pid == P_MFLT, "the CC map after a power-on");
        pset(P_LEARN, 2);
        CHECK(!ccmap[0].pid && !ccmap[1].pid, "LEARN CLEAR: the map empty");
    }
    host_boot(29);
    {                                            /* the master filter: a low-pass takes the hat's highs */
        double e[2];
        uint32_t m, k, j;
        for (m = 0; m < 2u; m++) {
            song.mflt = (int8_t)(m ? -50 : 0);
            e[m] = 0;
            ev_push(EV_HIT, 3, 120);
            for (k = 0; k < 10u; k++) {
                host_run(20, 0, 0);
                for (j = 0; j < CTL; j++) e[m] += out_l[j] * out_l[j];
            }
            host_run(300, 0, 0);
        }
        CHECK(e[1] < e[0] * 0.3, "MASTER LP: the hat %.0f %% as loud", 100.0 * e[1] / (e[0] > 0 ? e[0] : 1));
        song.mflt = 0;
    }
    host_boot(30);
    {                                            /* the store: locks, NUDGE, speeds, the song, MIDI OUT kept; LXP6 read */
        static proj6_t o;
        uint32_t t, i, k;
        lock_set(plk[5], 2, 7, P_SPITCH, 99);
        pat[5].s[2][7] = 90;
        pat[5].r[2][7] = R_MAKE(3, -5);
        pat[5].spd[6] = -1;
        snd[1].och = 7; snd[1].onote = 40;
        song.slen = 3; song.smode = 1; srows[2].pat = 9; srows[2].rep = 4;
        proj_save(3, SW_ALL);
        CHECK(ui.used[3], "LXP7 saved");
        host_boot(31);
        CHECK(!proj_load(3, SW_ALL), "LXP7 loaded");
        CHECK(lock_find(plk[5], 2, 7, P_SPITCH) && lock_find(plk[5], 2, 7, P_SPITCH)->val == 99u && pat[5].s[2][7] == 90u &&
              R_SUB(pat[5].r[2][7]) == 3u && R_MICRO(pat[5].r[2][7]) == -5 && pat[5].spd[6] == -1,
              "kept: the lock, substeps 3 and NUDGE -5, SPEED 3/4X");
        CHECK(snd[1].och == 7u && snd[1].onote == 40u && song.slen == 3u && song.smode && srows[2].pat == 9u && srows[2].rep == 4u,
              "kept: the snare's MIDI OUT, the song's rows");
        /* a full pattern of every step (the old formats' worst): still fits */
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < NSTEP; i++)
                for (k = 0; k < NPAT; k++) { pat[k].s[t][i] = 77; pat[k].x[t][i] = 9; pat[k].r[t][i] = 2; pat[k].len[t] = 64; }
        proj_save(4, SW_ALL);
        CHECK(!strncmp(ui.msg, "PATCH SAVED", 11), "every step of every pattern set: saved (%s)", ui.msg);
        /* an LXP6 patch as the firmware before wrote it */
        memset(&o, 0, sizeof o);
        o.magic = MAGIC_P6;
        o.size = sizeof o;
        for (t = 0; t < NTRK; t++) { memset(o.k.s[t].p, 30 + (int)t, 8); o.k.s[t].level = 66; o.k.s[t].smp = TR_OWN0 + 2; }
        o.k.ddec = 64; o.k.dly_mode = DL_STEREO; o.k.dly_div2 = 4;
        for (k = 0; k < NPAT; k++) memset(o.p.p[k].len, 16, NTRK);
        o.p.p[6].s[4][9] = 88;
        o.p.p[6].x[4][9] = (uint8_t)(20u << 3 | 3u);
        o.p.p[6].r3[(3u * (4u * NSTEP + 9u)) >> 3] |= (uint8_t)(4u << ((3u * (4u * NSTEP + 9u)) & 7u));
        o.p.bpm = 97;
        o.sum = fnv((const uint8_t *)&o.k, sizeof o.k + sizeof o.p);
        memset(h_store + ST_PROJ(9), 0xFF, 0x5000);
        memcpy(h_store + ST_PROJ(9), &o, sizeof o);
        host_boot(32);
        CHECK(ui.used[9] && !proj_load(9, SW_ALL), "LXP6 slot 10: found, loaded");
        CHECK(snd[2].p[0] == 32u && snd[2].level == 66u && snd[2].och == 0u && snd[2].onote == 39u && song.dly_mode == DL_STEREO &&
              song.dly_div2 == 4u && pat[6].s[4][9] == 88u && X_COND(pat[6].x[4][9]) == 20u && R_SUB(pat[6].r[4][9]) == 4u &&
              song.bpm == 97u, "LXP6: kit, step, condition, substeps, tempo as they were");
    }
    host_boot(34);
    {                                            /* a step set on SEQ is silent; LOAD on an empty slot: a new empty patch;
                                                  * SAVE twice: NEW PATCH (length, speed, tempo, kit) */
        uint32_t t, n = 0, k;
        song.sel = 1;
        reset_count();
        host_key(key_white(2));
        host_run(300, watch, 0);
        CHECK(pat[0].s[1][2] && hits[1] == 0u, "SEQ: a step set without a sound (%u)", hits[1]);
        host_tap(B_SAVE);
        CHECK(ui.page == PG_SAVE, "SAVE: STORE");
        host_turn(EN_K1, 6);                     /* slot 7, empty */
        snd[0].p[0] = 3;
        host_turn(EN_K4, 1);
        for (t = 0; t < NTRK; t++) for (k = 0; k < NSTEP; k++) n += pat[0].s[t][k] != 0u;
        CHECK(!ui.used[6] && n == 0u && !song.playing && snd[0].p[0] == LX_FACTORY[0][0], "LOAD an empty slot: an empty patch (%u steps), %s",
              n, ui.msg);
        host_tap(B_SAVE);
        CHECK(ui.page == PG_NEW, "SAVE again: NEW PATCH");
        host_turn(EN_K1, 16);                    /* 32 steps */
        host_turn(EN_K2, -1);                    /* 3/4X */
        host_turn(EN_K3, 20);                    /* 140 BPM */
        h_pressed |= 1u << B_GLO; h_held |= 1u << B_GLO;
        host_run(300, 0, 0);
        host_turn(EN_K1, 1);                     /* KIT: KEEP */
        h_held &= ~(1u << B_GLO);
        host_run(40, 0, 0);
        snd[0].p[0] = 9;
        pat[3].s[2][2] = 50;
        snprintf(path, sizeof path, "%s/lx_new.ppm", dir);
        host_ppm(path);
        host_turn(EN_K4, 1);
        CHECK(pat[3].s[2][2] == 0u && pat[5].len[4] == 32u && pat[5].spd[4] == -1 && song.bpm == 140u && snd[0].p[0] == 9u,
              "NEW PATCH: empty, 32 steps, 3/4X, 140 BPM, the kit kept (%u %d %u)", pat[5].len[4], pat[5].spd[4], song.bpm);
        host_tap(B_OCTUP);                       /* OCT+ on NEW PATCH: saved into the slot chosen at once */
        CHECK(ui.used[ui.slot] && !strncmp(ui.msg, "PATCH SAVED: ", 13) && (uint32_t)atoi(ui.msg + 13) == ui.slot + 1u,
              "OCT+ on NEW PATCH: saved at once into the slot chosen (%s)", ui.msg);
    }
    host_boot(35);
    {                                            /* D#4: RANDOM MUTE, the selected sound stays, no sound played */
        uint32_t k, m0;
        song.sel = 2;
        reset_count();
        host_key(key_blk(BK_AUDITION));
        host_run(200, watch, 0);
        m0 = song.mute;
        CHECK(m0 && !((m0 >> 2) & 1u) && hits[2] == 0u, "D#4: random mute %02X, the clap on, nothing played", m0);
        for (k = 0; k < 5u; k++) {
            host_key(key_blk(BK_AUDITION));
            CHECK(song.mute != m0 && !((song.mute >> 2) & 1u), "D#4 again: another set (%02X)", song.mute);
            m0 = song.mute;
        }
        shift_key(key_blk(BK_MUTE));
        CHECK(!song.mute, "SHIFT + A#3: all on again");
    }
    host_boot(36);
    {                                            /* the white keys on STORE, PATTERNS, SONG: no sounds, the page's choices */
        reset_count();
        host_tap(B_SAVE);
        host_key(key_white(5));
        host_run(200, watch, 0);
        CHECK(ui.slot == 5u && hits[5] == 0u, "STORE: key 6 chooses slot 6, no sound (%u)", ui.slot);
        host_tap(B_SCL);
        CHECK(ui.page == PG_PAT, "SCL: PATTERNS");
        host_key(key_white(3));
        CHECK(song.pat == 3u && song.chain == 1u, "PATTERNS: key 4, pattern 4 alone");
        h_keys |= key_white(1);                  /* key 2 held, key 5: the chain 2-5 */
        host_run(60, 0, 0);
        h_keys |= key_white(4);
        host_run(60, 0, 0);
        h_keys = 0;
        host_run(60, watch, 0);
        CHECK(song.chain0 == 1u && song.chain == 4u && song.pat == 1u && hits[0] == 0u, "keys 2 + 5: chain 2-5 (%u %u)", song.chain0 + 1u,
              song.chain);
        seq_play(1);
        host_run(2000 * 2 + 500, 0, 0);
        CHECK(song.pat == 3u, "the chain plays on: pattern 4 in bar 3 (%u)", song.pat + 1u);
        seq_play(0);
        host_run(40, 0, 0);
        host_tap(B_SCL);
        CHECK(ui.page == PG_SONG, "SCL again: SONG");
        host_key(key_white(0)); host_key(key_white(2)); host_key(key_white(2)); host_key(key_white(6));
        CHECK(song.slen == 4u && srows[0].pat == 0u && srows[1].pat == 2u && srows[2].pat == 2u && srows[3].pat == 6u && song.smode,
              "SONG: keys 1 3 3 7 make the song P1 P3 P3 P7, song on");
        host_tap(B_SCL); host_tap(B_SCL);        /* back to SONG: the next key starts a new song */
        host_key(key_white(9));
        CHECK(song.slen == 1u && srows[0].pat == 9u, "back on SONG: a new song from the first key");
        snprintf(path, sizeof path, "%s/lx_song.ppm", dir);
        host_ppm(path);
    }
    host_boot(37);
    {                                            /* ENV: attack, decay, release after the gate; LXP7 patches read */
        double e0 = 0, e1 = 0;
        uint32_t k, j;
        host_tap(B_ENV);
        CHECK(ui.page == PG_ENV && knob_id(0) == P_EATK && knob_id(1) == P_V1 && knob_id(2) == P_EREL && knob_id(3) == P_EGATE,
              "ENV: ATTACK, DECAY, RELEASE, GATE");
        snd[0].p[1] = 120;                       /* a long kick: the first 10 ms, then the tail after a gate */
        ev_push(EV_HIT, 0, 120);
        for (k = 0; k < 10u; k++) { host_run(1, 0, 0); for (j = 0; j < CTL; j++) e0 += out_l[j] * out_l[j]; }
        host_run(600, 0, 0);
        host_turn(EN_K1, 60);                    /* ATTACK ~56 ms */
        ev_push(EV_HIT, 0, 120);
        for (k = 0; k < 10u; k++) { host_run(1, 0, 0); for (j = 0; j < CTL; j++) e1 += out_l[j] * out_l[j]; }
        CHECK(snd[0].atk == 60u && e1 < e0 * 0.25, "ATTACK: the first 10 ms softer (%.0f %%)", 100.0 * e1 / (e0 > 0 ? e0 : 1));
        host_run(600, 0, 0);
        snd[0].atk = 0;
        {
            double t0 = 0, t1 = 0;
            ev_push(EV_HIT, 0, 120);
            host_run(150, 0, 0);
            for (k = 0; k < 20u; k++) { host_run(1, 0, 0); for (j = 0; j < CTL; j++) t0 += out_l[j] * out_l[j]; }
            host_run(600, 0, 0);
            snd[0].gate = 4;                     /* a step, then a release of 1 ms */
            snd[0].rel = 0;
            ev_push(EV_HIT, 0, 120);
            host_run(150, 0, 0);
            for (k = 0; k < 20u; k++) { host_run(1, 0, 0); for (j = 0; j < CTL; j++) t1 += out_l[j] * out_l[j]; }
            CHECK(t1 < t0 * 0.01, "GATE + short RELEASE: the tail cut after a step (%.2f %%)", 100.0 * t1 / (t0 > 0 ? t0 : 1));
        }
        snprintf(path, sizeof path, "%s/lx_env.ppm", dir);
        ui_draw(1);
        host_ppm(path);
        {                                        /* an LXP7 patch: header, its kit (sounds without the envelope), the blob */
            static uint8_t raw[0x5000];
            kit7_t k7;
            uint32_t t, bl, sum;
            memset(raw, 0xFF, sizeof raw);
            memset(&k7, 0, sizeof k7);
            for (t = 0; t < NTRK; t++) { memset(k7.s[t].s6.p, 20 + (int)t, 8); k7.s[t].s6.level = 70; k7.s[t].och = 3; k7.s[t].onote = 50; }
            k7.ddec = 64;
            pat[2].s[5][6] = 44;
            pat_take(&proj_buf);
            bl = proj_buf.blen;
            memcpy(raw, "LXP7", 4);
            wr32(raw + 4, 0x5000u);
            raw[12] = (uint8_t)bl; raw[13] = (uint8_t)(bl >> 8); raw[14] = raw[15] = 0;
            memcpy(raw + 16, &k7, sizeof k7);
            memcpy(raw + 16 + sizeof k7, proj_buf.blob, bl);
            sum = fnv(raw + 12, 4u + sizeof k7 + bl);
            wr32(raw + 8, sum);
            memcpy(h_store + ST_PROJ(8), raw, sizeof raw);
            host_boot(38);
            CHECK(ui.used[8] && !proj_load(8, SW_ALL) && snd[4].p[0] == 24u && snd[4].level == 70u && snd[4].och == 3u &&
                  snd[4].atk == 0u && snd[4].rel == 60u && pat[2].s[5][6] == 44u, "LXP7 slot 9: read, the envelope as before");
        }
    }
    host_boot(33);
    host_tap(B_SEQ);
    {                                            /* the screens of every page (doc/screens) */
        static const char *const N[PG_N] = {"kit", "seq", "live", "synth", "synth2", "sample", "sample2", "sound_mix", "lfo",
                                            "lfo2", "fx_sends", "drive", "delay", "delay2", "reverb", "patterns", "repeat",
                                            "mixer", "store", "quant", "info", "midi", "song", "master"};
        uint32_t pg;
        (void)N; (void)pg;
    }

    /* 9. UBOOT: OCT- and OCT+ 5 s */
    h_pressed |= 3u << B_OCTDN; h_held |= 3u << B_OCTDN;
    host_run(5200, 0, 0);
    CHECK(ui.uboot == 9u, "both OCT 5 s: UBOOT (%u)", ui.uboot);
    h_held &= ~(3u << B_OCTDN);
    host_run(40, 0, 0);

    printf(fails ? "lx: %d FAILED\n" : "lx: all ok\n", fails);
    return fails != 0;
}

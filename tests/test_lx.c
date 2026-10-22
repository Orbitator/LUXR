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
    host_key(key_blk(BK_ACCENT));
    host_key(key_white(0));
    CHECK(PAT->s[5][16] == (100u | S_ACC), "accent edit: the step accented");
    host_key(key_blk(BK_ACCENT));
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
    host_tap(B_SAVE);
    host_turn(EN_K1, -2);                        /* slot 3 again, the patterns only: its kit stays */
    host_turn(EN_K2, 1);
    pat[3].s[4][9] = 66;
    host_tap(B_OCTUP);
    snd[2].level = 1;
    proj_load(2, SW_ALL);
    CHECK(snd[2].level == 33u && pat[3].s[4][9] == 66u, "SAVE PATTERN kept the slot's kit (%u %u)", snd[2].level, pat[3].s[4][9]);
    /* STORE, KNOB 3: what is loaded, KNOB 4: a turn loads the slot */
    host_tap(B_SAVE);
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
    host_tap(B_FX);
    CHECK(ui.page == PG_FXR, "FX again: REVERB");
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
    host_turn(EN_K3, 4);                         /* the sound's -> L32 */
    host_turn(EN_K4, 1);
    snprintf(path, sizeof path, "%s/lx_stepedit.ppm", dir);
    host_ppm(path);
    h_keys = 0;
    host_run(40, 0, 0);
    CHECK(PAT->s[1][4] == (60u | S_ACC) && X_PROB(PAT->x[1][4]) == 8u && X_PAN(PAT->x[1][4]) == 4u && !ui.sedit,
          "STEP EDIT: velo 60, accent, 50 %%, own pan; let go: kept (%u %u %u)", PAT->s[1][4], X_PROB(PAT->x[1][4]),
          X_PAN(PAT->x[1][4]));
    host_key(key_white(12));
    CHECK(!PAT->s[1][12], "a set step tapped: cleared when let go");
    host_key(key_white(12));
    CHECK(PAT->s[1][12] == 100u, "an empty step tapped: set");
    {                                            /* probability 50 %: about half the passes; the step's pan heard */
        uint32_t pass, played = 0;
        memset(pat[0].s, 0, sizeof pat[0].s);
        memset(pat[0].x, 0, sizeof pat[0].x);
        pat[0].s[1][0] = 100;
        pat[0].x[1][0] = 8u << 4;
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
        voice_hit(1, 120, 0, (1 - 8) * 8);
        CHECK(voice[1].span == -56, "the step's pan reaches the voice (%d)", voice[1].span);
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
        }
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

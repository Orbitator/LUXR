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
    /* PRESETS rolls a sound; a few bars of them */
    host_turn(EN_PRESET, 7);
    CHECK(snd[0].preset == 7u, "PRESETS: preset %u", snd[0].preset);
    for (t = 0; t < NTRK; t++)
        sound_preset(t, 11u + t);
    host_run(2 * BAR_MS, watch, 0);
    if (wav) {
        frames0 = (uint32_t)((ftell(wav) - 44) / 4);
        wav_header(wav, frames0);
        fclose(wav);
        wav = 0;
    }
    host_tap(B_PLAY);
    CHECK(!song.playing, "PLAY stops");

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

    /* 7. the store: save, change, load */
    host_boot(5);
    snd[2].level = 33;
    pat[3].s[4][9] = 77;
    host_tap(B_SAVE);
    host_turn(EN_K1, 2);                         /* slot 3 */
    host_tap(B_OCTUP);
    CHECK(ui.used[2], "saved into slot 3");
    snd[2].level = 99;
    pat[3].s[4][9] = 0;
    host_turn(EN_K2, 1);                         /* LOAD */
    host_tap(B_OCTUP);
    CHECK(snd[2].level == 33u && pat[3].s[4][9] == 77u, "loaded back (%u %u)", snd[2].level, pat[3].s[4][9]);
    snprintf(path, sizeof path, "%s/lx_store.ppm", dir);
    host_ppm(path);
    host_tap(B_OCTDN);
    CHECK(ui.page != PG_SAVE, "OCT- leaves the store");
    host_boot(6);                                /* a new boot sees the slot */
    CHECK(ui.used[2] && ui.slot == 2u, "after a boot: slot 3 in use, selected");

    /* 8. the other pages' screens */
    host_tap(B_EDIT);
    CHECK(ui.page == PG_VOICE, "EDIT: the synth layer");
    snprintf(path, sizeof path, "%s/lx_synth.ppm", dir);
    host_ppm(path);
    host_tap(B_EDIT);
    CHECK(ui.page == PG_SAMPLE, "EDIT again: the sample layer");
    snprintf(path, sizeof path, "%s/lx_sample.ppm", dir);
    host_ppm(path);
    host_tap(B_HOME);
    snprintf(path, sizeof path, "%s/lx_kit.ppm", dir);
    host_ppm(path);
    host_tap(B_FX);
    host_tap(B_OCTUP);
    CHECK(song.sel == 1u, "OCT+ on FX: the next sound");

    /* 9. UBOOT: OCT- and OCT+ 5 s */
    h_pressed |= 3u << B_OCTDN; h_held |= 3u << B_OCTDN;
    host_run(5200, 0, 0);
    CHECK(ui.uboot == 9u, "both OCT 5 s: UBOOT (%u)", ui.uboot);
    h_held &= ~(3u << B_OCTDN);
    host_run(40, 0, 0);

    printf(fails ? "lx: %d FAILED\n" : "lx: all ok\n", fails);
    return fails != 0;
}

/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: how often the voice pool cuts audible notes. N songs x S seconds playing, the steals counted by
 * the taking and the losing track (oc_synth.c vstat). voices_bench [songs] [seconds] */
#include "../host/host.c"
int main(int argc, char **argv)
{
    uint32_t songs = argc > 1 ? (uint32_t)atoi(argv[1]) : 40u, sec = argc > 2 ? (uint32_t)atoi(argv[2]) : 8u, s, a, b, held = 0, tail = 0;
    host_boot(3);
    host_press(B_PLAY);
    for (s = 0; s < songs; s++) {
        host_oneclick();
        host_run(sec * 1000u, 0, 0);
    }
    printf("cuts per minute (taker -> victim), %u songs x %u s\n", songs, sec);
    printf("            held notes cut                  tails cut (still audible)\n");
    for (a = 0; a < NTRK; a++) {
        printf("%-7s ->", TYPE_NAME[trk[a].type]);
        for (b = 0; b < NTRK; b++) printf(" %6.1f", vstat.held[a][b] * 60.0 / (songs * sec));
        printf("    ");
        for (b = 0; b < NTRK; b++) printf(" %6.1f", vstat.tail[a][b] * 60.0 / (songs * sec));
        printf("\n");
        for (b = 0; b < NTRK; b++) { held += vstat.held[a][b]; tail += vstat.tail[a][b]; }
    }
    printf("victims:     BASS  CHORDS LEAD   DRUMS\n");
    printf("total: held %.1f/min, audible tails %.1f/min, quiet steals %.1f/min\n", held * 60.0 / (songs * sec),
           tail * 60.0 / (songs * sec), vstat.quiet * 60.0 / (songs * sec));
    printf("audible cuts by level (dB):  -36..-30 %u  -30..-24 %u  -24..-18 %u  -18..-12 %u  -12..-6 %u  > -6 %u; above -20 dB: %.1f/min\n",
           vstat.lvl[0], vstat.lvl[1], vstat.lvl[2], vstat.lvl[3], vstat.lvl[4], vstat.lvl[5], vstat.loud * 60.0 / (songs * sec));
    if (argc > 3 && (held + tail) * 60.0 / (songs * sec) > atof(argv[3])) {   /* a limit: the test fails above it */
        printf("voices_bench: FAIL, more than %s audible cuts a minute\n", argv[3]);
        return 1;
    }
    return 0;
}

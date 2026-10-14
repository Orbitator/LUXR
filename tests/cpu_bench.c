/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: render cost on this computer (only the ratios mean something for the device): typical songs, and the
 * worst case (8 synth voices of 7 saws, 8 drum lanes ringing). cpu_bench */
#include "../host/host.c"
#include <time.h>
static double secs(clock_t c) { return (double)(clock() - c) / CLOCKS_PER_SEC; }
int main(void)
{
    int32_t o[2 * CTL];
    uint32_t b, s, t, k;
    clock_t c;
    double typ, worst, synth8, drums8;
    host_boot(5);
    host_press(B_PLAY);
    c = clock();
    for (s = 0; s < 30u; s++) { host_press(B_REC); host_run(10000, 0, 0); }
    typ = secs(c) / 300.0;                                   /* per second of audio */
    /* 8 supersaw voices, no drums */
    memset(voice, 0, sizeof voice); memset(dvoice, 0, sizeof dvoice); seq_play(0); host_run(50, 0, 0);
    for (t = 0; t < 3; t++) { trk[t].s.engine = E_SAW; trk[t].s.saw_n = 7; trk[t].s.mono = 0; trk[t].s.atk = 0; trk[t].s.sus = 120; trk[t].s.dec = 120; trk[t].s.rel = 120; }
    for (k = 0; k < 8; k++) note_on(k % 3, 48 + k * 3, 100, 0);
    c = clock(); for (b = 0; b < 44100u * 10u / CTL; b++) mix_block(o, CTL); synth8 = secs(c) / 10.0;
    /* plus all 8 drum lanes, hit again every 0.25 s */
    trk[3].s.kit = 3;
    c = clock();
    for (b = 0; b < 44100u * 10u / CTL; b++) { if (b % 344u == 0u) note_on(3, 0xFF, 120, 0); mix_block(o, CTL); }
    worst = secs(c) / 10.0;
    drums8 = worst - synth8;
    printf("per second of audio on this computer: typical song %.2f ms, 8 synth voices %.2f ms, + 8 drum lanes %.2f ms (drums %.2f ms)\n",
           typ * 1000, synth8 * 1000, worst * 1000, drums8 * 1000);
    printf("drum lanes add %.0f %% to the synth worst case; a typical song uses %.0f %% of the worst case\n",
           100.0 * drums8 / synth8, 100.0 * typ / worst);
    return 0;
}

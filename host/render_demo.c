/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: a demo WAV (n ONE CLICKs, s seconds each) and a screenshot, from the host build.
 *   render_demo OUT.wav SHOT.ppm [seed] [n] [seconds] */
#include "host.c"
static FILE *wav;
static uint32_t frames;
static void put(const float *l, const float *r, void *u)
{
    uint32_t i;
    (void)u;
    for (i = 0; i < CTL; i++) {
        float a = l[i] * 1.8f, b = r[i] * 1.8f;
        int16_t s[2] = {(int16_t)(32767 * (a > 1 ? 1 : a < -1 ? -1 : a)), (int16_t)(32767 * (b > 1 ? 1 : b < -1 ? -1 : b))};
        fwrite(s, 4, 1, wav);
        frames++;
    }
}
static void header(void)
{
    uint32_t d = frames * 4u, v;
    uint16_t a;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); v = 36 + d; fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav); v = 16; fwrite(&v, 4, 1, wav);
    a = 1; fwrite(&a, 2, 1, wav); a = 2; fwrite(&a, 2, 1, wav);
    v = 44100; fwrite(&v, 4, 1, wav); v = 44100 * 4; fwrite(&v, 4, 1, wav);
    a = 4; fwrite(&a, 2, 1, wav); a = 16; fwrite(&a, 2, 1, wav);
    fwrite("data", 1, 4, wav); fwrite(&d, 4, 1, wav);
}
int main(int argc, char **argv)
{
    uint32_t i, n = argc > 4 ? (uint32_t)atoi(argv[4]) : 4u, sec = argc > 5 ? (uint32_t)atoi(argv[5]) : 16u;
    if (argc < 3 || !(wav = fopen(argv[1], "wb")))
        return 2;
    fseek(wav, 44, SEEK_SET);
    host_boot(argc > 3 ? (uint32_t)atoi(argv[3]) : 1u);
    host_press(B_PLAY);
    for (i = 0; i < n; i++) {
        if (i)
            host_press(B_REC);
        printf("%u: %s %s, %s / %s / %s / %s\n", i + 1, NOTE_NAME[song.root], SCALE_NAME[song.scale],
               ENGINE_NAME[trk[0].s.engine], ENGINE_NAME[trk[1].s.engine], ENGINE_NAME[trk[2].s.engine], ENGINE_NAME[trk[3].s.engine]);
        host_run(sec * 1000u, put, 0);
        if (i == 0)
            host_png_ppm(argv[2]);
    }
    header();
    fclose(wav);
    return 0;
}

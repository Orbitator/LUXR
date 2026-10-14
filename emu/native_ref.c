/* SPDX-License-Identifier: GPL-3.0-only
 * emu/emu_web.c built natively, with emu_test.mjs's gestures: its samples (float32 L R) for the wasm comparison.
 *   cc -O2 -Ibuild/gen -Ifirmware/src -o build/emu/native_ref emu/native_ref.c && build/emu/native_ref OUT.f32 */
#include <stdio.h>
#include "../emu/emu_web.c"
static FILE *f;
static void run(double ms, int rec)
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), k, i;
    for (k = 0; k < n; k += 128u) {
        uint32_t m = n - k < 128u ? n - k : 128u;
        emu_render(m);
        for (i = 0; rec && i < m; i++) { float s[2] = {e_l[i], e_r[i]}; fwrite(s, sizeof s, 1, f); }
    }
}
static void press(uint32_t b) { emu_buttons(1u << b); run(20, 0); emu_buttons(0); run(20, 0); }
int main(int argc, char **argv)
{
    if (argc < 2 || !(f = fopen(argv[1], "wb"))) return 2;
    emu_init(11);
    run(200, 0); press(B_PLAY); run(3000, 1); press(B_REC); run(3000, 1);
    fclose(f);
    return 0;
}

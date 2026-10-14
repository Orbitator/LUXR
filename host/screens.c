/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: every page, sub-page and layer as the FM-1's screen shows it, PPMs into a folder.
 *   screens OUT_DIR */
#include "host.c"
#include <math.h>
static const char *out;
static void shot(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s.ppm", out, name);
    ui_draw(1);
    host_png_ppm(p);
}
int main(int argc, char **argv)
{
    uint32_t pg, s, ns, t;
    static const char *const PN[PG_N] = {"home", "edit", "env", "lfo", "fx", "scl", "seq", "gen", "glo", "save", "menu"};
    (void)0;
    char n[64];
    out = argc > 1 ? argv[1] : "build/screens";
    host_boot(11);
    {   /* a kick and a 2-bar loop in the store (as the companion would send them) */
        static uint8_t d[22050 * 2];
        smp_t m;
        uint32_t i;
        for (i = 0; i < sizeof d; i++) {
            double x = (i % 5512u < 1500u ? 0.8 * sin(i * 0.07) * (1.0 - (i % 5512u) / 1500.0) : 0.05 * sin(i * 0.9));
            int32_t v = (int32_t)(x * 32000), sg = v < 0 ? 0x80 : 0, e;
            v = v < 0 ? -v : v; v = v > 32635 ? 32635 : v; v += 0x84;
            for (e = 7; e > 0 && !(v & (0x4000 >> (7 - e))); e--) ;
            d[i] = (uint8_t)~(sg | e << 4 | ((v >> (e + 3)) & 15));
        }
        memset(&m, 0, sizeof m); m.kind = SK_ONESHOT; m.rate = 22050; m.len = 6000; m.root = 60; str_cpy(m.name, "KICK 808", 12);
        smp_begin(0, &m, 4); smp_chunk(0, d, 6000); smp_commit();
        m.kind = SK_LOOP; m.bars = 2; m.len = sizeof d; str_cpy(m.name, "AMEN", 12);
        smp_begin(1, &m, 5);
        for (i = 0; i < sizeof d; i += 256u) smp_chunk(i, d + i, sizeof d - i < 256u ? sizeof d - i : 256u);
        smp_commit();
    }
    host_tap(B_PLAY);
    host_run(1500, 0, 0);
    for (t = 0; t < NTRK; t++) {
        song.sel = (uint8_t)t;
        for (pg = 0; pg < PG_N; pg++) {
            const sub_t *sp = subs_of(pg, TSEL, &ns);
            (void)sp;
            if (t && (pg == PG_HOME || pg == PG_SCL || pg == PG_GLO || pg == PG_SAVE || pg == PG_MENU))
                continue;
            ui.page = (uint8_t)pg;
            for (s = 0; s < ns; s++) {
                ui.sub[pg] = (uint8_t)s;
                host_run(20, 0, 0);
                snprintf(n, sizeof n, "t%u_%s_%u_%s", t + 1, PN[pg], s + 1, ENGINE_NAME[TSEL->s.engine]);
                shot(n);
                h_held = 1u << B_GLO;                  /* the second row (SHIFT) */
                host_run(20, 0, 0);
                snprintf(n, sizeof n, "t%u_%s_%u_%s_B", t + 1, PN[pg], s + 1, ENGINE_NAME[TSEL->s.engine]);
                shot(n);
                ui.btn_used |= 1u << B_GLO;
                h_held = 0;
                host_run(20, 0, 0);
                ui.page = (uint8_t)pg;
            }
            ui.sub[pg] = 0;
        }
    }
    song.sel = 2;
    ui.page = PG_HOME;
    h_enc[EN_K1] = 5;                              /* a knob just turned: its card lit */
    host_run(40, 0, 0);
    shot("t3_home_hot");
    ui_say("ONE CLICK: A MINOR");
    shot("message");
    song.rec = 1;                                  /* recording armed, playing */
    ui.page = PG_SEQ; ui.sub[PG_SEQ] = SQ_STEP; ui.cur = 5;
    shot("t3_step_rec");
    song.sel = 3; ui.lane = 1; shot("t4_step_grid_rec");
    song.rec = 0; song.sel = 2; ui.page = PG_HOME;
    { uint32_t ly; static const char *const LN[5] = {"", "seq", "scl", "edit", "type"};
      for (ly = 1; ly < 5; ly++) { ui.layer = (uint8_t)ly; snprintf(n, sizeof n, "layer_%s", LN[ly]); shot(n); }
      ui.layer = 0; }
    h_held = 1u << B_GLO; h_pressed = h_held; host_run(800, 0, 0); shot("shift_map"); h_held = 0; ui.btn_used |= 1u << B_GLO; host_run(30, 0, 0);
    ui.uboot = 3;
    shot("uboot");
    return 0;
}

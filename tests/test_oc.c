/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1 host test: many ONE CLICKs and track generations while playing; the parts (roles, LEN, notes in the
 * key, chords), audio finite and not silent, voices within the pool, the protocol (dump, set steps / sound), undo,
 * the keys, the UBOOT countdown. Run under ASan / UBSan by tests/run.sh. */
#include "../host/host.c"
#include <math.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double peak, sum2;
static uint64_t nsm;
static void meter(const float *l, const float *r, void *u)
{
    uint32_t i;
    (void)u;
    for (i = 0; i < CTL; i++) {
        double a = fabs(l[i]), b = fabs(r[i]);
        CHECK(isfinite(a) && isfinite(b), "not finite");
        if (a > peak) peak = a;
        if (b > peak) peak = b;
        sum2 += a * a;
        nsm++;
    }
}
static void check_parts(void)
{
    uint32_t i, j, k, notes;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        CHECK((t->s.engine == E_DRUM) == (i == ROLE_DRUM), "track %u engine %u", i, t->s.engine);
        CHECK(t->len == (i == ROLE_DRUM ? 16u : 64u), "track %u len %u", i, t->len);
        for (j = notes = 0; j < t->len; j++) {
            const step_t *s = &t->step[j];
            if (s->time != ST_NOTE) continue;
            notes++;
            if (i == ROLE_DRUM) { CHECK(s->hit, "drum step without hits"); continue; }
            CHECK(s->n >= 1 && s->n <= 4, "n %u", s->n);
            if (i == ROLE_CHORD) CHECK(s->n >= 3, "chord of %u", s->n);
            for (k = 0; k < s->n; k++) CHECK(in_key(s->note[k]), "track %u note %u off key", i, s->note[k]);
        }
        CHECK(notes >= 3, "track %u: %u notes", i, notes);
    }
}
int main(void)
{
    uint32_t r, i, quiet = 0;
    host_boot(7);
    check_parts();
    host_press(B_PLAY);
    for (r = 0; r < 80; r++) {
        peak = 0; sum2 = 0; nsm = 0;
        host_press(B_REC);
        check_parts();
        host_run(r < 10 ? 4000 : 600, meter, 0);
        CHECK(peak < 1.0, "round %u peak %.3f", r, peak);
        if (sqrt(sum2 / (double)nsm) < 0.01) quiet++;
        for (i = 0; i < 4; i++) {
            h_enc[EN_ALGO] = (int32_t)(rng() % 7) - 3;
            host_frame();
            host_press(i == 0 ? B_ARP : i == 1 ? B_EDIT : i == 2 ? B_SEQ : B_SCL);
            check_parts();
            host_run(200, meter, 0);
        }
        for (i = 0; i < NVOICE; i++) CHECK(!voice[i].active || voice[i].trk < ROLE_DRUM, "synth voice on track %u", voice[i].trk);
        for (i = 0; i < NLANE; i++) CHECK(!dvoice[i].active || (dvoice[i].trk == ROLE_DRUM && dvoice[i].lane == i), "drum voice %u", i);
    }
    CHECK(quiet < 8, "%u quiet rounds of 80", quiet);
    /* undo / redo */
    { step_t a[NSTEP]; memcpy(a, trk[2].step, sizeof a); host_press(B_REC); host_press(B_HOME);
      CHECK(!memcmp(a, trk[2].step, sizeof a), "undo"); }
    /* the keys play the selected track */
    song.sel = 2; h_keys = 1u << 7; host_run(100, 0, 0);
    { int on = 0; for (i = 0; i < NVOICE; i++) on |= voice[i].active && voice[i].trk == 2 && voice[i].gate; CHECK(on, "key note"); }
    h_keys = 0; host_run(100, 0, 0);
    /* protocol: dump, then set steps half 0 and the sound back */
    { uint8_t f[8] = {0x7D, 'O', 'C', 0x10};
      CHECK(proto_frame(f, 4), "dump not ours"); CHECK(h_sent_n > 2000 && h_sent[4] == 0x11, "dump %u", h_sent_n); }
    { static uint8_t raw[2 + 32 * sizeof(step_t)], fr[1000]; uint32_t n;
      raw[0] = 16; raw[1] = 50; memset(raw + 2, 0, 32 * sizeof(step_t));
      ((step_t *)(raw + 2))[0].time = ST_NOTE; ((step_t *)(raw + 2))[0].n = 1; ((step_t *)(raw + 2))[0].note[0] = 60;
      fr[0] = 0x7D; fr[1] = 'O'; fr[2] = 'C'; fr[3] = 0x12; fr[4] = 1; fr[5] = 0;
      n = 6 + pack7(raw, sizeof raw, fr + 6); CHECK(n < 640, "frame %u", n);
      proto_frame(fr, n); CHECK(trk[1].len == 16 && trk[1].step[0].note[0] == 60 && trk[1].step[1].time == ST_REST, "set steps"); }
    { uint8_t f[4] = {0x7D, 'X', 'C', 1}; CHECK(!proto_frame(f, 4), "foreign frame taken"); }
    /* UBOOT countdown */
    h_held = 3u << B_OCTDN; host_run(5200, 0, 0); CHECK(ui.uboot == 9, "uboot %u", ui.uboot);
    h_held = 0; host_frame(); CHECK(ui.uboot == 0, "uboot cancel");
    printf("test_oc: %s, %d failures\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}

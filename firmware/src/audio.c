/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * [ONE CLICK: Felucca's audio ISR, cut down: mix_block is oc_synth.c's; no USB audio, scope, click or voice shedding.] */
/* I2S output (ALNK0 -> external codec) and the audio ISR: per half buffer, blocks of CTL samples of mix_block,
 * Q15 -> 24-bit stereo. */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define CPU_AVG (4096u / HALF_FRAMES)
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint32_t t5_nested_ticks;      /* TIMER4 ticks TIMER5 spent nested in this ISR (oc_main.c) */
static volatile uint8_t in_audio;              /* TIMER5 nests while set (oc_main.c) */
static uint32_t audio_cpu_rem;

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    in_audio = 1;
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, i, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        for (b = 0; b < HALF_FRAMES; b += CTL) {
            mix_block(o + 2u * b, CTL);
            for (i = 0; i < CTL; i++) {
                o[2u * (b + i)] *= 1 << OUT_SHIFT;
                o[2u * (b + i) + 1u] *= 1 << OUT_SHIFT;
            }
        }
        fm1_audio_ack_half();
        us = (fm1_ticks() - t0 - t5_nested_ticks) / FM1_TICKS_PER_US;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

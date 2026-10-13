/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * [ONE CLICK: Felucca's MASTER pot reading, with its own level curve.] */
/* MASTER: the pot read before the audio starts (the first block plays at its level), then smoothed per UI frame */
static int32_t master_knob = 512 * 16;
static uint16_t master_of_pot(uint32_t a)       /* 0..1023 -> Q12, a square law (0..4092; 2048 near 3/4) */
{
    a = a > 1023u ? 1023u : a;
    return (uint16_t)((a * a) >> 8);
}
static void master_poll(void)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a >= 0) {
        master_knob += (a * 16 - master_knob) / 8;
        song.master_q12 = master_of_pot((uint32_t)(master_knob / 16));
    }
}
static void master_boot(void)
{
    uint32_t i;
    (void)fm1_adc_read(FM1_ADC_MASTER);                 /* (the first conversion after the pin setup: dropped) */
    for (i = 0; i < 3u; i++) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0) {
            master_knob = a * 16;
            song.master_q12 = master_of_pot((uint32_t)a);
            return;
        }
    }
}

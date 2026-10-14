/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * [ONE CLICK: Felucca's boot and main loop (main.c), with ONE CLICK's app in it. Kept as they were: the watchdog,
 * the boot-loop guard (two boots that die in their first 30 s -> UBOOT), the fault screen, OCT- + OCT+ held 5 s ->
 * UBOOT, the USB soft key -> UBOOT, the M-UPGRADE handshake and update (ota.c). These are how a broken firmware is
 * replaced without opening the FM-1: do not take them out.] */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

/* TIMER5 (10 kHz): the key matrix scan and LEDs, the millisecond clock, USB every 0.5 ms. It outranks the audio ISR
 * and nests in it (then it only scans and counts; USB waits for the next tick outside the render). */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed, last, acc;
    uint32_t t0 = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (sub % 5u == 0u)
        owed |= 1u;
    if (++sub == 10u)
        sub = 0;
    if (in_audio) {
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
    owed = 0;
}
extern void isr_timer5(void);

/* ------------------------------------------------------- the platform --- */
static uint8_t irq_live;                         /* after fm1_irq_enable_all: gen_lock may touch the IRQ flag */
static void gen_lock(void) { if (irq_live) fm1_irq_off(); }
static void gen_unlock(void) { if (irq_live) fm1_irq_on(); }
static uint32_t io_pressed(void)
{
    uint32_t m = fm1_input_edges(0), b, out = 0;
    for (b = 0; b < NB; b++)
        if ((m >> panel.btn[b]) & 1u)
            out |= 1u << b;
    return out;
}
static uint32_t io_held(void)
{
    uint32_t m = fm1_in.buttons, b, out = 0;
    for (b = 0; b < NB; b++)
        if ((m >> panel.btn[b]) & 1u)
            out |= 1u << b;
    return out;
}
static uint32_t io_keys(void) { return fm1_in.notes; }
static int32_t io_enc(uint32_t role) { return panel_enc(role); }
static void io_led_button(uint32_t b, int on) { fm1_led_key(panel.btn[b % NB], on); }
static void io_led_key(uint32_t k, int on) { fm1_led_key(14u + k, on); }
static int proto_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }

/* USB MIDI IN (audio ISR): notes (MIDI IN: channels 1..4 the tracks, or all the selected one); clock and the rest ignored */
static void midi_in_drain(void)
{
    if (midi_in_overflow) {
        mi_r = mi_w;
        midi_in_overflow = 0;
        return;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xFFu, ch = st & 15u;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        mi_r++;
        (void)ch;
        midi_note(st, d1, d2);                    /* (oc_seq.c: MIDI IN mode, drums, recording) */
    }
}
/* the store: OC_STORE_LO.. (oneclick.c), 4 KiB sectors; nothing without the expected flash */
static int io_store_read(uint32_t off, void *d, uint32_t n)
{
    if (!flash_ok || !FL_IN(OC_STORE_LO + off, n, OC_STORE_LO, OC_STORE_HI))
        return -1;
    return st_read(OC_STORE_LO + off, d, n);
}
static int io_store_erase(uint32_t off)           /* the 4 KiB sector at off */
{
    if (!flash_ok || (off & 0xFFFu) || !FL_IN(OC_STORE_LO + off, 0x1000u, OC_STORE_LO, OC_STORE_HI))
        return -1;
    return st_erase(OC_STORE_LO + off);
}
static int io_store_prog(uint32_t off, const void *s, uint32_t n)   /* into erased flash */
{
    if (!flash_ok || !FL_IN(OC_STORE_LO + off, n, OC_STORE_LO, OC_STORE_HI))
        return -1;
    return st_prog(OC_STORE_LO + off, s, n);
}
static const uint8_t *io_store_map(void)          /* the store read in place (the plain XIP window: the samples) */
{
    return flash_ok ? (const uint8_t *)FL_XIP(OC_STORE_LO) : 0;
}
/* the companion's frames (ours: 7D 'O' 'C'); the rest stays for the updater (ota_service) */
static void proto_service(void)
{
    const uint8_t *p;
    uint32_t n;
    if (ota_frame_get(&p, &n) && n >= 3u && p[0] == 0x7Du && p[1] == 'O' && p[2] == 'C') {
        (void)proto_frame(p, n);
        ota_frame_done();
    }
}

/* full-screen notices: the fault, UPDATE, UBOOT (straight to the panel, gfx.c's one-shot lines) */
static void oc_banner(const char *a, const char *b)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 92, 240, &AF_L, a, T_THEME, 1);
    if (b && b[0])
        draw_text_box(0, 132, 240, &AF_M, b, T_TEXT, 1);
}
static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}
static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_wake_now();
    hexs(b, c->pc);
    oc_banner("CRASH", b);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}
static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    oc_banner("UBOOT", why);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                        /* intentional: not a failed boot */
    fm1_enter_uboot();
}

static void fm1_main(void)
{
    uint32_t f;
    f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part (else no update from here) */
    irq_restore(f);
    if (flash_ok) {
        ota_boot_cleanup();                       /* the staging area an update left */
        fl_plain_window_init();                   /* the data area read in place unencrypted (the samples) */
    }
    lcd_init();
    palette_set(PAL_AMBER);                       /* (the banners' colours before ui_init) */
    oc_banner("ONE CLICK", "FM-1 " OC_VERSION);
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    ui_init();
    master_boot();
    audio_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 4);              /* above ALNK0 (3): the scan nests into the render */
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    irq_live = 1;
    fm1_delay_ms(400);
    ui_draw(1);
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {   /* 30 s up: this boot did not fail */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        master_poll();
        if (ui.uboot >= 9u)                       /* OCT- + OCT+ held 5 s (oc_ui.c) */
            enter_uboot("HELD 5 S");
        proto_service();                          /* the companion app */
        ota_service();                            /* M-UPGRADE handshake */
        if (usb.ota_req) {                        /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            seq_play(0);
            if (flash_ok)
                ota_session();                    /* returns only if nothing was committed */
            ui_draw(1);
        }
        if (usb.uboot_req)                        /* SysEx F0 22 24 35 7D F7 */
            enter_uboot("USB");
        ui_input();
        ui_leds();
        ui_draw(0);
        while (fm1_ms - m < 15u)                  /* ~60 frames a second at most */
            ;
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {                 /* two boots died early: wait in UBOOT for an installer */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                  /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}

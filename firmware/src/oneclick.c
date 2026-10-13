/* SPDX-License-Identifier: GPL-3.0-only
 * ONE CLICK FM-1: one compilation unit (as Felucca: the HAL is header-only, each file uses what is above it).
 * Felucca's platform (hal/, loader/, usb.c, ota.c, ota_hw.c, lcd.c, panel.c, storage_hw.c, libc.c, audio.c, master.c,
 * oc_main.c) carries its copyright; the oc_*.c app is ONE CLICK's own. GPL-3.0-only throughout. */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1
#endif                /* the update entry: keep it, it is how this firmware gets replaced */
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 0
#endif                /* plain USB-MIDI device */
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 0
#endif
#ifndef FELUCCA_UAC_TONE
#define FELUCCA_UAC_TONE 0
#endif
#ifndef FELUCCA_UART
#define FELUCCA_UART 0
#endif
#ifndef FELUCCA_USB_PID
#define FELUCCA_USB_PID 0x0001
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_810"        /* package identity (tools/build.py: the .fwsc marker string) */
#endif
#ifndef OC_VERSION
#define OC_VERSION "v0.1"
#endif

#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
/* this firmware writes the flash only to stage an update: never Felucca's or the stock store */
#define FL_RANGE_OK(off, n) FL_IN(off, n, FL_OTA_LO, FL_OTA_HI)
#include "fm1_flash.h"
#include "oc_tables.h"

#include "libc.c"
#include "lcd.c"
#include "oc_core.h"
#include "panel.c"
#include "oc_font.c"
#include "oc_synth.c"
#include "oc_seq.c"
#include "oc_gen.c"
#include "usb.c"
#include "audio.c"
#include "storage_hw.c"
#include "ota.c"
static void oc_banner(const char *a, const char *b);
#include "ota_hw.c"
#include "oc_ui.c"
#include "oc_proto.c"
#include "master.c"
#include "oc_main.c"

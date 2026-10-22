/* SPDX-License-Identifier: GPL-3.0-only
 * LXR FM-1: a drum machine for the M-VAVE FM-1 (eight LXR-style sounds with the LXR's transient samples as their
 * sample layer), one compilation unit as oneclick.c: Felucca's platform as ONE CLICK uses it (the HAL, USB-MIDI, the
 * update entry, UBOOT, the store, the display), the lx_*.c app instead of the oc_*.c one.
 * build/gen/lxr_transients.h (tools/gen_lxr_transients.py) carries the Sonic Potions LXR licence, not the GPL: a
 * firmware built with it is for personal, non-commercial use (README-LXR.md). */
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
#endif
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
#define FELUCCA_ID "FM-1_820"        /* package identity (tools/build.py --app lxr) */
#endif
#ifndef OC_VERSION
#define OC_VERSION "v0.1"
#endif
#define APP_BANNER "LXR FM-1"

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
/* the flash: the update staging area and the store (ONE CLICK's: 0x97000..0xE0000, never the stock firmware's) */
#define OC_STORE_LO 0x00097000u
#define OC_STORE_HI 0x000E0000u
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, OC_STORE_LO, OC_STORE_HI))
#include "fm1_flash.h"
#include "oc_tables.h"
#include "lxr_transients.h"

#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "lx_core.h"
#include "panel.c"
#include "lx_synth.c"
#include "lx_seq.c"
#include "usb.c"
#include "audio.c"
#include "storage_hw.c"
#include "ota.c"
static void oc_banner(const char *a, const char *b);
#include "ota_hw.c"
#include "lx_ui.c"
#include "master.c"
#include "oc_main.c"

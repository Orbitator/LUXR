/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): the sample layer's transients, once for every engine. The generated headers declare
 * them static; here they are renamed and made visible inside the plugin (luxr_engine.c refers to them as extern).
 * lxr_transients.h is the Sonic Potions LXR's transient samples (its own licence, not the GPL: README.md). */
#include <stdint.h>

#define LXR_TRANS lxe_LXR_TRANS
#define LXR_TRANS_NAME lxe_LXR_TRANS_NAME
#define OWN_TRANS lxe_OWN_TRANS
#define MACH_TRANS lxe_MACH_TRANS
#define MACH_NAME lxe_MACH_NAME
#define static __attribute__((visibility("hidden")))
#include "lxr_transients.h"
#include "own_transients.h"
#include "machine_transients.h"
#undef static

_Static_assert(LXR_TRANS_N == 12 && LXR_TRANS_LEN == 2205, "luxr_engine.c: LXR_TRANS");
_Static_assert(OWN_TRANS_N == 16 && OWN_TRANS_LEN == 2205, "luxr_engine.c: OWN_TRANS");
_Static_assert(MACH_TRANS_N == 16 && MACH_TRANS_LEN == 1024, "luxr_engine.c: MACH_TRANS");

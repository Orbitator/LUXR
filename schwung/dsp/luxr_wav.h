/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): WAV files for the sample layer, loaded off the audio thread.
 *
 * Schwung calls every entry point of a plugin on the audio callback, where file I/O and malloc are forbidden. So a
 * loader (one worker thread for each engine, SCHED_OTHER on cores 0-2) takes requests - a sound and a path - reads the
 * file, makes it 16 bit mono at its own rate, and publishes it; the audio thread picks it up at the start of a block
 * (a pointer swap) and hands the one it let go back to the loader to free. */
#ifndef LUXR_WAV_H
#define LUXR_WAV_H
#include <stdint.h>

#define LXW_PATH 256
#define LXW_MAX_FRAMES (1u << 20)                /* (Q12 positions: 23.7 s at 44.1 kHz; longer files are cut) */

typedef struct lxw_buf {
    uint32_t n, rate;                            /* frames, Hz */
    int16_t d[];
} lxw_buf_t;

typedef struct lxw_loader lxw_loader_t;
lxw_loader_t *lxw_start(uint32_t nslots);        /* (create_instance: the loader thread) */
void lxw_stop(lxw_loader_t *l);                  /* (destroy_instance: joined, everything freed) */
void lxw_request(lxw_loader_t *l, uint32_t slot, const char *path);   /* (audio thread: no I/O, no allocation) */
const lxw_buf_t *lxw_take(lxw_loader_t *l, uint32_t slot, const lxw_buf_t *cur);   /* (audio thread, a block's start:
                                                 * the newest buffer for slot - n 0: no file -, cur handed back when it
                                                 * changed) */
void lxw_free(const lxw_buf_t *b);              /* (a buffer taken by the audio thread, at the end) */
int lxw_status(lxw_loader_t *l, uint32_t slot);  /* 0 none, 1 loading, 2 loaded, -1 failed */
lxw_buf_t *lxw_read(const char *path);           /* (the worker's; the tests) a file decoded, or 0 */
#endif

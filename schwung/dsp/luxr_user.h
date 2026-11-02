/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): kits of your own - saved, listed, loaded and deleted as files (one JSON each: the
 * plugin's state) in a folder outside the module (it survives updates): /data/UserData/schwung/luxr/kits, or
 * $LUXR_KITS_DIR. All file work runs on the WAV loader's worker (luxr_wav.c), never on the audio thread; the audio
 * thread only posts requests and picks up results. */
#ifndef LUXR_USER_H
#define LUXR_USER_H
#include <stdint.h>

#define LXU_MAX 64                               /* kits of your own, at most */
#define LXU_NAME 24
#define LXU_TEXT (16u * 1024u)                   /* a kit's file: the state JSON */
#define LXU_DIR "/data/UserData/schwung/luxr/kits"

typedef struct lxu lxu_t;
lxu_t *lxu_open(void);                           /* (create_instance) */
void lxu_close(lxu_t *u);                        /* (destroy_instance, after the worker has stopped) */
void lxu_poll(void *u);                          /* (the worker: requests served, the folder read) */
/* the audio thread: */
uint32_t lxu_count(lxu_t *u);
const char *lxu_name(lxu_t *u, uint32_t i);      /* (sorted by name) */
int lxu_save(lxu_t *u, const char *name, const char *text, uint32_t n);   /* 0 posted, -1 busy */
int lxu_load(lxu_t *u, uint32_t i);              /* 0 posted, -1 busy or no such kit */
int lxu_delete(lxu_t *u, uint32_t i);
const char *lxu_loaded(lxu_t *u);                /* a kit read, once (its text until the next call), or 0 */
int lxu_saved(lxu_t *u, char *name, uint32_t n); /* 1 once a save is on disk and listed (its name), else 0 */
const char *lxu_error(lxu_t *u);                 /* the last failure ("" none) */
/* the CPU log: the audio thread reports each second (us a block on average and at most, voices, the kit); the worker
 * appends it to cpu.log next to the kits (the last 2000 lines kept) */
/* the MIDI log: each note-on as it reaches LUXR (channel, note, velocity, Schwung's source, played or not); the worker
 * appends them to midi.log next to cpu.log (the last 2000 lines kept) */
void lxu_note(lxu_t *u, uint32_t ch, uint32_t note, uint32_t vel, int source, int played);
void lxu_cpu(lxu_t *u, uint32_t avg_us, uint32_t peak_us, uint32_t voices, const char *kit);
#endif

/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): one engine. The firmware's sound (firmware/src/lx_synth.c) keeps its state in
 * statics, one machine per translation unit; luxr_engine.c is therefore compiled LXE_SLOTS times (-DLXE_SLOT=k), each
 * copy a machine of its own behind this table, and the plugin (luxr_plugin.c) hands one to each instance. */
#ifndef LUXR_ENGINE_H
#define LUXR_ENGINE_H
#include <stdint.h>

#ifndef LXE_SLOTS
#define LXE_SLOTS 6
#endif

typedef struct {
    void (*init)(uint32_t seed);                                   /* the factory kit, the voices quiet */
    void (*midi)(const uint8_t *msg, int len);                     /* a MIDI message (status, data) */
    void (*set)(const char *key, const char *val);
    int (*get)(const char *key, char *buf, int buf_len);           /* the length written, -1 unknown key */
    void (*render)(int16_t *out_lr, int frames, float bpm, double beat);   /* beat < 0: no transport */
    void (*fini)(void);                                            /* (destroy: the WAV loader stopped, freed) */
} lxe_ops_t;

#define LXE_DECL(k) const lxe_ops_t *lxe_ops_##k(void);
LXE_DECL(0)
LXE_DECL(1)
LXE_DECL(2)
LXE_DECL(3)
LXE_DECL(4)
LXE_DECL(5)
#endif

/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): the plugin (Plugin API v2, a sound generator). Each instance gets one of the
 * LXE_SLOTS engines (luxr_engine.c compiled once a slot); a slot is free again when its instance is destroyed. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"
#include "luxr_engine.h"

static const host_api_v1_t *g_host;
static const lxe_ops_t *(*const SLOT_OPS[LXE_SLOTS])(void) = {lxe_ops_0, lxe_ops_1, lxe_ops_2, lxe_ops_3, lxe_ops_4,
                                                             lxe_ops_5};
static uint8_t slot_used[LXE_SLOTS];
static uint32_t seed_n;

typedef struct {
    int slot;
    const lxe_ops_t *e;
} inst_t;

static void say(const char *m)
{
    if (g_host && g_host->log)
        g_host->log(m);
}

static void *create_instance(const char *module_dir, const char *json_defaults)
{
    inst_t *in;
    int k;
    (void)module_dir;
    (void)json_defaults;
    for (k = 0; k < LXE_SLOTS && __atomic_load_n(&slot_used[k], __ATOMIC_ACQUIRE); k++)
        ;
    if (k == LXE_SLOTS) {
        say("luxr: all engines in use (" "6" " instances at most)");
        return NULL;
    }
    if (!(in = calloc(1, sizeof *in)))
        return NULL;
    __atomic_store_n(&slot_used[k], 1, __ATOMIC_RELEASE);
    in->slot = k;
    in->e = SLOT_OPS[k]();
    in->e->init(0x9E3779B9u * ++seed_n);
    return in;
}
static void destroy_instance(void *instance)
{
    inst_t *in = instance;
    if (!in)
        return;
    in->e->fini();
    __atomic_store_n(&slot_used[in->slot], 0, __ATOMIC_RELEASE);
    free(in);
}
static void on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    inst_t *in = instance;
    (void)source;
    if (in && msg)
        in->e->midi(msg, len);
}
static void set_param(void *instance, const char *key, const char *val)
{
    inst_t *in = instance;
    if (in && key && val)
        in->e->set(key, val);
}
static int get_param(void *instance, const char *key, char *buf, int buf_len)
{
    inst_t *in = instance;
    if (!in || !key || !buf)
        return -1;
    return in->e->get(key, buf, buf_len);
}
static int get_error(void *instance, char *buf, int buf_len)
{
    (void)instance;
    if (buf && buf_len > 0)
        buf[0] = 0;
    return 0;
}
static void render_block(void *instance, int16_t *out, int frames)
{
    inst_t *in = instance;
    float bpm = 0.0f;
    double beat = -1.0;
    if (!in) {
        memset(out, 0, (size_t)frames * 2u * sizeof *out);
        return;
    }
    if (g_host) {
        if (g_host->get_bpm)
            bpm = g_host->get_bpm();
        if (g_host->get_beat_position)
            beat = g_host->get_beat_position();
    }
    in->e->render(out, frames, bpm, beat);
}

static plugin_api_v2_t g_api = {
    MOVE_PLUGIN_API_VERSION_2, create_instance, destroy_instance, on_midi, set_param, get_param, get_error, render_block,
};
__attribute__((visibility("default"))) plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}

/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): WAV files for the sample layer, loaded off the audio thread (luxr_wav.h).
 * Reads RIFF/WAVE PCM 8/16/24/32 bit, IEEE float 32 bit and WAVE_FORMAT_EXTENSIBLE of those, any channel count
 * (mixed down to mono). */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "luxr_wav.h"

#define HID __attribute__((visibility("hidden")))
#define NSLOT_MAX 8
static lxw_buf_t lxw_none;                       /* (published for "no file": n 0; never freed) */
static void buf_free(const lxw_buf_t *b)
{
    if (b && b != &lxw_none)
        free((void *)b);
}

struct lxw_loader {
    pthread_t th;
    int run;                                     /* (atomic) */
    uint32_t nslots;
    void (*idle)(void *);
    void *arg;
    struct {
        char path[LXW_PATH];                     /* written by the audio thread, then gen bumped (release) */
        uint32_t gen, done;                      /* (atomic) requested, served */
        lxw_buf_t *pub;                          /* (atomic) a mailbox: the newest, until the audio thread takes it */
        const lxw_buf_t *ret[4];                 /* (atomic) let go by the audio thread, for the worker to free */
        int status;                              /* (atomic) */
    } s[NSLOT_MAX];
};

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

HID lxw_buf_t *lxw_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[12], ck[8], fmt[40];
    uint32_t fmt_tag = 0, ch = 0, rate = 0, bits = 0, have_fmt = 0, i, c;
    lxw_buf_t *b = 0;
    if (!f)
        return 0;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        goto out;
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t len = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            uint32_t k = len < sizeof fmt ? len : (uint32_t)sizeof fmt;
            if (k < 16 || fread(fmt, 1, k, f) != k)
                goto out;
            if (len > k && fseek(f, (long)(len - k), SEEK_CUR))
                goto out;
            fmt_tag = rd16(fmt);
            ch = rd16(fmt + 2);
            rate = rd32(fmt + 4);
            bits = rd16(fmt + 14);
            if (fmt_tag == 0xFFFEu && k >= 26u)  /* (EXTENSIBLE: the sub-format's first two bytes) */
                fmt_tag = rd16(fmt + 24);
            have_fmt = 1;
        } else if (!memcmp(ck, "data", 4) && have_fmt) {
            uint32_t bps = bits / 8u, fr, n;
            uint8_t *raw;
            if (!ch || !rate || (!(fmt_tag == 1u && bps >= 1u && bps <= 4u) && !(fmt_tag == 3u && bits == 32u)))
                goto out;
            fr = len / (bps * ch);
            n = fr < LXW_MAX_FRAMES ? fr : LXW_MAX_FRAMES;
            if (!n || !(raw = malloc((size_t)n * bps * ch)))
                goto out;
            n = (uint32_t)(fread(raw, (size_t)bps * ch, n, f));
            if (n && (b = malloc(sizeof *b + (size_t)n * sizeof(int16_t)))) {
                b->n = n;
                b->rate = rate;
                for (i = 0; i < n; i++) {
                    double sum = 0;
                    for (c = 0; c < ch; c++) {
                        const uint8_t *p = raw + ((size_t)i * ch + c) * bps;
                        double v;
                        if (fmt_tag == 3u) {
                            float x;
                            uint32_t u = rd32(p);
                            memcpy(&x, &u, 4);
                            v = x;
                        } else if (bps == 1u) {
                            v = ((int)p[0] - 128) / 128.0;
                        } else if (bps == 2u) {
                            v = (int16_t)rd16(p) / 32768.0;
                        } else if (bps == 3u) {
                            v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0;
                        } else {
                            v = (int32_t)rd32(p) / 2147483648.0;
                        }
                        sum += v;
                    }
                    sum = sum / ch * 32767.0;
                    b->d[i] = (int16_t)(sum > 32767.0 ? 32767 : sum < -32767.0 ? -32767 : sum);
                }
            }
            free(raw);
            goto out;
        } else if (fseek(f, (long)(len + (len & 1u)), SEEK_CUR)) {
            goto out;
        }
    }
out:
    fclose(f);
    return b;
}

static void *worker(void *arg)
{
    lxw_loader_t *l = arg;
    struct sched_param sp = {0};
    cpu_set_t set;
    struct timespec ts = {0, 10 * 1000 * 1000};
    uint32_t k;
    sched_setscheduler(0, SCHED_OTHER, &sp);      /* (Schwung: never the audio callback's FIFO 70; core 3 left to it) */
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof set, &set);
    while (__atomic_load_n(&l->run, __ATOMIC_ACQUIRE)) {
        for (k = 0; k < l->nslots; k++) {
            uint32_t g = __atomic_load_n(&l->s[k].gen, __ATOMIC_ACQUIRE), r;
            for (r = 0; r < 4u; r++)
                buf_free(__atomic_exchange_n(&l->s[k].ret[r], (const lxw_buf_t *)0, __ATOMIC_ACQ_REL));
            if (g != l->s[k].done) {
                char p[LXW_PATH];
                lxw_buf_t *b, *old;
                memcpy(p, l->s[k].path, sizeof p);
                p[LXW_PATH - 1] = 0;
                if (__atomic_load_n(&l->s[k].gen, __ATOMIC_ACQUIRE) != g)
                    continue;                    /* (asked again meanwhile: the newer path next round) */
                __atomic_store_n(&l->s[k].status, 1, __ATOMIC_RELEASE);
                b = p[0] ? lxw_read(p) : 0;
                old = __atomic_exchange_n(&l->s[k].pub, b ? b : &lxw_none, __ATOMIC_ACQ_REL);
                buf_free(old);                   /* (still in the mailbox: never taken, ours) */
                __atomic_store_n(&l->s[k].status, !p[0] ? 0 : b ? 2 : -1, __ATOMIC_RELEASE);
                l->s[k].done = g;
            }
        }
        if (l->idle)
            l->idle(l->arg);
        nanosleep(&ts, 0);
    }
    return 0;
}

HID lxw_loader_t *lxw_start(uint32_t nslots, void (*idle)(void *), void *arg)
{
    lxw_loader_t *l = calloc(1, sizeof *l);
    if (!l)
        return 0;
    l->idle = idle;
    l->arg = arg;
    l->nslots = nslots < NSLOT_MAX ? nslots : NSLOT_MAX;
    l->run = 1;
    if (pthread_create(&l->th, 0, worker, l)) {
        free(l);
        return 0;
    }
    return l;
}
HID void lxw_stop(lxw_loader_t *l)
{
    uint32_t k;
    if (!l)
        return;
    __atomic_store_n(&l->run, 0, __ATOMIC_RELEASE);
    pthread_join(l->th, 0);
    for (k = 0; k < l->nslots; k++) {
        uint32_t r;
        for (r = 0; r < 4u; r++)
            buf_free(l->s[k].ret[r]);
        buf_free(l->s[k].pub);
    }
    free(l);
}
HID void lxw_request(lxw_loader_t *l, uint32_t slot, const char *path)
{
    size_t n;
    if (!l || slot >= l->nslots)
        return;
    n = strlen(path);
    if (n >= LXW_PATH)
        n = LXW_PATH - 1;
    memcpy(l->s[slot].path, path, n);
    l->s[slot].path[n] = 0;
    __atomic_add_fetch(&l->s[slot].gen, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&l->s[slot].status, path[0] ? 1 : 0, __ATOMIC_RELEASE);
}
HID const lxw_buf_t *lxw_take(lxw_loader_t *l, uint32_t slot, const lxw_buf_t *cur)
{
    lxw_buf_t *p;
    uint32_t r;
    if (!l || slot >= l->nslots || !__atomic_load_n(&l->s[slot].pub, __ATOMIC_ACQUIRE))
        return cur;
    p = __atomic_exchange_n(&l->s[slot].pub, (lxw_buf_t *)0, __ATOMIC_ACQ_REL);   /* (the mailbox emptied: ours now) */
    if (!p)
        return cur;
    for (r = 0; cur && cur != &lxw_none && r < 4u; r++) {   /* (the old one back to the loader) */
        const lxw_buf_t *z = 0;
        if (__atomic_compare_exchange_n(&l->s[slot].ret[r], &z, cur, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
    }
    return p;
}
HID void lxw_free(const lxw_buf_t *b) { buf_free(b); }
HID int lxw_status(lxw_loader_t *l, uint32_t slot)
{
    return l && slot < l->nslots ? __atomic_load_n(&l->s[slot].status, __ATOMIC_ACQUIRE) : 0;
}

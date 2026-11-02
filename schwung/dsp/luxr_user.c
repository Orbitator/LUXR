/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): kits of your own (luxr_user.h). One request at a time: the audio thread fills it in
 * and raises `req`; the worker serves it, lowers `req`. The list is double-buffered: the worker fills the table not in
 * use and flips `cur`. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "luxr_user.h"

#define HID __attribute__((visibility("hidden")))
enum { RQ_NONE, RQ_SAVE, RQ_LOAD, RQ_DELETE };

typedef struct { uint32_t n; char name[LXU_MAX][LXU_NAME]; } lxu_list_t;
struct lxu {
    char dir[200];
    int req;                                     /* (atomic) RQ_* */
    char rq_name[LXU_NAME];
    uint32_t rq_len;
    char sv_text[LXU_TEXT];                      /* a kit to save (the audio thread's copy) */
    char ld_text[LXU_TEXT];                      /* a kit read */
    int ld_ready;                                /* (atomic) */
    int saved;                                   /* (atomic) */
    char saved_name[LXU_NAME];
    lxu_list_t list[2];
    int cur;                                     /* (atomic) the table in use */
    int scanned;                                 /* (the worker's) */
    char err[64];
    uint32_t cpu_seq, cpu_done;                  /* (atomic) a report waiting */
    uint32_t cpu_avg, cpu_peak, cpu_voices;
    char cpu_kit[LXU_NAME];
    uint32_t cpu_lines;
};

static int name_ok(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_'; }
static void clean_name(char *d, const char *s)   /* a file name: letters, digits, space - _ (24 at most, trimmed) */
{
    uint32_t n = 0;
    while (*s == ' ')
        s++;
    for (; *s && n + 1u < LXU_NAME; s++)
        if (name_ok(*s))
            d[n++] = *s;
    while (n && d[n - 1u] == ' ')
        n--;
    d[n] = 0;
}
static int cmp_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void path_of(const lxu_t *u, const char *name, char *p, size_t n) { snprintf(p, n, "%s/%s.json", u->dir, name); }
static void mkdirs(const char *dir)              /* (mkdir -p) */
{
    char p[200];
    size_t i;
    snprintf(p, sizeof p, "%s", dir);
    for (i = 1; p[i]; i++)
        if (p[i] == '/') {
            p[i] = 0;
            mkdir(p, 0775);
            p[i] = '/';
        }
    mkdir(p, 0775);
}
static void scan(lxu_t *u)                       /* the folder read into the table not in use, then flipped */
{
    int nx = !__atomic_load_n(&u->cur, __ATOMIC_ACQUIRE);
    lxu_list_t *l = &u->list[nx];
    DIR *d = opendir(u->dir);
    struct dirent *e;
    l->n = 0;
    if (d) {
        while ((e = readdir(d)) && l->n < LXU_MAX) {
            size_t n = strlen(e->d_name);
            char nm[LXU_NAME];
            if (n < 6 || n - 5u >= LXU_NAME || strcmp(e->d_name + n - 5, ".json"))
                continue;
            memcpy(nm, e->d_name, n - 5u);
            nm[n - 5u] = 0;
            clean_name(l->name[l->n], nm);
            if (l->name[l->n][0] && !strcmp(l->name[l->n], nm))
                l->n++;
        }
        closedir(d);
    }
    qsort(l->name, l->n, LXU_NAME, cmp_name);
    __atomic_store_n(&u->cur, nx, __ATOMIC_RELEASE);
}

HID lxu_t *lxu_open(void)
{
    lxu_t *u = calloc(1, sizeof *u);
    const char *d = getenv("LUXR_KITS_DIR");
    if (u)
        snprintf(u->dir, sizeof u->dir, "%s", d && d[0] ? d : LXU_DIR);
    return u;
}
HID void lxu_close(lxu_t *u) { free(u); }

HID void lxu_poll(void *arg)
{
    lxu_t *u = arg;
    int r;
    char p[260], tmp[270];
    if (!u)
        return;
    if (!u->scanned) {
        scan(u);
        u->scanned = 1;
    }
    {                                            /* the CPU log: a line a report */
        uint32_t q = __atomic_load_n(&u->cpu_seq, __ATOMIC_ACQUIRE);
        if (q != __atomic_load_n(&u->cpu_done, __ATOMIC_ACQUIRE)) {
            char lp[260], ts[32], kit[LXU_NAME];
            FILE *f;
            time_t now = time(0);
            uint32_t avg = u->cpu_avg, peak = u->cpu_peak, vo = u->cpu_voices;
            memcpy(kit, u->cpu_kit, sizeof kit);
            __atomic_store_n(&u->cpu_done, q, __ATOMIC_RELEASE);   /* (copied: the next report may come) */
            strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
            mkdirs(u->dir);
            snprintf(lp, sizeof lp, "%s/../cpu.log", u->dir);
            if (u->cpu_lines++ % 2000u == 0u)    /* (a fresh file now and then: it stays small) */
                unlink(lp);
            if ((f = fopen(lp, "a"))) {
                fprintf(f, "%s  avg %4u us  peak %5u us  (%2u%% / %2u%% of a block)  voices %u  kit %s\n", ts, avg, peak,
                        avg * 100u / 2902u, peak * 100u / 2902u, vo, kit);
                fclose(f);
            }
        }
    }
    r = __atomic_load_n(&u->req, __ATOMIC_ACQUIRE);
    if (r == RQ_SAVE) {
        FILE *f;
        mkdirs(u->dir);
        path_of(u, u->rq_name, p, sizeof p);
        snprintf(tmp, sizeof tmp, "%s.tmp", p);
        if ((f = fopen(tmp, "wb")) && fwrite(u->sv_text, 1, u->rq_len, f) == u->rq_len && !fclose(f) && !rename(tmp, p)) {
            scan(u);
            memcpy(u->saved_name, u->rq_name, LXU_NAME);
            __atomic_store_n(&u->saved, 1, __ATOMIC_RELEASE);
            u->err[0] = 0;
        } else {
            snprintf(u->err, sizeof u->err, "save failed: %s", strerror(errno));
            unlink(tmp);
        }
    } else if (r == RQ_LOAD) {
        FILE *f;
        size_t n = 0;
        path_of(u, u->rq_name, p, sizeof p);
        if ((f = fopen(p, "rb"))) {
            n = fread(u->ld_text, 1, LXU_TEXT - 1u, f);
            fclose(f);
            u->ld_text[n] = 0;
            __atomic_store_n(&u->ld_ready, 1, __ATOMIC_RELEASE);
            u->err[0] = 0;
        } else {
            snprintf(u->err, sizeof u->err, "load failed: %s", strerror(errno));
        }
    } else if (r == RQ_DELETE) {
        path_of(u, u->rq_name, p, sizeof p);
        if (unlink(p))
            snprintf(u->err, sizeof u->err, "delete failed: %s", strerror(errno));
        scan(u);
    }
    if (r != RQ_NONE)
        __atomic_store_n(&u->req, RQ_NONE, __ATOMIC_RELEASE);
}

static const lxu_list_t *list(lxu_t *u) { return &u->list[__atomic_load_n(&u->cur, __ATOMIC_ACQUIRE)]; }
HID uint32_t lxu_count(lxu_t *u) { return u ? list(u)->n : 0u; }
HID const char *lxu_name(lxu_t *u, uint32_t i) { return u && i < list(u)->n ? list(u)->name[i] : ""; }
static int post(lxu_t *u, int r)
{
    __atomic_store_n(&u->req, r, __ATOMIC_RELEASE);
    return 0;
}
static int busy(lxu_t *u) { return !u || __atomic_load_n(&u->req, __ATOMIC_ACQUIRE) != RQ_NONE; }
HID int lxu_save(lxu_t *u, const char *name, const char *text, uint32_t n)
{
    if (busy(u) || n >= LXU_TEXT)
        return -1;
    clean_name(u->rq_name, name);
    if (!u->rq_name[0])
        return -1;
    memcpy(u->sv_text, text, n);
    u->rq_len = n;
    return post(u, RQ_SAVE);
}
HID int lxu_load(lxu_t *u, uint32_t i)
{
    if (busy(u) || i >= list(u)->n)
        return -1;
    memcpy(u->rq_name, list(u)->name[i], LXU_NAME);
    __atomic_store_n(&u->ld_ready, 0, __ATOMIC_RELEASE);
    return post(u, RQ_LOAD);
}
HID int lxu_delete(lxu_t *u, uint32_t i)
{
    if (busy(u) || i >= list(u)->n)
        return -1;
    memcpy(u->rq_name, list(u)->name[i], LXU_NAME);
    return post(u, RQ_DELETE);
}
HID const char *lxu_loaded(lxu_t *u)
{
    if (!u || !__atomic_load_n(&u->ld_ready, __ATOMIC_ACQUIRE))
        return 0;
    __atomic_store_n(&u->ld_ready, 0, __ATOMIC_RELEASE);
    return u->ld_text;                           /* (unchanged until this thread asks for the next load) */
}
HID int lxu_saved(lxu_t *u, char *name, uint32_t n)
{
    if (!u || !__atomic_load_n(&u->saved, __ATOMIC_ACQUIRE))
        return 0;
    snprintf(name, n, "%s", u->saved_name);
    __atomic_store_n(&u->saved, 0, __ATOMIC_RELEASE);
    return 1;
}
HID const char *lxu_error(lxu_t *u) { return u ? u->err : ""; }
HID void lxu_cpu(lxu_t *u, uint32_t avg_us, uint32_t peak_us, uint32_t voices, const char *kit)
{
    if (!u || __atomic_load_n(&u->cpu_seq, __ATOMIC_ACQUIRE) != __atomic_load_n(&u->cpu_done, __ATOMIC_ACQUIRE))
        return;                                  /* (the last one not written yet: skipped) */
    u->cpu_avg = avg_us;
    u->cpu_peak = peak_us;
    u->cpu_voices = voices;
    snprintf(u->cpu_kit, sizeof u->cpu_kit, "%s", kit);
    __atomic_add_fetch(&u->cpu_seq, 1u, __ATOMIC_RELEASE);
}

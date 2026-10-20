/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 *
 * Additional term under section 7(b) of the GNU GPL version 3: any work that
 * contains this file, or a work based on it, must preserve the author
 * attribution "Hany El Imam" and the credits of the WEFAX, NAVTEX/SITOR-B,
 * DSC, SSTV and FT8 CB applications in the Appropriate Legal Notices
 * displayed by its user interface (in this program: the Info window of each
 * application).  See NOTICE-1KO125.md.
 */

/* FT8 CB - settings and log, see cb_store.h */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE         /* timegm, strcasestr */
#endif
#include "cb_store.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_FREQ_HZ  27265000
#define WORKED_MAX       4000

/* ---- settings ------------------------------------------------------------------ */

void ft8cb_conf_load(ft8cb_conf_t *c)
{
    memset(c, 0, sizeof(*c));
    c->freq_hz = DEFAULT_FREQ_HZ;
    FILE *f = fopen(FT8CB_CONF_FILE, "r");
    if (!f) return;
    char line[96];
    while (fgets(line, sizeof(line), f)) {
        char *v = strchr(line, '=');
        if (!v) continue;
        *v++ = '\0';
        v[strcspn(v, "\r\n")] = '\0';
        if (strcmp(line, "call") == 0) {
            snprintf(c->call, sizeof(c->call), "%s", v);
            for (char *p = c->call; *p; p++) *p = (char)toupper((unsigned char)*p);
        } else if (strcmp(line, "freq") == 0) {
            long hz = atol(v);
            if (hz > 26000000 && hz < 28000000) c->freq_hz = (int32_t)hz;
        }
    }
    fclose(f);
}

void ft8cb_conf_save(const ft8cb_conf_t *c)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", FT8CB_CONF_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "call=%s\nfreq=%ld\n", c->call, (long)c->freq_hz);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    rename(tmp, FT8CB_CONF_FILE);
}

/* ---- log ------------------------------------------------------------------------ */

static void field(FILE *f, const char *key, const char *val)
{
    if (val && val[0]) fprintf(f, "<%s:%u>%s", key, (unsigned)strlen(val), val);
}

static void worked_add(const char *call);

bool ft8cb_log_add(const ft8cb_qso_rec_t *q)
{
    bool fresh = access(FT8CB_LOG_FILE, F_OK) != 0;
    FILE *f = fopen(FT8CB_LOG_FILE, "a");
    if (!f) return false;
    if (fresh)
        fprintf(f, "1KO125 FT8 CB log\r\n<PROGRAMID:12>X6100 FT8 CB<ADIF_VER:5>3.1.4<EOH>\r\n");

    struct tm tm;
    gmtime_r(&q->time, &tm);
    char date[12], tim[8], freq[16], rs[8], rr[8];
    strftime(date, sizeof(date), "%Y%m%d", &tm);
    strftime(tim, sizeof(tim), "%H%M%S", &tm);
    snprintf(freq, sizeof(freq), "%.6f", (double)q->freq_hz / 1e6);
    snprintf(rs, sizeof(rs), "%+03d", q->rst_sent);
    snprintf(rr, sizeof(rr), "%+03d", q->rst_rcvd);

    field(f, "CALL", q->his_call);
    field(f, "QSO_DATE", date);
    field(f, "TIME_ON", tim);
    field(f, "BAND", "11m");
    field(f, "FREQ", freq);
    field(f, "MODE", "FT8");
    field(f, "RST_SENT", rs);
    field(f, "RST_RCVD", rr);
    field(f, "STATION_CALLSIGN", q->my_call);
    field(f, "MY_GRIDSQUARE", q->my_grid);
    field(f, "GRIDSQUARE", q->his_grid);
    fprintf(f, "<EOR>\r\n");
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    worked_add(q->his_call);
    return true;
}

/* ---- worked before ---------------------------------------------------------------- */

typedef struct {
    char   call[12];
    time_t last;            /* last QSO, UTC */
} worked_t;

static worked_t *worked = NULL;
static int nworked = 0;
static pthread_mutex_t worked_mutex = PTHREAD_MUTEX_INITIALIZER;   /* UI and decoder threads */

/* call with worked_mutex held */
static worked_t *worked_find(const char *call)
{
    if (!worked || !call || !call[0]) return NULL;
    for (int i = 0; i < nworked; i++)
        if (strcasecmp(worked[i].call, call) == 0) return &worked[i];
    return NULL;
}

static void worked_add_at(const char *call, time_t t)
{
    if (!call || !call[0]) return;
    pthread_mutex_lock(&worked_mutex);
    worked_t *w = worked_find(call);
    if (w) {
        if (t > w->last) w->last = t;
    } else if (worked && nworked < WORKED_MAX) {
        snprintf(worked[nworked].call, sizeof(worked[nworked].call), "%s", call);
        worked[nworked++].last = t;
    }
    pthread_mutex_unlock(&worked_mutex);
}

static void worked_add(const char *call)
{
    worked_add_at(call, time(NULL));
}

/* value of the ADIF field name in the record rec ("" if missing) */
static void adif_field(const char *rec, const char *name, char *out, size_t sz)
{
    out[0] = '\0';
    size_t nl = strlen(name);
    for (const char *p = rec; (p = strchr(p, '<')) != NULL; p++) {
        if (strncasecmp(p + 1, name, nl) != 0 || p[1 + nl] != ':') continue;
        int n = atoi(p + 2 + nl);
        const char *v = strchr(p, '>');
        if (!v || n <= 0) return;
        snprintf(out, sz, "%.*s", n, v + 1);
        return;
    }
}

/* QSO_DATE YYYYMMDD + TIME_ON HHMM[SS], UTC; 0 if not valid */
static time_t adif_time(const char *date, const char *tim)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (strlen(date) != 8 || strlen(tim) < 4) return 0;
    if (sscanf(date, "%4d%2d%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return 0;
    if (sscanf(tim, "%2d%2d", &tm.tm_hour, &tm.tm_min) != 2) return 0;
    if (strlen(tim) >= 6) tm.tm_sec = atoi(tim + 4);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return timegm(&tm);
}

static void worked_record(const char *rec)
{
    char call[16], date[16], tim[16];
    adif_field(rec, "CALL", call, sizeof(call));
    if (!call[0]) return;
    adif_field(rec, "QSO_DATE", date, sizeof(date));
    adif_field(rec, "TIME_ON", tim, sizeof(tim));
    worked_add_at(call, adif_time(date, tim));
}

void ft8cb_worked_load(void)
{
    ft8cb_worked_free();
    pthread_mutex_lock(&worked_mutex);
    worked = calloc(WORKED_MAX, sizeof(worked_t));
    pthread_mutex_unlock(&worked_mutex);
    if (!worked) return;
    FILE *f = fopen(FT8CB_LOG_FILE, "r");
    if (!f) return;
    /* records end with <EOR>; they may span lines */
    char rec[2048], line[512];
    size_t rl = 0;
    rec[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        for (;;) {
            char *e = strcasestr(p, "<EOR>");
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (rl + n < sizeof(rec)) {
                memcpy(rec + rl, p, n);
                rl += n;
                rec[rl] = '\0';
            }
            if (!e) break;
            worked_record(rec);
            rl = 0;
            rec[0] = '\0';
            p = e + 5;
        }
    }
    fclose(f);
}

void ft8cb_worked_free(void)
{
    pthread_mutex_lock(&worked_mutex);
    free(worked);
    worked = NULL;
    nworked = 0;
    pthread_mutex_unlock(&worked_mutex);
}

ft8cb_worked_t ft8cb_worked(const char *call)
{
    ft8cb_worked_t r = FT8CB_WORKED_NO;
    time_t now = time(NULL);
    pthread_mutex_lock(&worked_mutex);
    worked_t *w = worked_find(call);
    if (w) r = (w->last > now - FT8CB_RECENT_S) ? FT8CB_WORKED_RECENT : FT8CB_WORKED_OLD;
    pthread_mutex_unlock(&worked_mutex);
    return r;
}

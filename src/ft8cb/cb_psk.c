/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (C) 2026 Hany El Imam
 *
 *  Xiegu X6100 LVGL GUI - FT8 CB: who heard you, from PSK Reporter's web
 *  query over HTTPS (see cb_psk.h for why)
 */

#include "cb_psk.h"
#include "cb_psk_roots.h"

#ifndef CBPSK_NO_GUI
#include "../scheduler.h"
#include "lvgl/lvgl.h"      /* LV_LOG_USER */
#endif

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <gnutls/gnutls.h>

#define HOST        "retrieve.pskreporter.info"
#define QUERY_S     300             /* PSK Reporter: no more than one query every 5 minutes
                                       (sooner it answers 429 Too Many Requests) */
#define WINDOW_S    86400           /* the reports of the last 24 hours */
#define NET_S       20              /* each step of the network gives up after this */
#define ANSWER_MAX  (1024 * 1024)   /* a day of one call's reports is far less */
#define CB_LO_HZ    26000000
#define CB_HI_HZ    28000000

/* ---- The answer: <receptionReport receiverCallsign="14AT061" receiverLocator="JN08ER"
 *      senderCallsign="1KO125" frequency="27267385" flowStartSeconds="1795092041"
 *      mode="FT8" sNR="-20" ... /> ---- */

/* The value of ` name="..."` inside one report, "" if missing */
static void attr(const char *rep, const char *end, const char *name, char *out, size_t size) {
    char key[40];
    snprintf(key, sizeof(key), " %s=\"", name);
    out[0]        = '\0';
    const char *p = rep;
    size_t      k = strlen(key);
    while ((p = strstr(p, key)) && p < end) {
        p += k;
        const char *q = memchr(p, '"', (size_t)(end - p));
        if (!q) return;
        size_t n = (size_t)(q - p);
        if (n >= size) n = size - 1;
        memcpy(out, p, n);
        out[n] = '\0';
        return;
    }
}

/* Upper case, 4 or 6 characters kept; false without a usable 4 */
static bool clean_grid(char *g) {
    g[strnlen(g, 6)] = '\0';
    for (char *c = g; *c; c++) *c = (char)toupper((unsigned char)*c);
    size_t n = strlen(g);
    if (n < 4 || g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !isdigit((unsigned char)g[2]) ||
        !isdigit((unsigned char)g[3]))
        return false;
    if (n != 6 || g[4] < 'A' || g[4] > 'X' || g[5] < 'A' || g[5] > 'X') g[4] = '\0';
    return true;
}

int cbpsk_parse(const char *xml, const char *me,
                void (*spot)(void *arg, const char *call, const char *grid, int snr, int64_t when), void *arg) {
    int         n = 0;
    const char *p = xml;
    while ((p = strstr(p, "<receptionReport "))) {
        const char *end = strstr(p, "/>");
        if (!end) break;
        char rc[16], rl[12], sc[16], f[16], t[16], md[12], snr[8];
        attr(p, end, "receiverCallsign", rc, sizeof(rc));
        attr(p, end, "receiverLocator", rl, sizeof(rl));
        attr(p, end, "senderCallsign", sc, sizeof(sc));
        attr(p, end, "frequency", f, sizeof(f));
        attr(p, end, "flowStartSeconds", t, sizeof(t));
        attr(p, end, "mode", md, sizeof(md));
        attr(p, end, "sNR", snr, sizeof(snr));
        p = end + 2;
        long long hz = atoll(f), when = atoll(t);
        if (strcasecmp(sc, me) != 0 || !rc[0] || strcasecmp(rc, me) == 0) continue;
        if (strcasecmp(md, "FT8") != 0 || hz < CB_LO_HZ || hz >= CB_HI_HZ || when <= 0) continue;
        if (!clean_grid(rl)) continue;
        for (char *c = rc; *c; c++) *c = (char)toupper((unsigned char)*c);
        if (spot) spot(arg, rc, rl, snr[0] ? atoi(snr) : -30, when);
        n++;
    }
    return n;
}

#ifndef CBPSK_NO_GUI

/* ---- HTTPS ---- */

static int connect_to(const char *host) {
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *res, *a;
    if (getaddrinfo(host, "443", &hints, &res) != 0) return -1;
    int fd = -1;
    for (a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv = {.tv_sec = NET_S};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)); /* connect() too */
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* GET https://HOST`path`; the body (malloc'ed, 0-terminated) or NULL */
static char *https_get(const char *path) {
    static gnutls_certificate_credentials_t cred;
    if (!cred) {
        if (gnutls_certificate_allocate_credentials(&cred) < 0) return NULL;
        int n = 0;
        for (size_t i = 0; i < sizeof(cb_psk_roots) / sizeof(cb_psk_roots[0]); i++) {
            gnutls_datum_t root = {(unsigned char *)cb_psk_roots[i], (unsigned)strlen(cb_psk_roots[i])};
            if (gnutls_certificate_set_x509_trust_mem(cred, &root, GNUTLS_X509_FMT_PEM) > 0) n++;
        }
        if (!n) {
            gnutls_certificate_free_credentials(cred);
            cred = NULL;
            return NULL;
        }
    }
    int fd = connect_to(HOST);
    if (fd < 0) {
        LV_LOG_USER("PSK Reporter: can't reach %s", HOST);
        return NULL;
    }
    gnutls_session_t s;
    char            *buf = NULL;
    size_t           len = 0;
    bool             ok  = false;
    if (gnutls_init(&s, GNUTLS_CLIENT) < 0) {
        close(fd);
        return NULL;
    }
    gnutls_server_name_set(s, GNUTLS_NAME_DNS, HOST, strlen(HOST));
    gnutls_set_default_priority(s);
    gnutls_credentials_set(s, GNUTLS_CRD_CERTIFICATE, cred);
    gnutls_session_set_verify_cert(s, HOST, 0); /* the chain to a root above, for this name */
    gnutls_transport_set_int(s, fd);
    gnutls_handshake_set_timeout(s, NET_S * 1000);
    int r;
    do r = gnutls_handshake(s);
    while (r < 0 && !gnutls_error_is_fatal(r) && r != GNUTLS_E_AGAIN);
    if (r < 0) {
        /* A wrong clock makes the certificate look expired or not yet valid */
        LV_LOG_USER("PSK Reporter: TLS %s", gnutls_strerror(r));
        goto out;
    }
    char req[512];
    int  n = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: " HOST "\r\nUser-Agent: X6100-1KO125-FT8CB\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                      path);
    if (n <= 0 || n >= (int)sizeof(req)) goto out;
    for (int sent = 0; sent < n;) {
        ssize_t k = gnutls_record_send(s, req + sent, (size_t)(n - sent));
        if (k == GNUTLS_E_INTERRUPTED) continue;
        if (k <= 0) goto out;
        sent += (int)k;
    }
    buf = malloc(ANSWER_MAX + 1);
    if (!buf) goto out;
    time_t until = time(NULL) + NET_S;
    for (;;) {
        if (len >= ANSWER_MAX) goto out;
        ssize_t k = gnutls_record_recv(s, buf + len, ANSWER_MAX - len);
        if (k == GNUTLS_E_INTERRUPTED) continue;
        /* TLS 1.3: after the server's session ticket GnuTLS 3.7 returns
         * E_AGAIN once, even on a blocking socket (tested on 3.7.3, the
         * radio has 3.7.8): read again. The socket's own timeout gives
         * E_AGAIN too, so not past NET_S. */
        if (k == GNUTLS_E_AGAIN && time(NULL) < until) continue;
        if (k == 0 || k == GNUTLS_E_PREMATURE_TERMINATION) break; /* the end: HTTP/1.0 closes */
        if (k < 0) goto out;
        len += (size_t)k;
    }
    buf[len] = '\0';
    ok       = true;
out:
    gnutls_deinit(s);
    close(fd);
    if (!ok) {
        free(buf);
        return NULL;
    }
    /* "HTTP/1.1 200 OK" ... blank line, the body */
    char *body = strstr(buf, "\r\n\r\n");
    if (strncmp(buf, "HTTP/1.", 7) != 0 || atoi(buf + 9) != 200 || !body) {
        LV_LOG_USER("PSK Reporter: %.*s", (int)strcspn(buf, "\r\n"), buf); /* "HTTP/1.1 429 Too Many Requests" */
        free(buf);
        return NULL;
    }
    memmove(buf, body + 4, len - (size_t)(body + 4 - buf) + 1);
    return buf;
}

/* ---- The thread ---- */

static pthread_mutex_t mux  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond;        /* on CLOCK_MONOTONIC, made once */
static pthread_once_t  cond_once = PTHREAD_ONCE_INIT;
static bool            thread_up;
static char            want[16];    /* "" stopped */
static unsigned        gen;         /* changes with each cbpsk_watch() of a new call */
static cbpsk_cb_t      cbs;         /* LVGL thread only */
static unsigned        ui_gen;      /* LVGL thread only: what's asked now */

typedef struct {
    unsigned gen;
    char     call[16];
    char     grid[8];
    int      snr;
    int64_t  when;
} spot_t;

typedef struct {
    unsigned gen;
    bool     ok;
    int      n;
} result_t;

/* A query's answers, dropped if the app changed its mind (or closed) since */
static void spot_cb(void *arg) {
    spot_t *s = arg;
    if (s->gen == ui_gen && cbs.on_spot) cbs.on_spot(s->call, s->grid, s->snr, s->when);
}

static void result_cb(void *arg) {
    result_t *r = arg;
    if (r->gen == ui_gen && cbs.on_result) cbs.on_result(r->ok, r->n);
}

static void put_spot(void *arg, const char *call, const char *grid, int snr, int64_t when) {
    spot_t s = {.gen = *(unsigned *)arg, .snr = snr, .when = when};
    snprintf(s.call, sizeof(s.call), "%s", call);
    snprintf(s.grid, sizeof(s.grid), "%s", grid);
    scheduler_put(spot_cb, &s, sizeof(s));
}

/* The last answer, for the app opened again within the 5 minutes */
static char *cache_xml;
static char  cache_call[16];

static void answer(const char *call, unsigned g, const char *xml) {
    result_t r = {.gen = g, .ok = xml != NULL};
    if (xml) r.n = cbpsk_parse(xml, call, put_spot, &g);
    LV_LOG_USER("PSK Reporter: %s, %s, %d reports", call, r.ok ? "ok" : "failed", r.n);
    scheduler_put(result_cb, &r, sizeof(r));
}

static void query(const char *call, unsigned g) {
    char enc[48], path[256];
    size_t n = 0;
    for (const char *c = call; *c && n + 4 < sizeof(enc); c++) {
        if (isalnum((unsigned char)*c)) enc[n++] = *c;
        else n += (size_t)snprintf(enc + n, sizeof(enc) - n, "%%%02X", (unsigned char)*c); /* "/" */
    }
    enc[n] = '\0';
    snprintf(path, sizeof(path), "/query?senderCallsign=%s&flowStartSeconds=-%d&rronly=1&noactive=1&mode=FT8", enc,
             WINDOW_S);
    char *xml = https_get(path);
    answer(call, g, xml);
    free(cache_xml);
    cache_xml = xml;        /* NULL after a failure: nothing old to show */
    snprintf(cache_call, sizeof(cache_call), "%s", call);
}

static int64_t mono_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec;
}

static void *thread_main(void *a) {
    (void)a;
    int64_t  last     = 0;      /* the last query, 0 none */
    unsigned last_gen = 0;
    pthread_mutex_lock(&mux);
    for (;;) {
        while (!want[0]) pthread_cond_wait(&cond, &mux);
        int64_t now  = mono_s();
        int64_t wait = last ? last + QUERY_S - now : 0;
        if (wait > 0 && gen != last_gen) {
            /* Asked again (the app reopened) before PSK Reporter may be
             * asked: the last answer now, if it was this call's */
            unsigned g = gen;
            char     call[16];
            snprintf(call, sizeof(call), "%s", want);
            last_gen = g;
            if (cache_xml && strcasecmp(call, cache_call) == 0) {
                pthread_mutex_unlock(&mux);
                answer(call, g, cache_xml);
                pthread_mutex_lock(&mux);
            }
            continue;
        }
        if (wait > 0) {
            struct timespec until;
            clock_gettime(CLOCK_MONOTONIC, &until);
            until.tv_sec += wait;
            pthread_cond_timedwait(&cond, &mux, &until); /* or sooner: a new call, a stop */
            continue;
        }
        char     call[16];
        unsigned g = gen;
        snprintf(call, sizeof(call), "%s", want);
        pthread_mutex_unlock(&mux);
        query(call, g);
        pthread_mutex_lock(&mux);
        last     = mono_s();
        last_gen = g;
    }
    return NULL;
}

static void cond_make(void) {
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&cond, &ca);
    pthread_condattr_destroy(&ca);
}

void cbpsk_watch(const char *call, const cbpsk_cb_t *cb) {
    pthread_once(&cond_once, cond_make);
    char c[16] = "";
    if (call) snprintf(c, sizeof(c), "%s", call);
    if (cb) cbs = *cb;
    pthread_mutex_lock(&mux);
    if (strcasecmp(c, want) != 0) {
        snprintf(want, sizeof(want), "%s", c);
        gen++;
        ui_gen = gen;
        pthread_cond_signal(&cond);
    }
    if (want[0] && !thread_up) {
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_t t;
        thread_up = pthread_create(&t, &at, thread_main, NULL) == 0;
        pthread_attr_destroy(&at);
    }
    pthread_mutex_unlock(&mux);
}

#endif /* CBPSK_NO_GUI */

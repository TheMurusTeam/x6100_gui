/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xiegu X6100 - diagnostic logs (1KO125, Hany El Imam 2026), see diag_log.h
 */

#include "diag_log.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define PTT_LOG         "/mnt/ptt.log"
#define I2C_LOG         "/mnt/i2c.log"
#define DIAG_MAX_BYTES  (512 * 1024)
#define DIAG_MAX_PER_S  10          /* lines per second and file, the rest only counted */
#define DIAG_ECHO_PER_S 5           /* of them, messages copied from stderr / kernel */

static __thread const char *thread_source = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int stderr_orig = -1;
static atomic_bool i2c_error;

/* lines written in the current second, per file */
typedef struct {
    time_t   sec;
    int      n;
    unsigned skipped;
} rate_t;
static rate_t rate_ptt, rate_i2c, rate_echo;

/* ---- writing ------------------------------------------------------------------- */

static void vlog_to(const char *path, rate_t *rate, int max_per_s, const char *fmt, va_list ap)
{
    char line[512];
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    int n = (int)strftime(line, sizeof(line), "%Y-%m-%d %H:%M:%S", &tm);
    n += snprintf(line + n, sizeof(line) - (size_t)n, ".%03ld UTC  ", ts.tv_nsec / 1000000);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';

    pthread_mutex_lock(&log_mutex);
    /* a burst of errors must not wear the SD card: DIAG_MAX_PER_S lines a second */
    if (rate->sec != ts.tv_sec) {
        rate->sec = ts.tv_sec;
        rate->n   = 0;
    }
    if (++rate->n > max_per_s) {
        rate->skipped++;
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    if (rate->skipped) {
        snprintf(line + len, sizeof(line) - len, "  (%u lines skipped before this one)", rate->skipped);
        len = strlen(line);
        rate->skipped = 0;
    }
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > DIAG_MAX_BYTES) {
        char old[64];
        snprintf(old, sizeof(old), "%s.old", path);
        rename(path, old);
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        if (len > sizeof(line) - 2) len = sizeof(line) - 2;
        line[len++] = '\n';
        ssize_t w = write(fd, line, len);
        (void)w;
        fsync(fd);              /* the next event may be a crash or a power cut */
        close(fd);
    }
    pthread_mutex_unlock(&log_mutex);
}

void diag_ptt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog_to(PTT_LOG, &rate_ptt, DIAG_MAX_PER_S, fmt, ap);
    va_end(ap);
}

void diag_i2c_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog_to(I2C_LOG, &rate_i2c, DIAG_MAX_PER_S, fmt, ap);
    va_end(ap);
}

/* a message copied from stderr or from the kernel: its own, lower limit, so
 * that a storm of them leaves room for the lines of the APP */
static void echo_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog_to(I2C_LOG, &rate_echo, DIAG_ECHO_PER_S, fmt, ap);
    va_end(ap);
}

void diag_set_source(const char *name)
{
    thread_source = name;
}

bool diag_i2c_error_take(void)
{
    return atomic_exchange(&i2c_error, false);
}

const char *diag_source(void)
{
    static __thread char buf[32];
    if (thread_source) return thread_source;
    snprintf(buf, sizeof(buf), "thread %ld", (long)syscall(SYS_gettid));
    return buf;
}

/* ---- I2C errors -------------------------------------------------------------------- */

static bool has_i2c(const char *s)
{
    for (const char *p = s; p[0] && p[1] && p[2]; p++)
        if (tolower((unsigned char)p[0]) == 'i' && p[1] == '2' && tolower((unsigned char)p[2]) == 'c')
            return true;
    return false;
}

/* stderr goes through a pipe: every line is copied to the real stderr, and
 * the lines about I2C (the perror() of the control library) to i2c.log */
static void *stderr_thread(void *arg)
{
    int fd = (int)(long)arg;
    char buf[1024], line[512];
    size_t ll = 0;
    diag_set_source("diag stderr");
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        if (stderr_orig >= 0) {
            ssize_t w = write(stderr_orig, buf, (size_t)n);
            (void)w;
        }
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n' || ll == sizeof(line) - 1) {
                line[ll] = '\0';
                if (strstr(line, "Can't write to i2c")) atomic_store(&i2c_error, true);
                if (has_i2c(line)) echo_log("APP: %s", line);
                ll = 0;
            } else {
                line[ll++] = buf[i];
            }
        }
    }
    return NULL;
}

/* kernel messages about I2C (bus timeouts, lost arbitration...) */
static void *kmsg_thread(void *arg)
{
    (void)arg;
    diag_set_source("diag kmsg");
    int fd = open("/dev/kmsg", O_RDONLY);
    if (fd < 0) return NULL;
    lseek(fd, 0, SEEK_END);             /* only the new messages */
    char rec[1024];
    for (;;) {
        ssize_t n = read(fd, rec, sizeof(rec) - 1);
        if (n < 0) {
            if (errno == EINTR || errno == EPIPE) continue;   /* EPIPE: overwritten records */
            break;
        }
        rec[n] = '\0';
        char *msg = strchr(rec, ';');   /* "prio,seq,usec,flags;text" */
        msg = msg ? msg + 1 : rec;
        msg[strcspn(msg, "\n")] = '\0';      /* without the SUBSYSTEM=/DEVICE= lines */
        if (strstr(msg, "i2c-0:")) atomic_store(&i2c_error, true);   /* the bus to the BASE */
        if (has_i2c(msg)) echo_log("KERNEL: %s", msg);
    }
    close(fd);
    return NULL;
}

void diag_init(void)
{
    diag_set_source("UI");              /* the thread of main(): LVGL, keys */
    diag_ptt_log("---- start (firmware running) ----");
    diag_i2c_log("---- start (firmware running) ----");

    int p[2];
    pthread_t t;
    if (pipe(p) == 0) {
        stderr_orig = dup(STDERR_FILENO);
        fcntl(p[1], F_SETFL, O_NONBLOCK);       /* a full pipe drops text, never blocks the app */
        dup2(p[1], STDERR_FILENO);
        close(p[1]);
        setvbuf(stderr, NULL, _IONBF, 0);
        if (pthread_create(&t, NULL, stderr_thread, (void *)(long)p[0]) == 0) pthread_detach(t);
    }
    if (pthread_create(&t, NULL, kmsg_thread, NULL) == 0) pthread_detach(t);
}

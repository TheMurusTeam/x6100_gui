/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
/*
 * Xiegu X6100 - NAVTEX / SITOR-B scan ("Scan"), included by dialog_navtex.c.
 *
 * The frequencies of the list ("Freq.", dialog_navtex_freq.inc) up to the
 * highest, each once and in order of frequency (fewer clicks of the band
 * relays): from the lowest (490 kHz) the first time after the app opens,
 * after the end of the list and on a long press of Scan; otherwise on from
 * the frequency where it stopped.  On each one:
 *
 *   1. Wait: the time the radio needs after a frequency change (relays,
 *      audio, spectrum); 0.5 to 1.5 s, chosen with the "Wait" button;
 *   2. 0.3 s of audio: is there energy around the two tones (1000 Hz
 *      +/- 85 Hz, AFC range included) well above the audio on both sides?
 *      If not, on to the next frequency;
 *   3. if so, up to 5 s for the decoder to lock (phasing or DX/RX
 *      repetitions of a real SITOR-B signal: RTTY, DGPS and other FSK do
 *      not lock it).
 *
 * The scan stops on the first lock, at the end of the list, when the
 * frequency is changed by hand, or when Scan is pressed again.  It never
 * starts again by itself.
 */

#define SCAN_MEASURE_MS     300
#define SCAN_CONFIRM_MS     5000
/* tone band over the bands beside it: plain noise reads 0 dB (at most
 * +1.5 dB in 0.3 s), a SITOR-B signal at -5 dB SNR (2500 Hz) +5 dB, at
 * -8 dB +3.5 dB (synthetic, tests/navtex/sitorb_gen) */
#define SCAN_RATIO_DB       3.0f
#define SCAN_FILE           NAVTEX_SAVE_DIR "/scan.txt"

/* Goertzel blocks of 20 ms at 12000 Hz: bins 50 Hz wide, every 25 Hz */
#define SCAN_BLOCK          240
#define SCAN_TONE_LO        850     /* 1000 -/+ (85 + AFC range + half a bin) */
#define SCAN_TONE_HI        1150
#define SCAN_REF1_LO        550     /* the audio on both sides */
#define SCAN_REF1_HI        750
#define SCAN_REF2_LO        1250
#define SCAN_REF2_HI        1450
#define SCAN_BIN_STEP       25
#define SCAN_BINS_MAX       40

typedef enum { SCAN_OFF, SCAN_SETTLE, SCAN_MEASURE, SCAN_CONFIRM } scan_state_t;

static const uint16_t scan_wait_ms[] = { 500, 700, 1000, 1200, 1500 };
#define SCAN_WAITS ((int)(sizeof(scan_wait_ms) / sizeof(scan_wait_ms[0])))

static scan_state_t scan_state = SCAN_OFF;
static int scan_wait = 1;                   /* index in scan_wait_ms */
static bool scan_wait_loaded = false;
static uint32_t scan_since;                 /* lv_tick of the current state */
static int32_t scan_dial;                   /* the dial the scan set */
static int32_t scan_hz;                     /* channel being looked at */
static bool scan_resume = false;            /* the next Scan goes on from scan_hz */
static char scan_btn_label[16] = "Scan";
static char scan_wait_label[16] = "Wait\n0.7 s";

/* from the decoder (DSP thread) */
static volatile bool scan_synced = false;

/* tone measurement, filled by scan_audio() in the DSP thread */
static pthread_mutex_t scan_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool scan_measuring = false;
static float scan_coef[SCAN_BINS_MAX];
static int scan_bin_kind[SCAN_BINS_MAX];    /* 0 tone band, 1 beside it */
static int scan_nbins = 0;
static float scan_s1[SCAN_BINS_MAX], scan_s2[SCAN_BINS_MAX];
static int scan_block_n = 0;
static double scan_pow[2];                  /* tone band, beside it */
static int scan_cnt[2];
static int scan_blocks = 0;

static void scan_cb(button_data_t *d);
static void scan_hold_cb(button_data_t *d);
static void scan_wait_cb(button_data_t *d);

static button_data_t btn_scan = { .type = BTN_TEXT, .label = scan_btn_label, .press = scan_cb,
                               .hold = scan_hold_cb };
static button_data_t btn_scan_wait = { .type = BTN_TEXT, .label = scan_wait_label, .press = scan_wait_cb };

/* ---- settings ------------------------------------------------------------------- */

static void scan_wait_update(void)
{
    unsigned ms = scan_wait_ms[scan_wait];
    snprintf(scan_wait_label, sizeof(scan_wait_label), "Wait\n%u.%u s", ms / 1000, (ms % 1000) / 100);
    if (btn_scan_wait.disp_btn) buttons_refresh(&btn_scan_wait);
}

static void scan_wait_load(void)
{
    if (scan_wait_loaded) return;
    scan_wait_loaded = true;
    FILE *f = fopen(SCAN_FILE, "r");
    if (f) {
        unsigned ms;
        if (fscanf(f, "wait_ms %u", &ms) == 1)
            for (int i = 0; i < SCAN_WAITS; ++i)
                if (scan_wait_ms[i] == ms) scan_wait = i;
        fclose(f);
    }
    scan_wait_update();
}

static void scan_wait_save(void)
{
    if (!nav_ensure_dir()) return;
    FILE *f = fopen(SCAN_FILE, "w");
    if (!f) return;
    fprintf(f, "wait_ms %u\n", (unsigned)scan_wait_ms[scan_wait]);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

static void scan_wait_cb(button_data_t *d)
{
    (void)d;
    scan_wait = (scan_wait + 1) % SCAN_WAITS;
    scan_wait_update();
    scan_wait_save();
    msg_update_text_fmt("Scan: wait %u ms after each frequency change", (unsigned)scan_wait_ms[scan_wait]);
}

/* ---- the tones (DSP thread) ------------------------------------------------------ */

static void scan_bins_init(void)
{
    if (scan_nbins) return;
    for (int f = SCAN_REF1_LO; f <= SCAN_REF2_HI && scan_nbins < SCAN_BINS_MAX; f += SCAN_BIN_STEP) {
        int kind;
        if (f >= SCAN_TONE_LO && f <= SCAN_TONE_HI) kind = 0;
        else if ((f >= SCAN_REF1_LO && f <= SCAN_REF1_HI) || (f >= SCAN_REF2_LO && f <= SCAN_REF2_HI)) kind = 1;
        else continue;
        scan_coef[scan_nbins] = 2.0f * cosf(2.0f * (float)M_PI * f / NAVTEX_SAMPLE_RATE);
        scan_bin_kind[scan_nbins] = kind;
        scan_nbins++;
    }
}

static void scan_measure_start(void)
{
    pthread_mutex_lock(&scan_mutex);
    memset(scan_s1, 0, sizeof(scan_s1));
    memset(scan_s2, 0, sizeof(scan_s2));
    scan_block_n = 0;
    scan_pow[0] = scan_pow[1] = 0.0;
    scan_cnt[0] = scan_cnt[1] = 0;
    scan_blocks = 0;
    scan_measuring = true;
    pthread_mutex_unlock(&scan_mutex);
}

/* tone band over the bands beside it, dB; false when too little audio came */
static bool scan_measure_end(float *ratio_db)
{
    bool ok;
    pthread_mutex_lock(&scan_mutex);
    scan_measuring = false;
    ok = scan_blocks >= 5 && scan_cnt[0] && scan_cnt[1];
    if (ok) {
        double t = scan_pow[0] / scan_cnt[0], r = scan_pow[1] / scan_cnt[1];
        *ratio_db = 10.0f * log10f((float)((t + 1e-20) / (r + 1e-20)));
    }
    pthread_mutex_unlock(&scan_mutex);
    return ok;
}

static void scan_audio(const float *samples, size_t n)
{
    if (!scan_measuring) return;            /* read without the lock: a hint */
    pthread_mutex_lock(&scan_mutex);
    if (scan_measuring) {
        for (size_t i = 0; i < n; ++i) {
            float x = samples[i];
            for (int b = 0; b < scan_nbins; ++b) {
                float s = x + scan_coef[b] * scan_s1[b] - scan_s2[b];
                scan_s2[b] = scan_s1[b];
                scan_s1[b] = s;
            }
            if (++scan_block_n == SCAN_BLOCK) {
                for (int b = 0; b < scan_nbins; ++b) {
                    float p = scan_s1[b] * scan_s1[b] + scan_s2[b] * scan_s2[b] -
                              scan_coef[b] * scan_s1[b] * scan_s2[b];
                    scan_pow[scan_bin_kind[b]] += p;
                    scan_cnt[scan_bin_kind[b]]++;
                    scan_s1[b] = scan_s2[b] = 0.0f;
                }
                scan_block_n = 0;
                scan_blocks++;
            }
        }
    }
    pthread_mutex_unlock(&scan_mutex);
}

/* the decoder's characters and events: a lock is what the scan looks for */
static void scan_q_cb(int c, int quality, void *user)
{
    if (quality == NAVTEX_Q_EVENT && c == NAVTEX_EV_SYNC_ON)
        scan_synced = true;
    nav_char_q_cb(c, quality, user);
}

/* ---- the scan (LVGL thread) ------------------------------------------------------ */

static void scan_set_button(void)
{
    snprintf(scan_btn_label, sizeof(scan_btn_label), "%s", scan_state == SCAN_OFF ? "Scan" : "Stop\nScan");
    if (btn_scan.disp_btn) buttons_refresh(&btn_scan);
}

static void scan_stop(const char *why)
{
    if (scan_state == SCAN_OFF) return;
    pthread_mutex_lock(&scan_mutex);
    scan_measuring = false;
    pthread_mutex_unlock(&scan_mutex);
    scan_state = SCAN_OFF;
    scan_set_button();
    if (why) msg_update_text_fmt("%s", why);
}

/* the next frequency of the list above hz, 0 if none; its first row */
static int32_t scan_next_hz(int32_t hz, int *row)
{
    int32_t best = 0;
    for (int i = 0; i < NF_N; ++i) {
        int32_t f = nf_freqs[i].hz;
        if (f > hz && (!best || f < best)) {
            best = f;
            *row = i;
        }
    }
    return best;
}

/* on to the next frequency; false at the end of the list */
static bool scan_step(void)
{
    int row = -1;
    int32_t hz = scan_next_hz(scan_hz, &row);
    if (!hz) return false;
    scan_hz = hz;
    nf_tune_row(row);                       /* USB-D, dial, its name on the screen */
    scan_dial = cparam_i_get(cfg.cur.fg_freq());
    scan_state = SCAN_SETTLE;
    scan_since = lv_tick_get();
    return true;
}

static void scan_start(void)
{
    if (test_mode) {
        msg_update_text_fmt("Scan: not during the test");
        return;
    }
    scan_wait_load();
    scan_bins_init();
    if (!scan_resume)
        scan_hz = 0;                        /* from the lowest frequency */
    if (!scan_step())
        return;
    scan_set_button();
}

static void scan_cb(button_data_t *d)
{
    (void)d;
    if (scan_state != SCAN_OFF) {
        scan_stop("Scan stopped");
        scan_resume = true;
        return;
    }
    if (nf_frame) nf_close();               /* started from the list: show the text */
    scan_start();
}

/* long press: a new scan from the lowest frequency, also during a scan */
static void scan_hold_cb(button_data_t *d)
{
    (void)d;
    scan_stop(NULL);
    scan_resume = false;
    if (nf_frame) nf_close();
    scan_start();
}

/* every UI tick */
static void scan_poll(void)
{
    if (scan_state == SCAN_OFF) return;

    /* a frequency change not made by the scan ends it */
    if (cparam_i_get(cfg.cur.fg_freq()) != scan_dial) {
        scan_stop("Scan stopped: frequency changed");
        scan_resume = true;
        return;
    }

    uint32_t t = lv_tick_elaps(scan_since);
    char k[16];
    nf_fmt_khz(k, sizeof(k), scan_hz);

    /* a lock at any time after the wait: found */
    if (scan_state != SCAN_SETTLE && scan_synced) {
        scan_stop(NULL);
        scan_resume = true;                 /* the next Scan goes on from here */
        msg_update_text_fmt("Scan: SITOR-B signal on %s kHz", k);
        return;
    }

    switch (scan_state) {
        case SCAN_SETTLE:
            if (t >= scan_wait_ms[scan_wait]) {
                scan_synced = false;        /* only a lock on this frequency counts */
                scan_measure_start();
                scan_state = SCAN_MEASURE;
                scan_since = lv_tick_get();
            }
            break;

        case SCAN_MEASURE:
            if (t >= SCAN_MEASURE_MS) {
                float db = -99.0f;
                bool ok = scan_measure_end(&db);
                fprintf(stderr, "NAVTEX SCAN: %s kHz tones %+.1f dB%s\n", k, db, ok ? "" : " (no audio)");
                if (ok && db >= SCAN_RATIO_DB) {
                    scan_state = SCAN_CONFIRM;
                    scan_since = lv_tick_get();
                    msg_update_text_fmt("Scan: %s kHz, a signal: waiting for the lock...", k);
                } else if (!scan_step()) {
                    scan_stop("Scan: end of the list, no SITOR-B signal found");
                    scan_resume = false;
                }
            }
            break;

        case SCAN_CONFIRM:
            if (t >= SCAN_CONFIRM_MS && !scan_step()) {
                scan_stop("Scan: end of the list, no SITOR-B signal found");
                scan_resume = false;
            }
            break;

        default:
            break;
    }
}

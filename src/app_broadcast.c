/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
/*
 * Xiegu X6100 - Broadcast scan (APP page 5), see app_broadcast.h.
 *
 * No window of its own: the main screen stays as it is (VFO, volume, MFK,
 * spectrum, waterfall), only the buttons are the application's:
 *
 *   page 1  Band   the list of the AM broadcast bands, 100 kHz - 27 MHz;
 *                  a band chosen: AM at its first channel, and Scan starts
 *          Mode   which channels Scan looks at: All (every channel of the
 *                  band: 9 kHz LW/MW, 5 kHz short waves), EiBi (those in
 *                  the schedule, broadcast.csv), On air (those the schedule
 *                  says are on the air now)
 *          Scan   the next station above the current frequency; stops there
 *                  (AM, on its carrier, what EiBi says about it) or at the
 *                  end of the band; pressed during a scan: stop
 *          Info   what EiBi says about the current frequency
 *   page 2  Wait, Level (temporary, until their best values are known), Close
 *
 * Scan looks at the spectrum, not at one channel at a time: the DSP gives
 * the power spectrum of the whole span around the dial (100 kHz at zoom
 * 1x).  The dial is moved so that the middle 80 % of the span covers the
 * next channels; after Wait, a few frames are averaged and each channel in
 * it is checked: a station is a peak at the channel (+/- 2 bins) at least
 * Level dB above the noise of the span (its median) and 3 dB above the
 * sidebands around it (a carrier, not the sideband of a strong neighbour).
 */

#include "app_broadcast.h"

#include "broadcast_db.h"
#include "buttons.h"
#include "cfg/cfg_api.h"
#include "dialog.h"
#include "dsp.h"
#include "keyboard.h"
#include "main_screen.h"
#include "msg.h"
#include "radio.h"
#include "styles.h"
#include "ui_list.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BC_FILE         "/mnt/broadcast_scan.txt"
#define BC_NFFT         1024
#define BC_FRAMES       4           /* frames averaged per span */
#define BC_USE          0.8f        /* part of the span looked at */
#define BC_PEAK_BINS    2           /* the carrier: +/- bins around the channel */
#define BC_SIDE_LO_HZ   1200        /* the sidebands: 1.2 to 2.5 kHz away */
#define BC_SIDE_HI_HZ   2500
#define BC_SIDE_DB      3.0f
#define BC_MAX_CH       1024
#define BC_FRAME_MS     3000        /* no frame for this long: on anyway */

typedef struct {
    const char *name;
    int32_t     lo, hi;             /* first and last channel, Hz */
    int32_t     step;               /* channel raster */
} bc_band_t;

static const bc_band_t bc_bands[] = {
    { "LW",     153000,   279000,   9000 },
    { "MW",     531000,   1602000,  9000 },
    { "120 m",  2300000,  2495000,  5000 },
    { "90 m",   3200000,  3400000,  5000 },
    { "75 m",   3900000,  4000000,  5000 },
    { "60 m",   4750000,  5060000,  5000 },
    { "49 m",   5850000,  6350000,  5000 },
    { "41 m",   7200000,  7500000,  5000 },
    { "31 m",   9400000,  9990000,  5000 },
    { "25 m",   11600000, 12100000, 5000 },
    { "22 m",   13570000, 13870000, 5000 },
    { "19 m",   15100000, 15800000, 5000 },
    { "16 m",   17480000, 17900000, 5000 },
    { "15 m",   18900000, 19020000, 5000 },
    { "13 m",   21450000, 21850000, 5000 },
    { "11 m",   25670000, 26100000, 5000 },
};
#define BC_BANDS ((int)(sizeof(bc_bands) / sizeof(bc_bands[0])))

static const char *bc_mode_names[] = { "All", "EiBi", "On air" };
static const uint16_t bc_wait_ms[] = { 300, 500, 700, 1000, 1500 };
static const uint8_t bc_level_db[] = { 6, 10, 15 };
#define BC_WAITS  ((int)(sizeof(bc_wait_ms) / sizeof(bc_wait_ms[0])))
#define BC_LEVELS ((int)(sizeof(bc_level_db) / sizeof(bc_level_db[0])))

typedef enum { BC_IDLE, BC_SETTLE, BC_COLLECT } bc_state_t;

static bool bc_on = false;
static int bc_band = -1;
static int bc_mode = 0;
static int bc_wait = 2;
static int bc_level = 1;
static bool bc_loaded = false;

static bc_state_t bc_state = BC_IDLE;
static lv_timer_t *bc_timer = NULL;
static uint32_t bc_since;
static int32_t bc_dial;             /* the dial the scan set */
static int32_t bc_ch[BC_MAX_CH];    /* the channels of this scan, from the lowest */
static int bc_nch = 0;
static int bc_next = 0;             /* the next channel to look at */
static bool bc_from_band_start = false;
static int32_t bc_lo_offset = 0;    /* centre of the spectrum - dial, measured */
static int bc_misses = 0;           /* spans that did not cover the next channel */

/* spectrum frames, filled in the DSP thread */
static uint32_t bc_sub = DSP_FRAME_SUB_INVALID;
static pthread_mutex_t bc_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool bc_collecting = false;
static float bc_acc[BC_NFFT];
static int bc_frames = 0;
static uint16_t bc_size = 0;
static uint32_t bc_base = 0, bc_width = 0;

static char bc_mode_label[24], bc_wait_label[16], bc_level_label[16], bc_scan_label[16] = "Scan";

static void bc_page2_cb(button_data_t *d);
static void bc_page1_cb(button_data_t *d);
static void bc_band_cb(button_data_t *d);
static void bc_mode_cb(button_data_t *d);
static void bc_scan_cb(button_data_t *d);
static void bc_info_cb(button_data_t *d);
static void bc_wait_cb(button_data_t *d);
static void bc_level_cb(button_data_t *d);
static void bc_close_cb(button_data_t *d);

static button_data_t btn_bc_p1    = { .type = BTN_TEXT, .label = "(BCAST 1:2)", .press = bc_page2_cb };
static button_data_t btn_bc_p2    = { .type = BTN_TEXT, .label = "(BCAST 2:2)", .press = bc_page1_cb };
static button_data_t btn_bc_band  = { .type = BTN_TEXT, .label = "Band", .press = bc_band_cb };
static button_data_t btn_bc_mode  = { .type = BTN_TEXT, .label = bc_mode_label, .press = bc_mode_cb };
static button_data_t btn_bc_scan  = { .type = BTN_TEXT, .label = bc_scan_label, .press = bc_scan_cb };
static button_data_t btn_bc_info  = { .type = BTN_TEXT, .label = "Info", .press = bc_info_cb };
static button_data_t btn_bc_wait  = { .type = BTN_TEXT, .label = bc_wait_label, .press = bc_wait_cb };
static button_data_t btn_bc_level = { .type = BTN_TEXT, .label = bc_level_label, .press = bc_level_cb };
static button_data_t btn_bc_close = { .type = BTN_TEXT, .label = "Close", .press = bc_close_cb };

static buttons_page_t bc_page_1 = { .items = { &btn_bc_p1, &btn_bc_band, &btn_bc_mode, &btn_bc_scan, &btn_bc_info } };
static buttons_page_t bc_page_2 = { .items = { &btn_bc_p2, &btn_bc_wait, &btn_bc_level, NULL, &btn_bc_close } };

/* ---- settings ---------------------------------------------------------------------- */

static void bc_labels(void)
{
    snprintf(bc_mode_label, sizeof(bc_mode_label), "Mode\n%s", bc_mode_names[bc_mode]);
    unsigned ms = bc_wait_ms[bc_wait];
    snprintf(bc_wait_label, sizeof(bc_wait_label), "Wait\n%u.%u s", ms / 1000, (ms % 1000) / 100);
    snprintf(bc_level_label, sizeof(bc_level_label), "Level\n%u dB", (unsigned)bc_level_db[bc_level]);
    snprintf(bc_scan_label, sizeof(bc_scan_label), "%s", bc_state == BC_IDLE ? "Scan" : "Stop\nScan");
    button_data_t *b[] = { &btn_bc_mode, &btn_bc_wait, &btn_bc_level, &btn_bc_scan };
    for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); ++i)
        if (b[i]->disp_btn) buttons_refresh(b[i]);
}

static void bc_load(void)
{
    if (bc_loaded) return;
    bc_loaded = true;
    FILE *f = fopen(BC_FILE, "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (sscanf(line, "band %d", &v) == 1 && v >= -1 && v < BC_BANDS) bc_band = v;
        else if (sscanf(line, "mode %d", &v) == 1 && v >= 0 && v < 3) bc_mode = v;
        else if (sscanf(line, "wait_ms %d", &v) == 1) {
            for (int i = 0; i < BC_WAITS; ++i) if (bc_wait_ms[i] == v) bc_wait = i;
        } else if (sscanf(line, "level_db %d", &v) == 1) {
            for (int i = 0; i < BC_LEVELS; ++i) if (bc_level_db[i] == v) bc_level = i;
        }
    }
    fclose(f);
}

static void bc_save(void)
{
    FILE *f = fopen(BC_FILE, "w");
    if (!f) return;
    fprintf(f, "band %d\nmode %d\nwait_ms %u\nlevel_db %u\n", bc_band, bc_mode,
            (unsigned)bc_wait_ms[bc_wait], (unsigned)bc_level_db[bc_level]);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

/* ---- spectrum (DSP thread) --------------------------------------------------------- */

static void bc_frame_cb(const dsp_frame_t *fr, void *user)
{
    (void)user;
    if (!bc_collecting || fr->tx || fr->size == 0 || fr->size > BC_NFFT) return;
    pthread_mutex_lock(&bc_mutex);
    if (bc_collecting) {
        if (bc_frames && (fr->base_freq != bc_base || fr->width_hz != bc_width || fr->size != bc_size))
            bc_frames = 0;                  /* moved: start again */
        if (bc_frames == 0) {
            memset(bc_acc, 0, sizeof(bc_acc));
            bc_base = fr->base_freq;
            bc_width = fr->width_hz;
            bc_size = fr->size;
        }
        for (uint16_t i = 0; i < fr->size; ++i)
            bc_acc[i] += fr->psd_db[i];
        bc_frames++;
    }
    pthread_mutex_unlock(&bc_mutex);
}

static void bc_collect(bool on)
{
    pthread_mutex_lock(&bc_mutex);
    bc_collecting = on;
    bc_frames = 0;
    pthread_mutex_unlock(&bc_mutex);
    if (bc_sub != DSP_FRAME_SUB_INVALID)
        dsp_frame_set_active(bc_sub, on);
}

/* ---- channels ---------------------------------------------------------------------- */

static void bc_build_channels(void)
{
    const bc_band_t *b = &bc_bands[bc_band];
    bc_nch = 0;
    if (bc_mode == 0) {
        for (int32_t f = b->lo; f <= b->hi && bc_nch < BC_MAX_CH; f += b->step)
            bc_ch[bc_nch++] = f;
    } else {
        bc_nch = (int)broadcast_db_list_freqs(b->lo, b->hi, bc_mode == 2, bc_ch, BC_MAX_CH);
    }
}

static int bc_cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float bc_bin_hz(void) { return (float)bc_width / bc_size; }

static int bc_bin(int32_t hz)
{
    double lo = (double)bc_base - bc_width / 2.0;
    return (int)floor((hz - lo) / bc_bin_hz());
}

/* the channel is in the part of the span looked at */
static bool bc_in_span(int32_t hz)
{
    return fabs((double)hz - bc_base) <= bc_width * BC_USE / 2.0;
}

static float bc_max(const float *a, int from, int to)
{
    float m = -1e9f;
    if (from < 0) from = 0;
    if (to >= bc_size) to = bc_size - 1;
    for (int i = from; i <= to; ++i) if (a[i] > m) m = a[i];
    return m;
}

/* a station on hz in the averaged spectrum a: its carrier over the noise */
static bool bc_station(const float *a, float noise, int32_t hz, float *over)
{
    int c = bc_bin(hz);
    float peak = bc_max(a, c - BC_PEAK_BINS, c + BC_PEAK_BINS);
    int s1 = (int)(BC_SIDE_LO_HZ / bc_bin_hz()), s2 = (int)(BC_SIDE_HI_HZ / bc_bin_hz());
    float side = fmaxf(bc_max(a, c - s2, c - s1), bc_max(a, c + s1, c + s2));
    *over = peak - noise;
    return peak - noise >= bc_level_db[bc_level] && peak - side >= BC_SIDE_DB;
}

/* ---- scan (LVGL thread) ------------------------------------------------------------ */

static void bc_show(int32_t hz)
{
    broadcast_match_t m[BROADCAST_MAX_MATCHES];
    size_t n = broadcast_db_find_matches(hz, 2500, m, BROADCAST_MAX_MATCHES);
    double khz = hz / 1000.0;
    if (n == 0) {
        msg_update_text_fmt("%.0f kHz: no station in the schedule now", khz);
        return;
    }
    msg_update_text_fmt("%.0f kHz  %s  %s  %02d%02d-%02d%02d%s", khz, m[0].station, m[0].language,
                        m[0].start_minute / 60, m[0].start_minute % 60,
                        m[0].stop_minute / 60, m[0].stop_minute % 60,
                        n > 1 ? "  (+ more)" : "");
}

static void bc_stop(const char *why)
{
    if (bc_state == BC_IDLE) return;
    bc_collect(false);
    bc_state = BC_IDLE;
    bc_labels();
    if (why) msg_update_text_fmt("%s", why);
}

static void bc_tune(int32_t hz)
{
    if (cparam_i_get(cfg.cur.mode()) != x6100_mode_am)
        cparam_i_set(cfg.cur.mode(), x6100_mode_am);
    cparam_i_set(cfg.cur.fg_freq(), hz);
    bc_dial = cparam_i_get(cfg.cur.fg_freq());
}

/* the dial for the span that starts with channel bc_next */
static void bc_move(void)
{
    int32_t width = FULL_BW_HZ / (param_i_get(cfg.mode.zoom()) > 0 ? param_i_get(cfg.mode.zoom()) : 1);
    int32_t half = (int32_t)(width * BC_USE / 2) - bc_bands[bc_band].step / 2;
    if (half < 0) half = 0;
    bc_tune(bc_ch[bc_next] + half - bc_lo_offset);
    bc_state = BC_SETTLE;
    bc_since = lv_tick_get();
}

static void bc_found(int32_t hz, float over)
{
    bc_stop(NULL);
    bc_tune(hz);
    fprintf(stderr, "BROADCAST SCAN: station on %ld Hz, %.1f dB over the noise\n", (long)hz, over);
    bc_show(hz);
}

static void bc_check_span(void)
{
    float a[BC_NFFT], tmp[BC_NFFT];
    int frames;

    pthread_mutex_lock(&bc_mutex);
    frames = bc_frames;
    if (frames)
        for (int i = 0; i < bc_size; ++i) a[i] = bc_acc[i] / frames;
    pthread_mutex_unlock(&bc_mutex);
    bc_collect(false);
    bc_lo_offset = (int32_t)bc_base - bc_dial;

    /* the noise: median of the part looked at */
    int lo = bc_bin(bc_base - (int32_t)(bc_width * BC_USE / 2));
    int hi = bc_bin(bc_base + (int32_t)(bc_width * BC_USE / 2));
    if (lo < 0) lo = 0;
    if (hi >= bc_size) hi = bc_size - 1;
    int n = 0;
    for (int i = lo; i <= hi; ++i) tmp[n++] = a[i];
    qsort(tmp, n, sizeof(float), bc_cmp_float);
    float noise = n ? tmp[n / 2] : 0.0f;

    int checked = 0;
    while (bc_next < bc_nch && bc_in_span(bc_ch[bc_next])) {
        float over;
        int32_t hz = bc_ch[bc_next++];
        checked++;
        if (bc_station(a, noise, hz, &over)) {
            bc_found(hz, over);
            return;
        }
    }
    fprintf(stderr, "BROADCAST SCAN: span %ld Hz +/- %ld, %d frames, %d channels, none\n",
            (long)bc_base, (long)(bc_width / 2), frames, checked);

    if (bc_next >= bc_nch) {
        bc_stop(NULL);
        msg_update_text_fmt("%s: end of the band", bc_bands[bc_band].name);
        return;
    }
    if (checked) {
        bc_misses = 0;
    } else if (++bc_misses >= 2) {          /* never stay on one channel */
        fprintf(stderr, "BROADCAST SCAN: %ld Hz never in the span, left out\n", (long)bc_ch[bc_next]);
        bc_next++;
        bc_misses = 0;
    }
    if (bc_next >= bc_nch) {
        bc_stop(NULL);
        msg_update_text_fmt("%s: end of the band", bc_bands[bc_band].name);
        return;
    }
    bc_move();
}

static void bc_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (bc_state == BC_IDLE) return;

    if (cparam_i_get(cfg.cur.fg_freq()) != bc_dial) {
        bc_stop("Scan stopped: frequency changed");
        return;
    }

    uint32_t el = lv_tick_elaps(bc_since);
    switch (bc_state) {
        case BC_SETTLE:
            if (el >= bc_wait_ms[bc_wait]) {
                bc_collect(true);
                bc_state = BC_COLLECT;
                bc_since = lv_tick_get();
            }
            break;

        case BC_COLLECT: {
            int frames;
            pthread_mutex_lock(&bc_mutex);
            frames = bc_frames;
            pthread_mutex_unlock(&bc_mutex);
            if (frames >= BC_FRAMES) {
                bc_check_span();
            } else if (el >= BC_FRAME_MS) {
                fprintf(stderr, "BROADCAST SCAN: no spectrum frames\n");
                bc_stop("Scan stopped: no spectrum");
            }
            break;
        }

        default:
            break;
    }
}

static void bc_scan_start(void)
{
    if (bc_band < 0) {
        msg_update_text_fmt("Choose a band first");
        return;
    }
    bc_build_channels();
    if (bc_nch == 0) {
        msg_update_text_fmt("%s: no channel to look at in mode %s", bc_bands[bc_band].name,
                            bc_mode_names[bc_mode]);
        return;
    }
    int32_t cur = cparam_i_get(cfg.cur.fg_freq());
    bc_next = 0;
    if (!bc_from_band_start)
        while (bc_next < bc_nch && bc_ch[bc_next] <= cur + 500) bc_next++;
    bc_from_band_start = false;
    bc_misses = 0;
    if (bc_next >= bc_nch) {
        msg_update_text_fmt("%s: end of the band", bc_bands[bc_band].name);
        return;
    }
    msg_update_text_fmt("Scan %s, %s: %d channels", bc_bands[bc_band].name, bc_mode_names[bc_mode],
                        bc_nch - bc_next);
    bc_move();
    bc_labels();
}

/* ---- buttons ----------------------------------------------------------------------- */

static void bc_page1_cb(button_data_t *d) { (void)d; buttons_load_page(&bc_page_1); }
static void bc_page2_cb(button_data_t *d) { (void)d; buttons_load_page(&bc_page_2); }

static void bc_mode_cb(button_data_t *d)
{
    (void)d;
    bc_mode = (bc_mode + 1) % 3;
    bc_labels();
    bc_save();
}

static void bc_scan_cb(button_data_t *d)
{
    (void)d;
    if (bc_state != BC_IDLE) bc_stop("Scan stopped");
    else bc_scan_start();
}

static void bc_info_cb(button_data_t *d)
{
    (void)d;
    bc_show(cparam_i_get(cfg.cur.fg_freq()));
}

static void bc_wait_cb(button_data_t *d)
{
    (void)d;
    bc_wait = (bc_wait + 1) % BC_WAITS;
    bc_labels();
    bc_save();
}

static void bc_level_cb(button_data_t *d)
{
    (void)d;
    bc_level = (bc_level + 1) % BC_LEVELS;
    bc_labels();
    bc_save();
}

static void bc_close_cb(button_data_t *d)
{
    (void)d;
    app_broadcast_stop();
    buttons_load_page_group(buttons_group_app);
}

/* ---- band list (a dialog) ------------------------------------------------------------ */

static lv_obj_t *bl_list = NULL;
static dialog_t bl_dialog;
static void bl_close_cb(button_data_t *d);
static void bl_choose_cb(button_data_t *d);
static button_data_t btn_bl_title  = { .type = BTN_TEXT, .label = "(Bands)", .press = NULL };
static button_data_t btn_bl_choose = { .type = BTN_TEXT, .label = "Choose", .press = bl_choose_cb };
static button_data_t btn_bl_close  = { .type = BTN_TEXT, .label = "Close\nList", .press = bl_close_cb };
static buttons_page_t bl_page = { .items = { &btn_bl_title, &btn_bl_choose, NULL, NULL, &btn_bl_close } };

static void bl_row_cb(void *user, int index, ui_list_row_t *row)
{
    (void)user;
    if (index < 0 || index >= BC_BANDS) return;
    const bc_band_t *b = &bc_bands[index];
    snprintf(row->cell[0], UI_LIST_CELL_LEN, "%s", b->name);
    snprintf(row->cell[1], UI_LIST_CELL_LEN, "%ld - %ld kHz", (long)(b->lo / 1000), (long)(b->hi / 1000));
    int32_t tmp[BC_MAX_CH];
    size_t now = broadcast_db_list_freqs(b->lo, b->hi, true, tmp, BC_MAX_CH);
    snprintf(row->cell[2], UI_LIST_CELL_LEN, "%zu on air (EiBi)", now);
    row->mark = index == bc_band;
}

static void bl_close_async(void *chosen)
{
    dialog_destruct();                      /* back to the Broadcast buttons */
    if (chosen) {
        const bc_band_t *b = &bc_bands[bc_band];
        bc_tune(b->lo);
        bc_from_band_start = true;          /* its first channel too */
        bc_scan_start();
    }
}

static void bl_activate_cb(void *user, int index)
{
    (void)user;
    if (index < 0 || index >= BC_BANDS) return;
    bc_band = index;
    bc_save();
    lv_async_call(bl_close_async, (void *)1);
}

static void bl_choose_cb(button_data_t *d)
{
    (void)d;
    if (bl_list) bl_activate_cb(NULL, ui_list_get_selected(bl_list));
}

static void bl_close_cb(button_data_t *d)
{
    (void)d;
    lv_async_call(bl_close_async, NULL);
}

static void bl_construct_cb(lv_obj_t *parent)
{
    bl_dialog.obj = dialog_init(parent);
    ui_list_cfg_t cfg = {
        .width = DIALOG_WIDTH - 20,
        .height = DIALOG_HEIGHT - 24,
        .col_count = 3,
        .cols = {
            { .x = 20,  .w = 120, .align = UI_LIST_ALIGN_LEFT },
            { .x = 160, .w = 260, .align = UI_LIST_ALIGN_LEFT, .muted = true },
            { .x = 440, .w = 280, .align = UI_LIST_ALIGN_LEFT, .muted = true },
        },
        .row_cb = bl_row_cb,
        .activate_cb = bl_activate_cb,
        .empty_text = "",
    };
    bl_list = ui_list_create(bl_dialog.obj, &cfg);
    lv_obj_center(bl_list);
    ui_list_set_count(bl_list, BC_BANDS);
    ui_list_select(bl_list, bc_band >= 0 ? bc_band : 0);
    lv_group_add_obj(keyboard_group, bl_list);
    lv_group_focus_obj(bl_list);
    lv_group_set_editing(keyboard_group, true);
}

static void bl_destruct_cb(void)
{
    if (bl_list && lv_obj_get_group(bl_list)) lv_group_remove_obj(bl_list);
    bl_list = NULL;                         /* deleted with the dialog */
}

static dialog_t bl_dialog = {
    .run = false,
    .construct_cb = bl_construct_cb,
    .destruct_cb = bl_destruct_cb,
    .btn_page = &bl_page,
};

static void bc_band_cb(button_data_t *d)
{
    (void)d;
    bc_stop(NULL);
    dialog_construct(&bl_dialog, lv_scr_act());
}

/* ---- the application ------------------------------------------------------------------ */

void app_broadcast_start(lv_obj_t *parent)
{
    bc_load();
    bc_on = true;
    bc_state = BC_IDLE;
    bc_labels();
    if (bc_sub == DSP_FRAME_SUB_INVALID) {
        const dsp_frame_cfg_t c = { .nfft = BC_NFFT, .chunks_per_frame = DSP_FRAME_DEFAULT_CHUNKS,
                                    .allow_vary_freq = false };
        bc_sub = dsp_frame_subscribe(&c, bc_frame_cb, NULL);
        if (bc_sub != DSP_FRAME_SUB_INVALID) dsp_frame_set_active(bc_sub, false);
    }
    if (!bc_timer) bc_timer = lv_timer_create(bc_timer_cb, 50, NULL);
    buttons_load_page(&bc_page_1);
    if (bc_band < 0) {
        dialog_construct(&bl_dialog, parent);   /* first the band */
    } else {
        msg_update_text_fmt("Broadcast %s, mode %s: Scan for the next station", bc_bands[bc_band].name,
                            bc_mode_names[bc_mode]);
    }
}

void app_broadcast_stop(void)
{
    if (!bc_on) return;
    bc_on = false;
    bc_stop(NULL);
    if (bl_dialog.run) dialog_destruct();
    if (bc_timer) {
        lv_timer_del(bc_timer);
        bc_timer = NULL;
    }
    if (bc_sub != DSP_FRAME_SUB_INVALID) {
        dsp_frame_unsubscribe(bc_sub);
        bc_sub = DSP_FRAME_SUB_INVALID;
    }
}

bool app_broadcast_is_on(void)
{
    return bc_on;
}

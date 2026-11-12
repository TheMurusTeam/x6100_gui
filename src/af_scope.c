/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 */
/*
 * Xiegu X6100 - AF scopes, see af_scope.h.
 *
 * Audio: dsp_audio_subscribe_float() at 24 kHz into a ring buffer (audio
 * thread).  Rendered at most every AF_FRAME_MS from af_scope_process() (main
 * loop) into img[], rotated as the RF spectrum is: logical x -> physical row
 * 799 - x, logical y -> physical column.  img[] goes onto the primary plane
 * after the waterfall in every frame the waterfall is drawn, so the views
 * stay opaque on it.  Their frames and texts (frequencies, TX, DNF AUTO) are
 * drawn by LVGL on the overlay plane above.
 *
 *   left  view: oscilloscope, 20 ms, triggered on a rising zero crossing
 *               (a steady trace for a tone or a voice), auto gain
 *   right view: spectrum of the passband of the RX filter (the TX filter
 *               while transmitting), ticks every 500 Hz, the manual notch
 *               as a band, averaged with Spec. beta
 *
 * In TX the transmitted audio reaches the GUI only through the monitor: with
 * Moni level 0, radio_scope_monitor() sets it to 1 and mutes the speaker for
 * the TX only, without changing the settings.
 */
#include "af_scope.h"

#include "globals.h"
#include "dsp.h"
#include "radio.h"
#include "panel.h"
#include "waterfall.h"
#include "broadcast_overlay.h"
#include "styles.h"
#include "util.h"
#include "common/time.h"
#include "cfg/cfg_api.h"
#include "pubsub_ids.h"

#include "lv_drivers/display/drm.h"

#include <liquid/liquid.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AF_RATE         24000
#define AF_RING         8192            /* samples kept, a power of 2 */
#define AF_NFFT         2048            /* 11.7 Hz per bin */
#define AF_SCOPE_MS     20
#define AF_SCOPE_N      (AF_RATE * AF_SCOPE_MS / 1000)
#define AF_FRAME_MS     50              /* 20 frames per second */
#define AF_VW           250             /* each view */
#define AF_H            AF_SCOPE_HEIGHT
#define AF_GAP          6               /* between the views */
#define AF_MARGIN       4               /* from the right edge of the screen */
#define AF_PAD          3               /* inside the frame */
#define AF_W            (2 * AF_VW + AF_GAP)                /* both views */
#define AF_X            (SCREEN_WIDTH - AF_MARGIN - AF_W)   /* logical x of the left view */
#define AF_Y            (SCREEN_HEIGHT - BTN_HEIGHT - PANEL_GAP_BOTTOM - AF_H)
#define AF_IW           (AF_VW - 2 * AF_PAD)               /* drawing width in a view */
#define AF_SX           (AF_VW + AF_GAP + AF_PAD)          /* first column of the spectrum */
#define AF_Y0           AF_PAD                             /* drawing rows AF_Y0..AF_Y1 */
#define AF_Y1           (AF_H - 1 - AF_PAD)
#define AF_IH           (AF_Y1 - AF_Y0 + 1)
#define AF_RADIUS       6
#define AF_DB_MIN       20.0f           /* dB shown at least, floor to top */
#define AF_LINE_W       2               /* the notch arrow: as the frames */
#define AF_NOTCH_TOP    100             /* its top, Hz: always this wide */

/* audio thread -> render */
static float           ring[AF_RING];
static uint32_t        ring_pos;        /* total samples written */
static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t        audio_sub = AUDIO_SUB_INVALID;

/* state */
static bool            dialog_open;
static bool            shown;
static uint32_t        img[AF_W * AF_H];   /* the views, rotated: row = AF_W - 1 - x */
static uint64_t        last_frame;
static bool            dirty = true;
static lv_obj_t       *label_obj;

/* spectrum */
static fftplan         fft;
static float complex  *fft_in, *fft_out;
static float           window[AF_NFFT];
static float           smooth[AF_NFFT / 2];
static bool            smooth_valid;
static float           scope_peak = 0.05f;
static float           floor_db, top_db;
static bool            floor_reset = true;

/* what the labels show, to redraw them only when it changes */
static int32_t         lab_from = -1, lab_to = -1;
static bool            lab_tx, lab_auto;
static bool            labels_dirty = true;

static lv_grad_dsc_t   grad_dsc;

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void audio_cb(size_t n, float *samples)
{
    pthread_mutex_lock(&ring_mutex);
    for (size_t i = 0; i < n; ++i)
        ring[(ring_pos + i) & (AF_RING - 1)] = samples[i];
    ring_pos += (uint32_t)n;
    pthread_mutex_unlock(&ring_mutex);
}

static void dialog_start_cb(void *s, lv_msg_t *m) { (void)s; (void)m; dialog_open = true; }
static void dialog_stop_cb(void *s, lv_msg_t *m)  { (void)s; (void)m; dialog_open = false; }

/* ---- passband ------------------------------------------------------------ */

/* audio frequencies shown, Hz */
static void passband(int32_t *from, int32_t *to, bool *tx)
{
    *tx = radio_get_state() == RADIO_TX;
    int32_t lo, hi;
    if (*tx) {
        lo = param_i_get(cfg.dsp.tx_filter_low());
        hi = param_i_get(cfg.dsp.tx_filter_high());
    } else {
        lo = subject_i_get((SubjectInt *)cfg.filter.low());
        hi = subject_i_get((SubjectInt *)cfg.filter.high());
        x6100_mode_t mode = subject_i_get((SubjectInt *)cfg.cur.mode());
        if (mode == x6100_mode_am || mode == x6100_mode_nfm)
            lo = 0;                     /* both sidebands: audio from 0 Hz */
    }
    if (lo < 0) lo = 0;
    if (hi > AF_RATE / 2) hi = AF_RATE / 2;
    if (hi < lo + 100) hi = lo + 100;
    *from = lo;
    *to = hi;
}

/* the manual notch, Hz; false when off, automatic or in AM / FM */
static bool notch(int32_t *from, int32_t *to, bool *is_auto)
{
    *is_auto = false;
    if (!param_i_get(cfg.dsp.dnf()))
        return false;
    x6100_mode_t mode = subject_i_get((SubjectInt *)cfg.cur.mode());
    if (mode == x6100_mode_am || mode == x6100_mode_nfm)
        return false;
    if (param_i_get(cfg.dsp.dnf_auto())) {
        *is_auto = true;                /* its frequency is not known here */
        return false;
    }
    int32_t c = param_i_get(cfg.dsp.dnf_center());
    int32_t w = param_i_get(cfg.dsp.dnf_width());
    *from = c - w / 2;              /* dnf_width: its bandwidth */
    *to = c + w - w / 2;
    return true;
}

/* ---- drawing on the rotated views ------------------------------------------ */

static inline uint32_t *px(int x, int y) { return &img[(AF_W - 1 - x) * AF_H + y]; }

static inline uint32_t blend(uint32_t dst, uint32_t src, uint8_t a)
{
    uint32_t rb = ((dst & 0x00FF00FFu) * (255u - a) + (src & 0x00FF00FFu) * a + 0x00800080u);
    uint32_t g  = ((dst & 0x0000FF00u) * (255u - a) + (src & 0x0000FF00u) * a + 0x00008000u);
    return ((rb >> 8) & 0x00FF00FFu) | ((g >> 8) & 0x0000FF00u);
}

/* logical vertical span y0..y1 at x: one physical row */
static void vspan(int x, int y0, int y1, uint32_t c, uint8_t a)
{
    if (x < 0 || x >= AF_W) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < AF_Y0) y0 = AF_Y0;
    if (y1 > AF_Y1) y1 = AF_Y1;
    uint32_t *p = px(x, 0);
    for (int y = y0; y <= y1; ++y)
        p[y] = a == 255 ? c : blend(p[y], c, a);
}

/* a line AF_LINE_W pixels wide, kept inside columns sx0..sx1 */
static void line(float x0, float y0, float x1, float y1, int sx0, int sx1, uint32_t c)
{
    int n = (int)ceilf(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)));
    if (n < 1) n = 1;
    for (int i = 0; i <= n; ++i) {
        float t = (float)i / n;
        int x = (int)lrintf(x0 + t * (x1 - x0)), y = (int)lrintf(y0 + t * (y1 - y0));
        for (int dx = 0; dx < AF_LINE_W; ++dx) {
            if (x + dx < sx0 || x + dx > sx1) continue;
            uint32_t *p = px(x + dx, 0);
            for (int dy = 0; dy < AF_LINE_W; ++dy)
                if (y + dy >= AF_Y0 && y + dy <= AF_Y1) p[y + dy] = c;
        }
    }
}

/* a trace: continuous between neighbouring columns */
static void trace(int x0, int n, const float *v, uint32_t c)
{
    int prev = (int)lrintf(v[0]);
    for (int i = 0; i < n; ++i) {
        int y = (int)lrintf(v[i]);
        vspan(x0 + i, i ? prev : y, y, c, 255);
        prev = y;
    }
}

static void render(void)
{
    memset(img, 0, sizeof(img));

    uint32_t c_line = style.colors.spectrum.line.full;
    uint32_t c_dim  = lv_color_hex(0x404040).full;

    /* copy the newest audio */
    static float a[AF_NFFT];
    pthread_mutex_lock(&ring_mutex);
    for (int i = 0; i < AF_NFFT; ++i)
        a[i] = ring[(ring_pos - AF_NFFT + (uint32_t)i) & (AF_RING - 1)];
    pthread_mutex_unlock(&ring_mutex);

    /* ---- oscilloscope -------------------------------------------------- */
    float pk = 0.0f;
    for (int i = AF_NFFT - 4 * AF_SCOPE_N; i < AF_NFFT; ++i)
        pk = fmaxf(pk, fabsf(a[i]));
    scope_peak = pk > scope_peak ? pk : scope_peak * 0.9f + pk * 0.1f;
    /* not below -34 dBFS: silence stays a flat line instead of its noise
     * being raised to full height */
    if (scope_peak < 0.02f) scope_peak = 0.02f;

    /* the last rising zero crossing that leaves a whole window after it */
    float thr = scope_peak * 0.1f;
    int start = AF_NFFT - AF_SCOPE_N;
    for (int i = AF_NFFT - AF_SCOPE_N; i > AF_NFFT - 3 * AF_SCOPE_N; --i) {
        if (a[i - 1] < 0.0f && a[i] >= 0.0f && a[i - 2] < -thr) { start = i; break; }
    }

    for (int x = 0; x < AF_IW; ++x) *px(AF_PAD + x, AF_H / 2) = c_dim;   /* zero line */

    static float yv[AF_IW];
    float gain = (AF_IH * 0.45f) / scope_peak;
    for (int x = 0; x < AF_IW; ++x) {
        float pos = start + (float)x * (AF_SCOPE_N - 1) / (AF_IW - 1);
        int i = (int)pos;
        float f = pos - i, s = a[i];
        if (i + 1 < AF_NFFT) s += f * (a[i + 1] - a[i]);
        yv[x] = AF_H / 2 - s * gain;
    }
    trace(AF_PAD, AF_IW, yv, c_line);

    /* ---- spectrum --------------------------------------------------------- */
    for (int i = 0; i < AF_NFFT; ++i)
        fft_in[i] = a[i] * window[i];
    fft_execute(fft);

    static float db[AF_NFFT / 2];
    for (int i = 0; i < AF_NFFT / 2; ++i) {
        float m = crealf(fft_out[i]) * crealf(fft_out[i]) + cimagf(fft_out[i]) * cimagf(fft_out[i]);
        db[i] = 10.0f * log10f(m + 1e-12f);
    }
    if (!smooth_valid) {
        memcpy(smooth, db, sizeof(smooth));
        smooth_valid = true;
    } else {
        lpf_block(smooth, db, param_i_get(cfg.ui.spectrum_beta()) * 0.01f, AF_NFFT / 2);
    }

    int32_t from, to;
    bool tx;
    passband(&from, &to, &tx);
    float hz_per_bin = (float)AF_RATE / AF_NFFT;
    int b0 = (int)(from / hz_per_bin), b1 = (int)(to / hz_per_bin) + 1;
    if (b1 > AF_NFFT / 2 - 1) b1 = AF_NFFT / 2 - 1;

    /* scale to what is in the passband: the bottom just under the noise (its
     * 20th percentile, so the filter edges do not pull it down), the top just
     * over the strongest peak; the peaks then use the whole height.  Top up
     * at once, down slowly; the bottom follows slowly. */
    static float sorted[AF_NFFT / 2];
    int nb = b1 - b0 + 1;
    memcpy(sorted, smooth + b0, sizeof(float) * (size_t)nb);
    qsort(sorted, (size_t)nb, sizeof(float), cmp_float);
    float noise = sorted[nb / 5], peak = sorted[nb - 1];
    if (floor_reset) {
        floor_db = noise;
        top_db = peak;
        floor_reset = false;
    } else {
        floor_db += 0.1f * (noise - floor_db);
        top_db = peak > top_db ? peak : top_db + 0.03f * (peak - top_db);
    }
    float lo_db = floor_db - 3.0f;
    float hi_db = fmaxf(top_db + 3.0f, lo_db + AF_DB_MIN);

    const int sx = AF_SX, sw = AF_IW;          /* spectrum columns */
    static float sy[AF_IW];
    for (int x = 0; x < sw; ++x) {
        float hz = from + (float)x * (to - from) / (sw - 1);
        float bf = hz / hz_per_bin;
        int bi = (int)bf;
        if (bi < 0) bi = 0;
        if (bi > AF_NFFT / 2 - 2) bi = AF_NFFT / 2 - 2;
        float f = bf - bi;
        float v = smooth[bi] + f * (smooth[bi + 1] - smooth[bi]);
        float t = (v - lo_db) / (hi_db - lo_db);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        sy[x] = AF_Y0 + (1.0f - t) * (AF_IH - 1);
    }

    int32_t n_from, n_to;
    bool n_auto = false;
    bool n_on = !tx && notch(&n_from, &n_to, &n_auto);

    /* ticks every 500 Hz */
    for (int32_t f = ((from + 499) / 500) * 500; f <= to; f += 500) {
        int x = (int)((float)(f - from) * (sw - 1) / (to - from));
        for (int y = AF_Y0; y <= AF_Y1; y += 3)
            *px(sx + x, y) = c_dim;
    }

    if (param_i_get(cfg.ui.spectrum_filled())) {
        lv_grad_t *g = lv_gradient_get(&grad_dsc, AF_IH, 1);
        for (int x = 0; x < sw; ++x) {
            int top = (int)sy[x];
            if (top < AF_Y0) top = AF_Y0;
            uint32_t *p = px(sx + x, 0);
            int n = AF_Y1 + 1 - top;
            for (int i = 0; i < n; ++i)
                p[top + i] = blend(p[top + i], g->map[n - 1 - i].full, 220);
        }
    }
    trace(sx, sw, sy, c_line);

    /* the notch: an arrow, outline only, from the top of the view (always
     * AF_NOTCH_TOP Hz wide) down to the bottom (as wide as the notch) */
    if (n_on) {
        float k = (float)(sw - 1) / (to - from);
        float xc = sx + ((n_from + n_to) / 2.0f - from) * k;
        float ht = AF_NOTCH_TOP / 2.0f * k, hb = (n_to - n_from) / 2.0f * k;
        float yt = AF_Y0, yb = AF_Y1 - (AF_LINE_W - 1);
        uint32_t c = lv_color_hex(0xA0A0A0).full;
        int s0 = sx, s1 = sx + sw - 1;
        line(xc - ht, yt, xc + ht, yt, s0, s1, c);
        line(xc - ht, yt, xc - hb, yb, s0, s1, c);
        line(xc + ht - (AF_LINE_W - 1), yt, xc + hb - (AF_LINE_W - 1), yb, s0, s1, c);
        line(xc - hb, yb, xc + hb, yb, s0, s1, c);
    }

    /* labels to redraw? */
    if (from != lab_from || to != lab_to || tx != lab_tx || n_auto != lab_auto) {
        lab_from = from; lab_to = to; lab_tx = tx; lab_auto = n_auto;
        labels_dirty = true;
    }
}

/* ---- labels (overlay plane) ------------------------------------------------ */

static void label_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_obj_t *o = lv_event_get_target(e);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &sony_14;
    d.color = lv_color_hex(0xAAAAAA);

    lv_coord_t x0 = o->coords.x1, y0 = o->coords.y1;

    /* the frames, over the edges of the views */
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_TRANSP;
    r.radius = AF_RADIUS;
    r.border_color = lv_color_hex(0xA0A0A0);
    r.border_width = 2;
    r.border_opa = LV_OPA_COVER;
    lv_area_t f1 = { x0, y0, x0 + AF_VW - 1, y0 + AF_H - 1 };
    lv_area_t f2 = { x0 + AF_VW + AF_GAP, y0, x0 + AF_W - 1, y0 + AF_H - 1 };
    lv_draw_rect(ctx, &r, &f1);
    lv_draw_rect(ctx, &r, &f2);

    const int sx = AF_SX, sw = AF_IW;
    int32_t from = lab_from, to = lab_to;
    if (to <= from) return;

    char t[16];
    for (int32_t f = ((from + 499) / 500) * 500; f <= to; f += 500) {
        int x = (int)((float)(f - from) * (sw - 1) / (to - from));
        if (x < 14 || x > sw - 14) continue;
        if (f % 1000) snprintf(t, sizeof(t), "%d.5", (int)(f / 1000));
        else snprintf(t, sizeof(t), "%dk", (int)(f / 1000));
        lv_area_t a = { x0 + sx + x - 14, y0 + AF_Y1 - 15, x0 + sx + x + 14, y0 + AF_Y1 };
        d.align = LV_TEXT_ALIGN_CENTER;
        lv_draw_label(ctx, &d, &a, t, NULL);
    }

    d.align = LV_TEXT_ALIGN_LEFT;
    if (lab_tx) {
        d.color = lv_color_hex(0xFF4040);
        lv_area_t a = { x0 + AF_PAD + 4, y0 + AF_Y0, x0 + AF_PAD + 60, y0 + AF_Y0 + 16 };
        lv_draw_label(ctx, &d, &a, "TX", NULL);
    }
    if (lab_auto) {
        d.color = lv_color_hex(0xAAAAAA);
        lv_area_t a = { x0 + sx + 4, y0 + AF_Y0, x0 + sx + 120, y0 + AF_Y0 + 16 };
        lv_draw_label(ctx, &d, &a, "DNF AUTO", NULL);
    }
}

/* ---- API --------------------------------------------------------------------- */

void af_scope_init(lv_obj_t *overlay)
{
    for (int i = 0; i < AF_NFFT; ++i)
        window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (AF_NFFT - 1));
    fft_in  = malloc(sizeof(float complex) * AF_NFFT);
    fft_out = malloc(sizeof(float complex) * AF_NFFT);
    fft = fft_create_plan(AF_NFFT, fft_in, fft_out, LIQUID_FFT_FORWARD, 0);

    grad_dsc.dir           = LV_GRAD_DIR_HOR;
    grad_dsc.stops_count   = 3;
    grad_dsc.stops[0].frac = 0;
    grad_dsc.stops[1].frac = 128;
    grad_dsc.stops[2].frac = 255;

    label_obj = lv_obj_create(overlay);
    lv_obj_remove_style_all(label_obj);
    lv_obj_set_style_bg_opa(label_obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(label_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(label_obj, AF_X, AF_Y);
    lv_obj_set_size(label_obj, AF_W, AF_H);
    lv_obj_add_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(label_obj, label_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    audio_sub = dsp_audio_subscribe_float(audio_cb, AF_RATE);
    dsp_audio_set_active(audio_sub, false);

    lv_msg_subscribe(MSG_DIALOG_START, dialog_start_cb, NULL);
    lv_msg_subscribe(MSG_DIALOG_STOP, dialog_stop_cb, NULL);
}

static bool wanted(void)
{
    return param_i_get(cfg.ui.af_scopes()) && !dialog_open && !panel_is_visible() &&
           !broadcast_overlay_visible();
}

static void set_shown(bool show)
{
    shown = show;
    dsp_audio_set_active(audio_sub, show);
    if (show) {
        lv_obj_clear_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        smooth_valid = false;
        floor_reset = true;
        labels_dirty = true;
        dirty = true;
    } else {
        lv_obj_add_flag(label_obj, LV_OBJ_FLAG_HIDDEN);
        waterfall_refresh();            /* the waterfall where the views were */
    }
}

/* one view: img[] rows r0.. (AF_VW of them) onto the primary plane */
static void push_view(int r0)
{
    drm_direct_ctx_t ctx;
    if (!drm_primary_begin_direct(&ctx, (uint32_t)AF_VW * AF_H))
        return;
    memcpy(ctx.buf, &img[r0 * AF_H], sizeof(uint32_t) * AF_VW * AF_H);
    /* row r of img[] is logical x = AF_X + AF_W - 1 - r: physical row 799 - x */
    lv_area_t area = { .x1 = AF_Y, .y1 = AF_MARGIN + r0,
                       .x2 = AF_Y + AF_H - 1, .y2 = AF_MARGIN + r0 + AF_VW - 1 };
    drm_primary_end_direct(&area);
}

bool af_scope_process(bool waterfall_drawn)
{
    bool w = wanted();
    if (w != shown)
        set_shown(w);
    /* the transmitted audio comes only through the monitor: on for the TX
     * while the views are shown (radio_scope_monitor) */
    radio_scope_monitor(shown && radio_get_state() == RADIO_TX);

    if (!shown)
        return false;

    uint64_t now = get_time();
    bool due = dirty || now - last_frame >= AF_FRAME_MS;
    if (!due && !waterfall_drawn)
        return false;               /* nothing drew over them, nothing new */

    if (due) {
        last_frame = now;
        dirty = false;
        grad_dsc.stops[0].color = style.colors.spectrum.low;
        grad_dsc.stops[1].color = style.colors.spectrum.mid;
        grad_dsc.stops[2].color = style.colors.spectrum.high;
        render();
    }

    /* after the waterfall of this frame: opaque on it */
    push_view(0);                   /* the spectrum, right */
    push_view(AF_VW + AF_GAP);      /* the oscilloscope, left */

    if (labels_dirty) {
        labels_dirty = false;
        lv_obj_invalidate(label_obj);
    }
    return true;
}

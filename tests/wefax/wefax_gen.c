/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 *
 * WEFAX test signal generator: a picture as a radio fax broadcast (120 lines
 * per minute, IOC 576), through a simulated HF channel, as the audio of the
 * radio (12000 Hz mono 16-bit WAV, the format of the radio's recordings).
 *
 *   wefax_gen IN.pgm OUT.wav [options]
 *
 *   IN.pgm         the picture, 8-bit PGM (P5), any size: each row is
 *                  stretched to the whole line, as Northwood sends it
 *   --snr DB       noise, signal to noise ratio in 2500 Hz (default: none)
 *   --paths N      propagation paths: 1 (default) or 2 or 3
 *   --delay MS     delay of the 2nd path (3rd: 1.7 times it), default 1.5
 *   --doppler HZ   speed of the fading of each path (default 0.2)
 *   --flat S       slow fading of the whole signal, period S seconds
 *   --depth DB     depth of the slow fading (default 15)
 *   --clicks N     static crashes per minute (default 0)
 *   --off HZ       tuning error (default 0)
 *   --ppm N        transmitter line period error (default 0)
 *   --seed N
 *
 * With 2 or 3 paths each one fades on its own (complex gains following a
 * slow random walk, Rayleigh-like): the two tones of the fax, 850 Hz apart,
 * then fade at different moments - the selective fading of HF.
 *
 * Broadcast: 5 s start tone (300 Hz black/white), 30 phasing lines (black
 * with the last 5% white), the picture, 5 s stop tone (450 Hz), 10 s black.
 * Tones: white 1925 Hz, black 1075 Hz, as the 1KO125 decoder expects
 * (src/wefax/decoder.c).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RATE        12000.0
#define LINE_S      0.5             /* 120 lines per minute */
#define F_WHITE     1925.0
#define F_BLACK     1075.0
#define MAX_DELAY   256             /* samples of path delay history */

static uint64_t rng = 88172645463325252ull;
static double urand(void)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return ((rng >> 11) + 0.5) / 9007199254740992.0;
}
static double grand(void) { return sqrt(-2 * log(urand())) * cos(2 * M_PI * urand()); }

/* the picture */
static int pw, ph;
static uint8_t *pic;

static int read_pgm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    char magic[3] = {0};
    int maxv;
    if (fscanf(f, "%2s %d %d %d", magic, &pw, &ph, &maxv) != 4 || strcmp(magic, "P5") || maxv != 255) {
        fprintf(stderr, "%s: not an 8-bit PGM (P5)\n", path);
        fclose(f);
        return -1;
    }
    fgetc(f);
    pic = malloc((size_t)pw * ph);
    if (fread(pic, 1, (size_t)pw * ph, f) != (size_t)pw * ph) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

/* gray (0 black .. 255 white) of the broadcast at time t (s) */
static double t_start, t_phase, t_pic, t_stop, t_end;
static double line_s;

static double gray_at(double t)
{
    if (t < t_start) {                              /* start tone: 300 Hz */
        return fmod(t * 300.0, 1.0) < 0.5 ? 0 : 255;
    }
    if (t < t_phase) {                              /* phasing */
        double x = fmod(t - t_start, line_s) / line_s;
        return x >= 0.95 ? 255 : 0;
    }
    if (t < t_pic) {                                /* the picture */
        double u = (t - t_phase) / line_s;
        int row = (int)u;
        if (row >= ph) row = ph - 1;
        double x = (u - row) * pw;
        int c = (int)x;
        double fr = x - c;
        int c2 = c + 1 < pw ? c + 1 : c;
        const uint8_t *r = pic + (size_t)row * pw;
        return r[c] * (1 - fr) + r[c2] * fr;
    }
    if (t < t_stop) {                               /* stop tone: 450 Hz */
        return fmod(t * 450.0, 1.0) < 0.5 ? 0 : 255;
    }
    return 0;                                       /* black */
}

/* a slowly varying complex gain (one propagation path) */
typedef struct { double re, im, vre, vim; } path_t;

static void path_step(path_t *p, double doppler)
{
    /* a damped random walk: Rayleigh-like amplitude, ~1/doppler s correlation */
    double a = 2 * M_PI * doppler / RATE;
    p->vre += (-p->re * a * a - p->vre * a * 1.4) + grand() * a * sqrt(a) * 1.2;
    p->vim += (-p->im * a * a - p->vim * a * 1.4) + grand() * a * sqrt(a) * 1.2;
    p->re += p->vre;
    p->im += p->vim;
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL;
    double snr = 1e9, delay_ms = 1.5, doppler = 0.2, flat = 0, depth = 15, clicks = 0, off = 0, ppm = 0;
    int paths = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--snr") && i + 1 < argc) snr = atof(argv[++i]);
        else if (!strcmp(argv[i], "--paths") && i + 1 < argc) paths = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc) delay_ms = atof(argv[++i]);
        else if (!strcmp(argv[i], "--doppler") && i + 1 < argc) doppler = atof(argv[++i]);
        else if (!strcmp(argv[i], "--flat") && i + 1 < argc) flat = atof(argv[++i]);
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc) depth = atof(argv[++i]);
        else if (!strcmp(argv[i], "--clicks") && i + 1 < argc) clicks = atof(argv[++i]);
        else if (!strcmp(argv[i], "--off") && i + 1 < argc) off = atof(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) rng ^= (uint64_t)atoll(argv[++i]) * 0x9E3779B97F4A7C15ull;
        else if (!in) in = argv[i];
        else if (!out) out = argv[i];
    }
    if (!in || !out || paths < 1 || paths > 3) {
        fprintf(stderr, "usage: wefax_gen IN.pgm OUT.wav [--snr DB] [--paths N] [--delay MS] [--doppler HZ]\n"
                        "       [--flat S] [--depth DB] [--clicks N] [--off HZ] [--ppm N] [--seed N]\n");
        return 2;
    }
    if (read_pgm(in)) return 1;
    for (int k = 0; k < 20; k++) urand();

    line_s = LINE_S * (1 + ppm * 1e-6);
    t_start = 5;
    t_phase = t_start + 30 * line_s;
    t_pic = t_phase + ph * line_s;
    t_stop = t_pic + 5;
    t_end = t_stop + 10;

    long n = (long)(t_end * RATE);
    int16_t *pcm = malloc((size_t)n * sizeof(int16_t));

    /* path delays in samples, gains starting at random */
    int dly[3] = {0, (int)lrint(delay_ms * 1e-3 * RATE), (int)lrint(1.7 * delay_ms * 1e-3 * RATE)};
    if (dly[2] >= MAX_DELAY) { fprintf(stderr, "delay too long\n"); return 2; }
    path_t pa[3];
    for (int k = 0; k < 3; k++) {
        double g = paths == 1 ? 1 : 0.7;
        double a = 2 * M_PI * urand();
        pa[k] = (path_t){g * cos(a), g * sin(a), 0, 0};
    }

    double hist_re[MAX_DELAY] = {0}, hist_im[MAX_DELAY] = {0};
    int hp = 0;
    double ph_acc = 0, amp = 0.25;
    double sigma = snr > 1e8 ? 0 : sqrt((amp * amp / 2) / pow(10, snr / 10) * (RATE / 2) / 2500);
    double fph = 2 * M_PI * urand();
    double click_left = 0, click_amp = 0;

    for (long i = 0; i < n; i++) {
        double t = i / RATE;
        double g = gray_at(t);
        double f = F_BLACK + (F_WHITE - F_BLACK) * g / 255.0 + off;
        ph_acc += 2 * M_PI * f / RATE;
        if (ph_acc > M_PI) ph_acc -= 2 * M_PI;

        /* analytic transmitted signal and its delayed copies */
        hist_re[hp] = cos(ph_acc);
        hist_im[hp] = sin(ph_acc);
        double re = 0, im = 0;
        for (int k = 0; k < paths; k++) {
            if (paths > 1) path_step(&pa[k], doppler);
            int j = (hp - dly[k] + MAX_DELAY) % MAX_DELAY;
            re += pa[k].re * hist_re[j] - pa[k].im * hist_im[j];
            im += pa[k].re * hist_im[j] + pa[k].im * hist_re[j];
        }
        hp = (hp + 1) % MAX_DELAY;
        (void)im;

        double v = amp * re;
        if (flat > 0) {
            double d = 0.5 + 0.5 * sin(2 * M_PI * t / flat + fph) * sin(2 * M_PI * t / (2.7 * flat));
            v *= pow(10, -depth * d * d / 20);
        }
        v += sigma * grand();

        /* static crashes: short bursts of strong noise */
        if (clicks > 0) {
            if (click_left <= 0 && urand() < clicks / 60.0 / RATE) {
                click_left = RATE * (0.002 + 0.02 * urand());
                click_amp = amp * (1 + 4 * urand());
            }
            if (click_left > 0) { v += click_amp * grand(); click_left--; }
        }
        pcm[i] = (int16_t)lrint(fmax(-32767, fmin(32767, v * 32767)));
    }

    FILE *fo = fopen(out, "wb");
    if (!fo) { perror(out); return 1; }
    uint32_t data = (uint32_t)n * 2, r = (uint32_t)RATE, v32;
    uint16_t v16;
    fwrite("RIFF", 1, 4, fo); v32 = 36 + data; fwrite(&v32, 4, 1, fo);
    fwrite("WAVEfmt ", 1, 8, fo); v32 = 16; fwrite(&v32, 4, 1, fo);
    v16 = 1; fwrite(&v16, 2, 1, fo); fwrite(&v16, 2, 1, fo);
    fwrite(&r, 4, 1, fo); v32 = r * 2; fwrite(&v32, 4, 1, fo);
    v16 = 2; fwrite(&v16, 2, 1, fo); v16 = 16; fwrite(&v16, 2, 1, fo);
    fwrite("data", 1, 4, fo); fwrite(&data, 4, 1, fo);
    fwrite(pcm, 2, (size_t)n, fo);
    fclose(fo);
    fprintf(stderr, "%s: %d x %d picture, %.0f s\n", out, pw, ph, t_end);
    return 0;
}

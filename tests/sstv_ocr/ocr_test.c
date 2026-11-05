/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 *
 * The SSTV callsign reading of the firmware (src/sstv/sstv_ocr.c) on
 * pictures, on a computer: the lines read and the suggested callsigns.
 *
 *   ocr_test sstv_ocr.bin cty.dat [--own CALL] [--threads N] PIC.ppm...
 *
 * Pictures as binary PPM (P6), 320 wide.  One line per picture:
 *   file<TAB>suggestions (space separated)<TAB>lines read (" | ")
 * The time and the multiply-adds go to standard error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sstv_ocr.h"

static uint8_t *read_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int maxv;
    if (fscanf(f, "P6 %d %d %d", w, h, &maxv) != 3 || maxv != 255) { fclose(f); return NULL; }
    fgetc(f);
    uint8_t *p = malloc((size_t)*w * *h * 3);
    if (p && fread(p, 3, (size_t)*w * *h, f) != (size_t)*w * *h) { free(p); p = NULL; }
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: ocr_test sstv_ocr.bin cty.dat [--own CALL] [--threads N] PIC.ppm...\n");
        return 1;
    }
    sstv_ocr_t *o = sstv_ocr_load(argv[1], argv[2]);
    if (!o) return 1;
    const char *own = "";
    int threads = 1;
    double total = 0, macs = 0;
    int n = 0;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--own") && i + 1 < argc) { own = argv[++i]; continue; }
        if (!strcmp(argv[i], "--threads") && i + 1 < argc) { threads = atoi(argv[++i]); continue; }
        int w, h;
        uint8_t *p = read_ppm(argv[i], &w, &h);
        if (!p) { fprintf(stderr, "%s: not a P6 picture\n", argv[i]); continue; }
        sstv_ocr_result_t r;
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        sstv_ocr_read(o, p, w, h, own, 3, threads, &r);
        clock_gettime(CLOCK_MONOTONIC, &b);
        total += (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
        macs += r.macs;
        n++;
        const char *base = strrchr(argv[i], '/');
        printf("%s\t", base ? base + 1 : argv[i]);
        for (int k = 0; k < r.n_sugg; ++k) printf("%s%s", k ? " " : "", r.sugg[k]);
        printf("\t");
        for (int k = 0; k < r.n_lines; ++k) printf("%s%s", k ? " | " : "", r.line[k].text);
        printf("\n");
        free(p);
    }
    if (n) fprintf(stderr, "%d pictures, %.3f s and %.2f G multiply-adds each\n", n, total / n, macs / n / 1e9);
    sstv_ocr_free(o);
    return 0;
}

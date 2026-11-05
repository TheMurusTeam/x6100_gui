/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
/*
 * SSTV callsign reading: the callsigns written on a received picture, to
 * suggest them for a reply.  Three steps, as sstv-callsign-ml (read.py):
 *
 *   1. a small convolutional network finds the lines of text (and where
 *      callsigns are) on the 320 x 256 picture;
 *   2. a second network reads each line (CTC, one character every 4 px);
 *   3. plain rules pick the callsigns among the words read (shape, country
 *      in cty.dat, not a locator / report / radio name...) and rank them.
 *
 * Plain C, no libraries; the same file runs in the firmware and in the
 * test program on a computer (tests/sstv_ocr).
 *
 * Model file: sstv_ocr.bin, written by export_radio.py of sstv-callsign-ml
 * (the networks with their batch norms folded in).
 */
#ifndef SSTV_OCR_H
#define SSTV_OCR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSTV_OCR_MAX_LINES  24
#define SSTV_OCR_LINE_LEN   112     /* characters of a line read (384 px / 4 + 1) */
#define SSTV_OCR_CALL_LEN   20
#define SSTV_OCR_MAX_SUGG   6

typedef struct sstv_ocr sstv_ocr_t;

typedef struct {
    int  n_lines;
    struct {
        int  box[4];                        /* x0, y0, x1, y1 in the picture */
        char text[SSTV_OCR_LINE_LEN];
    } line[SSTV_OCR_MAX_LINES];
    int  n_sugg;                            /* callsigns for a reply, the likeliest first */
    char sugg[SSTV_OCR_MAX_SUGG][SSTV_OCR_CALL_LEN];
    double macs;                            /* multiply-adds done */
} sstv_ocr_result_t;

/* Reads the model file and cty.dat; NULL on error (message on stderr) */
sstv_ocr_t *sstv_ocr_load(const char *model_path, const char *cty_path);
void        sstv_ocr_free(sstv_ocr_t *o);

/* Reads the picture (rgb, w x h, 3 bytes per pixel; 320 wide, up to 256
 * high, as received).  own: the operator's callsign, never suggested (also
 * when read with one character wrong); most: suggestions at most.
 * threads: 1..4.  Returns 0, <0 when out of memory. */
int sstv_ocr_read(sstv_ocr_t *o, const uint8_t *rgb, int w, int h, const char *own, int most,
                  int threads, sstv_ocr_result_t *res);

#ifdef __cplusplus
}
#endif

#endif

/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
/*
 * SSTV callsign reading, see sstv_ocr.h.  Step by step the same as
 * read.py / detector.py / reader.py / callparse.py of sstv-callsign-ml;
 * tests/sstv_ocr compares the two on real pictures.
 */
#include "sstv_ocr.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/* ---- model ------------------------------------------------------------------------ */

#define PIC_W       320
#define PIC_H       256
#define STRIDE      4               /* detector maps: one value every 4 x 4 px */
#define MAP_W       (PIC_W / STRIDE)
#define MAP_H       (PIC_H / STRIDE)
#define LH          32              /* reader: line height */
#define MAX_W       384             /* widest line */
#define STEP        4               /* reader: one output every 4 px */
#define MIN_CONF    0.4f            /* a callsign with a less sure character is left out */
#define N_LAYERS    25
#define DET_LAYERS  12

static const char ALPHABET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/.-:@'!?,&";

typedef struct {
    int co, ci, kh, kw;
    float *w;                       /* [co][ci][kh][kw] */
    float *b;
} layer_t;

typedef struct {
    char **v;
    int n, cap;
} strset_t;

struct sstv_ocr {
    int base, widths[5], classes;
    layer_t L[N_LAYERS];
    strset_t prefix, whole;         /* cty.dat */
};

static int rd_u32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *v = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

static int set_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int set_add(strset_t *s, const char *t, size_t n)
{
    if (s->n == s->cap) {
        int cap = s->cap ? s->cap * 2 : 1024;
        char **v = realloc(s->v, cap * sizeof(*v));
        if (!v) return -1;
        s->v = v;
        s->cap = cap;
    }
    char *c = malloc(n + 1);
    if (!c) return -1;
    memcpy(c, t, n);
    c[n] = '\0';
    s->v[s->n++] = c;
    return 0;
}

static int set_has(const strset_t *s, const char *t)
{
    return s->n && bsearch(&t, s->v, s->n, sizeof(char *), set_cmp) != NULL;
}

static void set_free(strset_t *s)
{
    for (int i = 0; i < s->n; ++i) free(s->v[i]);
    free(s->v);
    memset(s, 0, sizeof(*s));
}

/* the prefixes of cty.dat (as callparse.Cty): entries "name:...:...;" with
 * the prefixes after the 8th colon; "=CALL" a whole callsign */
static int load_cty(sstv_ocr_t *o, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "SSTV OCR: %s not found\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *t = malloc(sz + 1);
    if (!t || fread(t, 1, sz, f) != (size_t)sz) {
        free(t);
        fclose(f);
        return -1;
    }
    fclose(f);
    t[sz] = '\0';

    char *entry = t;
    while (entry && *entry) {
        char *end = strchr(entry, ';');
        if (end) *end = '\0';
        int colons = 0;
        char *p = entry;
        while (*p && colons < 8) if (*p++ == ':') colons++;
        if (colons == 8) {
            /* p: the prefixes, separated by commas */
            while (*p) {
                char *q = p;
                while (*q && *q != ',') q++;
                char *a = p, *z = q;
                while (a < z && isspace((unsigned char)*a)) a++;
                while (z > a && isspace((unsigned char)z[-1])) z--;
                for (char *c = a; c < z; ++c)
                    if (strchr("([<{~", *c)) { z = c; break; }
                while (z > a && isspace((unsigned char)z[-1])) z--;
                if (z > a) {
                    if (*a == '=') {
                        if (z - a > 1 && set_add(&o->whole, a + 1, z - a - 1)) break;
                    } else if (set_add(&o->prefix, a, z - a)) {
                        break;
                    }
                }
                p = *q ? q + 1 : q;
            }
        }
        entry = end ? end + 1 : NULL;
    }
    free(t);
    qsort(o->prefix.v, o->prefix.n, sizeof(char *), set_cmp);
    qsort(o->whole.v, o->whole.n, sizeof(char *), set_cmp);
    return o->prefix.n ? 0 : -1;
}

void sstv_ocr_free(sstv_ocr_t *o)
{
    if (!o) return;
    for (int i = 0; i < N_LAYERS; ++i) {
        free(o->L[i].w);
        free(o->L[i].b);
    }
    set_free(&o->prefix);
    set_free(&o->whole);
    free(o);
}

sstv_ocr_t *sstv_ocr_load(const char *model_path, const char *cty_path)
{
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        fprintf(stderr, "SSTV OCR: %s not found\n", model_path);
        return NULL;
    }
    sstv_ocr_t *o = calloc(1, sizeof(*o));
    char magic[8];
    uint32_t v, n;
    if (!o || fread(magic, 1, 8, f) != 8 || memcmp(magic, "SSTVOCR1", 8)) goto bad;
    if (rd_u32(f, &v)) goto bad;
    o->base = (int)v;
    for (int i = 0; i < 5; ++i) {
        if (rd_u32(f, &v)) goto bad;
        o->widths[i] = (int)v;
    }
    if (rd_u32(f, &v) || rd_u32(f, &n) || n != N_LAYERS) goto bad;
    o->classes = (int)v;
    if (o->classes != (int)sizeof(ALPHABET)) goto bad;     /* characters + "nothing" */
    for (int i = 0; i < N_LAYERS; ++i) {
        layer_t *L = &o->L[i];
        uint32_t d[4];
        for (int k = 0; k < 4; ++k) if (rd_u32(f, &d[k])) goto bad;
        L->co = d[0]; L->ci = d[1]; L->kh = d[2]; L->kw = d[3];
        size_t nw = (size_t)L->co * L->ci * L->kh * L->kw;
        if (nw == 0 || nw > 4000000) goto bad;
        L->w = malloc(nw * sizeof(float));
        L->b = malloc(L->co * sizeof(float));
        if (!L->w || !L->b) goto bad;
        if (fread(L->w, sizeof(float), nw, f) != nw || fread(L->b, sizeof(float), L->co, f) != (size_t)L->co)
            goto bad;
    }
    fclose(f);
    if (load_cty(o, cty_path)) {
        sstv_ocr_free(o);
        return NULL;
    }
    return o;
bad:
    fprintf(stderr, "SSTV OCR: %s is not a valid model\n", model_path);
    fclose(f);
    sstv_ocr_free(o);
    return NULL;
}

/* ---- convolutions ------------------------------------------------------------------ */

typedef struct {
    const layer_t *L;
    const float *in;
    float *out;
    int ih, iw, oh, ow, stride, ph, pw, dh, dw, relu;
    int co0, co1;
} conv_job_t;

static void conv_range(const conv_job_t *j)
{
    const layer_t *L = j->L;
    int ohw = j->oh * j->ow;
    for (int co = j->co0; co < j->co1; ++co) {
        float *o = j->out + (size_t)co * ohw;
        for (int i = 0; i < ohw; ++i) o[i] = L->b[co];
        for (int ci = 0; ci < L->ci; ++ci) {
            const float *plane = j->in + (size_t)ci * j->ih * j->iw;
            const float *wk = L->w + ((size_t)co * L->ci + ci) * L->kh * L->kw;
            for (int ky = 0; ky < L->kh; ++ky) {
                for (int kx = 0; kx < L->kw; ++kx) {
                    float wv = wk[ky * L->kw + kx];
                    int offx = kx * j->dw - j->pw;
                    /* ox with 0 <= ox * stride + offx < iw */
                    int x0 = offx >= 0 ? 0 : (-offx + j->stride - 1) / j->stride;
                    int x1 = (j->iw - 1 - offx) / j->stride + 1;
                    if (j->iw - 1 - offx < 0) x1 = 0;
                    if (x1 > j->ow) x1 = j->ow;
                    if (x0 >= x1) continue;
                    for (int oy = 0; oy < j->oh; ++oy) {
                        int iy = oy * j->stride - j->ph + ky * j->dh;
                        if (iy < 0 || iy >= j->ih) continue;
                        const float *row = plane + (size_t)iy * j->iw + offx;
                        float *orow = o + (size_t)oy * j->ow;
                        if (j->stride == 1) {
                            int ox = x0;
#ifdef __ARM_NEON
                            float32x4_t vw = vdupq_n_f32(wv);
                            for (; ox + 4 <= x1; ox += 4)
                                vst1q_f32(orow + ox, vmlaq_f32(vld1q_f32(orow + ox), vw, vld1q_f32(row + ox)));
#endif
                            for (; ox < x1; ++ox) orow[ox] += wv * row[ox];
                        } else {
                            for (int ox = x0; ox < x1; ++ox) orow[ox] += wv * row[ox * j->stride];
                        }
                    }
                }
            }
        }
        if (j->relu)
            for (int i = 0; i < ohw; ++i) if (o[i] < 0.0f) o[i] = 0.0f;
    }
}

static void *conv_thread(void *arg)
{
    conv_range(arg);
    return NULL;
}

/* out (co x oh x ow) = layer L on in (ci x ih x iw); returns the multiply-adds */
static double conv(const layer_t *L, const float *in, int ih, int iw, float *out, int *oh, int *ow,
                   int stride, int ph, int pw, int dh, int dw, int relu, int threads)
{
    conv_job_t j = { L, in, out, ih, iw, 0, 0, stride, ph, pw, dh, dw, relu, 0, L->co };
    j.oh = (ih + 2 * ph - dh * (L->kh - 1) - 1) / stride + 1;
    j.ow = (iw + 2 * pw - dw * (L->kw - 1) - 1) / stride + 1;
    *oh = j.oh;
    *ow = j.ow;
    if (threads > 4) threads = 4;
    if (threads > 1 && L->co >= 8 && (double)j.oh * j.ow * L->ci * L->co > 2e5) {
        pthread_t t[4];
        conv_job_t jj[4];
        int started[4] = { 0 };
        for (int k = 0; k < threads; ++k) {
            jj[k] = j;
            jj[k].co0 = L->co * k / threads;
            jj[k].co1 = L->co * (k + 1) / threads;
            if (k > 0) started[k] = pthread_create(&t[k], NULL, conv_thread, &jj[k]) == 0;
        }
        conv_range(&jj[0]);
        for (int k = 1; k < threads; ++k) {
            if (started[k]) pthread_join(t[k], NULL);
            else conv_range(&jj[k]);
        }
    } else {
        conv_range(&j);
    }
    return (double)j.oh * j.ow * L->ci * L->co * L->kh * L->kw;
}

static void maxpool(const float *in, int c, int h, int w, int ky, int kx, float *out)
{
    int oh = h / ky, ow = w / kx;
    for (int ch = 0; ch < c; ++ch)
        for (int y = 0; y < oh; ++y)
            for (int x = 0; x < ow; ++x) {
                float m = -1e30f;
                for (int a = 0; a < ky; ++a)
                    for (int b = 0; b < kx; ++b) {
                        float v = in[((size_t)ch * h + y * ky + a) * w + x * kx + b];
                        if (v > m) m = v;
                    }
                out[((size_t)ch * oh + y) * ow + x] = m;
            }
}

/* dst = [nearest x2 of a (ca x h/2 x w/2), b (cb x h x w)] */
static void up_cat(const float *a, int ca, const float *b, int cb, int h, int w, float *dst)
{
    for (int c = 0; c < ca; ++c)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                dst[((size_t)c * h + y) * w + x] = a[((size_t)c * (h / 2) + y / 2) * (w / 2) + x / 2];
    memcpy(dst + (size_t)ca * h * w, b, (size_t)cb * h * w * sizeof(float));
}

/* ---- 1. the detector: the lines of text ----------------------------------------- */

typedef struct { int x0, y0, x1, y1; } box_t;

static int find_lines(sstv_ocr_t *o, const uint8_t *pic, int threads, box_t *bx, int maxb, double *macs)
{
    int b = o->base;
    size_t big = (size_t)b * (PIC_H / 2) * (PIC_W / 2);
    float *x = malloc((size_t)3 * PIC_H * PIC_W * sizeof(float));
    float *t1 = malloc(big * sizeof(float)), *t2 = malloc(big * sizeof(float));
    float *x2 = malloc((size_t)2 * b * MAP_H * MAP_W * sizeof(float));
    float *x3 = malloc((size_t)4 * b * (MAP_H / 2) * (MAP_W / 2) * sizeof(float));
    float *cat = malloc((size_t)4 * b * MAP_H * MAP_W * sizeof(float));
    int n = -1;
    if (!x || !t1 || !t2 || !x2 || !x3 || !cat) goto out;

    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < PIC_H * PIC_W; ++i) x[c * PIC_H * PIC_W + i] = pic[i * 3 + c] / 255.0f;

    int h, w, h2, w2;
    const layer_t *L = o->L;
    *macs += conv(&L[0], x, PIC_H, PIC_W, t1, &h, &w, 2, 1, 1, 1, 1, 1, threads);   /* 1/2 */
    *macs += conv(&L[1], t1, h, w, t2, &h, &w, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&L[2], t2, h, w, t1, &h, &w, 2, 1, 1, 1, 1, 1, threads);          /* 1/4 */
    *macs += conv(&L[3], t1, h, w, x2, &h, &w, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&L[4], x2, h, w, t1, &h, &w, 2, 1, 1, 1, 1, 1, threads);          /* 1/8 */
    *macs += conv(&L[5], t1, h, w, x3, &h, &w, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&L[6], x3, h, w, t1, &h2, &w2, 2, 1, 1, 1, 1, 1, threads);        /* 1/16 */
    *macs += conv(&L[7], t1, h2, w2, t2, &h2, &w2, 1, 2, 2, 2, 2, 1, threads);
    *macs += conv(&L[8], t2, h2, w2, t1, &h2, &w2, 1, 4, 4, 4, 4, 1, threads);
    up_cat(t1, 4 * b, x3, 4 * b, h, w, cat);                                         /* u3, 1/8 */
    *macs += conv(&L[9], cat, h, w, t2, &h, &w, 1, 1, 1, 1, 1, 1, threads);
    up_cat(t2, 2 * b, x2, 2 * b, MAP_H, MAP_W, cat);                                 /* u2, 1/4 */
    *macs += conv(&L[10], cat, MAP_H, MAP_W, t1, &h, &w, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&L[11], t1, h, w, t2, &h, &w, 1, 0, 0, 1, 1, 0, threads);         /* 2 maps */

    /* text map, or callsign map where higher, over 0.5 (logit > 0); groups
     * of touching cells (at least 2) as boxes, a cell wider on each side */
    const float *call = t2, *text = t2 + MAP_H * MAP_W;
    static uint8_t on[MAP_H][MAP_W], seen[MAP_H][MAP_W];
    static int stack[MAP_H * MAP_W];
    for (int y = 0; y < MAP_H; ++y)
        for (int xx = 0; xx < MAP_W; ++xx) {
            float v = text[y * MAP_W + xx] > call[y * MAP_W + xx] ? text[y * MAP_W + xx] : call[y * MAP_W + xx];
            on[y][xx] = v > 0.0f;
            seen[y][xx] = 0;
        }
    n = 0;
    for (int y = 0; y < MAP_H; ++y)
        for (int xx = 0; xx < MAP_W; ++xx) {
            if (!on[y][xx] || seen[y][xx]) continue;
            int sp = 0, cells = 0, minx = xx, maxx = xx, miny = y, maxy = y;
            stack[sp++] = y * MAP_W + xx;
            seen[y][xx] = 1;
            while (sp) {
                int c = stack[--sp], cy = c / MAP_W, cx = c % MAP_W;
                cells++;
                if (cx < minx) minx = cx;
                if (cx > maxx) maxx = cx;
                if (cy < miny) miny = cy;
                if (cy > maxy) maxy = cy;
                const int ny[4] = { cy - 1, cy + 1, cy, cy }, nx[4] = { cx, cx, cx - 1, cx + 1 };
                for (int k = 0; k < 4; ++k)
                    if (ny[k] >= 0 && ny[k] < MAP_H && nx[k] >= 0 && nx[k] < MAP_W && on[ny[k]][nx[k]] &&
                        !seen[ny[k]][nx[k]]) {
                        seen[ny[k]][nx[k]] = 1;
                        stack[sp++] = ny[k] * MAP_W + nx[k];
                    }
            }
            if (cells < 2 || n >= maxb) continue;
            box_t nb = { (minx - 1) * STRIDE, miny * STRIDE, (maxx + 2) * STRIDE, (maxy + 1) * STRIDE };
            /* in order of y0, then x0 (stable, as Python's sort) */
            int k = n++;
            while (k > 0 && (bx[k - 1].y0 > nb.y0 || (bx[k - 1].y0 == nb.y0 && bx[k - 1].x0 > nb.x0))) {
                bx[k] = bx[k - 1];
                k--;
            }
            bx[k] = nb;
        }
out:
    free(x); free(t1); free(t2); free(x2); free(x3); free(cat);
    return n;
}

/* ---- 2. a line, cut and read --------------------------------------------------------- */

/* Pillow's bilinear resize of 8-bit pixels (antialiased when shrinking),
 * one axis: the same coefficients and fixed-point rounding */
#define PREC 22

static int *coeffs(int in_size, int out_size, int *ksize, int **bounds)
{
    double scale = (double)in_size / out_size, fs = scale > 1.0 ? scale : 1.0, support = fs;
    int k = (int)ceil(support) * 2 + 1;
    int *kk = malloc((size_t)out_size * k * sizeof(int));
    int *bd = malloc((size_t)out_size * 2 * sizeof(int));
    double *pre = malloc(k * sizeof(double));
    if (!kk || !bd || !pre) {
        free(kk); free(bd); free(pre);
        return NULL;
    }
    for (int xx = 0; xx < out_size; ++xx) {
        double center = (xx + 0.5) * scale, ww = 0.0, ss = 1.0 / fs;
        int xmin = (int)(center - support + 0.5), xmax;
        if (xmin < 0) xmin = 0;
        xmax = (int)(center + support + 0.5);
        if (xmax > in_size) xmax = in_size;
        xmax -= xmin;
        for (int x = 0; x < xmax; ++x) {
            double v = fabs((x + xmin - center + 0.5) * ss);
            pre[x] = v < 1.0 ? 1.0 - v : 0.0;
            ww += pre[x];
        }
        for (int x = 0; x < k; ++x) {
            double p = x < xmax && ww != 0.0 ? pre[x] / ww : 0.0;
            kk[xx * k + x] = p < 0 ? (int)(-0.5 + p * (1 << PREC)) : (int)(0.5 + p * (1 << PREC));
        }
        bd[xx * 2] = xmin;
        bd[xx * 2 + 1] = xmax;
    }
    free(pre);
    *ksize = k;
    *bounds = bd;
    return kk;
}

static uint8_t clip8(int v)
{
    v >>= PREC;
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

/* src (sw x sh RGB) to dst (dw x dh RGB): horizontal pass, then vertical */
static int resize_rgb(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    int kx, ky, *bx, *by;
    int *cx = coeffs(sw, dw, &kx, &bx), *cy = coeffs(sh, dh, &ky, &by);
    uint8_t *tmp = malloc((size_t)dw * sh * 3);
    int ok = cx && cy && tmp;
    if (ok) {
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < dw; ++x) {
                int s[3] = { 1 << (PREC - 1), 1 << (PREC - 1), 1 << (PREC - 1) };
                const int *k = cx + x * kx;
                for (int i = 0; i < bx[x * 2 + 1]; ++i) {
                    const uint8_t *p = src + ((size_t)y * sw + bx[x * 2] + i) * 3;
                    s[0] += p[0] * k[i]; s[1] += p[1] * k[i]; s[2] += p[2] * k[i];
                }
                uint8_t *q = tmp + ((size_t)y * dw + x) * 3;
                q[0] = clip8(s[0]); q[1] = clip8(s[1]); q[2] = clip8(s[2]);
            }
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                int s[3] = { 1 << (PREC - 1), 1 << (PREC - 1), 1 << (PREC - 1) };
                const int *k = cy + y * ky;
                for (int i = 0; i < by[y * 2 + 1]; ++i) {
                    const uint8_t *p = tmp + ((size_t)(by[y * 2] + i) * dw + x) * 3;
                    s[0] += p[0] * k[i]; s[1] += p[1] * k[i]; s[2] += p[2] * k[i];
                }
                uint8_t *q = dst + ((size_t)y * dw + x) * 3;
                q[0] = clip8(s[0]); q[1] = clip8(s[1]); q[2] = clip8(s[2]);
            }
    }
    free(cx); free(cy); free(bx); free(by); free(tmp);
    return ok ? 0 : -1;
}

typedef struct {
    char text[SSTV_OCR_LINE_LEN];
    float conf[SSTV_OCR_LINE_LEN];          /* each character: how sure */
    char alt[SSTV_OCR_LINE_LEN];            /* its second choice, 0: "nothing" */
    float altp[SSTV_OCR_LINE_LEN];
    int len;
} reading_t;

/* the line in box, with a margin of 10 % of its height, read */
static int read_line(sstv_ocr_t *o, const uint8_t *pic, box_t b, int threads, reading_t *r, double *macs)
{
    double h = b.y1 - b.y0 > 1 ? b.y1 - b.y0 : 1;
    double x0 = b.x0 - 0.1 * h, x1 = b.x1 + 0.1 * h, y0 = b.y0 - 0.1 * h, y1 = b.y1 + 0.1 * h;
    int L = (int)rint(x0), R = (int)rint(x1), T = (int)rint(y0), B = (int)rint(y1);
    int cw = R - L, ch = B - T;
    r->len = 0;
    r->text[0] = '\0';
    if (cw < 1 || ch < 1) return 0;
    int w = (int)rint((double)cw * LH / ch);
    if (w < STEP) w = STEP;
    w = (w + STEP - 1) / STEP * STEP;
    if (w > MAX_W) w = MAX_W;

    uint8_t *crop = calloc((size_t)cw * ch, 3), *line = malloc((size_t)w * LH * 3);
    float *a = malloc((size_t)96 * LH * w * sizeof(float)), *bb = malloc((size_t)96 * LH * w * sizeof(float));
    int ret = -1;
    if (!crop || !line || !a || !bb) goto out;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            int sx = L + x, sy = T + y;
            if (sx >= 0 && sx < PIC_W && sy >= 0 && sy < PIC_H)
                memcpy(crop + ((size_t)y * cw + x) * 3, pic + ((size_t)sy * PIC_W + sx) * 3, 3);
        }
    if (resize_rgb(crop, cw, ch, line, w, LH)) goto out;
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < LH * w; ++i) a[(size_t)c * LH * w + i] = line[i * 3 + c] / 255.0f;

    const layer_t *Ly = o->L + DET_LAYERS;
    int hh = LH, ww = w;
    *macs += conv(&Ly[0], a, hh, ww, bb, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    maxpool(bb, Ly[0].co, hh, ww, 2, 2, a); hh /= 2; ww /= 2;
    *macs += conv(&Ly[1], a, hh, ww, bb, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&Ly[2], bb, hh, ww, a, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    maxpool(a, Ly[2].co, hh, ww, 2, 2, bb); hh /= 2; ww /= 2;
    *macs += conv(&Ly[3], bb, hh, ww, a, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&Ly[4], a, hh, ww, bb, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    maxpool(bb, Ly[4].co, hh, ww, 2, 1, a); hh /= 2;
    *macs += conv(&Ly[5], a, hh, ww, bb, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    *macs += conv(&Ly[6], bb, hh, ww, a, &hh, &ww, 1, 1, 1, 1, 1, 1, threads);
    maxpool(a, Ly[6].co, hh, ww, 2, 1, bb); hh /= 2;
    *macs += conv(&Ly[7], bb, hh, ww, a, &hh, &ww, 1, 0, 0, 1, 1, 1, threads);   /* 1 x W/4 */
    int cs = Ly[7].co, T4 = ww;
    static const int dil[4] = { 1, 2, 4, 1 };
    for (int k = 0; k < 4; ++k) {              /* along the line, added to its input */
        *macs += conv(&Ly[8 + k], a, 1, T4, bb, &hh, &ww, 1, 0, dil[k], 1, dil[k], 1, threads);
        for (int i = 0; i < cs * T4; ++i) a[i] += bb[i];
    }
    *macs += conv(&Ly[12], a, 1, T4, bb, &hh, &ww, 1, 0, 0, 1, 1, 0, threads);   /* classes x T */

    /* best path: a character where the best class changes (not "nothing"),
     * how sure at its best step and the second choice there */
    int C = Ly[12].co, prev = 0;
    r->len = 0;
    for (int t = 0; t < T4; ++t) {
        float m = -1e30f, sum = 0.0f;
        for (int c = 0; c < C; ++c) if (bb[c * T4 + t] > m) m = bb[c * T4 + t];
        int k1 = 0, k2 = -1;
        float p1 = -1.0f, p2 = -1.0f;
        for (int c = 0; c < C; ++c) sum += expf(bb[c * T4 + t] - m);
        for (int c = 0; c < C; ++c) {
            float p = expf(bb[c * T4 + t] - m) / sum;
            if (p > p1) { p2 = p1; k2 = k1; p1 = p; k1 = c; }
            else if (p > p2) { p2 = p; k2 = c; }
        }
        if (k1 != 0 && k1 != prev && r->len < SSTV_OCR_LINE_LEN - 1) {
            r->text[r->len] = ALPHABET[k1 - 1];
            r->conf[r->len] = 0.0f;
            r->alt[r->len] = 0;
            r->altp[r->len] = 0.0f;
            r->len++;
        }
        if (k1 != 0 && r->len && p1 > r->conf[r->len - 1]) {
            r->conf[r->len - 1] = p1;
            r->alt[r->len - 1] = k2 <= 0 ? 0 : ALPHABET[k2 - 1];
            r->altp[r->len - 1] = p2;
        }
        prev = k1;
    }
    /* no spaces at the ends */
    int s = 0;
    while (s < r->len && r->text[s] == ' ') s++;
    int e = r->len;
    while (e > s && r->text[e - 1] == ' ') e--;
    memmove(r->text, r->text + s, e - s);
    memmove(r->conf, r->conf + s, (e - s) * sizeof(float));
    memmove(r->alt, r->alt + s, e - s);
    memmove(r->altp, r->altp + s, (e - s) * sizeof(float));
    r->len = e - s;
    r->text[r->len] = '\0';
    ret = 0;
out:
    free(crop); free(line); free(a); free(bb);
    return ret;
}

/* ---- 3. the callsigns (callparse.py) ------------------------------------------------- */

#define WMAX    40              /* words of a line */
#define WLEN    48

static int in_list(const char *w, const char *const *list)
{
    for (; *list; ++list) if (!strcmp(w, *list)) return 1;
    return 0;
}

static int all_alnum(const char *s, int n)
{
    for (int i = 0; i < n; ++i) if (!isupper((unsigned char)s[i]) && !isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static const char *const SUFFIXES[] = { "P", "M", "QRP", "MM", "AM", "A", "B",
                                        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", NULL };
static const char *const WORDS_NOT_CALLS[] = { "MP3", "MP4", "4K", "QSO", "QTH", NULL };
static const char *const SENDER_BEFORE[] = { "DE", "FROM", "CQ", "QRZ", "TNX", "TU", "BY", "OP", "<", NULL };
static const char *const TO_BEFORE[] = { "TO", "UR", "HELLO", "HI", "DEAR", "QSL", NULL };
static const char *const NOT_CALLS[] = { "ICOM", "YAESU", "KENWOOD", "XIEGU", "ELECRAFT", "ALINCO", "MMSSTV",
                                         "QSSTV", "JOTA", "JOTI", NULL };
static const char *const QSO_WORDS[] = { "DE", "CQ", "OP", "QTH", "TNX", "UR", "HI", "QSL", "RST", "RSV", "LOC",
                                         "QRA", "TO", "FROM", "DX", "SSTV", "THE", "AND", "IN", "MY", "GL", "TU",
                                         "PSE", "VIA", "BY", NULL };
static const char *const GLUED_AFTER[] = { "QTH", "FROM", "DE", "OP", "TNX", "RSV", "RST", "UR", "QSL", "LOC",
                                           "NAME", NULL };
static const char *const GLUED_BEFORE[] = { "CQCQDE", "CQSSTV", "CQCQ", "CQDE", "73DE", "TNXDE", "CQDX", "CQ",
                                            "DE", "HI", "73", NULL };

/* ^[A-Z0-9]{1,3}[0-9][A-Z0-9]*[A-Z]$ */
static int is_base(const char *p, int n)
{
    if (n < 3 || !all_alnum(p, n) || !isupper((unsigned char)p[n - 1])) return 0;
    for (int i = 1; i <= 3 && i < n - 1; ++i) if (isdigit((unsigned char)p[i])) return 1;
    return 0;
}

/* ^[A-R]{2}[0-9]{2}([A-X]{2}([0-9]{2})?)?$ */
static int is_locator(const char *b)
{
    int n = strlen(b);
    if (n != 4 && n != 6 && n != 8) return 0;
    if (b[0] < 'A' || b[0] > 'R' || b[1] < 'A' || b[1] > 'R' || !isdigit((unsigned char)b[2]) ||
        !isdigit((unsigned char)b[3]))
        return 0;
    if (n >= 6 && (b[4] < 'A' || b[4] > 'X' || b[5] < 'A' || b[5] > 'X')) return 0;
    if (n == 8 && (!isdigit((unsigned char)b[6]) || !isdigit((unsigned char)b[7]))) return 0;
    return 1;
}

/* the word as a callsign in out (the word without the punctuation at its
 * ends), 0 if it has not the shape of one */
static int shape(const char *word, char *out)
{
    static const char strip[] = ".,:;!?'\"()[]";
    const char *a = word, *z = word + strlen(word);
    while (a < z && strchr(strip, *a)) a++;
    while (z > a && strchr(strip, z[-1])) z--;
    int n = z - a;
    if (n == 0 || n > 16) return 0;
    char w[20];
    memcpy(w, a, n);
    w[n] = '\0';
    if (strchr(w, '@') || strchr(w, '.')) return 0;

    const char *part[4];
    int plen[4], np = 0;
    const char *s = w;
    for (;;) {
        const char *sl = strchr(s, '/');
        int l = sl ? sl - s : (int)strlen(s);
        if (np == 3 || l == 0) return 0;
        part[np] = s;
        plen[np++] = l;
        if (!sl) break;
        s = sl + 1;
    }
    int bi = -1, nb = 0;
    for (int i = 0; i < np; ++i)
        if (is_base(part[i], plen[i]) && plen[i] >= 3 && plen[i] <= 10) {
            nb++;
            bi = i;
        }
    if (nb != 1) return 0;
    for (int i = 0; i < np; ++i) {
        if (i == bi) continue;
        char p[12];
        if (plen[i] > 10) return 0;
        memcpy(p, part[i], plen[i]);
        p[plen[i]] = '\0';
        int first = plen[i] == plen[0] && !memcmp(part[i], part[0], plen[i]);
        if (in_list(p, SUFFIXES) || (plen[i] <= 3 && all_alnum(p, plen[i]) && first)) continue;
        return 0;
    }
    char b[12];
    int bn = plen[bi];
    memcpy(b, part[bi], bn);
    b[bn] = '\0';
    if (is_locator(b) || in_list(b, WORDS_NOT_CALLS)) return 0;
    /* [A-Z][0-9]{3,} anywhere, or two digits at the start: locator, time, zone... */
    for (int i = 0; i + 3 < bn; ++i)
        if (isupper((unsigned char)b[i]) && isdigit((unsigned char)b[i + 1]) && isdigit((unsigned char)b[i + 2]) &&
            isdigit((unsigned char)b[i + 3]))
            return 0;
    if (bn >= 2 && isdigit((unsigned char)b[0]) && isdigit((unsigned char)b[1])) return 0;
    int k = -1;
    for (int i = 0; i < bn; ++i) if (isdigit((unsigned char)b[i])) k = i;
    int letters = bn - k - 1;
    if (letters < 1 || letters > 4 || k < 1 || k > 3) return 0;
    int alldig = 1;
    for (int i = 0; i < k; ++i) if (!isdigit((unsigned char)b[i])) alldig = 0;
    if (alldig) return 0;
    strcpy(out, w);
    return 1;
}

static int has_country(const sstv_ocr_t *o, const char *call)
{
    if (set_has(&o->whole, call)) return 1;
    char c[24], *part[4];
    int np = 0;
    snprintf(c, sizeof(c), "%s", call);
    for (char *t = strtok(c, "/"); t && np < 4; t = strtok(NULL, "/")) part[np++] = t;
    if (!np) return 0;
    /* the longest part (the first one of the longest), but a short part
     * before "/" is the place where the station is (EA8/IU1ABC) */
    int bi = 0;
    for (int i = 1; i < np; ++i) if (strlen(part[i]) > strlen(part[bi])) bi = i;
    char *base = part[bi];
    if (np > 1 && strlen(part[0]) <= 3 && strcmp(part[0], base)) base = part[0];
    char p[24];
    for (int n = strlen(base); n > 0; --n) {
        memcpy(p, base, n);
        p[n] = '\0';
        if (set_has(&o->prefix, p)) return 1;
    }
    return 0;
}

/* ^([1-5][1-9][1-9]?|5[/X]9|5NN|73|73S|73'S|88)$ */
static int is_numeric_report(const char *w)
{
    int n = strlen(w);
    if ((n == 2 || n == 3) && w[0] >= '1' && w[0] <= '5' && w[1] >= '1' && w[1] <= '9' &&
        (n == 2 || (w[2] >= '1' && w[2] <= '9')))
        return 1;
    return !strcmp(w, "5/9") || !strcmp(w, "5X9") || !strcmp(w, "5NN") || !strcmp(w, "73") ||
           !strcmp(w, "73S") || !strcmp(w, "73'S") || !strcmp(w, "88");
}

static int is_report(const char *w)
{
    static const char *const more[] = { "UR", "RST", "RSV", "TNX", "QSO", "4QSO", "GL", "DX", NULL };
    return is_numeric_report(w) || in_list(w, more);
}

/* the word without . , ! at its ends */
static void strip_end(const char *w, char *out)
{
    const char *a = w, *z = w + strlen(w);
    while (a < z && strchr(".,!", *a)) a++;
    while (z > a && strchr(".,!", z[-1])) z--;
    memcpy(out, a, z - a);
    out[z - a] = '\0';
}

typedef struct { char w[WLEN]; float q; } word_t;

typedef struct {
    char call[SSTV_OCR_CALL_LEN];
    int role;                       /* 0 call, 1 from, 2 to */
    int line;
    float conf;
    int height;
    int nvar;
    char var[2][SSTV_OCR_CALL_LEN];
    float var_r[2];
} found_t;

#define FMAX    64

/* the words of a line with how sure the reader was of each, a callsign
 * written with a space inside put together (F4 NRG), QSO words glued to a
 * callsign taken apart (G9KVAQTH, CQSSTVAE2NTK) */
static int line_words(const reading_t *r, word_t *out)
{
    word_t raw[WMAX];
    int n = 0, k = 0;
    while (k <= r->len && n < WMAX) {
        int e = k;
        while (e < r->len && r->text[e] != ' ') e++;
        if (e > k) {
            int l = e - k < WLEN - 1 ? e - k : WLEN - 1;
            float q = r->conf[k];             /* its least sure character */
            for (int i = k + 1; i < e; ++i) if (r->conf[i] < q) q = r->conf[i];
            for (int i = 0; i < l; ++i) raw[n].w[i] = toupper((unsigned char)r->text[k + i]);
            raw[n].w[l] = '\0';
            raw[n].q = q;
            n++;
        }
        k = e + 1;
    }
    /* join "F4" "NRG" */
    word_t j[WMAX];
    int nj = 0;
    char tmp[WLEN * 2], sh[24];
    for (int i = 0; i < n; ++i) {
        const char *w = raw[i].w;
        int l = strlen(w);
        int pre = l >= 2 && l <= 4 && isdigit((unsigned char)w[l - 1]) && all_alnum(w, l);
        if (pre && i + 1 < n && !shape(w, sh)) {
            const char *nx = raw[i + 1].w;
            int ln = strlen(nx), letters = ln >= 1 && ln <= 4;
            for (int c = 0; c < ln; ++c) if (!isupper((unsigned char)nx[c])) letters = 0;
            int ok = l + ln < WLEN;
            if (ok) {
                memcpy(tmp, w, l);
                memcpy(tmp + l, nx, ln + 1);
            }
            if (ok && letters && !in_list(nx, QSO_WORDS) && shape(tmp, sh)) {
                strcpy(j[nj].w, tmp);
                j[nj].q = raw[i].q < raw[i + 1].q ? raw[i].q : raw[i + 1].q;
                nj++;
                i++;
                continue;
            }
        }
        j[nj++] = raw[i];
    }
    /* split glued words */
    int no = 0;
    for (int i = 0; i < nj && no < WMAX; ++i) {
        const char *w = j[i].w;
        int l = strlen(w), done = 0;
        if (!shape(w, sh)) {
            for (int g = 0; GLUED_AFTER[g] && !done; ++g) {
                int gl = strlen(GLUED_AFTER[g]);
                if (l > gl && !strcmp(w + l - gl, GLUED_AFTER[g])) {
                    memcpy(tmp, w, l - gl);
                    tmp[l - gl] = '\0';
                    if (shape(tmp, sh) && no + 1 < WMAX) {
                        strcpy(out[no].w, tmp); out[no++].q = j[i].q;
                        strcpy(out[no].w, GLUED_AFTER[g]); out[no++].q = j[i].q;
                        done = 1;
                    }
                }
            }
            for (int g = 0; GLUED_BEFORE[g] && !done; ++g) {
                int gl = strlen(GLUED_BEFORE[g]);
                if (l >= gl && !strncmp(w, GLUED_BEFORE[g], gl) && shape(w + gl, sh) && no + 1 < WMAX) {
                    strcpy(out[no].w, GLUED_BEFORE[g]); out[no++].q = j[i].q;
                    strcpy(out[no].w, w + gl); out[no++].q = j[i].q;
                    done = 1;
                }
            }
        }
        if (!done) out[no++] = j[i];
    }
    return no;
}

/* the callsign with one of its 2 least sure characters (under 0.9) changed
 * to the second choice of the reader (or left out): valid ones only */
static void variants(const sstv_ocr_t *o, const reading_t *r, found_t *f)
{
    f->nvar = 0;
    char up[SSTV_OCR_LINE_LEN];
    for (int i = 0; i <= r->len; ++i) up[i] = toupper((unsigned char)r->text[i]);
    const char *at = strstr(up, f->call);
    if (!at) return;
    int k = at - up, n = strlen(f->call);
    int order[SSTV_OCR_CALL_LEN];
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int i = 1; i < n; ++i) {          /* stable, by how sure */
        int v = order[i], j = i;
        while (j > 0 && r->conf[k + order[j - 1]] > r->conf[k + v]) { order[j] = order[j - 1]; j--; }
        order[j] = v;
    }
    for (int m = 0; m < 2 && m < n; ++m) {
        int j = order[m];
        float p1 = r->conf[k + j], p2 = r->altp[k + j];
        char alt = r->alt[k + j];
        if (p1 >= 0.9f || alt == ' ' || p2 < 0.05f) continue;
        char v[SSTV_OCR_CALL_LEN], sh[24];
        int l = 0;
        for (int i = 0; i < n; ++i) {
            if (i == j) { if (alt) v[l++] = alt; }
            else v[l++] = f->call[i];
        }
        v[l] = '\0';
        if (!strcmp(v, f->call) || !shape(v, sh) || !has_country(o, v)) continue;
        float ratio = p2 / (p1 > 1e-3f ? p1 : 1e-3f);
        if (ratio > 1.0f) ratio = 1.0f;
        int pos = f->nvar;
        if (pos == 1 && ratio > f->var_r[0]) {
            strcpy(f->var[1], f->var[0]);
            f->var_r[1] = f->var_r[0];
            pos = 0;
        }
        strcpy(f->var[pos], v);
        f->var_r[pos] = ratio;
        f->nvar++;
    }
}

static int find_calls(const sstv_ocr_t *o, const reading_t *rd, const int *heights, int nl, found_t *out)
{
    static word_t rows[SSTV_OCR_MAX_LINES][WMAX];
    int nw[SSTV_OCR_MAX_LINES], report_line[SSTV_OCR_MAX_LINES];
    char s[WLEN], sh[24];
    for (int li = 0; li < nl; ++li) {
        nw[li] = line_words(&rd[li], rows[li]);
        int all = nw[li] > 0, any = 0;
        for (int i = 0; i < nw[li]; ++i) {
            strip_end(rows[li][i].w, s);
            if (!is_report(s)) all = 0;
            if (is_numeric_report(s)) any = 1;
        }
        report_line[li] = all && any;
    }
    int n = 0;
    for (int li = 0; li < nl; ++li) {
        word_t *ws = rows[li];
        int calls_here = 0;
        for (int i = 0; i < nw[li] && n < FMAX; ++i) {
            char c[24];
            if (!shape(ws[i].w, c)) continue;
            char o0[24];                     /* ICOM read as IC0M */
            int t = 0;
            for (; c[t]; ++t) o0[t] = c[t] == '0' ? 'O' : c[t];
            o0[t] = '\0';
            if (in_list(o0, NOT_CALLS) || ws[i].q < MIN_CONF || strlen(c) >= SSTV_OCR_CALL_LEN) continue;
            if (!has_country(o, c)) continue;
            calls_here++;
            const char *prev = i > 0 ? ws[i - 1].w : "";
            char nxt[WLEN] = "";
            if (i + 1 < nw[li]) strip_end(ws[i + 1].w, nxt);
            int role;
            if (in_list(prev, SENDER_BEFORE) ||
                (i >= 2 && (!strcmp(ws[i - 2].w, "CQ") || !strcmp(ws[i - 2].w, "73")) &&
                 (!strcmp(prev, "DE") || !strcmp(prev, "SSTV") || !strcmp(prev, "DX"))))
                role = 1;
            else if (!strcmp(nxt, "DE") || !strcmp(nxt, "FROM") || !strcmp(nxt, "<") || in_list(prev, TO_BEFORE) ||
                     !strcmp(prev, "73") || !strcmp(prev, "73S") || !strcmp(prev, "TNX"))
                role = 2;
            else if (is_numeric_report(nxt) || (!strcmp(nxt, "UR") && i + 2 < nw[li]))
                role = 2;
            else
                role = 0;
            found_t *f = &out[n++];
            memset(f, 0, sizeof(*f));
            strcpy(f->call, c);
            f->role = role;
            f->line = li;
            f->conf = ws[i].q;
            f->height = heights[li];
            variants(o, &rd[li], f);
        }
        if (li + 1 < nl && report_line[li + 1] && calls_here == 1)
            for (int k = 0; k < n; ++k)
                if (out[k].line == li && out[k].role == 0) out[k].role = 2;
    }
    (void)sh;
    return n;
}

static void station(const char *call, char *out)
{
    char c[24];
    snprintf(c, sizeof(c), "%s", call);
    const char *best = "";
    for (char *t = strtok(c, "/"); t; t = strtok(NULL, "/")) if (strlen(t) > strlen(best)) best = t;
    strcpy(out, best);
}

static int edit_distance(const char *a, const char *b)
{
    int la = strlen(a), lb = strlen(b), prev[32], cur[32];
    if (lb > 30) return 99;
    for (int j = 0; j <= lb; ++j) prev[j] = j;
    for (int i = 1; i <= la; ++i) {
        cur[0] = i;
        for (int j = 1; j <= lb; ++j) {
            int v = prev[j] + 1;
            if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
            if (prev[j - 1] + (a[i - 1] != b[j - 1]) < v) v = prev[j - 1] + (a[i - 1] != b[j - 1]);
            cur[j] = v;
        }
        memcpy(prev, cur, sizeof(int) * (lb + 1));
    }
    return prev[lb];
}

static int is_own(const char *call, const char *own)
{
    if (!own || !own[0]) return 0;
    char a[24], b[24], up[24];
    int i = 0;
    for (; own[i] && i < 23; ++i) up[i] = toupper((unsigned char)own[i]);
    up[i] = '\0';
    station(call, a);
    station(up, b);
    return !strcmp(a, b) || (strlen(a) >= 4 && edit_distance(a, b) <= 1);
}

/* the callsigns for a reply, the likeliest first (callparse.suggestions) */
static int suggest(const found_t *f, int n, const char *own, int most, char out[][SSTV_OCR_CALL_LEN])
{
    char name[FMAX * 3][SSTV_OCR_CALL_LEN];
    double score[FMAX * 3];
    int ns = 0;
    for (int i = 0; i < n; ++i) {
        double s = (f[i].height ? f[i].height : 1) * (double)f[i].conf * (f[i].role == 1 ? 1.3 : 1.0);
        int k;
        for (k = 0; k < ns && strcmp(name[k], f[i].call); ++k) ;
        if (k == ns) {
            strcpy(name[ns], f[i].call);
            score[ns++] = s;
        } else {
            score[k] = (score[k] > s ? score[k] : s) * 1.15;
        }
        for (int v = 0; v < f[i].nvar; ++v) {
            double sv = 0.9 * s * f[i].var_r[v];
            for (k = 0; k < ns && strcmp(name[k], f[i].var[v]); ++k) ;
            if (k == ns) {
                strcpy(name[ns], f[i].var[v]);
                score[ns++] = sv;
            } else if (sv > score[k]) {
                score[k] = sv;
            }
        }
    }
    /* highest first; equal scores in the order they came (stable) */
    int idx[FMAX * 3];
    for (int i = 0; i < ns; ++i) idx[i] = i;
    for (int i = 1; i < ns; ++i) {
        int v = idx[i], j = i;
        while (j > 0 && score[idx[j - 1]] < score[v]) { idx[j] = idx[j - 1]; j--; }
        idx[j] = v;
    }
    int m = 0;
    for (int i = 0; i < ns && m < most; ++i)
        if (!is_own(name[idx[i]], own)) strcpy(out[m++], name[idx[i]]);
    return m;
}

/* ---- the whole reading -------------------------------------------------------------------- */

int sstv_ocr_read(sstv_ocr_t *o, const uint8_t *rgb, int w, int h, const char *own, int most, int threads,
                  sstv_ocr_result_t *res)
{
    memset(res, 0, sizeof(*res));
    if (most > SSTV_OCR_MAX_SUGG) most = SSTV_OCR_MAX_SUGG;
    if (threads < 1) threads = 1;

    /* the picture as the detector wants it: 320 x 256, black below */
    uint8_t *pic = calloc((size_t)PIC_W * PIC_H, 3);
    if (!pic) return -1;
    if (w == PIC_W && h <= PIC_H) {
        memcpy(pic, rgb, (size_t)w * h * 3);
    } else if (resize_rgb(rgb, w, h, pic, PIC_W, h > PIC_H ? PIC_H : h)) {
        free(pic);
        return -1;
    }

    box_t bx[SSTV_OCR_MAX_LINES];
    int nl = find_lines(o, pic, threads, bx, SSTV_OCR_MAX_LINES, &res->macs);
    if (nl < 0) {
        free(pic);
        return -1;
    }
    static reading_t rd[SSTV_OCR_MAX_LINES];
    int heights[SSTV_OCR_MAX_LINES];
    for (int i = 0; i < nl; ++i) {
        if (read_line(o, pic, bx[i], threads, &rd[i], &res->macs)) {
            free(pic);
            return -1;
        }
        heights[i] = bx[i].y1 - bx[i].y0;
        res->line[i].box[0] = bx[i].x0;
        res->line[i].box[1] = bx[i].y0;
        res->line[i].box[2] = bx[i].x1;
        res->line[i].box[3] = bx[i].y1;
        memcpy(res->line[i].text, rd[i].text, SSTV_OCR_LINE_LEN);
        res->line[i].text[SSTV_OCR_LINE_LEN - 1] = '\0';
    }
    res->n_lines = nl;
    free(pic);

    static found_t found[FMAX];
    int nf = find_calls(o, rd, heights, nl, found);
    res->n_sugg = suggest(found, nf, own, most, res->sugg);
    return 0;
}

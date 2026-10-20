/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

/*********************
 *      INCLUDES
 *********************/

#include "lv_waterfall.h"

#include <stdlib.h>     /* 1KO125: qsort */
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_waterfall_class

/* 1KO125: the colours follow the noise floor of the audio, as the main
 * waterfall does with the RF spectrum.  The FT8 audio comes after the AGC,
 * which moves the floor at every 15-s slot (a strong station lowers the gain):
 * with fixed levels the background changed colour slot by slot.  The floor
 * is a low percentile of each line, smoothed; it sits FLOOR_BELOW_DB above
 * the bottom of a RANGE_DB scale, like the main waterfall (5 dB of 48). */
#define FLOOR_PERCENTILE  0.25f
#define FLOOR_SMOOTH      0.5f      /* per line (5 lines/s) */
#define FLOOR_BELOW_DB    5.0f
#define RANGE_DB          48.0f

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_waterfall_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_waterfall_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_waterfall_class  = {
    .constructor_cb = lv_waterfall_constructor,
    .destructor_cb = lv_waterfall_destructor,
    .base_class = &lv_img_class,
    .instance_size = sizeof(lv_waterfall_t),
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * lv_waterfall_create(lv_obj_t * parent) {
    LV_LOG_USER("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);

    return obj;
}

/*=====================
 * Setter functions
 *====================*/

void lv_waterfall_set_palette(lv_obj_t * obj, lv_color_t * palette, uint16_t cnt) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(palette);

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    waterfall->palette = lv_mem_realloc(waterfall->palette, cnt * sizeof(waterfall->palette[0]));
    waterfall->palette_cnt = cnt;

    memcpy(waterfall->palette, palette, cnt * sizeof(waterfall->palette[0]));
    for (size_t i = 0; i < cnt; i++) {
        waterfall->palette[i].ch.alpha = 0xff;
    }
}

void lv_waterfall_set_size(lv_obj_t * obj, lv_coord_t w, lv_coord_t h) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_set_size(obj, w, h);

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    waterfall->dsc = lv_img_buf_alloc(w, h, LV_IMG_CF_TRUE_COLOR);
    lv_color_t *pixels = (lv_color_t*)waterfall->dsc->data;
    for (size_t i = 0; i < waterfall->dsc->data_size / sizeof(lv_color_t); i++) {
        pixels[i].ch.alpha = 0xFF;
    }

    waterfall->line_len = waterfall->dsc->data_size / waterfall->dsc->header.h;
    waterfall->line_buf = lv_mem_realloc(waterfall->line_buf, waterfall->line_len);

    lv_img_set_src(obj, waterfall->dsc);
    lv_img_cache_invalidate_src(waterfall->dsc);
}

void lv_waterfall_clear_data(lv_obj_t * obj) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    memset((void*)waterfall->dsc->data, 0, waterfall->dsc->data_size);
    waterfall->floor_valid = false;     /* 1KO125 */
    lv_img_cache_invalidate_src(waterfall->dsc);
    lv_obj_invalidate(obj);
}

void lv_waterfall_add_data(lv_obj_t * obj, float * data, uint16_t cnt) {
    struct timespec ts = {0, 0};
    lv_waterfall_add_data_with_ts(obj, data, cnt, ts);
}

void lv_waterfall_add_data_with_ts(lv_obj_t * obj, float * data, uint16_t cnt, struct timespec ts) {
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_waterfall_t  *waterfall = (lv_waterfall_t *)obj;
    lv_img_dsc_t    *dsc = waterfall->dsc;

    /* Store timestamp for time-aligned processing (e.g. DNF). */
    waterfall->frame_ts = ts;

    if (!dsc || !waterfall->palette) {
        return;
    }

    uint32_t        line_len = waterfall->line_len;

    /* 1KO125: levels from the noise floor of this line */
    float           lo = waterfall->min, hi = waterfall->max;

    if (cnt > 0) {
        if (waterfall->floor_buf_cnt < cnt) {
            waterfall->floor_buf = lv_mem_realloc(waterfall->floor_buf, cnt * sizeof(float));
            waterfall->floor_buf_cnt = waterfall->floor_buf ? cnt : 0;
        }
        if (waterfall->floor_buf) {
            float    *b = waterfall->floor_buf;
            uint32_t k = (uint32_t)(FLOOR_PERCENTILE * (cnt - 1));

            memcpy(b, data, cnt * sizeof(float));
            qsort(b, cnt, sizeof(float), cmp_float);   /* a few hundred bins, 5 lines/s */
            if (waterfall->floor_valid)
                waterfall->floor_db += FLOOR_SMOOTH * (b[k] - waterfall->floor_db);
            else
                waterfall->floor_db = b[k];
            waterfall->floor_valid = true;
            lo = waterfall->floor_db - FLOOR_BELOW_DB;
            hi = lo + RANGE_DB;
        }
    }

    /* Scroll down */

    memmove((void*)(dsc->data + line_len), dsc->data, dsc->data_size - line_len);

    /* Paint */

    for (uint32_t x = 0; x < dsc->header.w; x++) {
        uint32_t    index = x * cnt / dsc->header.w;
        float       d = data[index];
        float       v = (d - lo) / (hi - lo);

        if (v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }

        uint8_t id = v * 255;

        lv_img_buf_set_px_color(dsc, x, 0, waterfall->palette[id]);
    }

    lv_img_cache_invalidate_src(dsc);
    lv_obj_invalidate(obj);
}

struct timespec lv_waterfall_get_frame_ts(lv_obj_t * obj) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_waterfall_t *waterfall = (lv_waterfall_t *)obj;
    return waterfall->frame_ts;
}

void lv_waterfall_set_min(lv_obj_t * obj, int16_t val) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;
    waterfall->min = val;
}
void lv_waterfall_set_max(lv_obj_t * obj, int16_t val) {
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;
    waterfall->max = val;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_waterfall_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    waterfall->palette = NULL;
    waterfall->palette_cnt = 0;
    waterfall->line_len = 0;
    waterfall->line_buf = NULL;
    waterfall->min = -40;
    waterfall->max = 0;
    waterfall->floor_buf = NULL;        /* 1KO125 */
    waterfall->floor_buf_cnt = 0;
    waterfall->floor_db = 0.0f;
    waterfall->floor_valid = false;

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_waterfall_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj) {
    LV_UNUSED(class_p);
    lv_waterfall_t * waterfall = (lv_waterfall_t *)obj;

    if (waterfall->palette) lv_mem_free(waterfall->palette);
    if (waterfall->line_buf) lv_mem_free(waterfall->line_buf);
    if (waterfall->floor_buf) lv_mem_free(waterfall->floor_buf);     /* 1KO125 */
}

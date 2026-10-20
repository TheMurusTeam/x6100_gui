/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - FT8 CB message table view
 *
 *  Copyright (c) 2026
 *
 *  1KO125: copy of R1CBU ft8_ui/table_view.* for the FT8 CB application,
 *  modified by Hany El Imam: names prefixed cbtv_ (so the FT8 application
 *  is left untouched) and the country of a CB callsign shown on the row.
 */

/*
 *  Wraps the LVGL lv_table that displays FT8 RX/TX messages. Owns the cell
 *  backing data in a fixed-size ring pool, so truncate/scroll/close never
 *  call malloc or free and there is no way to leak or double-free a cell.
 *
 *  Usage (UI thread only unless noted):
 *      cbtv_build(parent, x, y, w, h);
 *      cbtv_set_press_cb(on_press);
 *      cbtv_set_actions(&actions);
 *
 *      // from worker thread:
 *      cbtv_cell_t cd = ...;
 *      scheduler_put(cbtv_add_msg_cb, &cd, sizeof(cd));
 *
 *      // on dialog close:
 *      cbtv_destroy();
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

#include "../qso_log.h"
#include "../ft8/qso.h"   /* ftx_msg_meta_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CELL_RX_INFO = 0,
    CELL_START_QSO,
    CELL_RX_MSG,
    CELL_RX_CQ,
    CELL_RX_TO_ME,
    CELL_TX_MSG,
} cbtv_cell_type_t;

typedef struct {
    cbtv_cell_type_t         cell_type;
    int16_t                 dist;
    bool                    odd;
    ftx_msg_meta_t          meta;
    char                    text[64];
    char                    country[24];    /* 1KO125: CB division country, "" if none */
    qso_log_search_worked_t worked_type;
} cbtv_cell_t;

/* Optional dialog-level actions invoked from the table's key handler. */
typedef struct {
    void (*on_close)(void);           /* LV_KEY_ESC */
    void (*on_vol_change)(int32_t d); /* KEY_VOL_*_EDIT/SELECT */
} cbtv_actions_t;

/* Fired when a data-bearing row (RX_MSG / RX_CQ / RX_TO_ME) is pressed. */
typedef void (*cbtv_press_cb_t)(const cbtv_cell_t *cell);

void      cbtv_build(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
void      cbtv_destroy(void);
lv_obj_t *cbtv_obj(void);

void cbtv_set_press_cb(cbtv_press_cb_t cb);
void cbtv_set_actions(const cbtv_actions_t *actions);

/* Clear table + ring pool and show a single "Wait sync" placeholder row. */
void cbtv_reset(void);

/* Insert on the UI thread. Prefer the scheduler trampoline below from
 * worker threads. The text field is used as the visible row content. */
void cbtv_push_ui(const cbtv_cell_t *src);

/* Trampoline that matches scheduler_fn_t signature. Expects 'data' to point
 * to a cbtv_cell_t value copy that the scheduler will free after this
 * returns; cbtv_push_ui() takes its own snapshot into the ring pool. */
void cbtv_add_msg_cb(void *data);

#ifdef __cplusplus
}
#endif

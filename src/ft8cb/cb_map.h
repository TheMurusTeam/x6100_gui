/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (C) 2026 Hany El Imam
 *
 *  Xiegu X6100 LVGL GUI - FT8 CB: the stations heard on the map
 *
 *  The FT8 map (ft8_ui/map_view.c) for FT8 CB. FT8 CB's list has its own
 *  cells (table_view_cb.h), whose names clash with FT8's, so this file
 *  alone sees map_view.h and turns one into the other. CB calls send no
 *  locator: a station is put at the centre of its country, from its CB
 *  division (cb_divisions.txt). LVGL thread only.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"
#include "../ft8/qso.h"     /* ftx_msg_meta_t */

#ifdef __cplusplus
extern "C" {
#endif

/* A list cell, with FT8's cell type numbers (the same in both lists) */
typedef struct {
    int            cell_type;
    int16_t        dist;
    bool           odd;
    ftx_msg_meta_t meta;
    char           text[64];
    int            worked_type;     /* qso_log_search_worked_t */
} cbmap_cell_t;

typedef void (*cbmap_press_cb_t)(const cbmap_cell_t *cell);

/* The window opens: your CB call, and the map's title ("FT8-CB 11m") */
void cbmap_open(const char *my_call, const char *title);
void cbmap_set_my_call(const char *my_call);

void cbmap_add(const cbmap_cell_t *cell);
void cbmap_clear(void);

/* The map over `parent` or the list back; false if it couldn't be made */
bool cbmap_toggle(lv_obj_t *parent, lv_obj_t *table, lv_obj_t *waterfall,
                  cbmap_press_cb_t on_press, void (*on_close)(void), void (*on_vol_change)(int32_t d));
bool cbmap_shown(void);
void cbmap_focus(void);
const char *cbmap_next_mode(void);

/* PSK Reporter: who hears you */
void cbmap_heard_you(const char *call, const char *grid, int snr, int64_t when);
void cbmap_set_you_minutes(int minutes);
void cbmap_forget_you(void);

/* The window closes */
void cbmap_close(void);

#ifdef __cplusplus
}
#endif

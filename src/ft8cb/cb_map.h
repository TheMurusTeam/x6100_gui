/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (C) 2026 Hany El Imam
 *
 *  Xiegu X6100 LVGL GUI - FT8 CB: the stations heard on a map
 *
 *  FT8 CB's own map, started from FT8's (ft8_ui/map_view.c, Randal Warren
 *  VE7NHW) and kept apart from it on purpose: FT8 CB is its own app, and
 *  the CB differs where the map cares. CB calls send no locator and are not
 *  amateur prefixes (1S... is Italy, not Spratly), so a station is put at
 *  the centre of its CB division's country (cb_divisions.txt) and places
 *  and continents come from grids only. Who hears you comes from PSK
 *  Reporter's web query (cb_psk.c), not from its live feed, which leaves
 *  CB spots out. Only the drawing (map_scene, map_widget) is shared.
 *
 *  Show: Map (page 2) puts it over the waterfall and the list: stations
 *  heard in the last 15 minutes, green calling CQ, yellow calling you,
 *  grey worked before, a red path for your QSO, thin grey lines for the
 *  QSOs heard between others, blue lines for who heard you. Turning the
 *  MFK picks a station, pressing it calls that station as pressing its row
 *  in the list does. LVGL thread only.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl/lvgl.h"
#include "table_view_cb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window opens: your CB call, and the map's title ("FT8-CB 11m") */
void cbmap_open(const char *my_call, const char *title);
void cbmap_set_my_call(const char *my_call);

/* Every message (RX, TX, Start QSO), shown in the list or not */
void cbmap_add(const cbtv_cell_t *cell);
void cbmap_clear(void);

/* The map over `parent` or the list back; false if it couldn't be made */
bool cbmap_toggle(lv_obj_t *parent, lv_obj_t *table, lv_obj_t *waterfall,
                  cbtv_press_cb_t on_press, void (*on_close)(void), void (*on_vol_change)(int32_t d));
bool cbmap_shown(void);
void cbmap_focus(void);                 /* the MFK back to the map, after the keyboard */
const char *cbmap_next_mode(void);      /* Auto -> Close-in -> World */

/* PSK Reporter: `call` at `grid` heard you at UTC seconds `when` (a blue
 * line from you until it is older than the minutes set here) */
void cbmap_heard_you(const char *call, const char *grid, int snr, int64_t when);
void cbmap_set_you_minutes(int minutes);   /* 0: off, no lines or legend */
void cbmap_forget_you(void);

/* The window closes */
void cbmap_close(void);

#ifdef __cplusplus
}
#endif

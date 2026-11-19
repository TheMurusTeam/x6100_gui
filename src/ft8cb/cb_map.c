/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (C) 2026 Hany El Imam
 *
 *  Xiegu X6100 LVGL GUI - FT8 CB: the stations heard on the map
 */

#include <stdio.h>
#include "cb_map.h"
#include "cb_div.h"

#include "../ft8_ui/map_view.h"

#include <string.h>

static cbmap_press_cb_t press_cb;

static void to_cell(const cbmap_cell_t *in, cell_data_t *out) {
    memset(out, 0, sizeof(*out));
    out->cell_type   = (ft8_cell_type_t)in->cell_type;
    out->dist        = in->dist;
    out->odd         = in->odd;
    out->meta        = in->meta;
    out->worked_type = (qso_log_search_worked_t)in->worked_type;
    snprintf(out->text, sizeof(out->text), "%s", in->text);
}

static void from_cell(const cell_data_t *in, cbmap_cell_t *out) {
    memset(out, 0, sizeof(*out));
    out->cell_type   = (int)in->cell_type;
    out->dist        = in->dist;
    out->odd         = in->odd;
    out->meta        = in->meta;
    out->worked_type = (int)in->worked_type;
    snprintf(out->text, sizeof(out->text), "%s", in->text);
}

/* The call without the <> of a hashed one */
static const char *grid_of(const char *call) {
    char c[16];
    size_t n = 0;
    for (const char *p = call; *p && n + 1 < sizeof(c); p++)
        if (*p != '<' && *p != '>') c[n++] = *p;
    c[n] = '\0';
    return ft8cb_grid(c);
}

void cbmap_open(const char *my_call, const char *title) {
    map_view_set_grid_fallback(grid_of);
    map_view_set_my_call(my_call);
    map_view_set_title(title);
}

void cbmap_set_my_call(const char *my_call) {
    map_view_set_my_call(my_call);
}

void cbmap_add(const cbmap_cell_t *cell) {
    cell_data_t c;
    to_cell(cell, &c);
    map_view_add(&c);
}

void cbmap_clear(void) {
    map_view_clear();
}

static void press(const cell_data_t *cell) {
    if (!press_cb) return;
    cbmap_cell_t c;
    from_cell(cell, &c);
    press_cb(&c);
}

bool cbmap_toggle(lv_obj_t *parent, lv_obj_t *table, lv_obj_t *waterfall,
                  cbmap_press_cb_t on_press, void (*on_close)(void), void (*on_vol_change)(int32_t d)) {
    press_cb = on_press;
    const map_view_actions_t actions = {
        .waterfall     = waterfall,
        .on_press      = press,
        .on_close      = on_close,
        .on_vol_change = on_vol_change,
    };
    return map_view_show(parent, table, !map_view_shown(), &actions);
}

bool cbmap_shown(void) {
    return map_view_shown();
}

void cbmap_focus(void) {
    map_view_focus();
}

const char *cbmap_next_mode(void) {
    return map_view_next_mode();
}

void cbmap_heard_you(const char *call, const char *grid, int snr, int64_t when) {
    map_view_heard_you(call, grid, snr, when);
}

void cbmap_set_you_minutes(int minutes) {
    map_view_set_you_minutes(minutes);
}

void cbmap_forget_you(void) {
    map_view_forget_you();
}

void cbmap_close(void) {
    press_cb = NULL;
    map_view_destroy();
}

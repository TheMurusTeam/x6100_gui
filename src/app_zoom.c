/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
#include "app_zoom.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <aether_radio/x6100_control/control.h>

#include "cfg/cfg_api.h"

#define APP_ZOOM        4
#define IF_HALF_WIDTH   40000   /* as update_zoom_on_if_shift_change() in main_screen.c */

/*
 * The zoom is a parameter of the mode group (SSB, DIGI, CW, AM, FM): a
 * mode change loads the zoom stored for the new group.  It is changed here
 * with subject_i_set(), which shows it without saving it (Parameter::set()
 * would save it for the group).
 */
static bool             active = false;
static ObserverDelayed *zoom_obs = NULL;
static int32_t          saved_zoom;     /* the zoom to put back (the stored one) ... */
static int              saved_group;    /* ... if the mode group is still this one */

static int mode_group(int mode)
{
    switch (mode) {
    case x6100_mode_lsb:
    case x6100_mode_usb:        return x6100_mode_lsb;
    case x6100_mode_lsb_dig:
    case x6100_mode_usb_dig:    return x6100_mode_lsb_dig;
    case x6100_mode_cw:
    case x6100_mode_cwr:        return x6100_mode_cw;
    default:                    return mode;
    }
}

static int cur_group(void)
{
    return mode_group(cparam_i_get(cfg.cur.mode()));
}

/* 4x, or less with a large IF shift, as main_screen.c allows (otherwise
 * the two would set the zoom back and forth) */
static int32_t app_zoom(void)
{
    int32_t if_shift = abs(param_i_get(cfg.band.if_shift()));
    int32_t z = APP_ZOOM;

    while (z > 1 && (if_shift * z / IF_HALF_WIDTH)) {
        z >>= 1;
    }
    return z;
}

static void apply(void)
{
    int32_t z = app_zoom();

    if (param_i_get(cfg.mode.zoom()) != z) {
        subject_i_set((SubjectInt *)cfg.mode.zoom(), z);
    }
}

static void zoom_changed_cb(Subject *subj, void *user_data)
{
    (void)subj;
    (void)user_data;

    if (!active) {
        return;
    }

    int32_t z = param_i_get(cfg.mode.zoom());

    if (z == app_zoom()) {
        return;
    }

    /* not ours: the zoom stored for the mode group, just loaded by a mode
     * change or just set (and saved) by the user: that is the one to put
     * back when the app closes */
    saved_zoom = z;
    saved_group = cur_group();
    apply();
}

void app_zoom_begin(void)
{
    if (active) {
        return;
    }
    saved_zoom = param_i_get(cfg.mode.zoom());
    saved_group = cur_group();
    active = true;
    apply();
    zoom_obs = subject_subscribe_delayed((Subject *)cfg.mode.zoom(), zoom_changed_cb, NULL);
    fprintf(stderr, "APP ZOOM: %dx while the app is open (was %dx)\n",
            (int)param_i_get(cfg.mode.zoom()), (int)saved_zoom);
}

void app_zoom_end(void)
{
    if (!active) {
        return;
    }
    active = false;
    if (zoom_obs) {
        param_unsubscribe((Observer *)zoom_obs);
        zoom_obs = NULL;
    }
    /* back to the stored zoom.  A mode group whose stored zoom is already
     * the one shown loads without a change, so without a call above: then
     * there is nothing to put back. */
    if (cur_group() == saved_group) {
        subject_i_set((SubjectInt *)cfg.mode.zoom(), saved_zoom);
    }
    fprintf(stderr, "APP ZOOM: back to %dx\n", (int)param_i_get(cfg.mode.zoom()));
}

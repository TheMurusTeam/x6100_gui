/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 */
/*
 * AF scopes: an oscilloscope (left half) and an AF spectrum (right half) of
 * the receiver audio, or of the transmitted audio while transmitting, in a
 * 100 px strip above the F keys.  Settings > Display > AF scopes.  Hidden
 * while an application is open (Broadcast excepted): the waterfall then goes
 * down to the bottom of the screen as before.
 *
 * Drawn on the primary plane as the RF spectrum and the waterfall are, with
 * their colours, fill and averaging (Spec. beta).
 */
#ifndef AF_SCOPE_H
#define AF_SCOPE_H

#include "lvgl/lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_SCOPE_HEIGHT 100

/* Once, from main_screen(): overlay is the screen of the LVGL widgets;
 * layout_cb is called when the strip must be shown or hidden. */
void af_scope_init(lv_obj_t *overlay, void (*layout_cb)(void));

/* Whether the strip should be shown now (setting on, no application open) */
bool af_scope_wanted(void);

/* From the main screen layout: shown at logical y, or hidden */
void af_scope_set_geometry(bool shown, lv_coord_t y);

/* Whether the strip is on the screen now */
bool af_scope_shown(void);

/* From the main loop, with spectrum_process() and waterfall_process() */
bool af_scope_process(void);

#ifdef __cplusplus
}
#endif

#endif

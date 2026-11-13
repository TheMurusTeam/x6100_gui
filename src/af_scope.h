/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam
 */
/*
 * AF scopes: an oscilloscope and an AF spectrum of the receiver audio, or of
 * the transmitted audio while transmitting, in two 250 x 100 views floating
 * on the waterfall above the F keys, at its right edge.  Settings > Display >
 * AF scopes.  Hidden while an application or the RTTY / CW decoder panel
 * is shown.
 *
 * Opaque on the primary plane, as the RF spectrum and the waterfall are,
 * with their colours, fill and averaging (Spec. beta).
 */
#ifndef AF_SCOPE_H
#define AF_SCOPE_H

#include "lvgl/lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_SCOPE_HEIGHT 100

/* Once, from main_screen(): overlay is the screen of the LVGL widgets */
void af_scope_init(lv_obj_t *overlay);

/* From the main loop, right after waterfall_process() and with what it
 * returned: the views go over the waterfall drawn in this frame */
bool af_scope_process(bool waterfall_drawn);

#ifdef __cplusplus
}
#endif

#endif

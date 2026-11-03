/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
/*
 * Xiegu X6100 - Broadcast scan (APP page 5): AM broadcast bands, scan for
 * the next station, what the EiBi schedule says about it.  Not a dialog:
 * its buttons over the main screen, like RTTY (see app_broadcast.c).
 */
#pragma once

#include <stdbool.h>

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Broadcast buttons; the band list first when no band was chosen yet */
void app_broadcast_start(lv_obj_t *parent);
/* Any other application, or Close: scan stopped, buttons left as they are */
void app_broadcast_stop(void);
bool app_broadcast_is_on(void);
/* Microphone while Broadcast is on: V/M opens / closes the band list,
 * DOWN (dir > 0) is Scan, UP (dir < 0) scans down.  false when not on. */
bool app_broadcast_vm_toggle(void);
bool app_broadcast_mic_scan(int dir);

#ifdef __cplusplus
}
#endif

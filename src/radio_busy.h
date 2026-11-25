/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  1KO125: "radio busy" for CAT 7E 05: an app is open on the radio.
 *  Computed in the UI thread, read from any thread.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI thread, once, after the main screen exists */
void radio_busy_init(void);

/* any thread */
bool radio_busy_get(void);

#ifdef __cplusplus
}
#endif

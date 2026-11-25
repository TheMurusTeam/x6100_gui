/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  1KO125: "radio busy" for CAT 7E 05: something other than the main
 *  screen is open on the radio (an app, Settings, Broadcast, a text
 *  window).  Computed in the UI thread, read from any thread.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_BUSY_NAME_MAX 16

/* UI thread, once, after the main screen exists */
void radio_busy_init(void);

/* any thread: true when busy; name (at most RADIO_BUSY_NAME_MAX bytes and
 * a terminator) is what is open, "" when free.  name may be NULL. */
bool radio_busy_get(char *name, size_t size);

#ifdef __cplusplus
}
#endif

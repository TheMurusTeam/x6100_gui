/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  1KO125: "radio busy" for CAT 7E 05.
 *
 *  Why: an app open on the radio (FT8, WEFAX, SSTV...) sets mode, filter
 *  and frequency for itself and holds them, so a network client changing
 *  them fights it (WEFAX open: voice from the Mac went out in USB-D).  The
 *  user's rule: anything open other than the main screen, with its button
 *  pages, makes the radio busy, with no exceptions; the client pauses
 *  until the radio is free.  The firmware only says whether it is busy and
 *  by what.
 *
 *  Busy = a dialog running (every app, Settings, QTH, Callsign, the
 *  frequency entry, the Channels window opened on its own), Broadcast on,
 *  or a text window open (the RTTY keyboard of the main screen).  The
 *  Channels overlay opens only over a running app, so the app counts.
 *
 *  The state is computed every BUSY_PERIOD_MS by an LVGL timer, in the UI
 *  thread, from the modules' own queries, and kept under a mutex for the
 *  CAT and LAN threads: they never touch the dialogs or LVGL objects.
 */

#include "radio_busy.h"

#include <pthread.h>
#include <string.h>

#include "lvgl/lvgl.h"

#include "app_broadcast.h"
#include "dialog.h"
#include "textarea_window.h"

#define BUSY_PERIOD_MS 200

static pthread_mutex_t busy_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool busy = false;
static char busy_name[RADIO_BUSY_NAME_MAX + 1] = "";

static void busy_timer_cb(lv_timer_t *t)
{
    (void)t;
    const char *name = NULL;
    if (dialog_is_run()) {
        name = dialog_current_name();
        if (!name || !name[0]) name = "App";
    } else if (app_broadcast_is_on()) {
        name = "Broadcast";
    } else if (textarea_window_is_open()) {
        name = "Keyboard";
    }
    pthread_mutex_lock(&busy_mutex);
    busy = name != NULL;
    strncpy(busy_name, name ? name : "", RADIO_BUSY_NAME_MAX);
    busy_name[RADIO_BUSY_NAME_MAX] = '\0';
    pthread_mutex_unlock(&busy_mutex);
}

void radio_busy_init(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    lv_timer_create(busy_timer_cb, BUSY_PERIOD_MS, NULL);
}

bool radio_busy_get(char *name, size_t size)
{
    pthread_mutex_lock(&busy_mutex);
    bool b = busy;
    if (name && size > 0) {
        strncpy(name, busy_name, size - 1);
        name[size - 1] = '\0';
    }
    pthread_mutex_unlock(&busy_mutex);
    return b;
}

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
 *  user's rule: anything open other than the main screen makes the radio
 *  busy, and the client pauses until it is free.  The firmware only says
 *  whether it is busy.
 *
 *  Busy = a dialog running (every app of the APP menu, Settings, QTH,
 *  Callsign, the frequency entry...: what MSG_DIALOG_START / STOP already
 *  announce, as for the waterfall and the AF scopes), or the RTTY or the
 *  Broadcast app on: those two run on the main screen instead of a dialog.
 *  Windows opened inside an app (Channels over it, text windows) belong to
 *  it.  Free: the main screen with its button pages, the short messages
 *  and the GENE info pop-up.
 *
 *  Computed every BUSY_PERIOD_MS by an LVGL timer in the UI thread and kept
 *  in an atomic for the CAT and LAN threads, which never touch the dialogs
 *  or LVGL.
 */

#include "radio_busy.h"

#include <stdatomic.h>

#include "lvgl/lvgl.h"

#include "app_broadcast.h"
#include "pubsub_ids.h"
#include "rtty.h"

#define BUSY_PERIOD_MS 200

static bool dialog_on = false;          /* UI thread */
static atomic_bool busy = false;

static void dialog_start_cb(void *s, lv_msg_t *m) { (void)s; (void)m; dialog_on = true; }
static void dialog_stop_cb(void *s, lv_msg_t *m)  { (void)s; (void)m; dialog_on = false; }

static void busy_timer_cb(lv_timer_t *t)
{
    (void)t;
    atomic_store(&busy, dialog_on || app_broadcast_is_on() || rtty_get_state() != RTTY_OFF);
}

void radio_busy_init(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    lv_msg_subscribe(MSG_DIALOG_START, dialog_start_cb, NULL);
    lv_msg_subscribe(MSG_DIALOG_STOP, dialog_stop_cb, NULL);
    lv_timer_create(busy_timer_cb, BUSY_PERIOD_MS, NULL);
}

bool radio_busy_get(void)
{
    return atomic_load(&busy);
}

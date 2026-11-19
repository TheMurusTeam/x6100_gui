/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (C) 2026 Hany El Imam
 *
 *  Xiegu X6100 LVGL GUI - FT8 CB: who heard you, from PSK Reporter
 *
 *  Why not FT8's psk_reporter.c: that one listens to PSK Reporter's live
 *  MQTT feed (mqtt.pskreporter.info), whose "v2" topics are enriched with
 *  each station's DXCC country and leave out the reports that have none.
 *  CB calls have no DXCC, so nothing of FT8-CB ever comes through: tested
 *  on 2026-11-19, the site listed new reports of 1KO125 while both the v2
 *  and v2raw topics stayed silent. pskreporter.info itself has them, and
 *  answers its web query (retrieve.pskreporter.info, the one its own map
 *  uses) for CB calls on 27 MHz. That query is HTTPS only, so it goes
 *  through GnuTLS, which the radio's image already has.
 *
 *  So: every 5 minutes (PSK Reporter asks for no more), the reports of your
 *  call heard in FT8 on 26-28 MHz in the last 24 hours, on a thread of its
 *  own; the map keeps them as long as set (cb_map.c). The feed's history is
 *  there at once when the app opens, which the live feed never had.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Both on the LVGL thread */
typedef struct {
    /* `call` at `grid` heard you at UTC seconds `when` */
    void (*on_spot)(const char *call, const char *grid, int snr, int64_t when);
    /* A query done: `ok` false if PSK Reporter couldn't be reached or read,
     * else `n` reports of you in the last 24 hours */
    void (*on_result)(bool ok, int n);
} cbpsk_cb_t;

/* Watch `call` (NULL: stop). LVGL thread; returns at once, the network is
 * the thread's. PSK Reporter is asked at once, then every 5 minutes; within
 * 5 minutes of the last query (the app reopened) its answer is given again
 * instead, and a new call waits for the 5 minutes. */
void cbpsk_watch(const char *call, const cbpsk_cb_t *cb);

/* Parses a query's answer (XML), calling `spot` for each report of `me` in
 * FT8 on 26-28 MHz with a usable locator; returns how many. Apart for tests. */
int cbpsk_parse(const char *xml, const char *me,
                void (*spot)(void *arg, const char *call, const char *grid, int snr, int64_t when), void *arg);

#ifdef __cplusplus
}
#endif

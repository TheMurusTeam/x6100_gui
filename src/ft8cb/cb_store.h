/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 */

/*
 * FT8 CB - settings and log.
 *
 *   /mnt/ft8cb.conf      the CB callsign and the frequency in use
 *   /mnt/ft8cb_log.adi   the QSO log (ADIF, BAND 11m), separate from the
 *                        amateur log of the FT8 application
 *
 * Pure C, no LVGL.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT8CB_CONF_FILE  "/mnt/ft8cb.conf"
#define FT8CB_LOG_FILE   "/mnt/ft8cb_log.adi"

typedef struct {
    char    call[12];
    int32_t freq_hz;
} ft8cb_conf_t;

void ft8cb_conf_load(ft8cb_conf_t *c);
void ft8cb_conf_save(const ft8cb_conf_t *c);

typedef struct {
    const char *my_call, *his_call, *my_grid, *his_grid;
    time_t      time;
    int64_t     freq_hz;
    int         rst_sent, rst_rcvd;
} ft8cb_qso_rec_t;

/* Appends a QSO to the log; false on error */
bool ft8cb_log_add(const ft8cb_qso_rec_t *q);

/* Worked before: the calls of the log with the time of their last QSO, read
 * once and kept up to date */
#define FT8CB_RECENT_S  (24 * 3600)

typedef enum {
    FT8CB_WORKED_NO = 0,
    FT8CB_WORKED_RECENT,        /* last QSO less than FT8CB_RECENT_S ago */
    FT8CB_WORKED_OLD,           /* earlier */
} ft8cb_worked_t;

void ft8cb_worked_load(void);
void ft8cb_worked_free(void);
ft8cb_worked_t ft8cb_worked(const char *call);

#ifdef __cplusplus
}
#endif

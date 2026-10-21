/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 */

/*
 * FT8 CB - QSO sequencing.
 *
 * The same behaviour as the FT8 application (ft8/qso.cpp: current and next
 * candidate, Auto, repeats, the QSO saved at R+report or RR73), plus the
 * messages of 11 m: one-callsign messages "ME -10", "ME R-10", "ME RR73",
 * "ME 73" sent as free text (the sender is the station of the current QSO),
 * and "ME HIS" without grid in reply to a CQ.  Answers: see cb_msg.h.
 *
 * Pure C, no LVGL.  Uses the types of ft8/qso.h.
 */
#pragma once

#include <stdbool.h>
#include "../ft8/qso.h"
#include "cb_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char           call[FT8CB_CALL_LEN];
    char           grid[9];
    ftx_msg_type_t last;
    int            local_snr;
    int            rcvd_snr;
    int            sent_snr;
    bool           saved;
} ft8cb_cand_t;

typedef struct {
    char          me[FT8CB_CALL_LEN];
    char          qth[5];
    save_qso_cb_t save_cb;
    int           max_repeats;
    bool          auto_mode;
    bool          has_cur, has_next;
    ft8cb_cand_t  cur, next;
} ft8cb_qso_t;

void ft8cb_qso_init(ft8cb_qso_t *p, const char *my_call, const char *qth, save_qso_cb_t save_cb, int max_repeats);
void ft8cb_qso_set_auto(ft8cb_qso_t *p, bool on);
void ft8cb_qso_start_new_slot(ft8cb_qso_t *p);
void ft8cb_qso_reset(ft8cb_qso_t *p);

/* A received text: fills meta (type, to_me, call_de...) and, when the QSO
 * moves on with Auto, the next TX in tx. */
void ft8cb_qso_add_rx_text(ft8cb_qso_t *p, const char *text, int snr, ftx_msg_meta_t *meta, ftx_tx_msg_t *tx);

/* The user picked a received message: answer it. */
void ft8cb_qso_start(ft8cb_qso_t *p, const ftx_msg_meta_t *meta, ftx_tx_msg_t *tx);

bool ft8cb_qso_can_save(const ft8cb_qso_t *p);
bool ft8cb_qso_force_save(ft8cb_qso_t *p);

/* The answer to a message of type last from his_call (exposed for tests) */
void ft8cb_answer(ftx_msg_type_t last, const char *his_call, const char *my_call, int snr, const char *grid,
                  char *out);

#ifdef __cplusplus
}
#endif

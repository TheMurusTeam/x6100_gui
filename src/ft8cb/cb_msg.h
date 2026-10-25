/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 */

/*
 * FT8 CB - callsigns and messages of 11 m (CB) operation.
 *
 * CB callsigns ("1AT106", "26AT101", "1AT1000", "999ZZ/ZZ") are not standard
 * amateur callsigns: FT8 carries them as "non-standard" calls (77-bit type 4,
 * up to 11 characters in clear, the other call as a 12-bit hash) or inside
 * free text.  The callsign rules (including 4-digit units with a 1-digit
 * prefix and the /XX form) and the messages below follow the conventions
 * of WSJT-CB (https://github.com/vash909/WSJT-CB, Lorenzo 1AT106 / IU1BOT
 * and contributors), so that the two interoperate: WSJT-CB was the
 * reference for them.  This file is a separate implementation in C on
 * ft8lib; it contains no WSJT-CB code.
 *
 *   both calls CB      CQ:  CQ ME            (type 4)
 *                      Tx1: <HIS> ME         (type 4, his call hashed)
 *                      Tx2: HIS -10          (free text)
 *                      Tx3: HIS R-10         (free text)
 *                      Tx4: HIS RR73         (free text)
 *                      Tx5: HIS 73           (free text)
 *   my call CB only    Tx1: <HIS> ME, Tx2/3: HIS ME -10 (my call hashed by
 *                      the encoder), Tx4/5: <HIS> ME RR73 / 73 (type 4)
 *   standard calls     as in the FT8 application.
 *
 * Pure C on top of ft8lib, no LVGL: tested on a PC.
 */
#pragma once

#include <stdbool.h>
#include <ft8lib/message.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT8CB_CALL_LEN  12      /* 11 characters + '\0' */

/* "1AT106", "26AT101", "1AT1000", "999ZZ/ZZ" */
bool ft8cb_is_cb_call(const char *call);

/* A standard amateur callsign, as FT8 packs it in 28 bits (optional /P, /R) */
bool ft8cb_is_std_call(const char *call);

/* A callsign FT8 can carry at all: 3..11 characters, A-Z 0-9 '/' */
bool ft8cb_is_valid_call(const char *call);

/* "+05" "-12" / "R+05" "R-12" */
bool ft8cb_is_report(const char *t);
bool ft8cb_is_r_report(const char *t);

/* The CQ text of my_call: "CQ ME" (non-standard call, no grid) or
 * "CQ ME GRID" / "CQ_MOD ME GRID" for a standard one.  out: 35 bytes. */
void ft8cb_make_cq(const char *my_call, const char *grid4, char *out);

/* Encodes a text built by this module (or typed) into msg, choosing the
 * message type as described above.  Uses (and fills) the ft8lib hash table. */
ftx_message_rc_t ft8cb_encode(const char *text, ftx_message_t *msg);

/* Puts my_call in the hash table and resets its age, so that "<ME>" in the
 * received messages is shown with the callsign.  Call once per slot, from
 * the decoder thread. */
void ft8cb_keep_hash(const char *my_call);

#ifdef __cplusplus
}
#endif

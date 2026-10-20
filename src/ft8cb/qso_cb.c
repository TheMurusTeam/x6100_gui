/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 *
 * Additional term under section 7(b) of the GNU GPL version 3: any work that
 * contains this file, or a work based on it, must preserve the author
 * attribution "Hany El Imam" and the credits of the WEFAX, NAVTEX/SITOR-B,
 * DSC, SSTV and FT8 CB applications in the Appropriate Legal Notices
 * displayed by its user interface (in this program: the Info window of each
 * application).  See NOTICE-1KO125.md.
 */

/* FT8 CB - QSO sequencing, see qso_cb.h */

#include "qso_cb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SNR 100
#define MAX_TOKENS  6
#define TOKEN_LEN   20

/* ---- answers ------------------------------------------------------------------ */

void ft8cb_answer(ftx_msg_type_t last, const char *his, const char *me, int snr, const char *grid, char *out)
{
    bool me_cb = !ft8cb_is_std_call(me);
    bool pair  = me_cb && !ft8cb_is_std_call(his);     /* two non-standard calls */
    const size_t sz = FTX_MAX_MESSAGE_LENGTH;

    out[0] = '\0';
    switch (last) {
    case FTX_MSG_TYPE_CQ:                               /* Tx1 */
        if (me_cb)
            snprintf(out, sz, "<%s> %s", his, me);
        else if (grid && strlen(grid) >= 4)
            snprintf(out, sz, "%s %s %.4s", his, me, grid);
        else
            snprintf(out, sz, "%s %s", his, me);
        break;
    case FTX_MSG_TYPE_GRID:                             /* Tx2 */
        if (pair) snprintf(out, sz, "%s %+03d", his, snr);
        else      snprintf(out, sz, "%s %s %+03d", his, me, snr);
        break;
    case FTX_MSG_TYPE_REPORT:                           /* Tx3 */
        if (pair) snprintf(out, sz, "%s R%+03d", his, snr);
        else      snprintf(out, sz, "%s %s R%+03d", his, me, snr);
        break;
    case FTX_MSG_TYPE_R_REPORT:                         /* Tx4 */
        if (pair)       snprintf(out, sz, "%s RR73", his);
        else if (me_cb) snprintf(out, sz, "<%s> %s RR73", his, me);
        else            snprintf(out, sz, "%s %s RR73", his, me);
        break;
    case FTX_MSG_TYPE_RR73:                             /* Tx5 */
        if (pair)       snprintf(out, sz, "%s 73", his);
        else if (me_cb) snprintf(out, sz, "<%s> %s 73", his, me);
        else            snprintf(out, sz, "%s %s 73", his, me);
        break;
    default:
        break;
    }
}

/* ---- candidates ----------------------------------------------------------------- */

static void cand_new(ft8cb_cand_t *c, const char *call)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->call, sizeof(c->call), "%s", call);
    c->rcvd_snr = DEFAULT_SNR;
    c->sent_snr = DEFAULT_SNR;
    c->last = FXT_MSG_TYPE_OTHER;
}

static bool cand_can_save(const ft8cb_cand_t *c)
{
    return c->call[0] && c->rcvd_snr != DEFAULT_SNR && c->sent_snr != DEFAULT_SNR && !c->saved;
}

static void cand_save(ft8cb_qso_t *p, ft8cb_cand_t *c)
{
    if (p->save_cb && cand_can_save(c)) {
        p->save_cb(c->call, c->grid, c->rcvd_snr, c->sent_snr);
        c->saved = true;
    }
}

/* the next TX of a candidate; the report sent is remembered */
static void cand_tx_text(ft8cb_qso_t *p, ft8cb_cand_t *c, char *out)
{
    ft8cb_answer(c->last, c->call, p->me, c->local_snr, p->qth, out);
    if (c->last == FTX_MSG_TYPE_GRID || c->last == FTX_MSG_TYPE_REPORT)
        c->sent_snr = c->local_snr;
}

/* the candidate a message of call_de updates: current (created if none) or next */
static ft8cb_cand_t *cand_to_update(ft8cb_qso_t *p, const char *call_de)
{
    if (!p->has_cur) {
        cand_new(&p->cur, call_de);
        p->has_cur = true;
    }
    if (strcmp(p->cur.call, call_de) == 0) return &p->cur;
    if (!p->has_next) {
        cand_new(&p->next, call_de);
        p->has_next = true;
        return &p->next;
    }
    return strcmp(p->next.call, call_de) == 0 ? &p->next : NULL;
}

/* ---- public ------------------------------------------------------------------------ */

void ft8cb_qso_init(ft8cb_qso_t *p, const char *my_call, const char *qth, save_qso_cb_t save_cb, int max_repeats)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->me, sizeof(p->me), "%s", my_call ? my_call : "");
    snprintf(p->qth, sizeof(p->qth), "%.4s", qth ? qth : "");
    p->save_cb = save_cb;
    p->max_repeats = max_repeats;
    p->auto_mode = true;
}

void ft8cb_qso_set_auto(ft8cb_qso_t *p, bool on) { p->auto_mode = on; }

void ft8cb_qso_start_new_slot(ft8cb_qso_t *p) { p->has_next = false; }

void ft8cb_qso_reset(ft8cb_qso_t *p) { p->has_cur = p->has_next = false; }

bool ft8cb_qso_can_save(const ft8cb_qso_t *p) { return p->has_cur && cand_can_save(&p->cur); }

bool ft8cb_qso_force_save(ft8cb_qso_t *p)
{
    if (!p->has_cur) return false;
    cand_save(p, &p->cur);
    p->has_cur = false;
    return true;
}

static int split(const char *text, char tok[MAX_TOKENS][TOKEN_LEN])
{
    int n = 0;
    const char *s = text;
    while (*s && n < MAX_TOKENS) {
        while (*s == ' ') s++;
        if (!*s) break;
        int k = 0;
        if (*s == '<') s++;                 /* "<CALL>" -> "CALL" */
        while (*s && *s != ' ') {
            if (*s != '>' && k < TOKEN_LEN - 1) tok[n][k++] = *s;
            s++;
        }
        tok[n][k] = '\0';
        n++;
    }
    return n;
}

static bool is_grid(const char *t)
{
    return strlen(t) == 4 && t[0] >= 'A' && t[0] <= 'R' && t[1] >= 'A' && t[1] <= 'R' &&
           t[2] >= '0' && t[2] <= '9' && t[3] >= '0' && t[3] <= '9' && strcmp(t, "RR73") != 0;
}

static bool is_cq_modifier(const char *t)
{
    size_t n = strlen(t);
    if (n == 3 && t[0] >= '0' && t[0] <= '9' && t[1] >= '0' && t[1] <= '9' && t[2] >= '0' && t[2] <= '9')
        return true;
    if (n < 1 || n > 4) return false;
    for (size_t i = 0; i < n; i++)
        if (t[i] < 'A' || t[i] > 'Z') return false;
    return true;
}

/* type of the third (or, in one-callsign messages, second) field */
static ftx_msg_type_t payload_type(const char *t)
{
    if (strcmp(t, "73") == 0) return FTX_MSG_TYPE_73;
    if (strcmp(t, "RR73") == 0 || strcmp(t, "RRR") == 0) return FTX_MSG_TYPE_RR73;
    if (ft8cb_is_r_report(t)) return FTX_MSG_TYPE_R_REPORT;
    if (ft8cb_is_report(t)) return FTX_MSG_TYPE_REPORT;
    if (is_grid(t)) return FTX_MSG_TYPE_GRID;
    return FXT_MSG_TYPE_OTHER;
}

/* a message of call_de to me, of the given type */
static void to_me(ft8cb_qso_t *p, ftx_msg_meta_t *meta, const char *call_de, ftx_msg_type_t type,
                  const char *payload, int snr, ftx_tx_msg_t *tx)
{
    meta->to_me = true;
    ft8cb_cand_t *c;

    switch (type) {
    case FTX_MSG_TYPE_GRID:             /* reply to my CQ, with or without grid */
    case FTX_MSG_TYPE_REPORT:
        c = cand_to_update(p, call_de);
        if (!c) return;
        c->last = type;
        c->local_snr = snr;
        if (type == FTX_MSG_TYPE_GRID) snprintf(c->grid, sizeof(c->grid), "%s", meta->grid);
        else c->rcvd_snr = meta->remote_snr;
        if (c == &p->cur && p->auto_mode) {
            tx->repeats = p->max_repeats;
            cand_tx_text(p, c, tx->msg);
        }
        break;

    case FTX_MSG_TYPE_R_REPORT:
    case FTX_MSG_TYPE_RR73:
        if (!p->has_cur || strcmp(p->cur.call, call_de) != 0) return;
        c = &p->cur;
        c->last = type;
        c->local_snr = snr;
        if (type == FTX_MSG_TYPE_R_REPORT) c->rcvd_snr = meta->remote_snr;
        cand_save(p, c);
        if (p->auto_mode) {
            tx->repeats = 1;
            cand_tx_text(p, c, tx->msg);
        }
        break;

    case FTX_MSG_TYPE_73:
        if (!p->has_cur || strcmp(p->cur.call, call_de) != 0) return;
        if (p->has_next) {
            p->cur = p->next;
            p->has_next = false;
            if (p->auto_mode) {
                tx->repeats = 1;
                cand_tx_text(p, &p->cur, tx->msg);
            }
        } else {
            p->has_cur = false;
        }
        break;

    default:
        break;
    }
    (void)payload;
}

void ft8cb_qso_add_rx_text(ft8cb_qso_t *p, const char *text, int snr, ftx_msg_meta_t *meta, ftx_tx_msg_t *tx)
{
    char tok[MAX_TOKENS][TOKEN_LEN];

    meta->type = FXT_MSG_TYPE_OTHER;
    meta->local_snr = snr;
    meta->to_me = false;
    meta->grid[0] = '\0';
    meta->call_de[0] = '\0';
    meta->remote_snr = 0;

    int n = split(text, tok);
    if (n == 0) return;

    /* "A2AA RR73; R2RFE <RP79AA> +05" (DXpedition mode): keep the part for me */
    if (n >= 5 && strchr(text, ';')) {
        char a[TOKEN_LEN], b[TOKEN_LEN], c[TOKEN_LEN];
        if (strcmp(tok[0], p->me) == 0) {
            snprintf(a, sizeof(a), "%s", tok[0]);
            snprintf(b, sizeof(b), "%s", tok[3]);
            snprintf(c, sizeof(c), "%.*s", (int)strlen(tok[1]) - 1, tok[1]);
        } else {
            snprintf(a, sizeof(a), "%s", tok[2]);
            snprintf(b, sizeof(b), "%s", tok[3]);
            snprintf(c, sizeof(c), "%s", tok[4]);
        }
        snprintf(tok[0], TOKEN_LEN, "%s", a);
        snprintf(tok[1], TOKEN_LEN, "%s", b);
        snprintf(tok[2], TOKEN_LEN, "%s", c);
        n = 3;
    }

    if (strcmp(tok[0], "CQ") == 0) {
        if (n < 2) return;
        int pos = (n > 2 && is_cq_modifier(tok[1]) && !is_grid(tok[2])) ? 2 : 1;
        meta->type = FTX_MSG_TYPE_CQ;
        snprintf(meta->call_de, sizeof(meta->call_de), "%s", tok[pos]);
        if (n > pos + 1 && is_grid(tok[pos + 1]))
            snprintf(meta->grid, sizeof(meta->grid), "%s", tok[pos + 1]);
        return;
    }

    const char *call_to = tok[0];
    const char *call_de;
    const char *payload;
    ftx_msg_type_t type;

    if (n >= 3) {
        /* "TO DE PAYLOAD" */
        call_de = tok[1];
        payload = tok[2];
        type = payload_type(payload);
    } else if (n == 2) {
        type = payload_type(tok[1]);
        if (type == FXT_MSG_TYPE_OTHER || type == FTX_MSG_TYPE_GRID) {
            /* "TO DE": reply to a CQ without grid (non-standard calls) */
            if (!ft8cb_is_valid_call(tok[1]) && strcmp(tok[1], "...") != 0) return;
            call_de = tok[1];
            payload = "";
            type = FTX_MSG_TYPE_GRID;
        } else {
            /* "TO -10": one callsign, the sender is the station of my QSO */
            payload = tok[1];
            call_de = (strcmp(call_to, p->me) == 0 && p->has_cur) ? p->cur.call : "";
        }
    } else {
        return;
    }

    meta->type = type;
    snprintf(meta->call_de, sizeof(meta->call_de), "%s", call_de);
    if (type == FTX_MSG_TYPE_GRID && payload[0])
        snprintf(meta->grid, sizeof(meta->grid), "%s", payload);
    if (type == FTX_MSG_TYPE_REPORT)
        meta->remote_snr = atoi(payload);
    if (type == FTX_MSG_TYPE_R_REPORT)
        meta->remote_snr = atoi(payload + 1);
    if (type == FXT_MSG_TYPE_OTHER) return;

    if (strcmp(call_to, p->me) == 0) {
        meta->to_me = true;
        if (call_de[0] && strcmp(call_de, "...") != 0)
            to_me(p, meta, call_de, type, payload, snr, tx);
    }
}

void ft8cb_qso_start(ft8cb_qso_t *p, const ftx_msg_meta_t *meta, ftx_tx_msg_t *tx)
{
    ftx_msg_type_t type = meta->type;
    tx->msg[0] = '\0';
    if (!meta->call_de[0] || strcmp(meta->call_de, "...") == 0) return;
    if (!meta->to_me) type = FTX_MSG_TYPE_CQ;
    else if (type == FTX_MSG_TYPE_73 || type == FXT_MSG_TYPE_OTHER) return;

    if (!p->has_cur || strcmp(p->cur.call, meta->call_de) != 0) {
        cand_new(&p->cur, meta->call_de);
        p->has_cur = true;
    }
    ft8cb_cand_t *c = &p->cur;
    c->local_snr = meta->local_snr;
    c->last = type;

    tx->repeats = p->max_repeats;
    switch (type) {
    case FTX_MSG_TYPE_CQ:
    case FTX_MSG_TYPE_GRID:
        snprintf(c->grid, sizeof(c->grid), "%s", meta->grid);
        break;
    case FTX_MSG_TYPE_R_REPORT:
        tx->repeats = 1;
        /* fall through */
    case FTX_MSG_TYPE_REPORT:
        c->rcvd_snr = meta->remote_snr;
        break;
    case FTX_MSG_TYPE_RR73:
        tx->repeats = 1;
        break;
    default:
        break;
    }
    cand_tx_text(p, c, tx->msg);
}

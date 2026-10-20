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

/* FT8 CB - callsigns and messages, see cb_msg.h */

#include "cb_msg.h"

#include <ft8lib/hashtable.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_TOKENS  6
#define TOKEN_LEN   20
#define FREE_MAX    13          /* characters of a free-text message */

static int digits_at(const char *s, int *n)
{
    int k = 0;
    while (isdigit((unsigned char)s[k])) k++;
    *n = k;
    return k;
}

static int letters_at(const char *s, int *n)
{
    int k = 0;
    while (isupper((unsigned char)s[k])) k++;
    *n = k;
    return k;
}

bool ft8cb_is_cb_call(const char *c)
{
    int p, l, s;
    if (!c) return false;
    digits_at(c, &p);                       /* country prefix: 1..3 digits */
    if (p < 1 || p > 3) return false;
    letters_at(c + p, &l);                  /* 1..2 letters */
    if (l < 1 || l > 2) return false;
    const char *t = c + p + l;
    if (t[0] == '/') {                      /* 999ZZ/ZZ */
        int l2;
        letters_at(t + 1, &l2);
        return l2 == 2 && t[3] == '\0';
    }
    digits_at(t, &s);                       /* unit: 1..3 digits, 4 with a 1-digit prefix */
    if (t[s] != '\0') return false;
    return (s >= 1 && s <= 3) || (s == 4 && p == 1);
}

static bool in_set(char c, const char *set)
{
    return c && strchr(set, c) != NULL;
}

bool ft8cb_is_std_call(const char *call)
{
    if (!call) return false;
    int len = (int)strlen(call);
    if (len >= 2 && call[len - 2] == '/' && (call[len - 1] == 'P' || call[len - 1] == 'R'))
        len -= 2;                           /* /P and /R fit in a standard message */
    if (len < 3 || len > 6) return false;

    /* the rules of ft8lib pack_basecall(): right-aligned in 6 characters,
     * digit in the 3rd position */
    char c6[7] = "      ";
    if (strncmp(call, "3DA0", 4) == 0 && len > 4 && len <= 7) {
        memcpy(c6, "3D0", 3);
        memcpy(c6 + 3, call + 4, (size_t)(len - 4));
    } else if (strncmp(call, "3X", 2) == 0 && isupper((unsigned char)call[2]) && len <= 7) {
        c6[0] = 'Q';
        memcpy(c6 + 1, call + 2, (size_t)(len - 2));
    } else if (isdigit((unsigned char)call[2]) && len <= 6) {
        memcpy(c6, call, (size_t)len);
    } else if (isdigit((unsigned char)call[1]) && len <= 5) {
        memcpy(c6 + 1, call, (size_t)len);
    } else {
        return false;
    }
    static const char an[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char ls[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return (c6[0] == ' ' || in_set(c6[0], an)) && in_set(c6[1], an) && isdigit((unsigned char)c6[2]) &&
           in_set(c6[3], ls) && in_set(c6[4], ls) && in_set(c6[5], ls);
}

bool ft8cb_is_valid_call(const char *call)
{
    if (!call) return false;
    size_t n = strlen(call);
    if (n < 3 || n > 11) return false;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)call[i]) && !isupper((unsigned char)call[i]) && call[i] != '/') return false;
    return true;
}

static bool is_snr(const char *t)
{
    if ((t[0] != '+' && t[0] != '-') || !isdigit((unsigned char)t[1])) return false;
    return t[2] == '\0' || (isdigit((unsigned char)t[2]) && t[3] == '\0');
}

bool ft8cb_is_report(const char *t)
{
    return t && is_snr(t);
}

bool ft8cb_is_r_report(const char *t)
{
    return t && t[0] == 'R' && is_snr(t + 1);
}

void ft8cb_make_cq(const char *my_call, const char *grid4, char *out)
{
    if (ft8cb_is_std_call(my_call) && grid4 && strlen(grid4) >= 4)
        snprintf(out, FTX_MAX_MESSAGE_LENGTH, "CQ %s %.4s", my_call, grid4);
    else
        snprintf(out, FTX_MAX_MESSAGE_LENGTH, "CQ %s", my_call);
}

/* splits text into tokens, '<' '>' kept */
static int split(const char *text, char tok[MAX_TOKENS][TOKEN_LEN])
{
    int n = 0;
    const char *p = text;
    while (*p && n < MAX_TOKENS) {
        while (*p == ' ') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ') {
            if (k < TOKEN_LEN - 1) tok[n][k++] = *p;
            p++;
        }
        tok[n][k] = '\0';
        n++;
    }
    return n;
}

static void unbracket(const char *in, char *out, size_t sz)
{
    size_t n = strlen(in);
    if (n >= 2 && in[0] == '<' && in[n - 1] == '>') {
        snprintf(out, sz, "%.*s", (int)(n - 2), in + 1);
    } else {
        snprintf(out, sz, "%s", in);
    }
}

static bool free_text_ok(const char *text)
{
    static const char table[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";
    if (strlen(text) > FREE_MAX) return false;
    for (const char *p = text; *p; p++)
        if (!strchr(table, *p)) return false;
    return true;
}

ftx_message_rc_t ft8cb_encode(const char *text, ftx_message_t *msg)
{
    char tok[MAX_TOKENS][TOKEN_LEN];
    int n = split(text, tok);
    ftx_message_rc_t rc = FTX_MESSAGE_RC_ERROR_TYPE;

    ftx_message_init(msg);
    if (n == 0) return rc;

    /* CQ of a non-standard (CB) call: type 4, no grid, no CQ modifier */
    if ((strcmp(tok[0], "CQ") == 0 || strncmp(tok[0], "CQ_", 3) == 0) && n >= 2) {
        const char *call = (n >= 3 && !ft8cb_is_std_call(tok[1]) && ft8cb_is_valid_call(tok[2]) &&
                            strlen(tok[2]) > 4) ? tok[2] : tok[1];
        if (!ft8cb_is_std_call(call))
            return ftx_message_encode_nonstd(msg, &hash_if, "CQ", call, "");
        return ftx_message_encode(msg, &hash_if, text);
    }

    /* "<HIS> ME [RR73|RRR|73]": type 4, his call hashed, mine in clear */
    if (tok[0][0] == '<' && n >= 2 && n <= 3) {
        char to[TOKEN_LEN];
        unbracket(tok[0], to, sizeof(to));
        rc = ftx_message_encode_nonstd(msg, &hash_if, to, tok[1], n == 3 ? tok[2] : "");
        if (rc == FTX_MESSAGE_RC_OK) return rc;
    }

    /* "HIS -10", "HIS R-10", "HIS RR73", "HIS 73": free text */
    if (n == 2 && free_text_ok(text))
        return ftx_message_encode_free(msg, text);

    rc = ftx_message_encode(msg, &hash_if, text);
    if (rc != FTX_MESSAGE_RC_OK && free_text_ok(text))
        rc = ftx_message_encode_free(msg, text);
    return rc;
}

void ft8cb_keep_hash(const char *my_call)
{
    if (!ft8cb_is_valid_call(my_call)) return;
    ftx_message_t m;
    ftx_message_init(&m);
    /* encoding the CQ saves the callsign and its hashes (age reset) */
    ftx_message_encode_nonstd(&m, &hash_if, "CQ", my_call, "");
}

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

/*
 * FT8 CB - country of a CB callsign from its division number (the digits
 * before the letters: 26AT101 -> 26 -> England).  The table is the text file
 * CB_DIV_FILE, "number;country" per line, read on first use.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CB_DIV_FILE
#define CB_DIV_FILE "/usr/share/x6100/cb_divisions.txt"
#endif

/* Country of call, or NULL (not a CB callsign, unknown division, no table) */
const char *ft8cb_country(const char *call);

#ifdef __cplusplus
}
#endif

/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
 * See NOTICE-1KO125.md.
 */
/*
 * Spectrum zoom held at 4x while an app is open (WEFAX, NAVTEX, DSC).
 *
 *   app_zoom_begin()   when the app opens: zoom 4x
 *   app_zoom_end()     when it closes: the zoom of before
 *
 * While the app is open, a band, mode or frequency change that brings
 * another zoom is set back to 4x.  The 4x is never saved: the zoom stored
 * for each mode stays the user's own.
 */
#pragma once

void app_zoom_begin(void);
void app_zoom_end(void);

/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xiegu X6100 LVGL GUI - FT8 CB TX player
 *
 * Copy of ft8/tx_worker (R1CBU x6100_gui) for the FT8 CB application: the
 * same synthesis, ALC-driven gain correction and settings, but the message
 * is encoded with ft8cb_encode() (non-standard calls, free text), FT8 only.
 * Modified for 1KO125 by Hany El Imam, 2026.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../ports/app_ports.h"
#include "../ft8/tx_worker.h"   /* tx_abort_fn_t */

#ifdef __cplusplus
extern "C" {
#endif

void ft8cb_tx_construct(const app_ports_t *ports, uint32_t sample_rate);
void ft8cb_tx_destruct(void);

/* Transmits text (one FT8 slot).  Returns false if aborted. */
bool ft8cb_tx_run(const char *text, float base_gain_offset, tx_abort_fn_t abort_check, void *abort_check_ctx);

#ifdef __cplusplus
}
#endif

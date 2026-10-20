/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xiegu X6100 - diagnostic logs (1KO125, Hany El Imam 2026)
 *
 * Two text files on the DATA partition, one line per event, UTC time:
 *
 *   /mnt/ptt.log   every TX command of the APP with its origin (front PTT
 *                  key, CAT USB/Bluetooth, CAT LAN, FT8, SSTV, voice
 *                  messages, ATU...), and every RX/TX change reported by the
 *                  BASE, marked "NOT COMMANDED" when the APP had not asked
 *                  for TX (microphone PTT, rear PTT jack, or the BASE itself).
 *                  CW is left out: the CW key and the TX of the CW modes.
 *   /mnt/i2c.log   the errors of the I2C link to the BASE written by the
 *                  control library ("Can't write to i2c..."), the kernel I2C
 *                  messages, the BASE resets and the restarts of the data
 *                  flow from the BASE.
 *
 * Each file is limited to DIAG_MAX_BYTES; the previous part is kept as
 * ptt.log.old / i2c.log.old.  At most DIAG_MAX_PER_S lines a second are
 * written to each file; the next line says how many were skipped.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the stderr and kernel-log readers.  Call first thing in main(). */
void diag_init(void);

/* Name of the origin of the TX commands of the calling thread
 * ("CAT LAN", "FT8"...); a static string. */
void diag_set_source(const char *name);
const char *diag_source(void);

/* True once after a write to the BASE failed: a "Can't write to i2c" of the
 * control library or a kernel message about i2c-0 (radio.c resends the
 * registers) */
bool diag_i2c_error_take(void);

void diag_ptt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void diag_i2c_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

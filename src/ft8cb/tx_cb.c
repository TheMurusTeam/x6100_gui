/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xiegu X6100 LVGL GUI - FT8 CB TX player, see tx_cb.h
 *
 * Copy of ft8/tx_worker.c (R1CBU x6100_gui, Copyright (c) 2026) with the
 * message encoded by ft8cb_encode().  Modified for 1KO125 by Hany El Imam.
 */

#include "tx_cb.h"
#include "cb_msg.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <ft8lib/constants.h>
#include <ft8lib/encode.h>

#include "lvgl/lvgl.h"

#include "../cfg/cfg_api.h"
#include "../ft8/gfsk.h"

/* FT8 audio tone offset inside the radio passband (as ft8/tx_worker.c) */
#define SIGNAL_FREQ_HZ   1325
#define MAX_PWR_W        5.0f
#define GAIN_MIN_DB     (-30.0f)
#define GAIN_MAX_DB      0.0f

static audio_port_player_t *player;
static const app_ports_t *g_ports = NULL;
static uint32_t sample_rate;

/* ALC-driven gain correction (as ft8/tx_worker.c) */
static float get_correction(void)
{
    static uint8_t msg_id = 0;
    float correction = 0.0f;
    float pwr        = 0.0f;
    float alc        = 0.0f;

    if (g_ports->telemetry->tx_info_refresh(&msg_id, &alc, &pwr, NULL)) {
        float target_pwr = LV_MIN(param_f_get(cfg.pwr()), MAX_PWR_W);
        if (alc > 0.5f) {
            correction = log10f(log10f(11.1f - alc)) * 20.0f - 0.38f;
        } else if (target_pwr - pwr > 0.5f) {
            correction = log10f(target_pwr / (pwr + 0.01f)) * 10.0f;
        }
    }
    return correction;
}

void ft8cb_tx_construct(const app_ports_t *ports, uint32_t rate)
{
    g_ports = ports;
    sample_rate = rate;
    player = g_ports->audio->get_player(rate, 1);
}

void ft8cb_tx_destruct(void)
{
    g_ports->audio->player_release(player);
}

/* the FT8 waveform of text, NULL on error */
static int16_t *make_samples(const char *text, uint32_t *n_samples)
{
    ftx_message_t msg;
    ftx_message_rc_t rc = ft8cb_encode(text, &msg);
    if (rc != FTX_MESSAGE_RC_OK) {
        LV_LOG_ERROR("FT8 CB: cannot encode '%s' (%d)", text, rc);
        return NULL;
    }
    uint8_t tones[FT8_NN];
    ft8_encode(msg.payload, tones);
    return gfsk_synth(tones, FT8_NN, SIGNAL_FREQ_HZ, FT8_SYMBOL_BT, FT8_SYMBOL_PERIOD, sample_rate, n_samples);
}

bool ft8cb_tx_run(const char *text, float base_gain_offset, tx_abort_fn_t abort_check, void *abort_check_ctx)
{
    uint32_t n_samples = 0;
    int16_t *samples = make_samples(text, &n_samples);
    if (!samples) {
        return true; /* nothing to send; not an abort */
    }

    if (param_f_get(cfg.pwr()) > MAX_PWR_W) {
        g_ports->radio->set_pwr(MAX_PWR_W);
    }

    float gain_offset      = base_gain_offset + param_f_get(cfg.ft8.output_gain_offset());
    float play_gain_offset = g_ports->audio->set_play_vol(gain_offset + 6.0f);
    gain_offset           -= play_gain_offset;

    uint64_t radio_freq = cparam_i_get(cfg.cur.fg_freq());
    g_ports->radio->set_freq((int32_t)radio_freq + (int32_t)param_i_get(cfg.ft8.tx_freq()) - SIGNAL_FREQ_HZ);
    g_ports->radio->set_modem(true);

    float    prev_gain_offset = gain_offset;
    size_t   counter          = 0;
    int16_t *ptr              = samples;
    size_t   part;

    bool aborted = false;
    while (true) {
        if (counter > 30) {
            gain_offset += get_correction() * 0.4f;
            if (gain_offset > GAIN_MAX_DB) {
                gain_offset = GAIN_MAX_DB;
            } else if (gain_offset < GAIN_MIN_DB) {
                gain_offset = GAIN_MIN_DB;
            }
        }
        if (n_samples <= 0) {
            break;
        }
        if (abort_check && abort_check(abort_check_ctx)) {
            aborted = true;
            break;
        }
        part = LV_MIN(1024 * 2, n_samples);
        if (gain_offset == prev_gain_offset) {
            if (gain_offset != 0.0f) {
                g_ports->audio->gain_db(ptr, part, gain_offset, ptr);
            }
        } else {
            g_ports->audio->gain_db_transition(ptr, part, prev_gain_offset, gain_offset, ptr);
            prev_gain_offset = gain_offset;
        }
        g_ports->audio->player_send(player, ptr, part);
        n_samples -= part;
        ptr       += part;
        counter++;
    }

    param_f_set(cfg.ft8.output_gain_offset(), gain_offset - base_gain_offset + play_gain_offset);
    g_ports->audio->player_wait(player);
    g_ports->radio->set_modem(false);
    g_ports->radio->set_freq((int32_t)radio_freq);
    free(samples);
    g_ports->audio->set_play_vol(param_f_get(cfg.audio.play_gain_db()));

    return !aborted;
}

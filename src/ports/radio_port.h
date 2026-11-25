#pragma once

// Radio control port. Implemented by the application (radio.c) and injected
// into libraries that need to observe or drive the transceiver, so those
// libraries never include the application's radio.h.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // True while the radio is in RX (radio_get_state() == RADIO_RX).
    bool (*is_rx)(void);
    void (*set_ptt)(bool on);
    void (*set_freq)(int32_t freq);
    void (*set_modem)(bool on);
    void (*set_pwr)(float w);
    // 1KO125: an app is open on the radio (CAT 7E 05).  May be NULL
    // (never busy).
    bool (*is_busy)(void);
} radio_port_t;

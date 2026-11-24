# 1KO125 – CAT (CI-V) changes

This document describes every change 1KO125 makes to the CAT interface of
the x6100_gui firmware (OpenX6100 / R1CBU), the reasons for each one, and
the exact protocol of the new commands. It is written for CAT client
developers and for the OpenX6100 maintainers who review these changes.

The changes were made for the native macOS client **x6100-mac**, which talks
to the radio over the network (the Icom/wfview LAN protocol, UDP ports
50001–50003), but they apply to every CAT client and every transport where
noted.

| # | Change | Commit | Files |
|---|---|---|---|
| 1 | LAN server accepts 6-byte CI-V frames | `021ffd6` | `src/cat/lan/cat_lan.cpp` |
| 2 | New vendor command `7E 01`: RX filter edges | `021ffd6` | `src/cat/civ_processor.cpp` |
| 3 | `14 09` (CW pitch) no longer reads/writes the TX power | `599bbc3` | `src/cat/civ_processor.cpp` |
| 4 | New vendor command `7E 02`: notch (DNF) | `599bbc3` | `src/cat/civ_processor.cpp` |
| 5 | Network PTT released when the client stops sending audio or goes away | `64ce8be` | `src/cat/lan/cat_lan.cpp` |
| 6 | New vendor command `7E 03`: station identity (read only) | `b00212d` | `src/cat/civ_processor.cpp` |
| 7 | Network PTT keys the modem: the client's audio goes on the air | `b3a115f` | `src/cat/lan/cat_lan.cpp` |
| 8 | New vendor command `7E 04`: TX meter (read only) | `48ae540` | `src/cat/civ_processor.cpp`, `src/tx_info.c` |

All eight are on the `1ko125` branch of TheMurusTeam/x6100_gui. Changes 1–5
have been tested on the radio; changes 6–8 are waiting for their test. None of them changes how an existing command behaves
for existing clients, except the two bug fixes (1 and 3), which make the
radio answer as the serial CAT and the CI-V reference already said it
should.

Conventions used below: the radio presents itself as an IC-705, CI-V
address `A4`; the controller is `E0`. Frames are written as hex bytes,
`FE FE` preamble and `FD` end included. `FB` = OK, `FA` = NG.

---

## 1. LAN: 6-byte CI-V frames are no longer dropped

**Commit:** `021ffd6` · **File:** `src/cat/lan/cat_lan.cpp`, CI-V packet
handling (around line 793)

### Problem

The LAN server only processed CI-V frames of at least 7 bytes:

```cpp
civ_dlen >= FRAME_ADD_LEN + 2      // FRAME_ADD_LEN = 5  ->  7 bytes minimum
```

A CI-V frame without data is 6 bytes long (`FE FE A4 E0 cmd FD`). Every such
command sent over the network was dropped silently, with no answer and no
NG. The serial/USB CAT (`src/cat/cat.cpp`) accepts any frame longer than 5
bytes, so the same commands worked over USB and failed over the network.

Commands affected (all of them are **reads**; no 6-byte command changes the
state of the radio):

| Command | Meaning |
|---|---|
| `03` | read frequency |
| `04` | read mode and filter |
| `07` | VFO |
| `0F` | split |
| `10` | tuning step |
| `11` | read attenuator |

Clients worked around it with `25 00` / `26 00` for frequency and mode, but
the attenuator could not be read over the network at all.

### Change

```cpp
civ_dlen >= FRAME_ADD_LEN + 1      // 6 bytes minimum, as on the serial port
```

The parsing after this check (`CivPacketView`) is the same the serial port
already uses for 6-byte frames, so nothing else had to change.

### Compatibility

Clients that never sent 6-byte frames see no difference. Clients that sent
them now get the answer they get over USB.

### Tests

1. Over the LAN: `FE FE A4 E0 03 FD` → frequency (`FE FE E0 A4 03 …5 BCD… FD`).
2. `FE FE A4 E0 04 FD` → mode and filter; `FE FE A4 E0 11 FD` → `11 00` or `11 20`.
3. wfview over the LAN: frequency, mode and scope work as before.
4. Serial/USB: unchanged (not touched).

---

## 2. New command `7E 01`: RX filter edges in Hz

**Commit:** `021ffd6` · **File:** `src/cat/civ_processor.cpp`,
`handle_1ko_vendor_x7e()` (command constant `C_1KO_VENDOR = 0x7E`,
subcommand `S_1KO_FILTER = 0x01`)

### Why a new command

The radio's RX filter is defined by two edges in audio Hz (low and high),
which is what the filter panel shows. Over CI-V a client could only:

- **read** the width with `1A 03`, as an Icom bandwidth code (50/100/200 Hz
  steps, symmetric around the centre), from which the edges cannot be told;
- **not set** it at all: the `1A 03` write is commented out in
  `civ_processor.cpp` (commit `ed268ae`, Georgy Dyuldin);
- PBT (`14 07` / `14 08`) always answers a fixed `01 28`.

Re-enabling the `1A 03` write was ruled out: it works with Icom's coarse
codes and a symmetric passband, and existing clients rely on its current
behaviour. `0x7E` is not used by Icom for the IC-705 nor by the firmware,
which answered NG to it; no existing client sends it. A new command adds
exact control without changing anything for anyone else.

### Protocol

Both edges are **4 BCD digits, most significant first** (2 bytes each),
range 0000–6000.

**Read**

```
TX:  FE FE A4 E0 7E 01 FD
RX:  FE FE E0 A4 7E 01 LL LL HH HH FD
```

Example, USB with the default filter 50–2950 Hz:
`FE FE E0 A4 7E 01 00 50 29 50 FD`.

**Write**

```
TX:  FE FE A4 E0 7E 01 LL LL HH HH FD
RX:  FE FE E0 A4 FB FD     (accepted)
     FE FE E0 A4 FA FD     (NG)
```

Example, 100–2500 Hz: `FE FE A4 E0 7E 01 01 00 25 00 FD`.

NG when: the payload is not exactly 4 bytes, a nibble is not a decimal
digit, `low >= high`, or `high > 6000`.

### Behaviour per mode

The command writes the same computed parameters the radio's own filter
panel uses, `cfg.filter.low()` and `cfg.filter.high()`
(`src/cfg/settings_manager.cpp`: `cur_filter_*_compute()`,
`cur_filter_*_reverse()`, `filter_low_validate`, `filter_high_validate`).
So the mode's rules are the radio's:

| Mode | Effect of a write |
|---|---|
| USB, LSB, USB-D, LSB-D | low and high are set as given |
| AM, NFM | low stays 0; only high is set (send low < high, e.g. `00 00`) |
| CW, CW-R | the filter stays centred on the key tone; the width is set. If low and high are not symmetric around the key tone, the high edge wins: width = 2 × (high − key tone) |

Values the mode's validation does not allow are clipped by the validation
and answered `FB`; a client should read the edges back after a write.

Each mode has its own filter on the radio: a write changes the filter of
the current mode only.

The two edges are written in the order that never puts low above high on
the way (low first if the new low is below the current high, otherwise high
first, as the FT8 app does); otherwise the validation would clip the first
edge against the old value of the other.

The DSP (`dsp.cpp`) and the filter panel are subscribed to these parameters
and update by themselves.

### BCD note

The two-byte values are decoded by a local helper, `bcd4_be()`.
`from_bcd_be()` reads one byte too many when the number of digits is even.
`src/cat/scope_streamer.cpp` calls `from_bcd_be()` that way (around lines
124, 351, 354); not changed here, to be checked separately.

### Not implemented

An unsolicited (transceive) `7E 01` when the filter changes on the radio.
Clients poll it (x6100-mac reads it once a second).

### Compatibility

Firmware without this command answers NG (`FA`). A client can send `7E 01`
once after connecting and, without an answer in `7E 01` form, fall back to
`1A 03` (x6100-mac then estimates the edges from mode and width and does not
offer filter editing).

### Tests

1. Read in USB with defaults → `7E 01 00 50 29 50`.
2. Write `01 00 25 00` in USB → `FB`; the filter panel shows 100–2500.
3. From 100–2500 write 2600–3000 → accepted, no intermediate clipping.
4. AM: write `00 00 40 00` → high 4000.
5. CW, key tone 600: write `04 00 08 00` → edges 400–800.
6. Bad BCD, `high` 7000, `low >= high` → `FA`.
7. Change the filter on the radio → the next `7E 01` read shows it.

---

## 3. Fix: `14 09` (CW pitch) read and wrote the TX power

**Commit:** `599bbc3` · **File:** `src/cat/civ_processor.cpp`,
`handle_ctl_lvl_x14()`

### Problem

In both the read and the write switch, `case 0x09` (CW key tone) had no
`break` and fell through into `case 0x0a` (TX power):

```cpp
case 0x09: // cw keytone
    to_bcd_be(bcd, x_cw_pitch_2_ci(cfg.cw.key_tone()->get()), 3);
case 0x0a: // Get Tx power        <- 0x09 ended up here too
    to_bcd_be(bcd, std::round(cfg.pwr()->get() * 255 / 10), 3);
    break;
...
case 0x09: // cw keytone
    cfg.cw.key_tone()->set(ci_cw_pitch_2_x(bcd_val));
case 0x0a: // Set Tx power        <- 0x09 ended up here too
    ...
```

Effects:

- **Read:** `14 09` answered with the TX power instead of the pitch.
  At 5 W the answer was `01 28` (128), which a client reads as a 600 Hz
  pitch whatever the real key tone was.
- **Write:** `14 09 xx xx` set the key tone **and the TX power** from the
  same raw value. Any CAT client adjusting the CW pitch changed the output
  power without knowing it.

The same code is in OpenX6100 `main` (around lines 658 and 682).

### How it was found

x6100-mac read the pitch with `14 09` to centre CW filter presets on it.
With a key tone of 700 Hz it read 600 (the power), sent a 500 Hz filter
centred on 600, and the radio, centring on 700, ended up with 300 Hz.

### Change

A `break;` after each of the two `case 0x09` lines. Nothing else.

### Value scale (unchanged)

0–255, 3 BCD digits most significant first; 0 = 300 Hz, 128 ≈ 600 Hz,
255 = 900 Hz (`x_cw_pitch_2_ci`, `ci_cw_pitch_2_x`).

### Tests

1. Key tone 700 Hz, power 5 W: `FE FE A4 E0 14 09 FD` → `14 09 01 70`
   (not `01 28`).
2. Write `14 09 01 28` → key tone 600 on the radio, **power unchanged**.
3. `14 0A` read and write of the power: unchanged.

---

## 4. New command `7E 02`: notch (DNF)

**Commit:** `599bbc3` · **File:** `src/cat/civ_processor.cpp`,
`handle_1ko_dnf()`, called by `handle_1ko_vendor_x7e()` for subcommand
`S_1KO_DNF = 0x02`

### Why

No CI-V command reached the radio's notch: `16 41` (auto notch) and
`16 48` (manual notch) answer NG, and there was nothing for its frequency
and width. A client could not even switch it on. The Icom commands for
position (`14 0D`, 0–255) and width (`16 57`, three steps) would not match
the radio's settings, so a vendor command sets the notch exactly as the
radio's own controls do.

### Parameters

The same parameters and ranges as on the radio (`settings_manager.h`,
`controls.cpp`):

| Field | Parameter | Values | Step of the radio's encoder |
|---|---|---|---|
| ON | `cfg.dsp.dnf()` | 00 / 01 | — |
| AU (AUTO) | `cfg.dsp.dnf_auto()` | 00 / 01 | — |
| Centre | `cfg.dsp.dnf_center()` | 100–3000 Hz | 50 Hz |
| Width | `cfg.dsp.dnf_width()` | 10–100 Hz | 5 Hz |

They are global, not per mode.

### Protocol

Centre and width are **4 BCD digits, most significant first** (2 bytes each).

**Read**

```
TX:  FE FE A4 E0 7E 02 FD
RX:  FE FE E0 A4 7E 02 ON AU CC CC WW WW FD
```

Example with the defaults (off, centre 1000, width 50):
`FE FE E0 A4 7E 02 00 00 10 00 00 50 FD`.

**Write** — always all four fields (a client sends back the ones it does
not change, as read):

```
TX:  FE FE A4 E0 7E 02 ON AU CC CC WW WW FD
RX:  FB, or FA (NG)
```

Example, notch on at 1500 Hz, width 30 Hz: `FE FE A4 E0 7E 02 01 00 15 00 00 30 FD`.

NG when: the payload is not exactly 6 bytes, ON or AU is not 00/01, a
nibble is not a decimal digit, the centre is outside 100–3000 or the width
outside 10–100. Multiples of the encoder steps are **not** required (the
parameter accepts any value in range); x6100-mac sends multiples of 50 and 5.

Write order: centre and width first, then AUTO, then ON, so the notch comes
on where it belongs. The radio's observers (`radio.c`) pass the values to
the base, and the spectrum and AF scopes redraw the notch.

### Decoder apps open

With SSTV (or another decoder app) open, NR, NB and the notch are held off
(commit `ac0313d`, `dsp_held_cb`). A `7E 02` received then behaves like a
change from the front panel: the notch stays off, the values are stored and
come back when the app closes.

### How the radio draws the notch (for clients that draw it)

`src/spectrum.c`, `update_dnf()`: a white band at 50 % over the whole
height, from `centre − width` to `centre + width` relative to the dial
(negative side in LSB, LSB-D and CW-R). It is not drawn when the notch is
off, when AUTO is on (its place is not known) and in AM/NFM.

### Open question: is the width full or half?

Two parts of the firmware disagree:

- `src/spectrum.c` (original code) draws `centre ± width`, treating
  `dnf_width` as a half width;
- `src/af_scope.c` (1KO125 code) draws `centre ± width/2`, treating it as
  the full width.

One of them draws the notch twice as wide as it is. The base library
(`x6100_control`) is binary only, so it has to be measured: a steady tone in
the passband, manual notch with width 100 Hz, centre moved in 50 Hz steps;
if the tone disappears within ±50 Hz of the centre the width is the full
width, within ±100 Hz it is the half width. Until then x6100-mac draws it
like `spectrum.c`.

### Tests

1. Read with defaults → `7E 02 00 00 10 00 00 50`.
2. Write `01 00 15 00 00 30` → DNF on, "DNF freq: 1500 Hz", "DNF width:
   30 Hz" on the radio; the notch moves on the spectrum.
3. Write with AU = 01 → AUTO on, notch no longer drawn.
4. NG: centre 50 or 3100, width 5 or 150, ON = 02, bad BCD, short frame.
5. Change the notch on the radio → the next `7E 02` read shows it.
6. With SSTV open: `7E 02` with ON = 01 → `FB`, notch stays off; after
   closing SSTV the notch comes on with the values sent.

---

## 5. Network PTT: released when the client stops sending audio or goes away

**Commit:** `64ce8be` · **File:** `src/cat/lan/cat_lan.cpp`
(`lan_ptt_note()`, `lan_ptt_watchdog()`, `lan_ptt_release()`)

### Background: phone over the network

A network client transmits with what the firmware already had:

- **PTT:** `1C 00 01` / `1C 00 00` (`handle_ctl_ptt_x1c()` →
  `radio->set_ptt()`); the answer to a write is `1C 00 FB`. `1C 00` reads
  the state: `1C 00 00` (RX) or `1C 00 01` (TX).
- **TX audio:** audio packets sent by the client to port 50003 (`ident`
  `0x0080`, the same layout the radio uses for RX audio: header 0x18 bytes,
  `sendseq` big-endian at 0x12, `datalen` big-endian at 0x16, then 16-bit
  little-endian mono PCM) go to `wfview_player` (`player_send`), created at
  the sample rate the client asked for receive audio.

### Problem

If the client vanished while the radio was transmitting (Mac asleep, app
hung or closed badly, Wi-Fi lost), **the radio stayed in TX for good**: on
ping timeout (`PING_TIMEOUT_SEC` = 15 s), on `CTL_TYPE_CLOSE` (control and
audio port) and when the server stopped, the session was closed but the PTT
was never released. In SSB the radio kept transmitting nothing; in AM/FM it
transmitted the carrier.

### Change

1. **PTT released when the network session ends.** On ping timeout, on
   `CTL_TYPE_CLOSE` on the control port and on the audio port, and in
   `cat_lan_shutdown()`: if the PTT was keyed from the network and the radio
   is still transmitting, `set_ptt(false)`, with a log line giving the
   reason.
2. **Audio watchdog, 3 seconds.** If the PTT was keyed from the network and
   no TX audio packet has come from the client for 3 s
   (`LAN_TX_AUDIO_TIMEOUT`), the PTT is released
   (log: `LAN: TX audio stopped for 3 s, PTT released`). Packets count, not
   silence: a client keeps sending packets through the pauses of speech.
3. **Only for a PTT keyed from the network.** A PTT from the microphone, the
   radio's keys, its own apps (FT8, voice messages…) or the serial/USB CAT is
   never touched.

### How it works

- `lan_ptt_note()`: before each CI-V frame from the LAN client is
  processed, an 8-byte `1C 00 01` arms the watchdog (`lan_ptt_active`, the
  time it was keyed, last audio = now); `1C 00 00` disarms it. A repeated
  `1C 00 01` while armed does not restart the 3 s.
- `process_audio_packet()`: every valid audio packet from the client sets
  `lan_last_tx_audio = now`.
- `lan_ptt_watchdog()`, called from the main loop (poll ≤ 100 ms):
  - when the radio is seen in TX, it notes that the transmission started;
  - if the radio is back in RX after that (released some other way), or does
    not go into TX within 2 s (`LAN_PTT_TX_WAIT`, keying refused), the
    watchdog disarms, so that a later microphone PTT is never released by it;
  - if 3 s have passed since the last audio packet, the PTT is released.
- `lan_ptt_release()`: releases only if the PTT was keyed from the network
  and the radio is still in TX.
- Threads: everything runs in the LAN thread (`set_ptt()` is already called
  from the CAT thread in `handle_ctl_ptt_x1c()`); `cat_lan_shutdown()` calls
  it after joining the thread.

### What a client should do

- Send audio continuously while transmitting (x6100-mac: 20 ms packets of
  320 samples at 16 kHz), including during pauses.
- On release, wait for the audio already sent to reach the radio (about
  0.2 s) before `1C 00 00`, or the end of the last word is cut.
- Keep its own safeguards too (x6100-mac: 3-minute limit, release on
  disconnect, quit and sleep).

### Tests

1. TX over the network, then unplug the network / switch off the Mac's
   Wi-Fi → the radio is back in RX within 3 s (not after 15 s).
2. TX over the network, then close the client normally → RX at once.
3. TX with continuous audio for more than 3 s, speech with pauses → the
   radio stays in TX.
4. `1C 00 01` without sending audio → back in RX after 3 s.
5. Microphone PTT with a network client connected and idle → never released
   after 3 s.
6. The radio's FT8 and voice messages with a network client connected →
   normal TX.
7. Serial/USB CAT: `1C 00 01` without audio → no watchdog (unchanged).

---

## 6. New command `7E 03`: station identity (read only)

**Commit:** `b00212d` · **File:** `src/cat/civ_processor.cpp`,
`handle_1ko_station()`, called by `handle_1ko_vendor_x7e()` for subcommand
`S_1KO_STATION = 0x03`

### Why

A client's macros (`<MYCALL>`, `<MYLOC>`, `<MYQTH>`, `<MYNAME>` in
x6100-mac, for RTTY now and FT8 and SSTV later) need the station's
identity. It is already set on the radio, for its keyboard modes and JS8,
and no CI-V command could read it. With `7E 03` there is one source: the
user sets it on the radio and the client reads it.

### Fields

The existing global settings (`src/cfg/settings_manager.h`):

| NN | Parameter | Contents |
|---|---|---|
| `00` | `p_callsign` | callsign |
| `01` | `p_qth` | locator (e.g. JN45) |
| `02` | `p_op_qth` | operator's town |
| `03` | `p_op_name` | operator's name |

### Protocol: one field per request

**Read**

```
TX:  FE FE A4 E0 7E 03 NN FD
RX:  FE FE E0 A4 7E 03 NN <text> FD
```

- `<text>` is the stored string as UTF-8 bytes, with no terminator and no
  padding; its length is implied by the `FD`.
- An empty field is answered `FE FE E0 A4 7E 03 NN FD` (no text bytes): it
  is not an error.
- **At most 32 bytes.** A longer string is cut at 32 bytes without
  splitting a UTF-8 character (31 bytes if a 2-byte character would cross
  the limit). The longest answer is 8 + 32 = 40 bytes.
- `FD`, `FE`, `FF` and control bytes (`< 0x20`, `0x7F`) are replaced by
  `?`. Valid UTF-8 never contains `FD`–`FF`, but a stray byte in the
  setting must never end or break the frame.

Example, callsign `IK1ABC`: `FE FE A4 E0 7E 03 00 FD` →
`FE FE E0 A4 7E 03 00 49 4B 31 41 42 43 FD`.

**NG (`FA`)** when there is no `NN`, `NN` is above `03`, or any byte
follows `NN`: that would be a write, and the command is read only. The
identity is changed on the radio only.

### Why one field per request

The CI-V answer buffer is 64 bytes, both on the serial port (`cat.cpp`) and
on the LAN (`cat_lan.cpp`), and `CivTxPacker::append_data_imp()` copies
without checking the length. Four strings in one frame could overflow it;
one field of at most 32 bytes cannot.

### Implementation notes

- The strings are read with `Parameter<std::string>::get()`, which copies
  the value under the parameter's own mutex, so reading from the CAT thread
  is safe.
- No transceive notification: x6100-mac reads the four fields when it
  connects and when it opens a digital mode.

### Compatibility

Firmware without this command answers NG (`FA`); a client then uses its own
settings (x6100-mac does).

### Tests

1. Callsign `IK1ABC` on the radio: `7E 03 00` → `7E 03 00 49 4B 31 41 42 43`.
2. Locator `JN45`: `7E 03 01` → `7E 03 01 4A 4E 34 35`.
3. Empty town: `7E 03 02` → `7E 03 02` with no text.
4. A name with an accent (`Niccolò`) → correct UTF-8 bytes (`C3 B2` for `ò`).
5. A 40-character string → 32 bytes; with a 2-byte character across the
   32nd byte → 31 bytes.
6. `7E 03 04` → NG; `7E 03` with no field → NG; `7E 03 00 41` → NG and
   **no** change on the radio.
7. Change the callsign on the radio → the next read shows it.
8. Same answers over the LAN and the serial/USB port.
9. `7E 01`, `7E 02` and the Icom commands: unchanged.

---

## 7. Network PTT keys the modem: the client's audio goes on the air

**Commit:** `b3a115f` · **File:** `src/cat/lan/cat_lan.cpp`
(`lan_ptt_write()`, `lan_tx_off()`, with the change 5 functions)

### Problem

Found on a dummy load: a network client transmitting in USB with its
microphone muted still put out about 1.7 W of 5 W, while the radio's own
PTT in silence put out nothing.

`1C 00 01` from the LAN went to `handle_ctl_ptt_x1c()` → `set_ptt(true)` →
`x6100_control_ptt_set()`, which keys the BASE with **the radio's own
microphone input** (built-in or handset, with their gains). The client's
audio goes to `wfview_player`, i.e. the codec, and the codec reaches the
modulator only with `x6100_control_modem_set()`, as for the radio's voice
messages, its FT8 transmitter and the Bluetooth headset (see the comment at
the top of `src/bt_audio.c`).

So over the network the radio's microphone went on the air, never the
client's audio. Voice from the client could not be heard, and RTTY, FT8 or
SSTV from a network client could not work. wfview over the LAN has the same
problem with OpenX6100.

### Change

Over the network the audio always comes from the client, so the PTT writes
from the LAN client are handled in `cat_lan.cpp`, before the CI-V
processor:

| Frame from the LAN client | Before | Now |
|---|---|---|
| `1C 00 01` | `set_ptt(true)`: radio microphone | `set_modem(true)`: client audio (codec) |
| `1C 00 00` | `set_ptt(false)` | `set_modem(false)` if the network keyed the modem, otherwise `set_ptt(false)` as before |
| `1C 00` (read) | `is_rx()` | unchanged |

The answer to a write is the same as before, `1C 00 FB`.

The releases of change 5 (3-second audio watchdog, `CTL_TYPE_CLOSE` on the
control and audio ports, ping timeout, `cat_lan_shutdown()`) now withdraw
the modem request, even when the radio is already back in RX. The watchdog
does the same when the radio went back to RX some other way or did not go
into TX within 2 s, so a stale modem request can never key the radio again.

Record mode (`audio_set_play_mode(AUDIO_PLAY_ON)`, the `x6100_voice_rec`
bit) is **not** set: with it the radio did not go into TX (`bt_audio.c`).

### Unchanged

- The serial/USB CAT: `1C 00 01` still keys with `set_ptt()`, i.e. the
  radio's microphone; a program there may want it.
- A PTT from the microphone, the radio's keys or its own apps.
- Reading `1C 00`.

### Tests (dummy load, power 5 W)

1. Client in USB, voice TX with its microphone muted → about 0 W (no longer
   1.7 W).
2. Speaking into the client's microphone → power follows the voice;
   speaking close to the radio's built-in microphone → nothing goes on the
   air.
3. A steady tone from the client → steady power, ALC moving as with the
   radio's own FT8.
4. Change 5 still works: Wi-Fi off during TX → RX within 3 s; client closed
   normally → RX at once.
5. The radio's microphone PTT with the client connected → the radio's
   microphone goes on the air, as before.
6. The radio's FT8, voice messages and RTTY with the client connected →
   unchanged.
7. Serial/USB CAT: `1C 00 01` → the radio's microphone, as before.
8. wfview over the LAN: its TX audio now goes on the air.

---

## 8. New command `7E 04`: TX meter (read only)

**Commit:** `48ae540` · **Files:** `src/cat/civ_processor.cpp`
(`handle_1ko_txmeter()`, subcommand `S_1KO_TXMETER = 0x04`),
`src/tx_info.c` (`tx_info_raw()`), `src/ports/telemetry_port.h`,
`src/app_ports.cpp`

### Why

A client should show power, SWR and ALC during TX, as the radio's display
does. The Icom commands exist but cannot be used (`handle_rd_sqsm_x15()`):

- **`15 11` (power):** a value is computed (`-pwr*pwr + 35*pwr`) but the
  answer carries **no data** (`append_data` is missing); and the value is a
  `uint8_t`, which overflows above about 8 W.
- **`15 12` (SWR):** `-21*swr*swr + 134*swr - 122` is −9 at SWR 1.0; stored
  in a `uint8_t`, a perfect antenna reads as a very high SWR.
- **`15 13` (ALC):** works (`alc × 12`, 0–120), with Icom's coarse scale.

`15 11` and `15 12` are **not fixed**, by the author's choice: only other
programs use them, and their behaviour is left as it is. A vendor command,
next to `7E 01`–`7E 03`, gives exact values instead.

### Protocol

```
TX:  FE FE A4 E0 7E 04 FD
RX:  FE FE E0 A4 7E 04 TX PP PP SS SS AA AA FD
```

| Field | Contents | Format |
|---|---|---|
| `TX` | `01` in TX (`!is_rx()`), `00` in RX | byte |
| `PP PP` | power in **tenths of a watt** (5.0 W → `00 50`) | 4 BCD digits, most significant first, 0000–9999 |
| `SS SS` | SWR in **hundredths** (1.35 → `01 35`) | 4 BCD digits, 0000–9999 |
| `AA AA` | ALC in **tenths**, as the radio shows it (2.5 → `00 25`) | 4 BCD digits, 0000–0100 |

Example, in TX at 5.0 W, SWR 1.35, ALC 2.5:
`FE FE E0 A4 7E 04 01 00 50 01 35 00 25 FD` (14 bytes).

- **Values:** the last ones from the BASE as they came
  (`radio.c` → `tx_info_update()`): **not** low-passed (the radio's display
  smooths them in the non-digital modes) and the SWR **not** cut at 5 (the
  display's limit; a cut would hide a missing antenna). The ALC is the one
  the display shows, `10 − value from the BASE`. Smoothing and peak hold are
  the client's job, as for the S-meter.
- **In RX:** `TX = 00` and the last values of the previous TX, not zeroed,
  so a client can show the SWR just measured. Before any TX: all zero.
- **Rounding** to the nearest tenth or hundredth; above the maximum →
  9999 (0100 for the ALC); negative → 0000.

**NG (`FA`)** when any byte follows `04`: the command is read only.

### Implementation notes

- `tx_info_update()` runs in the radio thread; the CAT and LAN threads read.
  It now stores the three raw values together under a mutex, and
  `tx_info_raw()` reads them together under the same mutex, so the three
  values in an answer always belong to the same packet from the BASE.
- `tx_info_refresh()` is not used: with a `msg_id` shared between callers
  it returns nothing when the message has not changed, while `7E 04` must
  always answer the last values.
- The radio's display, `15 11`, `15 12` and `15 13` are unchanged.

### Use

x6100-mac reads `7E 04` about 10 times a second during TX only, plus once
right after going back to RX.

### Compatibility

Firmware without this command answers NG (`FA`); a client then shows no TX
meter.

### Tests (dummy load, power set to 5 W)

1. In RX before any TX: `7E 04` → `7E 04 00 00 00 00 00 00 00`.
2. Steady carrier (RTTY) from the client: `TX = 01`, power about `00 50`,
   SWR as on the radio's display (e.g. `01 10`), ALC equal to the display.
3. SSB speaking: the power follows the voice (raw values, not smoothed).
4. Back in RX: `TX = 00` and the last values of the TX.
5. Antenna disconnected, minimum power, for an instant: SWR **above 5**.
6. `7E 04 00` → NG, nothing changes.
7. Same answers over the LAN and the serial/USB port.
8. `7E 01`–`7E 03`, `15 02` (S-meter) and the radio's TX display unchanged.

---

## Notes for client developers: commands used as they are

Not changed by 1KO125, listed because x6100-mac relies on them and their
behaviour is easy to get wrong.

| Command | Notes |
|---|---|
| `25 00` / `26 00` | Frequency and mode + data flag of the selected VFO. Work with every firmware over the LAN (unlike `03`/`04` before change 1). |
| `11` | Attenuator: read → `11 00` / `11 20` (`att × 0x20`); write `11 00` off, `11 20` on. The value goes through `cfg.cur.att()->set()`, which keeps it within 0…`x6100_att_on`, so `0x20` switches it on. Over the LAN the read needs change 1. |
| `14 02` / `14 03` | RF gain, squelch: 0–255 over CI-V; the radio stores `value × 100 / 255` rounded down. |
| `15 02` | S-meter: `value = dBm × 0.75 + 96`, 3 BCD digits most significant first. |
| `15 11` / `15 12` | Power and SWR: **broken, not fixed** (no data in the `15 11` answer, `uint8_t` overflow above ~8 W; `15 12` reads very high at SWR 1.0). Use `7E 04` (change 8). |
| `16 02` / `16 22` / `16 40` | Preamp, NB, NR. |
| `1A 03` | Filter width as an Icom code, read only. SSB/CW: code ≤ 9 → code × 50 + 50 Hz, otherwise code × 100 − 400; AM/FM: code × 200 + 200. |
| `27 15 00` + 5 BCD (least significant first) | Scope span = half width in Hz; the radio sets zoom = round(50 kHz / span), kept to 1, 2, 4, 8 (per mode). The zoom in use can be read from the scope frames (span = 100 kHz / zoom). |
| `1C 00` | PTT, see changes 5 and 7. Over the LAN it keys the modem (client audio); over the serial/USB port it keys the radio's microphone. |

---

## Status and upstream

- On `1ko125`: all eight changes; 1–5 tested on the radio, 6–8 waiting for
  their test.
- Pull request to OpenX6100: **not opened yet**. It will contain all eight
  changes; its description should give the reasons above: LAN/serial parity
  (only read commands unblocked), vendor commands so that no existing Icom
  command changes behaviour, the `14 09` bug that read and wrote the TX
  power, the radio staying in TX when a network client disappears, the
  station identity read from the radio by the clients' macros, and the
  network PTT that put the radio's microphone on the air instead of the
  client's audio (also a bug for wfview with OpenX6100), and a TX meter
  because `15 11` and `15 12` cannot be used (left unfixed by choice).
- Open items: width of the notch (full or half, change 4); `from_bcd_be()`
  with an even number of digits in `scope_streamer.cpp` (change 2).

Hany El Imam — 1KO125

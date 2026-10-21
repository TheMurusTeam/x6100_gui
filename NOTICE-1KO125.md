# 1KO125 – License notice

1KO125 is a firmware for the Xiegu X6100 based on R1CBU x6100_gui 1.0.2.

## Licenses

| Part | Author | License |
|---|---|---|
| R1CBU x6100_gui (the base firmware), including the R1CBU files changed by 1KO125 | Belousov Oleg aka R1CBU and contributors | LGPL 2.1 or later (see `LICENSE`) |
| WEFAX, NAVTEX/SITOR-B, DSC, SSTV and FT8 CB applications, channel list, Info window, shared widgets and the build scripts (`src/dialog_wefax*`, `src/wefax/`, `src/dialog_navtex*`, `src/navtex/decoder.*`, `src/navtex/bulletin.*`, `src/dialog_dsc*`, `src/dsc/` except `dsc_qrcodegen.*`, `src/dialog_sstv*`, `src/sstv/`, `src/sstv_resources.*`, `src/dialog_ft8cb.*`, `src/ft8cb/`, `src/channels.*`, `src/dialog_channels.*`, `src/broadcast_db.*`, `src/ui_list.*`, `src/diag_log.*`, `src/app_info.*`, `buildroot/make_sdcard.sh`, `buildroot/docker/`), and the SSTV denoiser model `rootfs/usr/share/x6100/sstv_dnn*.bin` | Hany El Imam (`dialog_ft8cb.*` is based on R1CBU `dialog_ft8.*`) | LGPL 2.1 or later (see `LICENSE`) |
| SITOR-B demodulator (`src/navtex/navtex_rx.*`, `fftfilt.*`, `complex.h`, `misc.h`, `gfft.h`) | fldigi (W1HKJ and contributors, after JNX by Paul Lutus), adapted by Franco Venturi | GPL 3.0 or later (`gfft.h`: LGPL 3.0 or later) |
| QR code generator (`src/dsc/dsc_qrcodegen.*`) | Project Nayuki | MIT |
| JSON parser (`src/cJSON.*`) | Dave Gamble and cJSON contributors | MIT |

## The firmware as a whole

The 1KO125 files are under the LGPL 2.1 or later, like R1CBU x6100_gui.

The SITOR-B demodulator taken from fldigi is under the GPL 3.0 or later.
**While it is part of the program, the firmware as a whole (the compiled
program and the SD card image) is distributed under the terms of the GNU
General Public License, version 3 or later** (see `COPYING`); the LGPL 2.1 of
the other files allows this (LGPL 2.1, section 3). It is going to be
replaced by a new demodulator under the LGPL 2.1; from then on the whole
firmware is under the LGPL 2.1 or later.

Each source file keeps the license written in its header.

## Warranty

This program comes with ABSOLUTELY NO WARRANTY, to the extent permitted by
applicable law. See `LICENSE`, sections 15 and 16, and `COPYING`, sections
15 and 16.

## On the radio

The full text of the GNU GPL v3 is installed on the SD card as
`/usr/share/x6100/COPYING`.

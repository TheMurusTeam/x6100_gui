# 1KO125 – License notice

1KO125 is a firmware for the Xiegu X6100 based on R1CBU x6100_gui 1.0.2.

## Licenses

| Part | Author | License |
|---|---|---|
| R1CBU x6100_gui (the base firmware), including the R1CBU files changed by 1KO125 | Belousov Oleg aka R1CBU and contributors | LGPL 2.1 or later (see `LICENSE`) |
| WEFAX, NAVTEX/SITOR-B, DSC, SSTV and FT8 CB applications, channel list, Info window, shared widgets, the build scripts and the test tools (`src/dialog_wefax*`, `src/wefax/`, `src/dialog_navtex*`, `src/navtex/`, `src/dialog_dsc*`, `src/dsc/` except `dsc_qrcodegen.*`, `src/dialog_sstv*`, `src/sstv/`, `src/sstv_resources.*`, `src/dialog_ft8cb.*`, `src/ft8cb/`, `src/channels.*`, `src/dialog_channels.*`, `src/broadcast_db.*`, `src/ui_list.*`, `src/diag_log.*`, `src/app_info.*`, `buildroot/make_sdcard.sh`, `buildroot/docker/`, `tests/navtex/` except `tests/navtex/old/`), and the SSTV denoiser model `rootfs/usr/share/x6100/sstv_dnn*.bin` | Hany El Imam (`dialog_ft8cb.*` is based on R1CBU `dialog_ft8.*`) | LGPL 2.1 or later (see `LICENSE`) |
| QR code generator (`src/dsc/dsc_qrcodegen.*`) | Project Nayuki | MIT |
| JSON parser (`src/cJSON.*`) | Dave Gamble and cJSON contributors | MIT |
| Previous SITOR-B demodulator, kept only to compare it with the new one in the NAVTEX test tools (`tests/navtex/old/`); it is not part of the firmware | fldigi (W1HKJ and contributors, after JNX by Paul Lutus), adapted by Franco Venturi | GPL 3.0 or later (`tests/navtex/old/COPYING`; `gfft.h`: LGPL 3.0 or later) |

## The firmware as a whole

The firmware (the compiled program and the SD card image) is distributed
under the terms of the **GNU Lesser General Public License, version 2.1 or
later**.

Up to 1.0.2 beta 5 the NAVTEX application used a SITOR-B demodulator taken
from fldigi, under the GPL 3.0, and the firmware as a whole was under the
GPL 3.0. From 1.0.2 beta 6 it uses a new SITOR-B receiver,
`src/navtex/sitorb.c`, written from the ITU-R recommendations M.476, M.625
and M.540.

Each source file keeps the license written in its header.

## Warranty

This program comes with ABSOLUTELY NO WARRANTY, to the extent permitted by
applicable law. See `LICENSE`, sections 15 and 16.

## On the radio

The full text of the GNU LGPL 2.1 is installed on the SD card as
`/usr/share/x6100/LICENSE`.

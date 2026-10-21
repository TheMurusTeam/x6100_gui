# 1KO125 – License notice

1KO125 is a firmware for the Xiegu X6100 based on R1CBU x6100_gui 1.1.0.

## Licenses

| Part | Author | License |
|---|---|---|
| R1CBU x6100_gui (the base firmware), including the R1CBU files changed by 1KO125 | Belousov Oleg aka R1CBU and contributors | LGPL 2.1 or later (see `LICENSE`) |
| WEFAX, NAVTEX/SITOR-B, DSC, SSTV and FT8 CB applications, channel list, Info window, shared widgets, the build scripts and the test tools (`src/dialog_wefax*`, `src/wefax/`, `src/dialog_navtex*`, `src/navtex/`, `src/dialog_dsc*`, `src/dsc/` except `dsc_qrcodegen.*`, `src/dialog_sstv*`, `src/sstv/`, `src/sstv_resources.*`, `src/dialog_ft8cb.*`, `src/ft8cb/`, `src/channels.*`, `src/dialog_channels.*`, `src/broadcast_db.*`, `src/ui_list.*`, `src/diag_log.*`, `src/app_info.*`, `buildroot/make_sdcard.sh`, `buildroot/docker/`, `tests/navtex/`, `tests/dsc/`), and the SSTV denoiser model `rootfs/usr/share/x6100/sstv_dnn*.bin` | Hany El Imam (`dialog_ft8cb.*` is based on R1CBU `dialog_ft8.*`) | LGPL 2.1 or later (see `LICENSE`) |
| QR code generator (`src/dsc/dsc_qrcodegen.*`) | Project Nayuki | MIT |
| JSON parser (`src/cJSON.*`) | Dave Gamble and cJSON contributors | MIT |
| JS8 engine (`third-party/js8core`), part of R1CBU x6100_gui 1.1.0 | JS8Call-improved/Android-port | GPL 3.0 (see `third-party/js8core/LICENSE`) |
| Eigen and CRC++, brought by js8core (`third-party/js8core/vendor/`) | their authors | MPL 2.0 and BSD-3-Clause |

## The firmware as a whole

Since R1CBU x6100_gui 1.1.0 the GUI program includes the JS8 engine, under
the GPL 3.0. As R1CBU does, the GUI program (`x6100_gui`, with the 1KO125
applications) is therefore distributed, as a whole, under the terms of the
**GNU General Public License, version 3**. The other software on the SD card
(the Linux kernel, the libraries) keeps its own license.

The 1KO125 files are not changed by this: each of them stays under the
**GNU LGPL 2.1 or later**, and can be reused under that license on its own.
Each source file keeps the license written in its header.

Up to 1.0.2 beta 5 the NAVTEX application used a SITOR-B demodulator taken
from fldigi, under the GPL 3.0. From 1.0.2 beta 6 it uses a new SITOR-B
receiver, `src/navtex/sitorb.c`, written from the ITU-R recommendations
M.476, M.625 and M.540.

## Warranty

This program comes with ABSOLUTELY NO WARRANTY, to the extent permitted by
applicable law. See `LICENSE`, sections 15 and 16.

## On the radio

The full texts are installed on the SD card: the GNU LGPL 2.1 as
`/usr/share/x6100/LICENSE`, the GNU GPL 3.0 as
`/usr/share/x6100/COPYING-GPL3`.

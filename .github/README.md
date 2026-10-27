<h1 align="center">The home of SSTV, WEFAX, DSC and NAVTEX/SITOR-B<br>for the Xiegu X6100</h1>

<p align="center">
This is where these applications were born, and where they keep growing:
better receivers and new features, release after release.<br>
They are free software, written to be shared: the goal is to see them in
every X6100 firmware, so that every owner of this radio can use them.
</p>

---

## 1KO125 firmware for the Xiegu X6100

An alternative firmware for the Xiegu X6100, based on
[R1CBU x6100_gui](https://github.com/OpenX6100/x6100_gui) (the LVGL GUI of
the X6100 by Oleg Belousov R1CBU, Georgy Dyuldin R2RFE and contributors),
with receiving and digital-mode applications added by Hany El Imam 1KO125.

The original README of R1CBU x6100_gui is [README.md](../README.md).

## What it adds

| Application | |
|---|---|
| **WEFAX** | Weather fax receiver: Auto RX on the start and stop tones, station list with schedules, Align and Tilt (remembered per station, Auto Slant), Denoise for fading signals, **Post Proc.** (each saved fax also cleaned by a neural network trained on weather charts), Fax Browser with a full-screen view |
| **NAVTEX** | SITOR-B receiver written from the ITU-R recommendations, saved messages and bulletins |
| **DSC** | Digital Selective Calling receiver (GMDSS): calls in plain words, ship and coast station names |
| **SSTV** | Receive and transmit, Auto RX, picture denoiser (classic or a neural network), reply templates |
| **FT8 CB** | FT8 on 11 metres with CB callsigns, works with WSJT-CB and standard FT8 stations |
| **Channel List** | Memory channels and a broadcast stations database |

Each application has an **Info** window on the radio that explains it.

## Installing

1. Download `sdcard-1KO125-<version>.img.zip` from
   [Releases](https://github.com/TheMurusTeam/x6100_gui/releases).
2. Write it to a microSD card with balenaEtcher or Rufus.
3. Insert the card in the radio and switch it on.

The radio keeps its original Xiegu BASE firmware: the SD card does not
change it. Without the card the radio starts as before.

## Building

The SD card image is built by GitHub for every release; to build it
yourself see [buildroot/README-1KO125.md](../buildroot/README-1KO125.md).

## License

- The 1KO125 files are under the **GNU LGPL 2.1 or later**, as R1CBU
  x6100_gui; each file says it in its header.
- Since R1CBU x6100_gui 1.1.0 the program includes the JS8 engine (GPL 3),
  so the program as a whole is distributed under the **GNU GPL 3**.

Details, third-party parts and their licenses:
[NOTICE-1KO125.md](../NOTICE-1KO125.md).

## Credits

- Oleg Belousov R1CBU, Georgy Dyuldin R2RFE and the contributors of
  [x6100_gui](https://github.com/OpenX6100/x6100_gui) and
  [AetherX6100Buildroot](https://github.com/OpenX6100/AetherX6100Buildroot).
- The CB callsign rules and CB message sequence of FT8 CB follow
  [WSJT-CB](https://github.com/vash909/WSJT-CB) (Lorenzo 1AT106 / IU1BOT and
  contributors).
- [ft8_lib](https://github.com/kgoba/ft8_lib) by Karlis Goba YL3JG.

Hany El Imam 1KO125 - [www.hanynet.com](https://www.hanynet.com) -
[github.com/TheMurusTeam](https://github.com/TheMurusTeam)

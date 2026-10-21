# NAVTEX decoder tests

Tools to test the SITOR-B receiver of the NAVTEX application
(`src/navtex/sitorb.c`) on a computer (Linux or macOS):

    make
    ./run_tests.py            # synthetic signals, new and previous decoder
    ./navtex_wav REC.wav      # a recording, any sample rate, 16-bit PCM
    ./navtex_wav REC.wav --quality

| Tool | What it does |
|---|---|
| `sitorb_gen OUT.wav [--snr DB] [--off HZ] [--ppm N] [--fade S] [--rate HZ] [--rev] < text` | a NAVTEX message sent in SITOR-B (phasing, DX/RX repetition, idle alpha), with noise in 2500 Hz, frequency offset, clock error, fading |
| `navtex_wav IN.wav` | decodes a recording with the firmware decoder; resamples to 12000 Hz; `--quality` writes the quality of each character after it (0 clean, 1 from the repetition, 2 guessed, 3 lost) |
| `navtex_wav_old IN.wav` | the same with the previous decoder (fldigi demodulator, GPL 3, in `old/`), only for comparison |
| `run_tests.py` | the same messages through both decoders at several signal to noise ratios: character error rate |

The previous decoder in `old/` is not part of the firmware; it is kept
here until the new one has been compared with it on real recordings.

# WEFAX decoder tests

A tool to run a recording through the WEFAX decoder (`src/wefax/decoder.c`)
on a computer (Linux or macOS):

    make
    ./wefax_wav REC.wav old.pgm              # phase discriminator
    ./wefax_wav REC.wav new.pgm --fading     # Fading demodulator

`--cont` decodes from the beginning of the recording (Manual RX) when the
start tone is not in it. The picture is written as PGM, 1809 pixels wide,
without the slant correction of the radio; any image viewer opens it.

The Fading demodulator (button "Fading" on page 4 of the WEFAX window) was
tuned on recordings of GYA Northwood: with selective fading it removes most
of the black streaks of the white areas, with a steady signal it gives a
cleaner background and slightly lighter thin lines.

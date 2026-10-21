# WEFAX decoder tests

A tool to run a recording through the WEFAX decoder (`src/wefax/decoder.c`)
on a computer (Linux or macOS):

    make
    ./wefax_wav REC.wav old.pgm              # phase discriminator
    ./wefax_wav REC.wav new.pgm --denoise    # Denoise ON

`--cont` decodes from the beginning of the recording (Manual RX) when the
start tone is not in it. The picture is written as PGM, 1809 pixels wide,
without the slant correction of the radio; any image viewer opens it.

Denoise (button on page 4 of the WEFAX window, ON by default) was tuned on
recordings of GYA Northwood and DWD: with selective fading it removes most
of the black streaks of the white areas; the sharpening that follows gives
thin lines and text the contrast of the classic receiver.

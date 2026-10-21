# SPDX-License-Identifier: LGPL-2.1-or-later
# NAVTEX decoder test tools, built on a computer (Linux, macOS):
#   make            navtex_wav, navtex_wav_old, sitorb_gen
#   make test       synthetic signals at several SNR, new and old decoder
NAV = ../../src/navtex
CFLAGS = -O2 -Wall -Wextra -I$(NAV)
CXXFLAGS = -O2 -I$(NAV) -Iold

all: navtex_wav navtex_wav_old sitorb_gen

navtex_wav: navtex_wav.c $(NAV)/sitorb.c $(NAV)/sitorb.h
	$(CC) $(CFLAGS) -o $@ navtex_wav.c $(NAV)/sitorb.c -lm

# the previous decoder (fldigi SITOR-B demodulator, GPL 3, in old/), only
# for comparison: it is not part of the firmware any more
navtex_wav_old: navtex_wav.c
	$(CC) $(CFLAGS) -DOLD_DECODER -c -o navtex_wav_old.o navtex_wav.c
	$(CXX) $(CXXFLAGS) -o $@ navtex_wav_old.o old/old_decoder.cpp old/navtex_rx.cpp old/fftfilt.cxx -lm

sitorb_gen: sitorb_gen.c
	$(CC) $(CFLAGS) -o $@ sitorb_gen.c -lm

test: all
	./run_tests.py

clean:
	rm -f navtex_wav navtex_wav_old sitorb_gen *.o

# SPDX-License-Identifier: LGPL-2.1-or-later
# NAVTEX decoder test tools, built on a computer (Linux, macOS):
#   make            navtex_wav, sitorb_gen
#   make test       synthetic signals at several SNR
NAV = ../../src/navtex
CFLAGS = -O2 -Wall -Wextra -I$(NAV)

all: navtex_wav sitorb_gen

navtex_wav: navtex_wav.c $(NAV)/sitorb.c $(NAV)/sitorb.h
	$(CC) $(CFLAGS) -o $@ navtex_wav.c $(NAV)/sitorb.c -lm

sitorb_gen: sitorb_gen.c
	$(CC) $(CFLAGS) -o $@ sitorb_gen.c -lm

test: all
	./run_tests.py

clean:
	rm -f navtex_wav sitorb_gen *.o

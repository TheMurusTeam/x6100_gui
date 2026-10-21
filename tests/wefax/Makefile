# SPDX-License-Identifier: LGPL-2.1-or-later
# WEFAX decoder test tool, built on a computer (Linux, macOS):
#   make
WEFAX = ../../src/wefax
CFLAGS = -O2 -Wall -I$(WEFAX)

wefax_wav: wefax_wav.c $(WEFAX)/decoder.c
	$(CC) $(CFLAGS) -o $@ wefax_wav.c $(WEFAX)/decoder.c -lm

clean:
	rm -f wefax_wav

# SPDX-License-Identifier: LGPL-2.1-or-later
# SSTV callsign reading (src/sstv/sstv_ocr.c) on a computer
SSTV = ../../src/sstv
CFLAGS = -O2 -Wall -Wextra -I$(SSTV)

ocr_test: ocr_test.c $(SSTV)/sstv_ocr.c $(SSTV)/sstv_ocr.h
	$(CC) $(CFLAGS) -o $@ ocr_test.c $(SSTV)/sstv_ocr.c -lm -lpthread

clean:
	rm -f ocr_test

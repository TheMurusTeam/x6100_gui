# SSTV callsign reading: test on a computer

`ocr_test` runs the callsign reading of the firmware (`src/sstv/sstv_ocr.c`)
on received pictures and prints, for each one, the suggested callsigns and
the lines of text read:

    make
    ./ocr_test ../../rootfs/usr/share/x6100/sstv_ocr.bin ../../third-party/cty/cty.dat \
               [--own IU1ABC] [--threads 3] PIC.ppm ...

Pictures as binary PPM (P6), 320 pixels wide, as the radio receives them
(`python -c "from PIL import Image; Image.open('a.png').convert('RGB').save('a.ppm')"`).

The networks and the rules are the ones of sstv-callsign-ml (`read.py`);
the model file is written by its `export_radio.py`. On the 260 test
pictures of sstv-callsign-ml the lines read and the suggestions are the
same as `read.py`, picture by picture.

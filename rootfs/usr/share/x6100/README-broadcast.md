# broadcast.csv: the shortwave broadcast schedule

`broadcast.csv` is the station schedule behind the Channel List's
**Broadcast Info** (microphone GENE) and **Save as channel** (NW).

## Source

The **EiBi shortwave schedule** by Eike Bierwirth, https://www.eibispace.de/
(the CSV database, `sked-<season>.csv`; its text is only converted from
Latin-1 to UTF-8, with Unix line ends).

This copy is season **B26** (winter 2026/27, 25 October 2026 to 28 March
2027), downloaded on 2 November 2026 (Hany El Imam). EiBi publishes a new
file for each season (A27, summer, starts at the end of March 2027) and
updates each file during its season.

## Terms of use

From EiBi's README.TXT, *Conditions of use*:

> All my frequency lists are free of cost, and any person is absolutely
> free to download, use, copy, or distribute these files or to use them
> within third-party software.

Corrections go to EiBi (the address is in that README).

## Updating it

- In the firmware: replace this file with the season's `sked-<season>.csv`
  from https://www.eibispace.de/dx/ (same format, header line included),
  converted to UTF-8 (`iconv -f latin1 -t utf-8 sked-b26.csv | tr -d '\r'
  > broadcast.csv`), and update the season above.
- On a radio: this file is read where it is, so each firmware brings its
  own season's schedule. To use another one (a newer season, or your own
  list), put it on the SD card as `/mnt/broadcast.csv` (the DATA
  partition): when that file is there, it is read instead. Delete it to go
  back to the firmware's copy.

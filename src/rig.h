/*
 * rig.h - rádió vezérlése CAT-on (Yaesu FT-891 és a vele azonos parancsú
 *         Yaesu rádiók: FT-991, FTDX10, FTDX101 ...)
 *
 * Csak frekvenciát és üzemmódot állít, adásba nem kapcsol. A port
 * megnyitásakor a DTR és az RTS jelet kikapcsolja, hogy egy "PTT = RTS/DTR"
 * beállítású rádió véletlenül se menjen adásba.
 *
 * Port: Linuxon pl. /dev/ttyUSB0 (az FT-891 két soros portot ad, a CAT az
 * első, "Enhanced"), Windowson pl. COM5. Sebesség: a rádió menüjében
 * beállított CAT RATE (FT-891: 05-06, alapból 4800).
 */
#ifndef IONOPROP_RIG_H
#define IONOPROP_RIG_H

#include <stddef.h>

typedef struct Rig Rig;

/* Megnyitás. Hiba esetén NULL, err-ben az ok. */
Rig *rig_open(const char *port, int baud, char *err, size_t errlen);
void rig_close(Rig *r);

/* Sáv és üzemmód (MODE_SSB / MODE_CW / MODE_FT8) szerinti szokásos
 * frekvencia beállítása. Visszatérés: 0 = rendben; hz-be a beállított frekvencia */
int  rig_tune_band(Rig *r, int band, int mode, long *hz, char *err, size_t errlen);

/* A sáv szokásos frekvenciája (Hz) az üzemmódhoz */
long rig_band_freq(int band, int mode);

/* "port[,sebesség]" szöveg értelmezése, pl. "/dev/ttyUSB0,38400" vagy "COM5".
 * 0 = rendben */
int  rig_parse_spec(const char *spec, char *port, size_t portlen, int *baud);

#endif

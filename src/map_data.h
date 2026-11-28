/*
 * map_data.h - szárazföldek körvonala (Natural Earth 1:110m, közkincs)
 */
#ifndef IONOPROP_MAP_DATA_H
#define IONOPROP_MAP_DATA_H

#include <stdint.h>

extern const int     MAP_NRINGS;
extern const int     MAP_RING_START[];  /* gyűrű i pontjai: [START[i], START[i+1]) */
extern const int16_t MAP_PTS[];         /* hosszúság*100, szélesség*100 párok */

#endif

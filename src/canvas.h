/*
 * canvas.h - egyszerű szoftveres rajzolás egy pixeltömbbe (0xRRGGBB)
 *
 * A grafikus felület mindent ide rajzol (térkép, panelek, szöveg), az X11
 * csak kirakja a kész képet. Így a rajzolás ablak nélkül is tesztelhető.
 */
#ifndef IONOPROP_CANVAS_H
#define IONOPROP_CANVAS_H

#include <stdint.h>

typedef uint32_t Color;            /* 0xRRGGBB */

/* A rajzoló függvények "logikai" képpontokban kapják a koordinátákat; a
 * vászon a nagyítás (s) szerinti valódi felbontásban rajzol. Nagyított
 * Windows-kijelzőn (pl. 150%) így éles marad a szöveg és a térkép.
 * w, h, px és a vágás már a valódi (fizikai) képpontokban értendő. */
typedef struct {
    int       w, h;                /* fizikai méret */
    uint32_t *px;
    int       cx0, cy0, cx1, cy1;  /* vágási téglalap (cx1, cy1 kizárva), fizikai */
    double    s;                   /* nagyítás: fizikai = logikai * s (alap: 1) */
} Canvas;

int  cv_init(Canvas *c, int w, int h);
/* Nagyítás beállítása (utána cv_resize a logikai mérettel). A szöveg mérete
 * is ezzel nő; egyszerre egy nagyítás érvényes a programban. */
void cv_set_scale(Canvas *c, double s);
void cv_free(Canvas *c);
int  cv_resize(Canvas *c, int w, int h);
void cv_clip(Canvas *c, int x, int y, int w, int h);
void cv_noclip(Canvas *c);

void cv_clear(Canvas *c, Color col);
void cv_pixel(Canvas *c, int x, int y, Color col, int alpha);   /* alpha 0..255 */
void cv_fill_rect(Canvas *c, int x, int y, int w, int h, Color col);
void cv_blend_rect(Canvas *c, int x, int y, int w, int h, Color col, int alpha);
void cv_rect(Canvas *c, int x, int y, int w, int h, Color col);
void cv_line(Canvas *c, double x0, double y0, double x1, double y1, Color col, int alpha);
void cv_line_thick(Canvas *c, double x0, double y0, double x1, double y1,
                   double width, Color col, int alpha);
void cv_circle(Canvas *c, double x, double y, double r, Color col, int alpha);

/* Sokszög kitöltése (páros-páratlan szabály). xy: x0,y0,x1,y1,... */
void cv_polygon(Canvas *c, const double *xy, int n, Color col, int alpha);

/* Színek keverése: t = 0 -> a, t = 1 -> b */
Color color_mix(Color a, Color b, double t);

/* ---------- szöveg (FreeType) ---------- */
enum { FONT_NORMAL = 0, FONT_BOLD = 1 };

/* Betűtípus betöltése. Visszatérés: 0 rendben, -1 nincs betűtípus.
 * Az IONOPROP_FONT környezeti változóval saját .ttf adható meg. */
int  font_init(void);
void font_done(void);

/* Szöveg kirajzolása; (x, y) a szöveg bal felső sarka. size: pixel.
 * Visszatérés: a szöveg szélessége pixelben. */
int  cv_text(Canvas *c, int x, int y, int size, int font, Color col, const char *utf8);
int  text_width(int size, int font, const char *utf8);

/* Kép mentése PNG helyett egyszerű PPM-be (teszteléshez) */
int  cv_save_ppm(const Canvas *c, const char *path);

#endif

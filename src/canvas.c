/*
 * canvas.c - szoftveres rajzolás + szöveg (lásd canvas.h)
 *   szöveg Linuxon FreeType-pal, Windowson a rendszer GDI-jével
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(IONOPROP_NO_FONT)
/* nincs szövegrajzolás */
#elif defined(_WIN32)
#include <windows.h>
#else
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include "canvas.h"

/* ---------- alapok ---------- */

int cv_init(Canvas *c, int w, int h)
{
    memset(c, 0, sizeof *c);
    return cv_resize(c, w, h);
}

void cv_free(Canvas *c)
{
    free(c->px);
    c->px = NULL;
}

static double g_scale = 1.0;      /* a szövegméréshez (text_width) */

void cv_set_scale(Canvas *c, double s)
{
    if (s < 0.5) s = 0.5;
    if (s > 4.0) s = 4.0;
    c->s = s;
    g_scale = s;
}

/* logikai -> fizikai */
static inline int P(const Canvas *c, double v) { return (int)lround(v * c->s); }

int cv_resize(Canvas *c, int w, int h)
{
    if (c->s <= 0) c->s = 1.0;
    w = P(c, w);
    h = P(c, h);
    uint32_t *p = realloc(c->px, (size_t)w * (size_t)h * sizeof(uint32_t));
    if (!p) return -1;
    c->px = p;
    c->w = w;
    c->h = h;
    cv_noclip(c);
    return 0;
}

void cv_clip(Canvas *c, int x, int y, int w, int h)
{
    int x0 = P(c, x), y0 = P(c, y), x1 = P(c, x + w), y1 = P(c, y + h);
    c->cx0 = x0 < 0 ? 0 : x0;
    c->cy0 = y0 < 0 ? 0 : y0;
    c->cx1 = x1 > c->w ? c->w : x1;
    c->cy1 = y1 > c->h ? c->h : y1;
}

void cv_noclip(Canvas *c)
{
    c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h;
}

Color color_mix(Color a, Color b, double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = (int)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t + 0.5);
    int g = (int)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t + 0.5);
    int bl = (int)((a & 255) * (1 - t) + (b & 255) * t + 0.5);
    return (Color)((r << 16) | (g << 8) | bl);
}

static inline uint32_t blend(uint32_t dst, Color src, int a)
{
    if (a >= 255) return src;
    int ia = 255 - a;
    uint32_t r = (((dst >> 16) & 255) * ia + ((src >> 16) & 255) * a) / 255;
    uint32_t g = (((dst >> 8) & 255) * ia + ((src >> 8) & 255) * a) / 255;
    uint32_t b = ((dst & 255) * ia + (src & 255) * a) / 255;
    return (r << 16) | (g << 8) | b;
}

/* fizikai képpont */
static inline void px_put(Canvas *c, int x, int y, Color col, int alpha)
{
    if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1 || alpha <= 0) return;
    uint32_t *p = &c->px[(size_t)y * c->w + x];
    *p = blend(*p, col, alpha);
}

static void blend_rect_phys(Canvas *c, int x0, int y0, int x1, int y1, Color col, int alpha);

/* logikai képpont: nagyításnál egy kis négyzet */
void cv_pixel(Canvas *c, int x, int y, Color col, int alpha)
{
    if (c->s == 1.0) { px_put(c, x, y, col, alpha); return; }
    blend_rect_phys(c, P(c, x), P(c, y), P(c, x + 1), P(c, y + 1), col, alpha);
}

void cv_clear(Canvas *c, Color col)
{
    for (size_t i = 0; i < (size_t)c->w * c->h; i++) c->px[i] = col;
}

static void blend_rect_phys(Canvas *c, int x0, int y0, int x1, int y1, Color col, int alpha)
{
    if (x0 < c->cx0) x0 = c->cx0;
    if (y0 < c->cy0) y0 = c->cy0;
    if (x1 > c->cx1) x1 = c->cx1;
    if (y1 > c->cy1) y1 = c->cy1;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *p = &c->px[(size_t)yy * c->w];
        for (int xx = x0; xx < x1; xx++) p[xx] = blend(p[xx], col, alpha);
    }
}

void cv_blend_rect(Canvas *c, int x, int y, int w, int h, Color col, int alpha)
{
    int x0 = P(c, x), y0 = P(c, y), x1 = P(c, x + w), y1 = P(c, y + h);
    /* az 1 képpontos vonalak nagyításnál se tűnjenek el */
    if (w > 0 && x1 <= x0) x1 = x0 + 1;
    if (h > 0 && y1 <= y0) y1 = y0 + 1;
    blend_rect_phys(c, x0, y0, x1, y1, col, alpha);
}

void cv_fill_rect(Canvas *c, int x, int y, int w, int h, Color col)
{
    cv_blend_rect(c, x, y, w, h, col, 255);
}

void cv_rect(Canvas *c, int x, int y, int w, int h, Color col)
{
    cv_fill_rect(c, x, y, w, 1, col);
    cv_fill_rect(c, x, y + h - 1, w, 1, col);
    cv_fill_rect(c, x, y, 1, h, col);
    cv_fill_rect(c, x + w - 1, y, 1, h, col);
}

/* ---------- vonal (Xiaolin Wu, élsimítással) ---------- */

static inline double fpart(double x) { return x - floor(x); }

static void line_phys(Canvas *c, double x0, double y0, double x1, double y1, Color col, int alpha)
{
    int steep = fabs(y1 - y0) > fabs(x1 - x0);
    double t;
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    double dx = x1 - x0, dy = y1 - y0;
    double grad = dx < 1e-9 ? 1.0 : dy / dx;
    double y = y0 + grad * (round(x0) - x0);

    for (int x = (int)round(x0); x <= (int)round(x1); x++) {
        int yi = (int)floor(y);
        int a1 = (int)(alpha * (1.0 - fpart(y)));
        int a2 = (int)(alpha * fpart(y));
        if (steep) { px_put(c, yi, x, col, a1); px_put(c, yi + 1, x, col, a2); }
        else       { px_put(c, x, yi, col, a1); px_put(c, x, yi + 1, col, a2); }
        y += grad;
    }
}

static void circle_phys(Canvas *c, double cx, double cy, double r, Color col, int alpha);
static void polygon_phys(Canvas *c, const double *xy, int n, Color col, int alpha);

void cv_line(Canvas *c, double x0, double y0, double x1, double y1, Color col, int alpha)
{
    double s = c->s;
    if (s <= 1.25) { line_phys(c, x0 * s, y0 * s, x1 * s, y1 * s, col, alpha); return; }
    /* nagyításnál a vonal is vastagodjon */
    double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    double w = s * 0.5, nx = -dy / len * w, ny = dx / len * w;
    double q[8] = { (x0 * s + nx), (y0 * s + ny), (x1 * s + nx), (y1 * s + ny),
                    (x1 * s - nx), (y1 * s - ny), (x0 * s - nx), (y0 * s - ny) };
    polygon_phys(c, q, 4, col, alpha);
    line_phys(c, q[0], q[1], q[2], q[3], col, alpha / 2);
    line_phys(c, q[4], q[5], q[6], q[7], col, alpha / 2);
}

void cv_line_thick(Canvas *c, double x0, double y0, double x1, double y1,
                   double width, Color col, int alpha)
{
    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-6) { cv_circle(c, x0, y0, width / 2, col, alpha); return; }
    double nx = -dy / len * width / 2, ny = dx / len * width / 2;
    double q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    cv_polygon(c, q, 4, col, alpha);
    cv_line(c, q[0], q[1], q[2], q[3], col, alpha / 2);   /* élsimítás a széleken */
    cv_line(c, q[4], q[5], q[6], q[7], col, alpha / 2);
}

void cv_circle(Canvas *c, double cx, double cy, double r, Color col, int alpha)
{
    circle_phys(c, cx * c->s, cy * c->s, r * c->s, col, alpha);
}

static void circle_phys(Canvas *c, double cx, double cy, double r, Color col, int alpha)
{
    int x0 = (int)floor(cx - r - 1), x1 = (int)ceil(cx + r + 1);
    int y0 = (int)floor(cy - r - 1), y1 = (int)ceil(cy + r + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            double d = sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
            double cov = r + 0.5 - d;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            px_put(c, x, y, col, (int)(alpha * cov));
        }
}

/* ---------- sokszög kitöltés ---------- */

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

void cv_polygon(Canvas *c, const double *xy, int n, Color col, int alpha)
{
    if (n < 3) return;
    if (c->s == 1.0) { polygon_phys(c, xy, n, col, alpha); return; }
    double stack[512];
    double *q = 2 * n <= 512 ? stack : malloc(sizeof(double) * 2 * (size_t)n);
    if (!q) return;
    for (int i = 0; i < 2 * n; i++) q[i] = xy[i] * c->s;
    polygon_phys(c, q, n, col, alpha);
    if (q != stack) free(q);
}

static void polygon_phys(Canvas *c, const double *xy, int n, Color col, int alpha)
{
    if (n < 3) return;
    double ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    int ys = (int)floor(ymin), ye = (int)ceil(ymax);
    if (ys < c->cy0) ys = c->cy0;
    if (ye > c->cy1 - 1) ye = c->cy1 - 1;

    double stackbuf[256];
    double *xs = n <= 256 ? stackbuf : malloc(sizeof(double) * (size_t)n);
    if (!xs) return;

    for (int y = ys; y <= ye; y++) {
        double sy = y + 0.5;
        int k = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            double yi = xy[2 * i + 1], yj = xy[2 * j + 1];
            if ((yi <= sy && yj > sy) || (yj <= sy && yi > sy)) {
                double xi = xy[2 * i], xj = xy[2 * j];
                xs[k++] = xi + (sy - yi) * (xj - xi) / (yj - yi);
            }
        }
        if (k < 2) continue;
        qsort(xs, (size_t)k, sizeof(double), cmp_double);
        for (int i = 0; i + 1 < k; i += 2) {
            int xa = (int)ceil(xs[i] - 0.5), xb = (int)floor(xs[i + 1] - 0.5);
            if (xa < c->cx0) xa = c->cx0;
            if (xb > c->cx1 - 1) xb = c->cx1 - 1;
            uint32_t *p = &c->px[(size_t)y * c->w];
            for (int x = xa; x <= xb; x++) p[x] = blend(p[x], col, alpha);
        }
    }
    if (xs != stackbuf) free(xs);
}

/* ---------- szöveg ---------- */
#ifdef IONOPROP_NO_FONT
/* FreeType nélkül (Android): a feliratokat a felület rajzolja */
int  font_init(void) { return 0; }
void font_done(void) { }
int  text_width(int size, int font, const char *s) { (void)size; (void)font; (void)s; return 0; }
int  cv_text(Canvas *c, int x, int y, int size, int font, Color col, const char *s)
{
    (void)c; (void)x; (void)y; (void)size; (void)font; (void)col; (void)s;
    return 0;
}

#elif defined(_WIN32)
/* ---------- szöveg Windowson: GDI ----------
 * A szöveget fehérrel egy fekete memóriabeli képre írjuk (élsimítással),
 * és a fényességet átlátszóságként használva keverjük a vászonra.
 * Fő betűtípus: Segoe UI; a benne hiányzó jelekhez (◀ ▶ ✎ ☀): Segoe UI Symbol. */
#define FMAX 128
static HDC      g_dc;
static HBITMAP  g_bmp, g_oldbmp;
static uint32_t *g_bits;
static int      g_bw, g_bh;
static HFONT    g_font[2][2][FMAX];      /* [tartalék][félkövér][méret] */

static HFONT get_font(int sym, int bold, int size)
{
    if (size < 4) size = 4;
    if (size >= FMAX) size = FMAX - 1;
    HFONT *slot = &g_font[sym ? 1 : 0][bold ? 1 : 0][size];
    if (!*slot)
        *slot = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS,
                            sym ? L"Segoe UI Symbol" : L"Segoe UI");
    return *slot;
}

static int ensure_bitmap(int w, int h)
{
    if (g_bits && w <= g_bw && h <= g_bh) return 0;
    if (w < g_bw) w = g_bw;
    if (h < g_bh) h = g_bh;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                 /* felülről lefelé */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP b = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!b || !bits) return -1;
    if (g_bmp) {
        SelectObject(g_dc, g_oldbmp);
        DeleteObject(g_bmp);
    }
    g_oldbmp = (HBITMAP)SelectObject(g_dc, b);
    g_bmp = b;
    g_bits = bits;
    g_bw = w;
    g_bh = h;
    return 0;
}

int font_init(void)
{
    g_dc = CreateCompatibleDC(NULL);
    if (!g_dc || ensure_bitmap(1024, 96) != 0) {
        fprintf(stderr, "A szövegrajzolás nem indul (GDI)\n");
        return -1;
    }
    SetBkMode(g_dc, TRANSPARENT);
    SetTextColor(g_dc, RGB(255, 255, 255));
    SetTextAlign(g_dc, TA_LEFT | TA_BASELINE | TA_NOUPDATECP);
    return 0;
}

void font_done(void)
{
    if (!g_dc) return;
    if (g_bmp) { SelectObject(g_dc, g_oldbmp); DeleteObject(g_bmp); }
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int i = 0; i < FMAX; i++)
                if (g_font[a][b][i]) { DeleteObject(g_font[a][b][i]); g_font[a][b][i] = NULL; }
    DeleteDC(g_dc);
    g_dc = NULL;
    g_bmp = NULL;
    g_bits = NULL;
    g_bw = g_bh = 0;
}

/* UTF-8 -> UTF-16, és minden karakterhez: kell-e a tartalék betűtípus */
static int to_wide(const char *s, int size, int font, wchar_t *w, unsigned char *sym, int max)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, max);
    if (n <= 0) return 0;
    n--;                                         /* a lezáró nulla nélkül */
    SelectObject(g_dc, get_font(0, font, size));
    WORD idx[512];
    if (n > 512) n = 512;
    if (GetGlyphIndicesW(g_dc, w, n, idx, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR)
        memset(idx, 0, sizeof idx);
    for (int i = 0; i < n; i++) sym[i] = idx[i] == 0xFFFF;
    return n;
}

/* a szöveg szakaszai (azonos betűtípussal) egymás után; ha draw, ki is írja */
static int run_text(const wchar_t *w, const unsigned char *sym, int n, int size, int font,
                    int x, int base, int draw)
{
    int pos = 0, i = 0;
    while (i < n) {
        int j = i;
        while (j < n && sym[j] == sym[i]) j++;
        SelectObject(g_dc, get_font(sym[i], font, size));
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(g_dc, w + i, j - i, &sz);
        if (draw) ExtTextOutW(g_dc, x + pos, base, 0, NULL, w + i, (UINT)(j - i), NULL);
        pos += sz.cx;
        i = j;
    }
    return pos;
}

static int text_width_phys(int size, int font, const char *s)
{
    if (!g_dc || !s || !*s) return 0;
    wchar_t w[513];
    unsigned char sym[512];
    int n = to_wide(s, size, font, w, sym, 513);
    return run_text(w, sym, n, size, font, 0, 0, 0);
}

int text_width(int size, int font, const char *s)
{
    int ps = (int)lround(size * g_scale);
    return (int)lround(text_width_phys(ps, font, s) / g_scale);
}

static int cv_text_phys(Canvas *c, int x, int y, int size, int font, Color col, const char *s);

int cv_text(Canvas *c, int x, int y, int size, int font, Color col, const char *s)
{
    int tw = cv_text_phys(c, P(c, x), P(c, y), (int)lround(size * c->s), font, col, s);
    return (int)lround(tw / c->s);
}

static int cv_text_phys(Canvas *c, int x, int y, int size, int font, Color col, const char *s)
{
    if (!g_dc || !s || !*s) return 0;
    wchar_t w[513];
    unsigned char sym[512];
    int n = to_wide(s, size, font, w, sym, 513);
    if (n <= 0) return 0;
    int tw = run_text(w, sym, n, size, font, 0, 0, 0);

    /* a FreeType-os változattal azonos alapvonal: y + 0,8 * méret */
    int pad = size / 2 + 2;
    int base = pad + (int)(size * 0.8 + 0.5);
    int bw = tw + 8, bh = pad + size * 2;
    if (ensure_bitmap(bw, bh) != 0) return tw;
    for (int r = 0; r < bh; r++) memset(g_bits + (size_t)r * g_bw, 0, (size_t)bw * 4);
    run_text(w, sym, n, size, font, 2, base, 1);
    GdiFlush();

    for (int r = 0; r < bh; r++) {
        const uint32_t *row = g_bits + (size_t)r * g_bw;
        for (int k = 0; k < bw; k++) {
            uint32_t v = row[k];
            int a = (int)((v >> 8) & 0xFF);          /* zöld csatorna = fedettség */
            if (a) px_put(c, x - 2 + k, y - pad + r, col, a);
        }
    }
    return tw;
}

#else
/* ---------- szöveg Linuxon: FreeType ---------- */

static FT_Library ft_lib;
static FT_Face    ft_face[2];

typedef struct {
    uint32_t cp;
    uint8_t  size, font, used;
    int16_t  left, top, adv;
    uint16_t w, h;
    uint8_t *bmp;
} Glyph;

#define GCACHE 4096
static Glyph gcache[GCACHE];

static const char *FONT_PATHS[2][6] = {
    { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf" },
    { "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf" },
};

int font_init(void)
{
    if (FT_Init_FreeType(&ft_lib)) return -1;
    const char *env = getenv("IONOPROP_FONT");
    for (int f = 0; f < 2; f++) {
        if (env && f == 0 && FT_New_Face(ft_lib, env, 0, &ft_face[0]) == 0) continue;
        for (int i = 0; i < 6 && !ft_face[f]; i++)
            if (FT_New_Face(ft_lib, FONT_PATHS[f][i], 0, &ft_face[f]) != 0) ft_face[f] = NULL;
    }
    if (!ft_face[0]) {
        fprintf(stderr, "Nem találok betűtípust. Telepítsd: sudo apt install fonts-dejavu-core\n"
                        "vagy add meg: IONOPROP_FONT=/út/a/betűtípushoz.ttf\n");
        return -1;
    }
    if (!ft_face[1]) ft_face[1] = ft_face[0];   /* félkövér helyett normál */
    return 0;
}

void font_done(void)
{
    for (int i = 0; i < GCACHE; i++) free(gcache[i].bmp);
    memset(gcache, 0, sizeof gcache);
    if (ft_face[1] && ft_face[1] != ft_face[0]) FT_Done_Face(ft_face[1]);
    if (ft_face[0]) FT_Done_Face(ft_face[0]);
    ft_face[0] = ft_face[1] = NULL;
    if (ft_lib) FT_Done_FreeType(ft_lib);
    ft_lib = NULL;
}

/* UTF-8 dekódolás: a következő karakter kódja, *s továbblép */
static uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp;
    int n;
    if (p[0] < 0x80)      { cp = p[0]; n = 1; }
    else if (p[0] < 0xE0) { cp = p[0] & 0x1F; n = 2; }
    else if (p[0] < 0xF0) { cp = p[0] & 0x0F; n = 3; }
    else                  { cp = p[0] & 0x07; n = 4; }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += n;
    return cp;
}

static const Glyph *get_glyph(uint32_t cp, int size, int font)
{
    if (!ft_face[0]) return NULL;
    if (size > 255) size = 255;
    unsigned h = (cp * 2654435761u ^ (unsigned)size * 40503u ^ (unsigned)font) % GCACHE;
    for (int i = 0; i < 16; i++) {
        Glyph *g = &gcache[(h + i) % GCACHE];
        if (g->used && g->cp == cp && g->size == size && g->font == font) return g;
        if (!g->used) {
            FT_Face face = ft_face[font ? 1 : 0];
            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size);
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) return NULL;
            FT_Bitmap *b = &face->glyph->bitmap;
            g->cp = cp; g->size = (uint8_t)size; g->font = (uint8_t)font; g->used = 1;
            g->left = (int16_t)face->glyph->bitmap_left;
            g->top  = (int16_t)face->glyph->bitmap_top;
            g->adv  = (int16_t)(face->glyph->advance.x >> 6);
            g->w = (uint16_t)b->width;
            g->h = (uint16_t)b->rows;
            g->bmp = NULL;
            if (g->w && g->h) {
                g->bmp = malloc((size_t)g->w * g->h);
                if (g->bmp)
                    for (unsigned r = 0; r < g->h; r++)
                        memcpy(g->bmp + (size_t)r * g->w, b->buffer + (long)r * b->pitch, g->w);
            }
            return g;
        }
    }
    return NULL;   /* tele a gyorsítótár ezen a helyen - ritka */
}

static int text_width_phys(int size, int font, const char *s)
{
    int w = 0;
    while (*s) {
        const Glyph *g = get_glyph(utf8_next(&s), size, font);
        if (g) w += g->adv;
    }
    return w;
}

int text_width(int size, int font, const char *s)
{
    if (g_scale == 1.0) return text_width_phys(size, font, s);
    return (int)lround(text_width_phys((int)lround(size * g_scale), font, s) / g_scale);
}

static int cv_text_phys(Canvas *c, int x, int y, int size, int font, Color col, const char *s);

int cv_text(Canvas *c, int x, int y, int size, int font, Color col, const char *s)
{
    if (c->s == 1.0) return cv_text_phys(c, x, y, size, font, col, s);
    int tw = cv_text_phys(c, P(c, x), P(c, y), (int)lround(size * c->s), font, col, s);
    return (int)lround(tw / c->s);
}

static int cv_text_phys(Canvas *c, int x, int y, int size, int font, Color col, const char *s)
{
    int base = y + (int)(size * 0.8 + 0.5);   /* alapvonal */
    int x0 = x;
    while (*s) {
        const Glyph *g = get_glyph(utf8_next(&s), size, font);
        if (!g) continue;
        if (g->bmp) {
            int gx = x + g->left, gy = base - g->top;
            for (int r = 0; r < g->h; r++)
                for (int k = 0; k < g->w; k++) {
                    int a = g->bmp[(size_t)r * g->w + k];
                    if (a) px_put(c, gx + k, gy + r, col, a);
                }
        }
        x += g->adv;
    }
    return x - x0;
}

#endif /* IONOPROP_NO_FONT */

int cv_save_ppm(const Canvas *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (size_t i = 0; i < (size_t)c->w * c->h; i++) {
        unsigned char rgb[3] = { (unsigned char)(c->px[i] >> 16), (unsigned char)(c->px[i] >> 8),
                                 (unsigned char)c->px[i] };
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f);
}

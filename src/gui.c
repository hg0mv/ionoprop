/*
 * gui.c - IonoProp grafikus felület (X11)
 *
 * Felépítés:
 *   - felül: fejléc (QTH, idő, adatok kora, állapot)
 *   - bal oldalt: világtérkép (MUF / foF2 / sávonkénti lefedettség)
 *   - alatta: módváltó gombok, időcsúszka, 24 órás előrejelzés a kijelölt célra
 *   - jobb oldalt: napaktivitás, rétegek a QTH fölött, javaslat, célterületek
 *
 * Minden a canvas-ra rajzolódik, az X11 csak kirakja a képet.
 *
 * Billentyűk:  ←/→ idő ±1 óra   ↑/↓ cél kijelölése   m MUF   f foF2
 *              1..9,0 sávok (160..10 m)   r frissítés   q / Esc kilépés
 * Egér:        kattintás a térképen = új cél, a táblázatban = cél kijelölése,
 *              az előrejelzésen = időpont; görgő = idő
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#ifdef _WIN32
#include <getopt.h>
#endif


#include "data.h"
#include "loader.h"
#include "geo.h"
#include "model.h"
#include "predict.h"
#include "mapview.h"
#include "canvas.h"
#include "config.h"
#include "pskr.h"
#include "validate.h"
#include "platform.h"
#include "i18n.h"
#include "spots.h"
#include "targets.h"
#include "rig.h"
#include "compat.h"
#include "alerts.h"
#include "meteor.h"

#define VERSION "0.9.1"
#define DEFAULT_LOCATOR "JN97mm"
#define RELOAD_SEC (15 * 60)

/* színek */
#define C_BG      0x0f151d
#define C_PANEL   0x161e28
#define C_BORDER  0x2a3646
#define C_TEXT    0xdfe6ee
#define C_DIM     0x8494a7
#define C_ACCENT  0x4fb3ff
#define C_HEAD    0xffffff
#define C_GOOD    0x3ccf6e
#define C_WARN    0xf2c94c
#define C_ES      0xd65cff    /* sporadikus E (lila) */
#define C_METEOR  0x7fd6ff    /* meteorraj (világoskék) */
#define C_BAD     0xe5533d

#define SIDE_W   440
#define TOP_H     38
#define BTN_H     30

typedef struct { int x, y, w, h; } Rect;


typedef struct {
    Rect r;
    int  action;        /* lásd BTN_* */
    int  arg;
    char label[16];
} Button;

enum { BTN_MODE, BTN_TPREV, BTN_TNOW, BTN_TNEXT, BTN_RELOAD, BTN_LP };

/* a szerkesztőablak fajtái */
enum { EDIT_NONE = 0, EDIT_LOCATOR, EDIT_TARGET, EDIT_RIG };

typedef struct {
    /* beállítások */
    char   locator[12];
    double qlat, qlon;
    int    min_cs;

    /* adatok és számítások */
    PropData   data;
    IonoModel  model;
    MapView    map;
    time_t     now;         /* a legutóbbi újraszámolás ideje */
    time_t     loaded;      /* a legutóbbi letöltés ideje */
    int        toff;        /* időeltolás órában (0..23) */

    Target     tg[TARGETS_MAX];
    int        nt;          /* ebből az utolsó lehet a térképen kijelölt pont */
    int        n_default;   /* beépített célterületek */
    int        n_custom;    /* saját (mentett) célterületek, a beépítettek után */
    int        sel;
    PathResult res[TARGETS_MAX];
    Forecast   fc;

    /* megjelenítés */
    Canvas     cv;
    int        W, H;
    Rect       r_top, r_btn, r_fc, r_side;
    Rect       r_rows;      /* célterület-táblázat sorai */
    int        row_h;
    int        tbl_bx;      /* a sávcellák kezdete a táblázatban (jobb kattintáshoz) */
    Rect       r_fcgrid;    /* előrejelzés rácsa (kattintáshoz) */
    int        fc_cellw;
    Button     btn[24];
    int        nbtn;
    char       status[160];

    /* PSK Reporter */
    Pskr      *psk;
    char       mqtt_host[128];
    int        mqtt_port;
    double     radius;
    int        no_psk;
    int        show_spots;
    SpotStore  spots;          /* valódi vételek (spots.c) */
    time_t     last_spot_render;

    /* szerkesztőablak: QTH lokátor, új célterület, rádió */
    int        editing;        /* EDIT_* */
    char       edit_buf[64];
    char       edit_err[160];
    Rect       r_qth;          /* a fejlécben a "QTH: ..." felirat (kattintható) */
    Rect       r_mode;         /* a fejlécben az üzemmód (kattintható) */
    Rect       r_rig;          /* a fejlécben a rádió (kattintható) */
    Rect       r_lang;         /* a fejlécben a nyelv: HU / EN (kattintható) */
    int        load_n;         /* hány adatforrás jött le (az állapotsorhoz) */

    /* riasztás sávnyitáskor (alerts.c) */
    int        alerts_on;
    int        headless;       /* -o: képmentés, nem riaszt */
    char       alert_text[700];
    time_t     alert_until;    /* eddig látszik a sáv */
    int        alert_beep;     /* sípolni kell */
    Rect       r_alerts;       /* fejléc: "Riasztás: be/ki" */
    Rect       r_alert_box;    /* a felugró sáv (kattintásra eltűnik) */
    Rect       r_help;         /* fejléc: "?" (magyarázatok) */
    Rect       r_help_box;     /* a magyarázat-ablak */
    int        show_help;

    /* rádió (CAT) */
    Rig       *rig;
    char       rig_spec[128];  /* "port,sebesség" */
    char       rig_msg[160];
    int        rig_err;
} App;

static App app;   /* nagy, ezért statikus */

/* ---------- segédfüggvények ---------- */

static int in_rect(const Rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* szöveg, ha túl hosszú, "…"-re végződve levágva */
static int text_fit(Canvas *c, int x, int y, int size, int font, Color col,
                    const char *s, int maxw)
{
    if (text_width(size, font, s) <= maxw) return cv_text(c, x, y, size, font, col, s);
    char buf[128];
    snprintf(buf, sizeof buf, "%s", s);
    size_t n = strlen(buf);
    while (n > 0) {
        do { n--; } while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80);
        buf[n] = '\0';
        char t[136];
        snprintf(t, sizeof t, "%s…", buf);
        if (text_width(size, font, t) <= maxw) return cv_text(c, x, y, size, font, col, t);
    }
    return 0;
}

static void panel(Canvas *c, Rect r)
{
    cv_fill_rect(c, r.x, r.y, r.w, r.h, C_PANEL);
    cv_rect(c, r.x, r.y, r.w, r.h, C_BORDER);
}

static void normalize_locator(const char *in, char *out, size_t len)
{
    size_t i;
    for (i = 0; in[i] && i + 1 < len; i++) {
        char ch = in[i];
        if (i < 2 && ch >= 'a' && ch <= 'z') ch -= 32;
        if (i >= 4 && i < 6 && ch >= 'A' && ch <= 'Z') ch += 32;
        out[i] = ch;
    }
    out[i] = '\0';
}

/* ---------- számítás ---------- */

/* riasztás: csak a jelenre (nem az idősávval nézett órákra) */
static void check_alerts(App *a)
{
    if (!a->alerts_on || a->headless || a->toff != 0 || a->load_n <= 0) return;
    EsStatus es;
    es_status(&a->data, a->psk ? &a->spots : NULL, a->qlat, a->qlon, a->now, &es);
    Alert cur[ALERT_MAX], nw[ALERT_MAX];
    int n = alerts_collect(a->tg, a->res, a->nt, a->n_default, a->n_custom, &es, cur, ALERT_MAX);
    n = alerts_add_meteor(cur, n, ALERT_MAX, a->now, a->qlat, a->qlon);
    int k = alerts_new(cur, n, a->now, NULL, nw, ALERT_MAX);
    if (k <= 0) return;
    alerts_join(nw, k, a->alert_text, sizeof a->alert_text);
    a->alert_until = a->now + 15 * 60;
    a->alert_beep = 1;
}

static void recompute(App *a)
{
    a->now = time(NULL);
    model_init(&a->model, &a->data, a->now, 3 * 3600, a->min_cs);
    model_set_time(&a->model, a->now + a->toff * 3600);

    map_compute(&a->map, &a->model, &a->data, a->qlat, a->qlon, a->map.mode >= 0);

    for (int i = 0; i < a->nt; i++)
        predict_path(&a->model, &a->data, a->qlat, a->qlon, &a->tg[i], &a->res[i]);

    predict_forecast(&a->model, &a->data, a->qlat, a->qlon, &a->tg[a->sel],
                     a->now - a->now % 3600, &a->fc);
    check_alerts(a);
}

static void status_from_load(App *a)
{
    int n = a->load_n;
    if (n == 4) snprintf(a->status, sizeof a->status, "%s", TR("Adatok rendben", "Data OK"));
    else if (n > 0) snprintf(a->status, sizeof a->status, TR("Néhány forrás nem érhető el (%d/4)", "Some sources unavailable (%d/4)"), n);
    else snprintf(a->status, sizeof a->status, "%s", TR("Nincs adat - nincs internet?", "No data - no internet?"));
}

static void load(App *a, int force)
{
    a->load_n = load_all(&a->data, a->qlat, a->qlon, force);
    a->loaded = time(NULL);
    status_from_load(a);
    recompute(a);
}

/* új spotok átvétele, kiértékelése és naplózása. Visszatérés: új darab */
static int psk_update(App *a)
{
    if (!a->psk) return 0;
    static PskSpot buf[1024];
    static SpotEval ev[1024];
    int n = pskr_poll(a->psk, buf, 1024);
    if (n <= 0) return 0;
    spots_add(&a->spots, &a->model, &a->data, buf, n, ev);
    validate_log_append(ev, n);
    return n;
}

/* volt-e valódi vétel a célterületen az adott sávon az utóbbi 30 percben
 * (0 = nem, 1 = csak gyenge, 2 = erős is) - lásd spots_on_target() */
static int spot_on_target(const App *a, const Target *t, int band)
{
    return spots_on_target(&a->spots, a->qlat, a->qlon, t, band, time(NULL));
}

static void draw_spots(App *a)
{
    if (!a->show_spots || a->toff != 0 || !a->psk) return;
    Canvas *c = &a->cv;
    time_t now = time(NULL);
    spots_draw(c, &a->map, &a->spots, now, 1.0);

    /* jelmagyarázat a térkép bal alsó sarkában lévő doboz fölé */
    EsNow es;
    spots_es_now(&a->spots, now, &es);
    int any_es = 0;
    for (int b = a->map.mode >= 0 ? a->map.mode : 9; b < NBANDS; b++) {
        if (es.pairs[b]) any_es = 1;
        if (a->map.mode >= 0) break;
    }
    if (a->map.mode < 0 && !any_es) return;
    cv_clip(c, a->map.x, a->map.y, a->map.w, a->map.h);
    int lx = a->map.x + 10, ly = a->map.y + a->map.h - (a->map.mode >= 0 ? 74 : 92);
    int lw = a->map.mode >= 0 ? (any_es ? 390 : 250) : 160;
    cv_blend_rect(c, lx - 6, ly - 4, lw, 22, 0x000000, 150);
    int xx = lx;
    if (a->map.mode >= 0) {
        cv_circle(c, xx + 5, ly + 7, 4.2, 0x000000, 190);
        cv_circle(c, xx + 5, ly + 7, 3, 0xffffff, 235);
        xx += 16 + cv_text(c, xx + 16, ly, 12, FONT_NORMAL, 0xdddddd, TR("valódi vétel (PSK Reporter, 30 perc)", "real reception (PSK Reporter, 30 min)"));
        xx += 12;
    }
    if (any_es || a->map.mode >= 0) {
        cv_circle(c, xx + 6, ly + 7, 6, 0xd65cff, 70);
        cv_circle(c, xx + 6, ly + 7, 2.4, 0xd65cff, 230);
        cv_text(c, xx + 16, ly, 12, FONT_NORMAL, 0xdddddd, TR("sporadikus E", "sporadic E"));
    }
    cv_noclip(c);
}

/* kijelölt pont a térképen -> utolsó cél */
static int fixed_targets(const App *a) { return a->n_default + a->n_custom; }
static int is_custom(const App *a, int i) { return i >= a->n_default && i < fixed_targets(a); }
static int is_picked(const App *a, int i) { return i == fixed_targets(a) && i < a->nt; }

static void set_custom_target(App *a, double lat, double lon)
{
    int idx = fixed_targets(a);
    Target *t = &a->tg[idx];
    memset(t, 0, sizeof *t);
    t->lat = lat;
    t->lon = lon;
    char loc[8];
    latlon_to_locator(lat, lon, loc);
    t->nvis = distance_km(a->qlat, a->qlon, lat, lon) < 300.0;
    snprintf(t->name, sizeof t->name, TR("Kijelölt: %s", "Picked: %s"), loc);
    a->nt = idx + 1;
    a->sel = idx;
}

/* ---------- elrendezés ---------- */

static void add_button(App *a, int x, int y, int w, int action, int arg, const char *label)
{
    if (a->nbtn >= 24) return;
    Button *b = &a->btn[a->nbtn++];
    b->r = (Rect){ x, y, w, BTN_H - 6 };
    b->action = action;
    b->arg = arg;
    snprintf(b->label, sizeof b->label, "%s", label);
}

static void layout(App *a)
{
    int W = a->W, H = a->H;
    a->r_top  = (Rect){ 0, 0, W, TOP_H };
    a->r_side = (Rect){ W - SIDE_W, TOP_H, SIDE_W, H - TOP_H };

    int mw = W - SIDE_W;
    int mh = (int)(mw * (MAP_LAT_TOP - MAP_LAT_BOT) / 360.0);
    int min_bottom = BTN_H + 250;
    if (H - TOP_H - mh < min_bottom) {
        mh = H - TOP_H - min_bottom;
        if (mh < 120) mh = 120;
        mw = (int)(mh * 360.0 / (MAP_LAT_TOP - MAP_LAT_BOT));
    }
    a->map.x = (W - SIDE_W - mw) / 2;
    a->map.y = TOP_H;
    a->map.w = mw;
    a->map.h = mh;

    a->r_btn = (Rect){ 0, TOP_H + mh, W - SIDE_W, BTN_H };
    a->r_fc  = (Rect){ 0, TOP_H + mh + BTN_H, W - SIDE_W, H - TOP_H - mh - BTN_H };

    /* gombok */
    a->nbtn = 0;
    int x = 8, y = a->r_btn.y + 3;
    add_button(a, x, y, 52, BTN_MODE, MAP_MUF, "MUF");  x += 56;
    add_button(a, x, y, 52, BTN_MODE, MAP_FOF2, "foF2"); x += 64;
    /* a sávgombok szélessége a rendelkezésre álló helyhez igazodik */
    int avail = a->r_btn.w - 8;
    int bw = (avail - x - 12 - 119) / NBANDS - 3;
    if (bw > 36) bw = 36;
    if (bw < 26) bw = 26;
    for (int b = 0; b < NBANDS; b++) {
        char l[8];
        snprintf(l, sizeof l, "%s", BANDS[b].name);
        l[strlen(l) - 1] = '\0';                /* "20m" -> "20" */
        add_button(a, x, y, bw, BTN_MODE, b, l);
        x += bw + 3;
    }
    x += 12;
    add_button(a, x, y, 30, BTN_TPREV, 0, "◀"); x += 33;
    add_button(a, x, y, 50, BTN_TNOW, 0, TR("Most", "Now")); x += 53;
    add_button(a, x, y, 30, BTN_TNEXT, 0, "▶");  x += 33;
    if (x + 104 < a->r_btn.w)
        add_button(a, a->r_btn.w - 100, y, 92, BTN_RELOAD, 0, TR("Frissítés", "Refresh"));
}

/* ---------- rajzolás: részek ---------- */

static void draw_top(App *a)
{
    Canvas *c = &a->cv;
    Rect r = a->r_top;
    cv_fill_rect(c, r.x, r.y, r.w, r.h, 0x0b1017);
    cv_fill_rect(c, r.x, r.y + r.h - 1, r.w, 1, C_BORDER);

    int x = 12;
    x += cv_text(c, x, 10, 17, FONT_BOLD, C_HEAD, "IonoProp");
    x += 8;
    x += cv_text(c, x, 13, 12, FONT_NORMAL, C_DIM, VERSION);
    x += 24;

    char s[256], tb[32], tu[32];
    time_t t = a->now + a->toff * 3600;
    strftime(tb, sizeof tb, "%Y-%m-%d %H:%M", localtime(&t));
    strftime(tu, sizeof tu, "%H:%M UTC", gmtime(&t));
    snprintf(s, sizeof s, "QTH: %s", a->locator);
    int qx = x;
    x += cv_text(c, x, 12, 14, FONT_BOLD, C_TEXT, s);
    x += 6 + cv_text(c, x + 6, 12, 13, FONT_NORMAL, C_ACCENT, "✎");
    a->r_qth = (Rect){ qx - 4, 4, x - qx + 8, TOP_H - 8 };
    x += 24;

    if (a->toff == 0) snprintf(s, sizeof s, "%s  (%s)", tb, tu);
    else snprintf(s, sizeof s, TR("%s  (%s)   előrejelzés: +%d óra", "%s  (%s)   forecast: +%d h"), tb, tu, a->toff);
    x += cv_text(c, x, 12, 14, plat_fullscreen() ? FONT_BOLD : FONT_NORMAL, a->toff ? C_WARN : C_TEXT, s) + 20;

    /* üzemmód (kattintásra vált: SSB -> CW -> FT8) */
    {
        int mx = x;
        x += cv_text(c, x, 13, 12, FONT_NORMAL, C_DIM, TR("Üzemmód ", "Mode "));
        int bw = text_width(13, FONT_BOLD, MODE_NAMES[g_mode]) + 14;
        cv_fill_rect(c, x, 8, bw, 22, 0x1f2a38);
        cv_rect(c, x, 8, bw, 22, C_ACCENT);
        cv_text(c, x + 7, 11, 13, FONT_BOLD, C_ACCENT, MODE_NAMES[g_mode]);
        x += bw;
        a->r_mode = (Rect){ mx, 4, x - mx, TOP_H - 8 };
        x += 18;
    }
    /* rádió */
    {
        int rx = x;
        if (a->rig) snprintf(s, sizeof s, TR("Rádió: %s", "Radio: %s"), a->rig_msg[0] ? a->rig_msg : TR("kész", "ready"));
        else snprintf(s, sizeof s, TR("Rádió: %s", "Radio: %s"), a->rig_spec[0] ? (a->rig_msg[0] ? a->rig_msg : TR("nincs kapcsolat", "not connected")) : TR("nincs (X)", "none (X)"));
        Color col = a->rig ? (a->rig_err ? C_BAD : C_GOOD) : (a->rig_spec[0] ? C_BAD : C_DIM);
        x += text_fit(c, x, 13, 12, FONT_NORMAL, col, s, 260);
        a->r_rig = (Rect){ rx - 2, 4, x - rx + 4, TOP_H - 8 };
        x += 20;
    }
    /* nyelv (kattintásra vált) */
    {
        const char *ln = g_lang == LANG_EN ? "EN" : "HU";
        int bw = text_width(12, FONT_BOLD, ln) + 12;
        cv_rect(c, x, 9, bw, 20, C_BORDER);
        cv_text(c, x + 6, 12, 12, FONT_BOLD, C_DIM, ln);
        a->r_lang = (Rect){ x - 2, 4, bw + 4, TOP_H - 8 };
        x += bw + 16;
    }
    /* riasztás ki/be */
    {
        int ax = x;
        x += cv_text(c, x, 13, 12, FONT_NORMAL, a->alerts_on ? C_ES : C_DIM,
                     a->alerts_on ? TR("Riasztás: be", "Alerts: on") : TR("Riasztás: ki", "Alerts: off"));
        a->r_alerts = (Rect){ ax - 2, 4, x - ax + 4, TOP_H - 8 };
        x += 16;
    }
    /* magyarázatok */
    {
        int bw = 22;
        cv_rect(c, x, 9, bw, 20, a->show_help ? C_ACCENT : C_BORDER);
        int tw = text_width(13, FONT_BOLD, "?");
        cv_text(c, x + (bw - tw) / 2, 11, 13, FONT_BOLD, a->show_help ? C_ACCENT : C_DIM, "?");
        a->r_help = (Rect){ x - 2, 4, bw + 4, TOP_H - 8 };
        x += bw + 16;
    }

    long age = (long)(time(NULL) - a->loaded) / 60;
    snprintf(s, sizeof s, TR("Adatok: %ld perce  -  %s", "Data: %ld min ago  -  %s"), age, a->status);
    int w = text_width(12, FONT_NORMAL, s);
    int xr = r.w - 12 - w;
    if (x + w < r.w - 12) cv_text(c, xr, 13, 12, FONT_NORMAL, C_DIM, s);

    if (a->psk) {
        int n30 = spots_recent(&a->spots, time(NULL));
        int con = pskr_connected(a->psk);
        if (con) snprintf(s, sizeof s, TR("PSK: %d spot / 30 perc", "PSK: %d spots / 30 min"), n30);
        else snprintf(s, sizeof s, "%s", TR("PSK: kapcsolódás...", "PSK: connecting..."));
        int w2 = text_width(12, FONT_BOLD, s);
        if (x + w + w2 + 30 < r.w - 12)
            cv_text(c, xr - w2 - 24, 13, 12, FONT_BOLD, con ? C_GOOD : C_WARN, s);
    }
}

static void draw_buttons(App *a)
{
    Canvas *c = &a->cv;
    Rect r = a->r_btn;
    cv_fill_rect(c, r.x, r.y, r.w, r.h, C_BG);
    for (int i = 0; i < a->nbtn; i++) {
        Button *b = &a->btn[i];
        int active = (b->action == BTN_MODE && b->arg == a->map.mode) ||
                     (b->action == BTN_TNOW && a->toff == 0);
        Color bg = active ? C_ACCENT : 0x223040;
        Color fg = active ? 0x081018 : C_TEXT;
        cv_fill_rect(c, b->r.x, b->r.y, b->r.w, b->r.h, bg);
        cv_rect(c, b->r.x, b->r.y, b->r.w, b->r.h, active ? C_ACCENT : C_BORDER);
        int tw = text_width(12, FONT_BOLD, b->label);
        cv_text(c, b->r.x + (b->r.w - tw) / 2, b->r.y + 5, 12, FONT_BOLD, fg, b->label);
    }
}

static void draw_forecast(App *a)
{
    Canvas *c = &a->cv;
    Rect r = a->r_fc;
    cv_fill_rect(c, r.x, r.y, r.w, r.h, C_BG);
    panel(c, (Rect){ r.x + 6, r.y + 4, r.w - 12, r.h - 10 });

    const Target *t = &a->tg[a->sel];
    const PathResult *pr = &a->res[a->sel];
    char s[160];
    if (t->nvis) snprintf(s, sizeof s, TR("24 órás előrejelzés: %s", "24-hour forecast: %s"), t->name);
    else snprintf(s, sizeof s, TR("24 órás előrejelzés: %s%s   (%.0f %s, %.0f°, %d ugrás)",
                                  "24-hour forecast: %s%s   (%.0f %s, %.0f°, %d hops)"),
                  t->name, t->lp ? TR(" – HOSSZÚ ÚT", " – LONG PATH") : "", dist_out(pr->dist_km), dist_unit(),
                  pr->bearing, pr->hops);
    int tx = r.x + 16 + text_fit(c, r.x + 16, r.y + 12, 14, FONT_BOLD, t->lp ? C_WARN : C_HEAD, s, r.w - 380);

    /* napkelte / napnyugta (helyi idő) a QTH-n és a célnál - a szürkevonalhoz */
    {
        char q[48], g[48];
        time_t rs, st, now = a->now + a->toff * 3600;
        int k = sun_rise_set(a->qlat, a->qlon, now, &rs, &st);
        if (k == 0) {
            char t1[8], t2[8];
            strftime(t1, sizeof t1, "%H:%M", localtime(&rs));
            strftime(t2, sizeof t2, "%H:%M", localtime(&st));
            snprintf(q, sizeof q, "%s / %s", t1, t2);
        } else snprintf(q, sizeof q, "%s", k > 0 ? TR("egész nap fenn", "up all day") : TR("egész nap lent", "down all day"));
        g[0] = '\0';
        if (!t->nvis) {
            k = sun_rise_set(t->lat, t->lon, now, &rs, &st);
            if (k == 0) {
                char t1[8], t2[8];
                strftime(t1, sizeof t1, "%H:%M", localtime(&rs));
                strftime(t2, sizeof t2, "%H:%M", localtime(&st));
                snprintf(g, sizeof g, "%s / %s", t1, t2);
            } else snprintf(g, sizeof g, "%s", k > 0 ? TR("egész nap fenn", "up all day") : TR("egész nap lent", "down all day"));
        }
        if (g[0]) snprintf(s, sizeof s, TR("☀ fel / le:  QTH %s   cél %s", "☀ rise / set:  QTH %s   target %s"), q, g);
        else snprintf(s, sizeof s, TR("☀ fel / le:  QTH %s", "☀ rise / set:  QTH %s"), q);
        int sw = text_width(12, FONT_NORMAL, s);
        int sx = r.x + r.w - 20 - sw;
        if (sx > tx + 12) cv_text(c, sx, r.y + 14, 12, FONT_NORMAL, 0xffc14d, s);
    }

    int lab = 48;
    int gx = r.x + 16 + lab, gy = r.y + 52;
    int cw = (r.w - 40 - lab) / FC_HOURS;
    if (cw > 40) cw = 40;
    int rows = NBANDS;
    int ch = (r.h - 52 - 30) / (rows + 3);
    if (ch > 26) ch = 26;
    if (ch < 9) ch = 9;
    a->fc_cellw = cw;
    a->r_fcgrid = (Rect){ gx, gy - 18, cw * FC_HOURS, rows * ch + 18 + 3 * ch };

    /* órák fejléce (helyi idő) */
    for (int h = 0; h < FC_HOURS; h++) {
        time_t th = a->fc.t0 + h * 3600;
        snprintf(s, sizeof s, "%d", localtime(&th)->tm_hour);
        int tw = text_width(11, FONT_NORMAL, s);
        cv_text(c, gx + h * cw + (cw - tw) / 2, gy - 17, 11, FONT_NORMAL,
                h == a->toff ? C_HEAD : C_DIM, s);
    }
    /* sávok */
    for (int b = NBANDS - 1, row = 0; b >= 0; b--, row++) {
        int y = gy + row * ch;
        cv_text(c, r.x + 16, y + (ch - 12) / 2 - 1, 12, FONT_NORMAL, C_TEXT, BANDS[b].name);
        for (int h = 0; h < FC_HOURS; h++) {
            int g = a->fc.grade[h][b];
            cv_fill_rect(c, gx + h * cw + 1, y + 1, cw - 2, ch - 2,
                         g ? grade_color(g) : 0x1f2833);
        }
    }
    /* MUF és LUF sor */
    int y = gy + rows * ch + 4;
    cv_text(c, r.x + 16, y, 11, FONT_BOLD, C_DIM, "MUF");
    cv_text(c, r.x + 16, y + ch, 11, FONT_BOLD, C_DIM, "LUF");
    for (int h = 0; h < FC_HOURS; h++) {
        snprintf(s, sizeof s, "%.0f", a->fc.muf[h]);
        int tw = text_width(11, FONT_NORMAL, s);
        cv_text(c, gx + h * cw + (cw - tw) / 2, y, 11, FONT_NORMAL, C_TEXT, s);
        if (a->fc.luf[h] > 0) snprintf(s, sizeof s, "%.0f", a->fc.luf[h]);
        else snprintf(s, sizeof s, "-");
        tw = text_width(11, FONT_NORMAL, s);
        cv_text(c, gx + h * cw + (cw - tw) / 2, y + ch, 11, FONT_NORMAL, C_DIM, s);
    }
    /* Kp sor: a mért, később a NOAA előrejelzése (színezve) */
    int yk = y + 2 * ch;
    cv_text(c, r.x + 16, yk, 11, FONT_BOLD, C_DIM, a->data.have_kpf ? "Kp" : "Kp*");
    for (int h = 0; h < FC_HOURS; h++) {
        double kp = a->fc.kp[h];
        if (isnan(kp)) snprintf(s, sizeof s, "-");
        else snprintf(s, sizeof s, "%.0f", kp);
        Color col = isnan(kp) ? C_DIM : kp < 4 ? C_DIM : kp < 5 ? C_WARN : C_BAD;
        int tw = text_width(11, kp >= 4 ? FONT_BOLD : FONT_NORMAL, s);
        cv_text(c, gx + h * cw + (cw - tw) / 2, yk, 11, kp >= 4 ? FONT_BOLD : FONT_NORMAL, col, s);
    }
    /* a kiválasztott óra kerete */
    cv_rect(c, gx + a->toff * cw - 1, gy - 19, cw + 2, rows * ch + 21 + 3 * ch, C_HEAD);
}

static void draw_side(App *a)
{
    Canvas *c = &a->cv;
    Rect r = a->r_side;
    cv_fill_rect(c, r.x, r.y, r.w, r.h, C_BG);
    int x = r.x + 14, y = r.y + 10, w = r.w - 28;
    char s[160];
    const PropData *d = &a->data;

    /* --- napaktivitás --- */
    panel(c, (Rect){ r.x + 4, y - 4, r.w - 10, 88 });
    cv_text(c, x, y, 14, FONT_BOLD, C_HEAD, TR("Napaktivitás", "Solar activity"));
    y += 22;
    if (d->have_sfi) {
        Color col = d->solar.sfi < 80 ? C_BAD : d->solar.sfi < 120 ? C_WARN : C_GOOD;
        int xx = x + cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, "SFI ");
        snprintf(s, sizeof s, "%.0f", d->solar.sfi);
        xx += cv_text(c, xx, y, 13, FONT_BOLD, col, s);
        snprintf(s, sizeof s, TR("  (90 napos átlag: %.0f)", "  (90-day mean: %.0f)"), d->solar.sfi_90day);
        cv_text(c, xx, y, 13, FONT_NORMAL, C_DIM, s);
    } else cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, TR("SFI: nincs adat", "SFI: no data"));
    y += 19;
    if (d->have_kp) {
        double kp = d->solar.kp;
        Color col = kp < 3 ? C_GOOD : kp < 5 ? C_WARN : C_BAD;
        int xx = x + cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, "Kp ");
        snprintf(s, sizeof s, "%.1f", kp);
        xx += cv_text(c, xx, y, 13, FONT_BOLD, col, s);
        cv_text(c, xx, y, 13, FONT_NORMAL, C_DIM,
                kp < 3 ? TR("  nyugodt", "  quiet") : kp < 4 ? TR("  enyhén zavart", "  unsettled")
                : kp < 5 ? TR("  zavart", "  active") : TR("  geomágneses vihar", "  geomagnetic storm"));
    } else cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, TR("Kp: nincs adat", "Kp: no data"));
    y += 19;
    if (d->have_xray) {
        char k = d->solar.xray_class[0];
        Color col = k == 'X' ? C_BAD : k == 'M' ? C_WARN : C_GOOD;
        int xx = x + cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, TR("Röntgen ", "X-ray "));
        xx += cv_text(c, xx, y, 13, FONT_BOLD, col, d->solar.xray_class);
        cv_text(c, xx, y, 13, FONT_NORMAL, C_DIM,
                k == 'X' ? TR("  erős napkitörés!", "  strong flare!") : k == 'M' ? TR("  napkitörés", "  flare") : TR("  nyugodt", "  quiet"));
    } else cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, TR("Röntgen: nincs adat", "X-ray: no data"));
    y += 30;

    /* --- rétegek --- */
    IonoPoint q;
    model_point(&a->model, a->qlat, a->qlon, &q);
    EsStatus es;
    es_status(d, a->psk ? &a->spots : NULL, a->qlat, a->qlon, time(NULL), &es);
    panel(c, (Rect){ r.x + 4, y - 4, r.w - 10, 122 });
    snprintf(s, sizeof s, TR("Rétegek a QTH fölött%s", "Layers above QTH%s"), a->toff ? TR(" (előrejelzés)", " (forecast)") : "");
    int xx = x + cv_text(c, x, y, 14, FONT_BOLD, C_HEAD, s);
    const char *dn = q.cosx > 0.2 ? TR("nappal", "day") : q.cosx > -0.05 ? TR("napkelte/nyugta", "sunrise/sunset")
                   : q.cosx > -0.3 ? TR("szürkület", "twilight") : TR("éjszaka", "night");
    snprintf(s, sizeof s, "  %s", dn);
    cv_text(c, xx, y + 2, 12, FONT_NORMAL, C_DIM, s);
    y += 22;
    cv_text(c, x, y, 13, FONT_NORMAL, C_TEXT, q.cosx > 0.05
            ? TR("D:  van - elnyeli az alsó sávokat", "D:  present - absorbs the low bands")
            : TR("D:  nincs - alsó sávok nyitva", "D:  none - low bands open"));
    y += 18;
    snprintf(s, sizeof s, "E:  foE %.1f MHz", q.foe);
    cv_text(c, x, y, 13, FONT_NORMAL, C_TEXT, s);
    y += 18;
    snprintf(s, sizeof s, "F2: foF2 %.2f MHz   hmF2 %.0f km   MUF %.1f", q.fof2, q.hmf2, q.mufd);
    text_fit(c, x, y, 13, FONT_NORMAL, C_TEXT, s, w);
    y += 18;
    if (a->toff) {
        cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, TR("Es: előre nem jelezhető", "Es: cannot be forecast"));
    } else if (es.have_foes) {
        snprintf(s, sizeof s, TR("Es: foEs %.1f MHz (%s, %d perce) - Es-MUF ~%.0f MHz", "Es: foEs %.1f MHz (%s, %d min ago) - Es MUF ~%.0f MHz"),
                 es.foes, es.foes_name, es.foes_age_min, es.es_muf);
        text_fit(c, x, y, 13, FONT_NORMAL, es.es_muf >= 28 ? C_ES : C_TEXT, s, w);
    } else {
        cv_text(c, x, y, 13, FONT_NORMAL, C_DIM, TR("Es: nincs friss foEs-mérés a közelben", "Es: no recent foEs measurement nearby"));
    }
    y += 18;
    snprintf(s, sizeof s, TR("Alapja: %d ionoszonda, mérési arány %.0f%%", "Based on %d ionosondes, measured share %.0f%%"),
             a->model.nref, q.conf * 100);
    cv_text(c, x, y, 11, FONT_NORMAL, C_DIM, s);
    y += 28;

    /* --- javaslat --- */
    panel(c, (Rect){ r.x + 4, y - 4, r.w - 10, 120 });
    cv_text(c, x, y, 14, FONT_BOLD, C_HEAD, TR("Javaslat", "Suggestion"));
    y += 22;
    const char *gname[3] = { TR("Közeli", "Local"), TR("Európa", "Europe"), "DX" };
    int gsum[3][NBANDS] = {{0}}, gcnt[3] = {0};
    for (int i = 0; i < a->n_default; i++) {
        int g = a->tg[i].nvis ? 0 : a->res[i].dist_km < 4000 ? 1 : 2;
        gcnt[g]++;
        for (int b = 0; b < NBANDS; b++) gsum[g][b] += a->res[i].grade[b];
    }
    for (int g = 0; g < 3; g++) {
        int xx2 = x + 4;
        cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, gname[g]);
        xx2 += 64;
        int shown = 0;
        for (int lvl = 3; lvl >= 2 && shown < 3; lvl--)
            for (int b = NBANDS - 1; b >= 0 && shown < 3; b--) {
                double avg = gcnt[g] ? (double)gsum[g][b] / gcnt[g] : 0;
                int a2 = avg >= 2.5 ? 3 : avg >= 1.75 ? 2 : 0;
                if (a2 != lvl) continue;
                cv_fill_rect(c, xx2, y + 3, 10, 10, grade_color(lvl));
                xx2 += 14;
                xx2 += cv_text(c, xx2, y, 13, FONT_BOLD, C_TEXT, BANDS[b].name) + 12;
                shown++;
            }
        if (!shown) {
            /* ha nincs jó/közepes: a legjobb gyenge sáv, amit érdemes próbálni */
            int best = -1;
            double bavg = 0.75;
            for (int b = 0; b < NBANDS; b++) {
                double avg = gcnt[g] ? (double)gsum[g][b] / gcnt[g] : 0;
                if (avg >= bavg) { bavg = avg; best = b; }
            }
            if (best >= 0) {
                cv_fill_rect(c, xx2, y + 3, 10, 10, grade_color(1));
                xx2 += 14;
                xx2 += cv_text(c, xx2, y, 13, FONT_BOLD, C_TEXT, BANDS[best].name);
                cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, TR("  gyenge, de próbálható", "  poor, but worth a try"));
            } else cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, TR("nincs használható sáv", "no usable band"));
        }
        y += 18;
    }
    /* sporadikus E most: valódi vételekből, vagy a közeli ionoszonda foEs-éből */
    {
        int xx2 = x + 4;
        cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, TR("Es most", "Es now"));
        xx2 += 64;
        int shown = 0;
        if (!a->toff) {
            for (int b = NBANDS - 1; b >= B20 && shown < 4; b--) {
                if (es.live.pairs[b] < SPOT_MINPAIR) continue;
                cv_fill_rect(c, xx2, y + 3, 10, 10, C_ES);
                xx2 += 14;
                snprintf(s, sizeof s, "%s", BANDS[b].name);
                xx2 += cv_text(c, xx2, y, 13, FONT_BOLD, C_TEXT, s);
                snprintf(s, sizeof s, " (%d)", es.live.pairs[b]);
                xx2 += cv_text(c, xx2, y, 12, FONT_NORMAL, C_DIM, s) + 10;
                shown++;
            }
            if (!shown && es.have_foes && es.es_muf >= BANDS[B10].f) {
                int best = B10;
                for (int b = NBANDS - 1; b >= B10; b--) if (es.es_muf >= BANDS[b].f) { best = b; break; }
                cv_fill_rect(c, xx2, y + 3, 10, 10, C_ES);
                xx2 += 14;
                xx2 += cv_text(c, xx2, y, 13, FONT_BOLD, C_TEXT, BANDS[best].name);
                cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, TR("  lehet (ionoszonda szerint)", "  possible (ionosonde)"));
                shown = 1;
            }
        }
        if (!shown)
            cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM,
                    a->toff ? TR("előre nem jelezhető", "cannot be forecast") : a->psk ? TR("nincs jele", "no sign")
                    : TR("nincs jele (PSK Reporter ki)", "no sign (PSK Reporter off)"));
        y += 18;
    }
    /* meteorraj: az aktív vagy a következő, a rádiáns legjobb óráival */
    {
        int xx2 = x + 4;
        cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, "Meteor");
        xx2 += 64;
        MeteorStatus ms;
        time_t t = a->now + a->toff * 3600;
        if (meteor_status(t, a->qlat, a->qlon, &ms)) {
            meteor_line(&ms, s, sizeof s);
            if (ms.active) {
                cv_fill_rect(c, xx2, y + 3, 10, 10, C_METEOR);
                xx2 += 14;
            }
            text_fit(c, xx2, y + 1, 12, ms.active ? FONT_BOLD : FONT_NORMAL, ms.active ? C_TEXT : C_DIM, s, x + w - xx2);
        } else {
            cv_text(c, xx2, y, 13, FONT_NORMAL, C_DIM, TR("nincs közeli meteorraj", "no meteor shower soon"));
        }
        y += 18;
    }
    y += 14;

    /* --- célterületek --- */
    int top = y - 4;
    panel(c, (Rect){ r.x + 4, top, r.w - 10, r.y + r.h - top - 6 });
    cv_text(c, x, y, 14, FONT_BOLD, C_HEAD, TR("Célterületek", "Targets"));
    int bx = x + w - NBANDS * 16;
    for (int b = 0; b < NBANDS; b++) {
        char l[8];
        snprintf(l, sizeof l, "%s", BANDS[b].name);
        l[strlen(l) - 1] = '\0';
        int tw = text_width(9, FONT_NORMAL, l);
        cv_text(c, bx + b * 16 + (14 - tw) / 2, y + 5, 9, FONT_NORMAL, C_DIM, l);
    }
    cv_text(c, bx - 74, y + 3, 11, FONT_NORMAL, C_DIM, dist_unit());
    cv_text(c, bx - 36, y + 3, 11, FONT_NORMAL, C_DIM, "MUF");
    y += 22;

    int avail = r.y + r.h - y - 12;
    int rh = avail / (a->nt > 0 ? a->nt : 1);
    if (rh > 20) rh = 20;
    if (rh < 12) rh = 12;
    a->row_h = rh;
    a->r_rows = (Rect){ r.x + 4, y, r.w - 10, rh * a->nt };
    a->tbl_bx = bx;
    for (int i = 0; i < a->nt; i++) {
        int yy = y + i * rh;
        if (yy + rh > r.y + r.h - 6) break;
        if (i == a->sel) cv_fill_rect(c, r.x + 6, yy, r.w - 14, rh, 0x24364a);
        int fs = rh >= 17 ? 12 : 11;
        char nm[64];
        snprintf(nm, sizeof nm, "%s%s%s", is_custom(a, i) ? "★ " : "", a->tg[i].name,
                 a->tg[i].lp ? " (LP)" : "");
        text_fit(c, x, yy + (rh - fs) / 2 - 1, fs, i == a->sel ? FONT_BOLD : FONT_NORMAL,
                 a->tg[i].lp ? C_WARN : is_custom(a, i) ? C_ACCENT : C_TEXT, nm, bx - x - 86);
        if (a->tg[i].nvis) snprintf(s, sizeof s, "-");
        else snprintf(s, sizeof s, "%.0f", dist_out(a->res[i].dist_km));
        int tw = text_width(11, FONT_NORMAL, s);
        cv_text(c, bx - 50 - tw, yy + (rh - 11) / 2 - 1, 11, FONT_NORMAL, C_DIM, s);
        snprintf(s, sizeof s, "%.1f", a->res[i].muf);
        tw = text_width(11, FONT_NORMAL, s);
        cv_text(c, bx - 8 - tw, yy + (rh - 11) / 2 - 1, 11, FONT_NORMAL, C_TEXT, s);
        for (int b = 0; b < NBANDS; b++) {
            int g = a->res[i].grade[b];
            cv_fill_rect(c, bx + b * 16, yy + 2, 14, rh - 4, g ? grade_color(g) : 0x1f2833);
            /* valódi vétel az utóbbi 30 percben: teli fehér pötty = erős jel,
             * szürke karika = csak gyenge, FT8-szintű jel */
            int sp = (a->psk && a->toff == 0) ? spot_on_target(a, &a->tg[i], b) : 0;
            double cx = bx + b * 16 + 7, cy = yy + rh / 2.0;
            if (sp == 2) {
                cv_circle(c, cx, cy, 3.4, 0x000000, 200);
                cv_circle(c, cx, cy, 2.5, 0xffffff, 255);
            } else if (sp == 1) {
                cv_circle(c, cx, cy, 3.0, 0x9aa6b4, 255);
                cv_circle(c, cx, cy, 1.7, g ? grade_color(g) : 0x1f2833, 255);
            }
        }
        if (a->res[i].polar || a->res[i].flare)
            cv_circle(c, bx - 64 - text_width(11, 0, "8888"), yy + rh / 2, 3, C_BAD, 255);
    }
    if (a->psk) {                               /* jelmagyarázat a táblázat alatt */
        int ly = y + a->nt * rh + 8;
        if (ly + 16 < r.y + r.h - 6) {
            int lx = x;
            cv_circle(c, lx + 4, ly + 7, 3.4, 0x000000, 200);
            cv_circle(c, lx + 4, ly + 7, 2.5, 0xffffff, 255);
            lx += 12 + cv_text(c, lx + 12, ly, 11, FONT_NORMAL, C_DIM, TR("erős vétel (≥ −10 dB)", "strong (≥ −10 dB)"));
            lx += 16;
            cv_circle(c, lx + 4, ly + 7, 3.0, 0x9aa6b4, 255);
            cv_circle(c, lx + 4, ly + 7, 1.7, C_PANEL, 255);
            lx += 12 + cv_text(c, lx + 12, ly, 11, FONT_NORMAL, C_DIM, TR("csak gyenge (FT8)", "weak only (FT8)"));
            lx += 16;
            cv_circle(c, lx + 4, ly + 7, 3, C_BAD, 255);
            cv_text(c, lx + 12, ly, 11, FONT_NORMAL, C_DIM, TR("zavar", "disturbance"));
        }
    }
}

/* ---------- szerkesztőablak: QTH lokátor, új célterület, rádió ---------- */

static void start_edit_kind(App *a, int kind)
{
    a->editing = kind;
    a->edit_err[0] = '\0';
    if (kind == EDIT_LOCATOR) snprintf(a->edit_buf, sizeof a->edit_buf, "%s", a->locator);
    else if (kind == EDIT_RIG) snprintf(a->edit_buf, sizeof a->edit_buf, "%.63s", a->rig_spec);
    else a->edit_buf[0] = '\0';
}

static void start_edit(App *a) { start_edit_kind(a, EDIT_LOCATOR); }

static void draw_edit(App *a)
{
    Canvas *c = &a->cv;
    cv_blend_rect(c, 0, 0, a->W, a->H, 0x000000, 150);
    int bw = 560, bh = 230;
    int bx = (a->W - bw) / 2, by = (a->H - bh) / 2;
    cv_fill_rect(c, bx, by, bw, bh, C_PANEL);
    cv_rect(c, bx, by, bw, bh, C_ACCENT);

    const char *title, *hint, *foot;
    if (a->editing == EDIT_TARGET) {
        title = TR("Új célterület", "New target");
        hint = TR("Lokátor (pl. PM95), hívójel (pl. JA1ABC) vagy prefix (pl. VK6, ZS)",
                  "Locator (e.g. PM95), callsign (e.g. JA1ABC) or prefix (e.g. VK6, ZS)");
        foot = TR("A saját célterületek a lista végére kerülnek (legfeljebb 10). Törlés: Delete.",
                  "Own targets go to the end of the list (max. 10). Delete key removes.");
    } else if (a->editing == EDIT_RIG) {
        title = TR("Rádió (Yaesu CAT, pl. FT-891)", "Radio (Yaesu CAT, e.g. FT-891)");
#ifdef _WIN32
        hint = TR("Port és sebesség, pl. COM5,38400   (üresen hagyva: nincs rádió)",
                  "Port and speed, e.g. COM5,38400   (empty: no radio)");
#else
        hint = TR("Port és sebesség, pl. /dev/ttyUSB0,38400   (üresen: nincs rádió)",
                  "Port and speed, e.g. /dev/ttyUSB0,38400   (empty: no radio)");
#endif
        foot = TR("Jobb kattintás egy sávra a táblázatban: a rádió arra a sávra áll.",
                  "Right-click a band in the table: the radio tunes to that band.");
    } else {
        title = TR("Saját QTH lokátor", "Your QTH locator");
        hint = TR("Maidenhead lokátor: 4, 6 vagy 8 karakter, pl. KN07sp",
                  "Maidenhead locator: 4, 6 or 8 characters, e.g. KN07sp");
#ifdef _WIN32
        foot = TR("A lokátort a program megjegyzi (%APPDATA%\\IonoProp\\settings.conf).",
                  "The locator is saved (%APPDATA%\\IonoProp\\settings.conf).");
#else
        foot = TR("A lokátort a program megjegyzi (~/.config/ionoprop/settings.conf).",
                  "The locator is saved (~/.config/ionoprop/settings.conf).");
#endif
    }
    cv_text(c, bx + 20, by + 16, 17, FONT_BOLD, C_HEAD, title);
    cv_text(c, bx + 20, by + 44, 13, FONT_NORMAL, C_DIM, hint);

    /* beviteli mező villogó kurzorral */
    int fx = bx + 20, fy = by + 72, fw = bw - 40, fh = 40;
    cv_fill_rect(c, fx, fy, fw, fh, 0x0b1017);
    cv_rect(c, fx, fy, fw, fh, a->edit_err[0] ? C_BAD : C_BORDER);
    int tw = cv_text(c, fx + 12, fy + 9, 20, FONT_BOLD, C_HEAD, a->edit_buf);
    if (time(NULL) % 2 == 0) cv_fill_rect(c, fx + 14 + tw, fy + 8, 2, 24, C_ACCENT);

    /* élő ellenőrzés */
    char s[160];
    if (a->edit_err[0]) {
        text_fit(c, bx + 20, by + 124, 13, FONT_NORMAL, C_BAD, a->edit_err, bw - 40);
    } else if (a->editing == EDIT_LOCATOR) {
        double la, lo;
        if (locator_to_latlon(a->edit_buf, &la, &lo) == 0) {
            snprintf(s, sizeof s, "%.2f° %s,  %.2f° %s", fabs(la), la >= 0 ? TR("É", "N") : TR("D", "S"),
                     fabs(lo), lo >= 0 ? TR("K", "E") : TR("Ny", "W"));
            cv_text(c, bx + 20, by + 124, 13, FONT_NORMAL, C_GOOD, s);
        }
    } else if (a->editing == EDIT_TARGET && a->edit_buf[0]) {
        Target t;
        char err[128];
        if (target_resolve(a->edit_buf, &t, err, sizeof err) == 0) {
            snprintf(s, sizeof s, "%s  –  %.0f %s, %.0f°", t.name,
                     dist_out(distance_km(a->qlat, a->qlon, t.lat, t.lon)), dist_unit(),
                     bearing_deg(a->qlat, a->qlon, t.lat, t.lon));
            text_fit(c, bx + 20, by + 124, 13, FONT_NORMAL, C_GOOD, s, bw - 40);
        }
    }
    cv_text(c, bx + 20, by + 164, 13, FONT_NORMAL, C_TEXT, TR("Enter: mentés     Esc: mégse", "Enter: save     Esc: cancel"));
    text_fit(c, bx + 20, by + 190, 11, FONT_NORMAL, C_DIM, foot, bw - 40);
}

/* új QTH beállítása: mentés, adatok újraszámolása, PSK-gyűjtő újraindítása */
static void apply_qth(App *a, const char *loc)
{
    normalize_locator(loc, a->locator, sizeof a->locator);
    locator_to_latlon(a->locator, &a->qlat, &a->qlon);
    if (config_save_locator(a->locator) != 0)
        snprintf(a->status, sizeof a->status, "%s", TR("A lokátort nem tudtam elmenteni", "Could not save the locator"));

    if (a->psk) {                               /* a környékbeli négyzetek változnak */
        pskr_stop(a->psk);
        a->psk = pskr_start(a->mqtt_host, a->mqtt_port, a->qlat, a->qlon, a->radius);
    }
    a->spots.n = 0;
    a->nt = fixed_targets(a);                   /* a térképen kijelölt cél törlődik */
    if (a->sel >= a->nt) a->sel = 0;
    a->map.cover_valid = 0;
    load(a, 0);                                 /* a gyorsítótárból, az új QTH szerint */
    if (a->map.mode >= 0) map_compute(&a->map, &a->model, &a->data, a->qlat, a->qlon, 1);
}

static void set_sel(App *a, int sel);

/* saját célterületek mentése (a beépítettek utáni rész) */
static void save_custom(App *a)
{
    if (targets_save_custom(&a->tg[a->n_default], a->n_custom) != 0)
        snprintf(a->status, sizeof a->status, "%s", TR("A célterületeket nem tudtam elmenteni", "Could not save the targets"));
}

/* új saját célterület a végére (a térképen kijelölt pont elé) */
static int add_custom(App *a, const Target *t)
{
    if (a->n_custom >= TARGETS_CUSTOM_MAX) return -1;
    int idx = fixed_targets(a);
    a->tg[idx] = *t;
    a->tg[idx].nvis = 0;
    a->n_custom++;
    a->nt = fixed_targets(a);                   /* a kijelölt pont törlődik */
    save_custom(a);
    predict_path(&a->model, &a->data, a->qlat, a->qlon, &a->tg[idx], &a->res[idx]);
    set_sel(a, idx);
    return idx;
}

static void remove_custom(App *a, int i)
{
    if (!is_custom(a, i)) return;
    int last = fixed_targets(a) - 1;
    for (int k = i; k < last; k++) { a->tg[k] = a->tg[k + 1]; a->res[k] = a->res[k + 1]; }
    a->n_custom--;
    a->nt = fixed_targets(a);
    save_custom(a);
    set_sel(a, i < a->nt ? i : a->nt - 1);
}

static void rig_connect(App *a)
{
    if (a->rig) { rig_close(a->rig); a->rig = NULL; }
    a->rig_msg[0] = '\0';
    a->rig_err = 0;
    if (!a->rig_spec[0]) return;
    char port[128];
    int baud;
    if (rig_parse_spec(a->rig_spec, port, sizeof port, &baud) != 0) {
        snprintf(a->rig_msg, sizeof a->rig_msg, "%s", TR("hibás beállítás", "invalid setting"));
        return;
    }
    a->rig = rig_open(port, baud, a->rig_msg, sizeof a->rig_msg);
    if (a->rig) snprintf(a->rig_msg, sizeof a->rig_msg, "%s, %d", port, baud);
}

/* a rádió a sávra áll, a választott üzemmód szokásos frekvenciájára */
static void rig_tune(App *a, int band)
{
    if (!a->rig) {
        snprintf(a->status, sizeof a->status, "%s", TR("Nincs rádió beállítva (X billentyű, vagy a fejlécben a „Rádió” felirat)",
                                                    "No radio set up (X key, or click \"Radio\" in the header)"));
        return;
    }
    long hz;
    char err[128];
    if (rig_tune_band(a->rig, band, g_mode, &hz, err, sizeof err) == 0) {
        snprintf(a->rig_msg, sizeof a->rig_msg, "%.3f MHz %s", hz / 1e6, MODE_NAMES[g_mode]);
        a->rig_err = 0;
    } else {
        snprintf(a->rig_msg, sizeof a->rig_msg, "%s", err);
        a->rig_err = 1;
    }
}

/* nyelvváltás: a beépített célok neve és a gombok felirata is változik */
static void set_lang(App *a, int lang)
{
    g_lang = lang;
    config_set("lang", lang_name(lang));
    Target tmp[TARGETS_MAX];
    int n = predict_default_targets(tmp, a->n_default);
    for (int i = 0; i < n; i++) {
        int lp = a->tg[i].lp;
        a->tg[i] = tmp[i];
        a->tg[i].lp = lp;
    }
    layout(a);
    status_from_load(a);
    recompute(a);
}

static void set_alerts(App *a, int on)
{
    a->alerts_on = on;
    alerts_set_enabled(on);
    if (!on) a->alert_text[0] = '\0';
    snprintf(a->status, sizeof a->status, "%s", on ? TR("Riasztás bekapcsolva", "Alerts on")
                                                   : TR("Riasztás kikapcsolva", "Alerts off"));
}

static void set_op_mode(App *a, int mode)
{
    g_mode = mode;
    config_set("mode", MODE_NAMES[mode]);
    a->map.cover_valid = 0;
    recompute(a);
}

/* billentyű szerkesztés közben. Visszatérés: 1 = újrarajzolás */
static int edit_key(App *a, int key)
{
    size_t len = strlen(a->edit_buf);

    if (key == K_ESC) { a->editing = 0; return 1; }
    if (key == K_ENTER) {
        if (a->editing == EDIT_LOCATOR) {
            double la, lo;
            size_t l = strlen(a->edit_buf);
            if ((l == 4 || l == 6 || l == 8) && locator_to_latlon(a->edit_buf, &la, &lo) == 0) {
                a->editing = 0;
                apply_qth(a, a->edit_buf);
            } else {
                snprintf(a->edit_err, sizeof a->edit_err,
                         "%s", TR("Hibás lokátor (2 betű A-R, 2 szám, 2 betű A-X)", "Invalid locator (2 letters A-R, 2 digits, 2 letters A-X)"));
            }
        } else if (a->editing == EDIT_TARGET) {
            Target t;
            if (target_resolve(a->edit_buf, &t, a->edit_err, sizeof a->edit_err) == 0) {
                if (add_custom(a, &t) < 0)
                    snprintf(a->edit_err, sizeof a->edit_err,
                             TR("Legfeljebb %d saját célterület lehet - előbb törölj egyet (Delete).", "At most %d own targets - delete one first (Delete)."),
                             TARGETS_CUSTOM_MAX);
                else a->editing = 0;
            }
        } else if (a->editing == EDIT_RIG) {
            char port[128];
            int baud;
            if (a->edit_buf[0] && rig_parse_spec(a->edit_buf, port, sizeof port, &baud) != 0) {
                snprintf(a->edit_err, sizeof a->edit_err,
                         "%s", TR("Hibás: port,sebesség (4800, 9600, 19200 vagy 38400)", "Invalid: port,speed (4800, 9600, 19200 or 38400)"));
                return 1;
            }
            snprintf(a->rig_spec, sizeof a->rig_spec, "%s", a->edit_buf);
            config_set("rig", a->rig_spec[0] ? a->rig_spec : NULL);
            rig_connect(a);
            a->editing = 0;
        }
        return 1;
    }
    if (key == K_BACKSPACE) {
        if (len > 0) a->edit_buf[len - 1] = '\0';
        a->edit_err[0] = '\0';
        return 1;
    }
    if (a->editing == EDIT_LOCATOR) {
        if (key > 0 && key < 128 && isalnum(key) && len < 8) {
            char tmp[12];
            a->edit_buf[len] = (char)key;
            a->edit_buf[len + 1] = '\0';
            normalize_locator(a->edit_buf, tmp, sizeof tmp);      /* KN07sp alakra */
            snprintf(a->edit_buf, sizeof a->edit_buf, "%s", tmp);
            a->edit_err[0] = '\0';
            return 1;
        }
        return 0;
    }
    /* célterület, rádió: bármilyen látható karakter */
    if (key >= 32 && key < 127 && len < sizeof a->edit_buf - 1) {
        a->edit_buf[len] = (char)(a->editing == EDIT_TARGET ? toupper(key) : key);
        a->edit_buf[len + 1] = '\0';
        a->edit_err[0] = '\0';
        return 1;
    }
    return 0;
}

/* felugró sáv a térkép tetején, ha nyílt valami */
static void draw_alert(App *a)
{
    a->r_alert_box = (Rect){ 0, 0, 0, 0 };
    if (!a->alert_text[0] || time(NULL) > a->alert_until) return;
    Canvas *c = &a->cv;
    const char *title = TR("Sávnyitás!", "Band opening!");
    const char *hint = TR("(kattints a bezáráshoz)", "(click to close)");
    /* sorokra bontás */
    char buf[sizeof a->alert_text];
    snprintf(buf, sizeof buf, "%s", a->alert_text);
    const char *lines[12];
    int nl = 0, w = text_width(14, FONT_BOLD, title) + 12 + text_width(11, FONT_NORMAL, hint);
    for (char *p = strtok(buf, "\n"); p && nl < 12; p = strtok(NULL, "\n")) {
        lines[nl++] = p;
        int lw = text_width(13, FONT_NORMAL, p) + 20;
        if (lw > w) w = lw;
    }
    w += 28;
    if (w > a->map.w - 20) w = a->map.w - 20;
    int h = 34 + nl * 20 + 8;
    int x = a->map.x + (a->map.w - w) / 2, y = a->map.y + 12;
    cv_blend_rect(c, x, y, w, h, 0x1a0f24, 235);
    cv_rect(c, x, y, w, h, C_ES);
    cv_rect(c, x + 1, y + 1, w - 2, h - 2, C_ES);
    int tx = x + 14;
    tx += cv_text(c, tx, y + 9, 14, FONT_BOLD, C_ES, title) + 12;
    cv_text(c, tx, y + 12, 11, FONT_NORMAL, C_DIM, hint);
    for (int i = 0; i < nl; i++) {
        cv_fill_rect(c, x + 14, y + 40 + i * 20, 8, 8, C_ES);
        text_fit(c, x + 28, y + 35 + i * 20, 13, FONT_NORMAL, C_TEXT, lines[i], w - 40);
    }
    a->r_alert_box = (Rect){ x, y, w, h };
}

/* szöveg tördelve egy adott szélességre; visszatér: az új y */
static int draw_wrapped(Canvas *c, int x, int y, int w, int size, int font, Color col, const char *txt, int lh)
{
    char line[640] = "", word[128];
    const char *p = txt;
    while (*p) {
        while (*p == ' ') p++;
        int n = 0;
        while (p[n] && p[n] != ' ' && n < (int)sizeof word - 1) n++;
        if (!n) break;
        memcpy(word, p, (size_t)n);
        word[n] = '\0';
        p += n;
        char tryl[640];
        snprintf(tryl, sizeof tryl, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && text_width(size, font, tryl) > w) {
            cv_text(c, x, y, size, font, col, line);
            y += lh;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", tryl);
        }
    }
    if (line[0]) { cv_text(c, x, y, size, font, col, line); y += lh; }
    return y;
}

/* magyarázatok: a térkép fölött, a fogalom félkövérrel */
static void draw_help(App *a)
{
    a->r_help_box = (Rect){ 0, 0, 0, 0 };
    if (!a->show_help) return;
    Canvas *c = &a->cv;
    Rect r = { 16, TOP_H + 12, a->r_btn.w - 32, a->r_fc.y + a->r_fc.h - TOP_H - 24 };
    cv_blend_rect(c, r.x, r.y, r.w, r.h, 0x0b121a, 242);
    cv_rect(c, r.x, r.y, r.w, r.h, C_ACCENT);
    int x = r.x + 18, y = r.y + 12, w = r.w - 36;
    int tx = x + cv_text(c, x, y, 15, FONT_BOLD, C_HEAD, TR("Mit jelentenek az értékek?", "What do the values mean?")) + 14;
    cv_text(c, tx, y + 3, 11, FONT_NORMAL, C_DIM, TR("(kattints vagy ? a bezáráshoz)", "(click or ? to close)"));
    y += 28;
    /* a fogalmak oszlopa olyan széles, mint a leghosszabb fogalom */
    int kw = 0;
    for (const char *q = help_text(); *q; ) {
        const char *e = strchr(q, '\n');
        size_t n = e ? (size_t)(e - q) : strlen(q);
        const char *d = strstr(q, " - ");
        if (d && d < q + n) {
            char k[96];
            size_t kl = (size_t)(d - q) < sizeof k - 1 ? (size_t)(d - q) : sizeof k - 1;
            memcpy(k, q, kl);
            k[kl] = '\0';
            int tw = text_width(13, FONT_BOLD, k);
            if (tw > kw) kw = tw;
        }
        q = e ? e + 1 : q + n;
    }
    kw += 16;
    if (kw > w / 3) kw = w / 3;
    const char *p = help_text();
    while (*p && y < r.y + r.h - 16) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char ln[700];
        if (n >= sizeof ln) n = sizeof ln - 1;
        memcpy(ln, p, n);
        ln[n] = '\0';
        char *dash = strstr(ln, " - ");
        if (dash) {
            *dash = '\0';
            cv_text(c, x, y, 13, FONT_BOLD, C_ACCENT, ln);
            y = draw_wrapped(c, x + kw, y, w - kw, 13, FONT_NORMAL, C_TEXT, dash + 3, 17);
        } else {
            y = draw_wrapped(c, x, y, w, 13, FONT_NORMAL, C_TEXT, ln, 17);
        }
        y += 6;
        p = e ? e + 1 : p + n;
    }
    a->r_help_box = r;
}

static void render(App *a)
{
    Canvas *c = &a->cv;
    cv_clear(c, C_BG);
    draw_top(a);
    map_draw(c, &a->map, &a->data, &a->model, a->qlat, a->qlon, &a->tg[a->sel]);

    /* mód felirat a térkép sarkában */
    char s[96];
    if (a->map.mode == MAP_MUF) snprintf(s, sizeof s, "%s", TR("MUF(3000) térkép", "MUF(3000) map"));
    else if (a->map.mode == MAP_FOF2) snprintf(s, sizeof s, "%s", TR("foF2 térkép", "foF2 map"));
    else snprintf(s, sizeof s, TR("%s lefedettség innen: %s", "%s coverage from %s"), BANDS[a->map.mode].name, a->locator);
    int tw = text_width(13, FONT_BOLD, s);
    cv_blend_rect(c, a->map.x + a->map.w - tw - 22, a->map.y + 8, tw + 14, 24, 0x000000, 150);
    cv_text(c, a->map.x + a->map.w - tw - 15, a->map.y + 12, 13, FONT_BOLD, C_HEAD, s);

    draw_spots(a);
    draw_alert(a);
    draw_buttons(a);
    draw_forecast(a);
    draw_side(a);
    draw_help(a);
    if (a->editing) draw_edit(a);
}

/* ---------- vezérlés ---------- */

static void set_mode(App *a, int mode)
{
    a->map.mode = mode;
    if (mode >= 0 && (!a->map.cover_valid || a->map.cover_t != a->model.t))
        map_compute(&a->map, &a->model, &a->data, a->qlat, a->qlon, 1);
}

static void set_toff(App *a, int toff)
{
    if (toff < 0) toff = 0;
    if (toff > FC_HOURS - 1) toff = FC_HOURS - 1;
    if (toff == a->toff) return;
    a->toff = toff;
    recompute(a);
}

static void set_sel(App *a, int sel)
{
    if (sel < 0 || sel >= a->nt) return;
    a->sel = sel;
    predict_forecast(&a->model, &a->data, a->qlat, a->qlon, &a->tg[sel],
                     a->now - a->now % 3600, &a->fc);
}

/* visszatérés: 1 = újra kell rajzolni, -1 = kilépés */
static int on_key(App *a, int k)
{
    switch (k) {
    case K_LEFT:  set_toff(a, a->toff - 1); return 1;
    case K_RIGHT: set_toff(a, a->toff + 1); return 1;
    case K_HOME:  set_toff(a, 0); return 1;
    case K_UP:    set_sel(a, a->sel - 1); return 1;
    case K_DOWN:  set_sel(a, a->sel + 1); return 1;
    case 'm': case 'M': set_mode(a, MAP_MUF); return 1;
    case 'f': case 'F': set_mode(a, MAP_FOF2); return 1;
    case 'r': case 'R': return 2;        /* frissítés */
    case 'p': case 'P': a->show_spots = !a->show_spots; return 1;
    case 'l': case 'L': start_edit(a); return 1;
    case K_F11: plat_set_fullscreen(!plat_fullscreen()); return 1;
    case K_ESC:
        /* teljes képernyőn az Esc csak kilép belőle, nem zárja be a programot */
        if (plat_fullscreen()) { plat_set_fullscreen(0); return 1; }
        return -1;
    case 'q': case 'Q': return -1;
    case '0': set_mode(a, B10); return 1;
    case '-': set_mode(a, B8); return 1;
    case '=': set_mode(a, B6); return 1;
    case 'c': case 'C': set_mode(a, B11); return 1;   /* 11 m (CB) */
    case 'u': case 'U': set_op_mode(a, (g_mode + 1) % NMODES); return 1;
    case 'a': case 'A': set_alerts(a, !a->alerts_on); return 1;
    case 'n': case 'N': set_lang(a, g_lang == LANG_HU ? LANG_EN : LANG_HU); return 1;
    case '?': a->show_help = !a->show_help; return 1;
    case 'k': case 'K':
        units_set(!g_units_mi);
        snprintf(a->status, sizeof a->status, "%s", g_units_mi ? TR("Távolság mérföldben", "Distances in miles")
                                                               : TR("Távolság km-ben", "Distances in km"));
        return 1;
    case 't': case 'T': start_edit_kind(a, EDIT_TARGET); return 1;
    case 'x': case 'X': start_edit_kind(a, EDIT_RIG); return 1;
    case 'h': case 'H':                                 /* hosszú út ki/be */
        if (!a->tg[a->sel].nvis) {
            a->tg[a->sel].lp = !a->tg[a->sel].lp;
            predict_path(&a->model, &a->data, a->qlat, a->qlon, &a->tg[a->sel], &a->res[a->sel]);
            set_sel(a, a->sel);
        }
        return 1;
    case 's': case 'S':                                 /* a kijelölt pont mentése */
        if (is_picked(a, a->sel)) {
            Target t = a->tg[a->sel];
            const char *pre = TR("Kijelölt: ", "Picked: ");
            size_t pl = strlen(pre);
            if (strncmp(t.name, pre, pl) == 0) memmove(t.name, t.name + pl, strlen(t.name + pl) + 1);
            if (add_custom(a, &t) < 0)
                snprintf(a->status, sizeof a->status, TR("Legfeljebb %d saját célterület lehet", "At most %d own targets"), TARGETS_CUSTOM_MAX);
        }
        return 1;
    case K_DELETE:
        if (is_custom(a, a->sel)) remove_custom(a, a->sel);
        return 1;
    default:
        if (k >= '1' && k <= '9') { set_mode(a, k - '1'); return 1; }
        if (k == '+') { start_edit_kind(a, EDIT_TARGET); return 1; }
    }
    return 0;
}

static int on_click(App *a, int x, int y, int button)
{
    if (button == 4) { set_toff(a, a->toff - 1); return 1; }   /* görgő */
    if (button == 5) { set_toff(a, a->toff + 1); return 1; }
    if (button == 3) {                                         /* jobb gomb: rádió a sávra */
        if (in_rect(&a->r_rows, x, y) && x >= a->tbl_bx && a->row_h > 0) {
            int b = (x - a->tbl_bx) / 16;
            if (b >= 0 && b < NBANDS) { rig_tune(a, b); return 1; }
        }
        for (int i = 0; i < a->nbtn; i++)
            if (in_rect(&a->btn[i].r, x, y) && a->btn[i].action == BTN_MODE && a->btn[i].arg >= 0) {
                rig_tune(a, a->btn[i].arg);
                return 1;
            }
        return 0;
    }
    if (button != 1) return 0;
    if (a->r_alert_box.w && in_rect(&a->r_alert_box, x, y)) { a->alert_text[0] = '\0'; return 1; }
    if (a->show_help) { a->show_help = 0; return 1; }       /* bármilyen kattintás bezárja */
    if (in_rect(&a->r_help, x, y)) { a->show_help = 1; return 1; }
    if (in_rect(&a->r_alerts, x, y)) { set_alerts(a, !a->alerts_on); return 1; }
    if (in_rect(&a->r_qth, x, y)) { start_edit(a); return 1; }
    if (in_rect(&a->r_mode, x, y)) { set_op_mode(a, (g_mode + 1) % NMODES); return 1; }
    if (in_rect(&a->r_lang, x, y)) { set_lang(a, g_lang == LANG_HU ? LANG_EN : LANG_HU); return 1; }
    if (in_rect(&a->r_rig, x, y)) { start_edit_kind(a, EDIT_RIG); return 1; }

    for (int i = 0; i < a->nbtn; i++) {
        Button *b = &a->btn[i];
        if (!in_rect(&b->r, x, y)) continue;
        switch (b->action) {
        case BTN_MODE:   set_mode(a, b->arg); break;
        case BTN_TPREV:  set_toff(a, a->toff - 1); break;
        case BTN_TNOW:   set_toff(a, 0); break;
        case BTN_TNEXT:  set_toff(a, a->toff + 1); break;
        case BTN_RELOAD: return 2;
        }
        return 1;
    }
    double lat, lon;
    if (screen_to_map(&a->map, x, y, &lat, &lon) == 0) {
        set_custom_target(a, lat, lon);
        predict_path(&a->model, &a->data, a->qlat, a->qlon, &a->tg[a->sel], &a->res[a->sel]);
        set_sel(a, a->sel);
        return 1;
    }
    if (in_rect(&a->r_rows, x, y) && a->row_h > 0) {
        set_sel(a, (y - a->r_rows.y) / a->row_h);
        return 1;
    }
    if (in_rect(&a->r_fcgrid, x, y) && a->fc_cellw > 0) {
        set_toff(a, (x - a->r_fcgrid.x) / a->fc_cellw);
        return 1;
    }
    return 0;
}

/* ---------- indítás ---------- */

static void usage(const char *p)
{
    printf("IonoProp %s - grafikus felület\n\n", VERSION);
    printf("Használat: %s [-l LOKÁTOR] [-c MIN] [-g SZxMA]\n\n", p);
    printf("  -l LOKÁTOR  saját QTH (alap: %s)\n", DEFAULT_LOCATOR);
    printf("  -c MIN      ionoszondák minimális megbízhatósága (alap: 25)\n");
    printf("  -g SZxMA    ablakméret, pl. 1400x860\n");
    printf("  -o FÁJL     ablak nélkül egy képet ment (PPM), teszteléshez\n");
    printf("  -N          PSK Reporter (valódi vételek) kikapcsolása\n");
    printf("  -R KM       a QTH körüli körzet a spotokhoz (alap: 300 km)\n");
    printf("  -M HOST[:PORT]  MQTT szerver (alap: %s:%d)\n", PSKR_HOST, PSKR_PORT);
    printf("  -t ÓRA      időeltolás (0-23), -m MÓD (-2 MUF, -1 foF2, 0-9 sáv), -s CÉL\n");
    printf("  -F          teljes képernyő (shack-kijelző, Raspberry Pi)\n");
    printf("  -C MP       a térkép magától vált MP másodpercenként:\n"
           "              MUF, foF2, 40 m, 20 m, 15 m, 10 m\n\n");
    printf("Billentyűk: ←/→ idő, ↑/↓ cél, m MUF, f foF2, 1-9 0 - = c sávok, u üzemmód,\n"
           "            t új célterület, s kijelölt pont mentése, Delete saját cél törlése,\n"
           "            h hosszú út, x rádió, n nyelv (HU/EN), a riasztás, p spotok,\n"
           "            k km/mérföld, ? magyarázatok, r frissítés, F11 teljes képernyő, q kilépés\n");
}

int main(int argc, char **argv)
{
    App *a = &app;
    const char *outfile = NULL;
    int W = 1360, H = 820, opt, mode = MAP_MUF, sel = 0, fullscreen = 0, cycle_sec = 0;
    snprintf(a->locator, sizeof a->locator, "%s", DEFAULT_LOCATOR);
    int loc_given = 0;
    a->min_cs = 25;

    while ((opt = getopt(argc, argv, "l:c:g:o:t:m:s:NR:M:FC:h")) != -1) {
        switch (opt) {
        case 'l': snprintf(a->locator, sizeof a->locator, "%s", optarg); loc_given = 1; break;
        case 'c': a->min_cs = atoi(optarg); break;
        case 'g': if (sscanf(optarg, "%dx%d", &W, &H) != 2) { usage(argv[0]); return 1; } break;
        case 'o': outfile = optarg; break;
        case 't': a->toff = atoi(optarg); break;
        case 'm': mode = atoi(optarg); break;
        case 's': sel = atoi(optarg); break;
        case 'N': a->no_psk = 1; break;
        case 'F': fullscreen = 1; break;
        case 'C': cycle_sec = atoi(optarg); if (cycle_sec > 0 && cycle_sec < 5) cycle_sec = 5; break;
        case 'R': a->radius = atof(optarg); break;
        case 'M': {
            char *colon;
            snprintf(a->mqtt_host, sizeof a->mqtt_host, "%s", optarg);
            if ((colon = strrchr(a->mqtt_host, ':')) != NULL) { *colon = '\0'; a->mqtt_port = atoi(colon + 1); }
            break;
        }
        default:  usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }
    /* a lokátor: -l kapcsoló, vagy a mentett, vagy első indításkor megkérdezzük */
    int first_start = 0;
    if (!loc_given && !config_load_locator(a->locator, sizeof a->locator)) first_start = 1;
    {
        double la, lo;
        if (locator_to_latlon(a->locator, &la, &lo) != 0 && !loc_given) {
            snprintf(a->locator, sizeof a->locator, "%s", DEFAULT_LOCATOR);   /* hibás mentett érték */
            first_start = 1;
        }
    }
    if (W < 1000) W = 1000;
    if (H < 650) H = 650;
    if (locator_to_latlon(a->locator, &a->qlat, &a->qlon) != 0) {
        fprintf(stderr, "Hibás lokátor: %s\n", a->locator);
        return 1;
    }
    {
        char tmp[12];
        normalize_locator(a->locator, tmp, sizeof tmp);
        snprintf(a->locator, sizeof a->locator, "%s", tmp);
    }
    if (font_init() != 0) return 1;
    if (spots_init(&a->spots) != 0) { fprintf(stderr, "Kevés a memória\n"); return 1; }
    config_load(&g_params);
    if (a->radius <= 0) a->radius = 300.0;
    if (!a->mqtt_host[0]) snprintf(a->mqtt_host, sizeof a->mqtt_host, "%s", PSKR_HOST);
    if (a->mqtt_port <= 0) a->mqtt_port = PSKR_PORT;
    a->show_spots = 1;

    lang_load();
    units_load();
    a->alerts_on = alerts_enabled();
    a->headless = outfile != NULL;
    a->n_default = predict_default_targets(a->tg, TARGETS_MAX - TARGETS_CUSTOM_MAX - 1);
    a->n_custom = targets_load_custom(&a->tg[a->n_default], TARGETS_CUSTOM_MAX);
    a->nt = fixed_targets(a);
    {
        char v[64];
        if (config_get("mode", v, sizeof v) && mode_from_name(v) >= 0) g_mode = mode_from_name(v);
        if (config_get("rig", v, sizeof v)) snprintf(a->rig_spec, sizeof a->rig_spec, "%s", v);
    }
    rig_connect(a);
    a->sel = (sel >= 0 && sel < a->nt) ? sel : 0;
    a->map.mode = mode < MAP_MUF || mode >= NBANDS ? MAP_MUF : mode;
    if (a->toff < 0 || a->toff >= FC_HOURS) a->toff = 0;

    a->W = W;
    a->H = H;
    if (cv_init(&a->cv, W, H) != 0) return 1;
    layout(a);

    /* --- ablak nélküli mód (teszt) --- */
    if (outfile) {
        load(a, 0);
        render(a);
        int rc = cv_save_ppm(&a->cv, outfile);
        cv_free(&a->cv);
        font_done();
        return rc ? 1 : 0;
    }

    if (fullscreen) plat_set_fullscreen(1);
    if (plat_open(TR("IonoProp - ionoszféra és sávjavaslat", "IonoProp - ionosphere and band advisor"), W, H, 1000, 650) != 0) return 1;
    /* nagyított kijelzőn (pl. Windows 150%) a vászon a valódi felbontásban rajzol */
    if (plat_scale() != 1.0) {
        cv_set_scale(&a->cv, plat_scale());
        if (cv_resize(&a->cv, a->W, a->H) != 0) return 1;
    }

    /* első kép a letöltés előtt, hogy látszódjon, hogy dolgozik */
    snprintf(a->status, sizeof a->status, "%s", TR("Adatok letöltése...", "Downloading data..."));
    cv_clear(&a->cv, C_BG);
    cv_text(&a->cv, 40, 40, 18, FONT_BOLD, C_TEXT, TR("IonoProp - adatok letöltése...", "IonoProp - downloading data..."));
    plat_present(a->cv.px, a->cv.w, a->cv.h);
    load(a, 0);
    set_mode(a, a->map.mode);
    if (first_start) start_edit(a);            /* első indítás: kérjük be a lokátort */
    if (!a->no_psk) {
        validate_log_trim(14);
        a->psk = pskr_start(a->mqtt_host, a->mqtt_port, a->qlat, a->qlon, a->radius);
    }

    int running = 1, need_render = 1, need_blit = 1;
    int last_min = -1;
    /* -C: a térkép körbejár (shack-kijelző) */
    static const int cycle_modes[] = { MAP_MUF, MAP_FOF2, B40, B20, B15, B10 };
    int cycle_i = 0;
    time_t cycle_t = time(NULL), last_input = 0;

    while (running) {
        PlatEvent ev;
        while (running && plat_next_event(&ev)) {
            int r = 0;
            switch (ev.type) {
            case EV_EXPOSE:
                need_blit = 1;
                break;
            case EV_RESIZE:
                if (ev.w >= 100 && ev.h >= 100 &&
                    (ev.w != a->W || ev.h != a->H || plat_scale() != a->cv.s)) {
                    a->W = ev.w;
                    a->H = ev.h;
                    cv_set_scale(&a->cv, plat_scale());
                    if (cv_resize(&a->cv, a->W, a->H) != 0) { running = 0; break; }
                    layout(a);
                    need_render = 1;
                }
                break;
            case EV_KEY:
                if (a->editing) r = edit_key(a, ev.key);
                else r = on_key(a, ev.key);
                break;
            case EV_CLICK:
                if (a->editing) break;              /* szerkesztés közben nincs kattintás */
                r = on_click(a, ev.x, ev.y, ev.button);
                break;
            case EV_QUIT:
                running = 0;
                break;
            }
            if (ev.type == EV_KEY || ev.type == EV_CLICK) last_input = time(NULL);
            if (r == -1) running = 0;
            if (r == 2) {   /* frissítés: előbb jelezzük, aztán letöltünk */
                snprintf(a->status, sizeof a->status, "%s", TR("Frissítés...", "Refreshing..."));
                render(a);
                plat_present(a->cv.px, a->cv.w, a->cv.h);
                load(a, 1);
                set_mode(a, a->map.mode);
            }
            if (r) need_render = 1;
        }
        if (!running) break;

        /* időzítők: percenként újraszámolás, 15 percenként letöltés */
        time_t now = time(NULL);
        int min = (int)(now / 60);
        /* 15 percenként; ha nem jött adat (pl. bekapcsoláskor még nincs hálózat), percenként */
        if (now - a->loaded >= RELOAD_SEC || (a->load_n <= 0 && now - a->loaded >= 60)) {
            load(a, 0);
            set_mode(a, a->map.mode);
            need_render = 1;
        } else if (min != last_min) {
            if (last_min != -1) {
                recompute(a);
                set_mode(a, a->map.mode);
            }
            need_render = 1;
        }
        last_min = min;
        if (a->editing) need_render = 1;            /* villogó kurzor */

        /* körbejáró térkép; kézi használat után egy percig nem vált */
        if (cycle_sec > 0 && !a->editing && a->toff == 0 &&
            now - cycle_t >= cycle_sec && now - last_input >= 60) {
            cycle_i = (cycle_i + 1) % (int)(sizeof cycle_modes / sizeof cycle_modes[0]);
            set_mode(a, cycle_modes[cycle_i]);
            cycle_t = now;
            need_render = 1;
        }

        /* valódi vételek: legfeljebb 5 másodpercenként rajzolunk újra miattuk */
        if (psk_update(a) > 0 && now - a->last_spot_render >= 5) {
            need_render = 1;
            a->last_spot_render = now;
        }

        if (a->alert_beep) { plat_beep(); a->alert_beep = 0; }
        if (a->alert_text[0] && now > a->alert_until) { a->alert_text[0] = '\0'; need_render = 1; }
        if (need_render) { render(a); need_render = 0; need_blit = 1; }
        if (need_blit) {
            plat_present(a->cv.px, a->cv.w, a->cv.h);
            need_blit = 0;
        }
        plat_wait(a->editing ? 500 : 1000);
    }

    pskr_stop(a->psk);
    rig_close(a->rig);
    plat_close();
    spots_free(&a->spots);
    cv_free(&a->cv);
    font_done();
    return 0;
}

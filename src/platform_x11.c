/*
 * platform_x11.c - ablak és események Linuxon (X11)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include "platform.h"

static Display *dpy;
static Window   win;
static GC       gc;
static Visual  *vis;
static int      depth;
static Atom     wm_delete;
static XImage  *img;
static const uint32_t *img_px;   /* a kép, amire az XImage mutat */
static int      img_w, img_h;
static double   g_scale = 1.0;
static int      want_fs;          /* teljes képernyő kérve */
static int      is_fs;            /* most teljes képernyőn van */
static int      mapped;
static int      save_x, save_y, save_w, save_h;   /* az ablak helye a teljes képernyő előtt */
static Cursor   blank_cursor;
static int      cursor_hidden;
static time_t   last_motion;
static int      pend_w, pend_h;   /* az induló méret (ha az X nem küld ConfigureNotify-t) */
#define CURSOR_HIDE_SEC 4

/* Van-e ablakkezelő, ami érti a teljes képernyő kérést (EWMH)?
 * Kioszk módban (csak X, ablakkezelő nélkül) nincs: ott mi méretezünk. */
static int have_ewmh(void)
{
    Atom check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", True);
    if (check == None) return 0;
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    int ok = XGetWindowProperty(dpy, DefaultRootWindow(dpy), check, 0, 1, False, XA_WINDOW,
                                &type, &fmt, &n, &after, &data) == Success && n == 1;
    if (data) XFree(data);
    return ok;
}

static void send_fs_state(int on)
{
    Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    if (!mapped) {
        /* még nincs kint: elég a tulajdonság, az ablakkezelő ebből indul */
        if (on) XChangeProperty(dpy, win, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&fs, 1);
        else XDeleteProperty(dpy, win, state);
        return;
    }
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = win;
    e.xclient.message_type = state;
    e.xclient.format = 32;
    e.xclient.data.l[0] = on ? 1 : 0;          /* _NET_WM_STATE_ADD / _REMOVE */
    e.xclient.data.l[1] = (long)fs;
    e.xclient.data.l[3] = 1;
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

static void show_cursor(int show)
{
    if (!dpy || show == !cursor_hidden) return;
    if (show) XUndefineCursor(dpy, win);
    else XDefineCursor(dpy, win, blank_cursor);
    cursor_hidden = !show;
    XFlush(dpy);
}

void plat_set_fullscreen(int on)
{
    want_fs = on ? 1 : 0;
    if (!dpy || is_fs == want_fs) return;
    int scr = DefaultScreen(dpy);
    if (want_fs) {
        XWindowAttributes wa;
        Window child;
        if (XGetWindowAttributes(dpy, win, &wa)) {
            XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &save_x, &save_y, &child);
            save_w = wa.width;
            save_h = wa.height;
        }
    }
    if (have_ewmh()) {
        send_fs_state(want_fs);
    } else if (want_fs) {
        /* ablakkezelő nélkül: az egész képernyőt kitöltjük */
        XMoveResizeWindow(dpy, win, 0, 0, (unsigned)DisplayWidth(dpy, scr), (unsigned)DisplayHeight(dpy, scr));
        XRaiseWindow(dpy, win);
    } else if (save_w > 0) {
        XMoveResizeWindow(dpy, win, save_x, save_y, (unsigned)save_w, (unsigned)save_h);
    }
    is_fs = want_fs;
    last_motion = time(NULL);
    if (!is_fs) show_cursor(1);
    XFlush(dpy);
}

int plat_fullscreen(void) { return is_fs; }

int plat_open(const char *title, int w, int h, int min_w, int min_h)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Nem tudok csatlakozni az X szerverhez (DISPLAY=%s)\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "");
        return -1;
    }
    int scr = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, scr);
    depth = DefaultDepth(dpy, scr);
    if (depth < 24 || vis->red_mask != 0xff0000 || vis->blue_mask != 0xff) {
        fprintf(stderr, "24 bites színmélység szükséges (most: %d bit)\n", depth);
        XCloseDisplay(dpy);
        dpy = NULL;
        return -1;
    }

    /* nagyítás: IONOPROP_SCALE, különben kis képernyőn kicsinyítünk,
     * hogy a legkisebb ablakméret is elférjen (Raspberry Pi kijelzők) */
    {
        int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);
        const char *e = getenv("IONOPROP_SCALE");
        double s = e ? atof(e) : 0;
        if (s < 0.5 || s > 3.0) {
            s = 1.0;
            if (min_w > 0 && sw < min_w) s = (double)sw / min_w;
            if (min_h > 0 && sh < min_h && (double)sh / min_h < s) s = (double)sh / min_h;
            if (s < 0.6) s = 0.6;
        }
        g_scale = s;
        w = (int)(w * s);
        h = (int)(h * s);
        if (w > sw) w = sw;
        if (h > sh) h = sh;
    }
    min_w = (int)(min_w * plat_scale());
    min_h = (int)(min_h * plat_scale());
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, (unsigned)w, (unsigned)h,
                              0, 0, 0x0f151d);
    XStoreName(dpy, win, "IonoProp");
    {
        Atom netname = XInternAtom(dpy, "_NET_WM_NAME", False);
        Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
        XChangeProperty(dpy, win, netname, utf8, 8, PropModeReplace,
                        (const unsigned char *)title, (int)strlen(title));
    }
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize;
        hints->min_width = min_w;
        hints->min_height = min_h;
        XSetWMNormalHints(dpy, win, hints);
        XFree(hints);
    }
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask |
                 PointerMotionMask);
    {
        /* üres egérmutató a teljes képernyős módhoz */
        static char empty[8];
        Pixmap pm = XCreateBitmapFromData(dpy, win, empty, 8, 8);
        XColor black;
        memset(&black, 0, sizeof black);
        blank_cursor = XCreatePixmapCursor(dpy, pm, pm, &black, &black, 0, 0);
        XFreePixmap(dpy, pm);
    }
    if (want_fs) {
        if (have_ewmh()) send_fs_state(1);
        else XMoveResizeWindow(dpy, win, 0, 0, (unsigned)DisplayWidth(dpy, scr), (unsigned)DisplayHeight(dpy, scr));
        is_fs = 1;
        last_motion = time(NULL);
    }
    XMapWindow(dpy, win);
    mapped = 1;
    {
        /* ablakkezelő nélkül nem jön méretesemény: a tényleges méretet mi jelentjük */
        int fw = want_fs && !have_ewmh() ? DisplayWidth(dpy, scr) : w;
        int fh = want_fs && !have_ewmh() ? DisplayHeight(dpy, scr) : h;
        pend_w = (int)(fw / g_scale + 0.5);
        pend_h = (int)(fh / g_scale + 0.5);
    }
    gc = XCreateGC(dpy, win, 0, NULL);
    return 0;
}

/* Linuxon a nagyítás az IONOPROP_SCALE környezeti változóval adható meg
 * (pl. IONOPROP_SCALE=1.5 ./ionoprop-gui nagy felbontású kijelzőn,
 * 0.8 kis kijelzőn); ha nincs megadva, a plat_open() választ */
double plat_scale(void) { return g_scale; }

void plat_present(const uint32_t *px, int w, int h)
{
    if (!dpy) return;
    if (!img || px != img_px || w != img_w || h != img_h) {
        if (img) { img->data = NULL; XDestroyImage(img); }
        /* az XImage nem másolja a pixeleket, csak rájuk mutat */
        img = XCreateImage(dpy, vis, (unsigned)depth, ZPixmap, 0, (char *)px,
                           (unsigned)w, (unsigned)h, 32, 0);
        img_px = px;
        img_w = w;
        img_h = h;
        if (!img) return;
    }
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XFlush(dpy);
}

static int map_key(XKeyEvent *ev)
{
    char txt[8] = "";
    KeySym ks;
    int n = XLookupString(ev, txt, sizeof txt - 1, &ks, NULL);
    switch (ks) {
    case XK_Left:      case XK_KP_Left:  return K_LEFT;
    case XK_Right:     case XK_KP_Right: return K_RIGHT;
    case XK_Up:        case XK_KP_Up:    return K_UP;
    case XK_Down:      case XK_KP_Down:  return K_DOWN;
    case XK_Home:      case XK_KP_Home:  return K_HOME;
    case XK_Escape:    return K_ESC;
    case XK_Return:    case XK_KP_Enter: return K_ENTER;
    case XK_BackSpace: return K_BACKSPACE;
    case XK_Delete:    case XK_KP_Delete: return K_DELETE;
    case XK_F11:       return K_F11;
    }
    if (n == 1 && (unsigned char)txt[0] >= 32 && (unsigned char)txt[0] < 127) return txt[0];
    return 0;
}

int plat_next_event(PlatEvent *e)
{
    memset(e, 0, sizeof *e);
    if (pend_w > 0) {
        e->type = EV_RESIZE;
        e->w = pend_w;
        e->h = pend_h;
        pend_w = pend_h = 0;
        return 1;
    }
    while (dpy && XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) { e->type = EV_EXPOSE; return 1; }
            break;
        case ConfigureNotify:
            e->type = EV_RESIZE;
            e->w = (int)(ev.xconfigure.width / plat_scale() + 0.5);
            e->h = (int)(ev.xconfigure.height / plat_scale() + 0.5);
            return 1;
        case MotionNotify:
            last_motion = time(NULL);
            show_cursor(1);
            break;
        case KeyPress:
            e->key = map_key(&ev.xkey);
            if (e->key) { e->type = EV_KEY; return 1; }
            break;
        case ButtonPress:
            last_motion = time(NULL);
            show_cursor(1);
            e->type = EV_CLICK;
            e->x = (int)(ev.xbutton.x / plat_scale());
            e->y = (int)(ev.xbutton.y / plat_scale());
            e->button = (int)ev.xbutton.button;
            return 1;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == wm_delete) { e->type = EV_QUIT; return 1; }
            break;
        }
    }
    /* teljes képernyőn a mozdulatlan egérmutató eltűnik */
    if (dpy && is_fs && !cursor_hidden && time(NULL) - last_motion >= CURSOR_HIDE_SEC) show_cursor(0);
    return 0;
}

void plat_wait(int ms)
{
    if (!dpy) return;
    if (XPending(dpy)) return;
    int fd = ConnectionNumber(dpy);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    select(fd + 1, &fds, NULL, NULL, &tv);
}

void plat_beep(void)
{
    if (dpy) { XBell(dpy, 0); XFlush(dpy); }
}

void plat_close(void)
{
    if (!dpy) return;
    if (img) { img->data = NULL; XDestroyImage(img); img = NULL; }
    if (blank_cursor) XFreeCursor(dpy, blank_cursor);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    dpy = NULL;
}

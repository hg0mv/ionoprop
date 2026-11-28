/*
 * platform_win32.c - ablak és események Windowson (Win32 API)
 *
 * Az ablakkezelő (WndProc) az eseményeket egy kis sorba teszi, a program
 * fő ciklusa (gui.c) innen veszi ki őket, ugyanúgy, mint Linuxon.
 */
#include <windows.h>
#include <string.h>
#include <stdio.h>

#include "platform.h"

#define QLEN 128
static PlatEvent queue[QLEN];
static int q_head, q_tail;

static HWND     hwnd;
static double   g_scale = 1.0;       /* DPI / 96 */
static int      min_cw, min_ch;      /* legkisebb belső méret */
static const uint32_t *last_px;      /* az utoljára kirakott kép (WM_PAINT-hez) */
static int      last_w, last_h;
static int      want_fs, is_fs;      /* teljes képernyő */
static WINDOWPLACEMENT saved_wp;
static DWORD    saved_hide;          /* GetTickCount: mikor mozdult utoljára az egér */
static int      cursor_hidden;

/* a monitor DPI-je (Windows 10-től ablakonként, előtte a rendszeré) */
static double dpi_scale(HWND h)
{
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn;
    static int tried;
    if (!tried) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) fn = (GetDpiForWindowFn)(void (*)(void))GetProcAddress(u, "GetDpiForWindow");
        tried = 1;
    }
    UINT dpi = 0;
    if (h && fn) dpi = fn(h);
    if (!dpi) {
        HDC dc = GetDC(NULL);
        dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return dpi ? dpi / 96.0 : 1.0;
}

double plat_scale(void) { return g_scale; }

static int L(int v) { return (int)(v / g_scale + 0.5); }   /* fizikai -> logikai */

static void push(const PlatEvent *e)
{
    int next = (q_head + 1) % QLEN;
    if (next == q_tail) return;               /* tele: eldobjuk */
    queue[q_head] = *e;
    q_head = next;
}

static void push_key(int key)
{
    PlatEvent e;
    memset(&e, 0, sizeof e);
    e.type = EV_KEY;
    e.key = key;
    push(&e);
}

static void blit(HDC dc)
{
    if (!last_px || last_w <= 0 || last_h <= 0) return;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = last_w;
    bi.bmiHeader.biHeight = -last_h;          /* felülről lefelé */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;             /* 0x00RRGGBB = a DIB BGRA sorrendje */
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, (DWORD)last_w, (DWORD)last_h, 0, 0, 0, (UINT)last_h,
                      last_px, &bi, DIB_RGB_COLORS);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PlatEvent e;
    memset(&e, 0, sizeof e);
    switch (msg) {
    case WM_CLOSE:
        e.type = EV_QUIT;
        push(&e);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            e.type = EV_RESIZE;
            e.w = L(LOWORD(lp));
            e.h = L(HIWORD(lp));
            push(&e);
        }
        return 0;
    case 0x02E0: {                              /* WM_DPICHANGED: másik monitorra került */
        g_scale = HIWORD(wp) / 96.0;
        if (g_scale < 1.0) g_scale = 1.0;
        const RECT *sr = (const RECT *)lp;
        SetWindowPos(h, NULL, sr->left, sr->top, sr->right - sr->left, sr->bottom - sr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        RECT r = { 0, 0, (LONG)(min_cw * g_scale), (LONG)(min_ch * g_scale) };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        blit(dc);
        EndPaint(h, &ps);
        e.type = EV_EXPOSE;
        push(&e);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;                               /* ne villogjon: mi rajzolunk mindent */
    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:   push_key(K_LEFT); return 0;
        case VK_RIGHT:  push_key(K_RIGHT); return 0;
        case VK_UP:     push_key(K_UP); return 0;
        case VK_DOWN:   push_key(K_DOWN); return 0;
        case VK_HOME:   push_key(K_HOME); return 0;
        case VK_ESCAPE: push_key(K_ESC); return 0;
        case VK_RETURN: push_key(K_ENTER); return 0;
        case VK_BACK:   push_key(K_BACKSPACE); return 0;
        case VK_DELETE: push_key(K_DELETE); return 0;
        case VK_F11:    push_key(K_F11); return 0;
        }
        break;                                  /* a többiből WM_CHAR lesz */
    case WM_CHAR:
        if (wp >= 32 && wp < 127) push_key((int)wp);
        return 0;
    case WM_MOUSEMOVE:
        saved_hide = GetTickCount();
        if (cursor_hidden) { ShowCursor(TRUE); cursor_hidden = 0; }
        break;
    case WM_LBUTTONDOWN:
        e.type = EV_CLICK;
        e.x = L((short)LOWORD(lp));
        e.y = L((short)HIWORD(lp));
        e.button = 1;
        push(&e);
        return 0;
    case WM_RBUTTONDOWN:
        e.type = EV_CLICK;
        e.x = L((short)LOWORD(lp));
        e.y = L((short)HIWORD(lp));
        e.button = 3;
        push(&e);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };   /* képernyő-koordináta */
        ScreenToClient(h, &pt);
        e.type = EV_CLICK;
        e.x = L(pt.x);
        e.y = L(pt.y);
        e.button = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 4 : 5;
        push(&e);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* teljes képernyő: keret nélkül, a monitor teljes területén */
static void apply_fullscreen(int on)
{
    if (!hwnd || on == is_fs) return;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (on) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        saved_wp.length = sizeof saved_wp;
        if (GetWindowPlacement(hwnd, &saved_wp) &&
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &saved_wp);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        if (cursor_hidden) { ShowCursor(TRUE); cursor_hidden = 0; }
    }
    is_fs = on;
    saved_hide = GetTickCount();
}

void plat_set_fullscreen(int on)
{
    want_fs = on ? 1 : 0;
    apply_fullscreen(want_fs);
}

int plat_fullscreen(void) { return is_fs; }

int plat_open(const char *title, int w, int h, int min_w, int min_h)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    min_cw = min_w;
    min_ch = min_h;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));          /* ionoprop.rc: 1 = ikon */
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = CreateSolidBrush(RGB(0x0f, 0x15, 0x1d));
    wc.lpszClassName = L"IonoPropWindow";
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Nem tudom létrehozni az ablakot.", L"IonoProp", MB_ICONERROR);
        return -1;
    }

    /* a nagyítás az ablak létrehozása előtt a rendszeré */
    g_scale = dpi_scale(NULL);
    if (g_scale < 1.0) g_scale = 1.0;
    w = (int)(w * g_scale);
    h = (int)(h * g_scale);

    /* az ablak belső mérete legyen w x h, és férjen el a képernyőn */
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int maxw = work.right - work.left, maxh = work.bottom - work.top;
    if (ww > maxw) ww = maxw;
    if (wh > maxh) wh = maxh;

    wchar_t wtitle[256];
    if (!MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256)) wcscpy(wtitle, L"IonoProp");
    hwnd = CreateWindowExW(0, wc.lpszClassName, wtitle, WS_OVERLAPPEDWINDOW,
                           work.left + (maxw - ww) / 2, work.top + (maxh - wh) / 2, ww, wh,
                           NULL, NULL, inst, NULL);
    if (!hwnd) {
        MessageBoxW(NULL, L"Nem tudom létrehozni az ablakot.", L"IonoProp", MB_ICONERROR);
        return -1;
    }
    g_scale = dpi_scale(hwnd);                  /* a tényleges monitoré */
    if (g_scale < 1.0) g_scale = 1.0;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    if (want_fs) apply_fullscreen(1);
    UpdateWindow(hwnd);
    return 0;
}

void plat_present(const uint32_t *px, int w, int h)
{
    last_px = px;
    last_w = w;
    last_h = h;
    if (!hwnd) return;
    HDC dc = GetDC(hwnd);
    if (dc) {
        blit(dc);
        ReleaseDC(hwnd, dc);
    }
}

int plat_next_event(PlatEvent *ev)
{
    MSG m;
    /* előbb a Windows üzeneteit dolgozzuk fel, azok töltik a sort */
    while (q_head == q_tail && PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    /* teljes képernyőn a mozdulatlan egérmutató eltűnik */
    if (is_fs && !cursor_hidden && GetTickCount() - saved_hide > 4000) {
        ShowCursor(FALSE);
        cursor_hidden = 1;
    }
    if (q_head == q_tail) return 0;
    *ev = queue[q_tail];
    q_tail = (q_tail + 1) % QLEN;
    return 1;
}

void plat_wait(int ms)
{
    if (q_head != q_tail) return;
    MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)ms, QS_ALLINPUT);
}

void plat_beep(void)
{
    MessageBeep(MB_ICONASTERISK);
}

void plat_close(void)
{
    if (hwnd) DestroyWindow(hwnd);
    hwnd = NULL;
}

// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.
//
// Code taken from MAME, linuxloader, sm2-emu, Flycast or segatools stays under the license of
// its own project; such code is marked with its origin.

// The picture is placed by Present itself: the swap chain is made a COPY one (it takes a
// destination rectangle), the window takes the mode's style and size, and the parts of the
// client area outside the picture are painted black. A multisampled back buffer cannot be
// presented that way: the picture then fills the window (the window still has the aspect ratio
// in WINDOW mode).

#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"

static wchar_t g_ini[MAX_PATH];
static volatile LONG g_aspect = WL_ASPECT_ORIGINAL;
static volatile LONG g_mode = WL_MODE_WINDOW;
static int g_width, g_height;      // internal resolution, 0 = AUTO
static volatile LONG g_apply;      // the window must take the mode again
static volatile LONG g_toggle;     // Alt+Enter: window <-> full screen
static volatile LONG g_repaint;    // the black bars must be painted again

static HWND g_wnd;
static WNDPROC g_proc;
static int g_unicode;
static int g_bbW, g_bbH;           // the game's picture
static int g_copy;                 // the swap chain takes a destination rectangle
static HWND g_child;               // or it shows the picture in this child window (Direct3D 11)
static int g_gdiSetting = 1;       // [Display] PRESENT: GDI (1) or DIRECT (0)
static int g_gdi;                  // windowsloader copies the picture itself, by GDI
static RECT g_childRect;
static int g_appliedMode = -1;
static RECT g_dest, g_client;      // the picture in the client area, and that area
static int g_full = 1;             // the picture is the whole client area

static const char *g_aspectNames[WL_ASPECT_COUNT] = {"ORIGINAL", "4:3", "16:9", "4:3_LETTERBOX"};
static const char *g_modeNames[WL_MODE_COUNT] = {"WINDOW", "WINDOW_STRETCH", "FULLSCREEN", "FULLSCREEN_STRETCH"};

// ------------------------------------------------------------------------------
// windowsloader.ini
// ------------------------------------------------------------------------------

static const char *g_defaults =
    "# windowsloader settings (the same keys as linuxloader.ini)\r\n"
    "\r\n"
    "[Display]\r\n"
    "# Render resolution of the game: AUTO keeps the game's own setting. Read when the game starts,\r\n"
    "# for the games whose module can set it.\r\n"
    "WIDTH = AUTO\r\n"
    "HEIGHT = AUTO\r\n"
    "\r\n"
    "# ORIGINAL: the picture keeps the shape of the game's render resolution. 4:3 or 16:9: the shape of\r\n"
    "# the frame the picture is shown in; the picture keeps its own shape inside, with black bars where\r\n"
    "# the shapes differ (a 4:3 game in 16:9: bars left and right). 4:3_LETTERBOX: the same as 4:3.\r\n"
    "ASPECT_RATIO = ORIGINAL\r\n"
    "\r\n"
    "# WINDOW: a window at the aspect ratio, square pixels. WINDOW_STRETCH: the picture fills the window,\r\n"
    "# which can be resized. FULLSCREEN: the whole screen, the picture at the aspect ratio between black\r\n"
    "# bars. FULLSCREEN_STRETCH: the picture fills the screen. F11 or Alt+Enter: window / full screen.\r\n"
    "DISPLAY_MODE = WINDOW\r\n"
    "\r\n"
    "# Direct3D 9 and 11 games in a window. GDI: windowsloader copies the picture into the window itself;\r\n"
    "# DIRECT: the game's own Present, faster, but some PCs show nothing of it (Windows 11 on a laptop).\r\n"
    "PRESENT = GDI\r\n";

static int name_index(const char *const *names, int count, const char *value, int def)
{
    for (int i = 0; i < count; i++)
        if (!_stricmp(value, names[i]))
            return i;
    return def;
}

static void read_key(const wchar_t *key, char *out, size_t size, const wchar_t *def)
{
    wchar_t v[64];
    GetPrivateProfileStringW(L"Display", key, def, v, 64, g_ini);
    WideCharToMultiByte(CP_UTF8, 0, v, -1, out, (int)size, NULL, NULL);
}

void wl_display_load(const wchar_t *iniPath)
{
    wcsncpy_s(g_ini, MAX_PATH, iniPath, _TRUNCATE);
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES)
    {
        FILE *f = _wfopen(g_ini, L"wb");
        if (f)
        {
            fputs(g_defaults, f);
            fclose(f);
        }
    }
    char v[64];
    read_key(L"ASPECT_RATIO", v, sizeof v, L"ORIGINAL");
    g_aspect = name_index(g_aspectNames, WL_ASPECT_COUNT, v, WL_ASPECT_ORIGINAL);
    read_key(L"DISPLAY_MODE", v, sizeof v, L"WINDOW");
    g_mode = name_index(g_modeNames, WL_MODE_COUNT, v, WL_MODE_WINDOW);
    read_key(L"PRESENT", v, sizeof v, L"GDI");
    g_gdiSetting = _stricmp(v, "DIRECT") != 0;
    read_key(L"WIDTH", v, sizeof v, L"AUTO");
    g_width = atoi(v);
    read_key(L"HEIGHT", v, sizeof v, L"AUTO");
    g_height = atoi(v);
    if (g_width <= 0 || g_height <= 0)
        g_width = g_height = 0;
    char res[32] = "AUTO";
    if (g_width)
        snprintf(res, sizeof res, "%dx%d", g_width, g_height);
    wl_log("display: ASPECT_RATIO %s, DISPLAY_MODE %s, resolution %s, picture %s", g_aspectNames[g_aspect], g_modeNames[g_mode], res,
           g_gdiSetting ? "copied by GDI (PRESENT = GDI)" : "presented by the game (PRESENT = DIRECT)");
}

static void write_key(const wchar_t *key, const char *value)
{
    if (!g_ini[0])
        return;
    wchar_t v[64];
    MultiByteToWideChar(CP_UTF8, 0, value, -1, v, 64);
    WritePrivateProfileStringW(L"Display", key, v, g_ini);
}

int wl_display_aspect_by_name(const char *name)
{
    return name_index(g_aspectNames, WL_ASPECT_COUNT, name, -1);
}

int wl_display_mode_by_name(const char *name)
{
    return name_index(g_modeNames, WL_MODE_COUNT, name, -1);
}

const char *wl_display_aspect_name(int aspect)
{
    return aspect >= 0 && aspect < WL_ASPECT_COUNT ? g_aspectNames[aspect] : "?";
}

const char *wl_display_mode_name(int mode)
{
    return mode >= 0 && mode < WL_MODE_COUNT ? g_modeNames[mode] : "?";
}

int wl_display_aspect(void)
{
    return g_aspect;
}

int wl_display_mode(void)
{
    return g_mode;
}

void wl_display_set(int aspect, int mode)
{
    if (aspect < 0 || aspect >= WL_ASPECT_COUNT || mode < 0 || mode >= WL_MODE_COUNT)
        return;
    InterlockedExchange(&g_aspect, aspect);
    InterlockedExchange(&g_mode, mode);
    write_key(L"ASPECT_RATIO", g_aspectNames[aspect]);
    write_key(L"DISPLAY_MODE", g_modeNames[mode]);
    InterlockedExchange(&g_apply, 1);
}

static int g_internalSupported;

void wl_display_set_internal_supported(int supported)
{
    g_internalSupported = supported;
}

int wl_display_internal_supported(void)
{
    return g_internalSupported;
}

void wl_display_internal(int *width, int *height)
{
    *width = g_width;
    *height = g_height;
}

void wl_display_set_internal(int width, int height)
{
    char v[16];
    snprintf(v, sizeof v, "%d", width);
    write_key(L"WIDTH", width > 0 ? v : "AUTO");
    snprintf(v, sizeof v, "%d", height);
    write_key(L"HEIGHT", height > 0 ? v : "AUTO");
    // The game reads its resolution once, when it starts: g_width / g_height stay as they are
}

// ------------------------------------------------------------------------------
// Placing the window and the picture
// ------------------------------------------------------------------------------

// Shape of the frame the picture is shown in
// The picture is placed by windowsloader (else it fills the window whatever the layout)
static int placed(void)
{
    return g_copy || g_child || g_gdi;
}

static double frame_aspect(void)
{
    if (g_aspect == WL_ASPECT_4_3 || g_aspect == WL_ASPECT_4_3_LETTERBOX)
        return 4.0 / 3.0;
    if (g_aspect == WL_ASPECT_16_9)
        return 16.0 / 9.0;
    return g_bbH > 0 ? (double)g_bbW / g_bbH : 16.0 / 9.0;
}

// The largest rectangle of the given shape centred in w x h
static RECT fit(int w, int h, double aspect)
{
    RECT r;
    int fw = w, fh = (int)floor(w / aspect + 0.5);
    if (fh > h)
    {
        fh = h;
        fw = (int)floor(h * aspect + 0.5);
    }
    r.left = (w - fw) / 2;
    r.top = (h - fh) / 2;
    r.right = r.left + fw;
    r.bottom = r.top + fh;
    return r;
}

static void layout(void)
{
    GetClientRect(g_wnd, &g_client);
    int w = g_client.right, h = g_client.bottom;
    int stretch = g_mode == WL_MODE_WINDOW_STRETCH || g_mode == WL_MODE_FULLSCREEN_STRETCH;
    RECT frame = stretch ? g_client : fit(w, h, frame_aspect());
    RECT pic = frame;
    if (g_aspect != WL_ASPECT_ORIGINAL && g_bbH > 0)
    {
        // The picture whole inside the frame at the game's own shape (its render resolution's), its
        // bars as parts of the frame: a 4:3 game in a 16:9 frame gets bars left and right, a 16:9 one
        // in a 4:3 frame above and below. The bars follow the frame when it is stretched (a 4:3
        // screen getting a 16:9 signal).
        double own = (double)g_bbW / g_bbH, frameShape = frame_aspect();
        int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
        if (own > frameShape)
        {
            int ph = (int)floor(fh * frameShape / own + 0.5);
            pic.top = frame.top + (fh - ph) / 2;
            pic.bottom = pic.top + ph;
        }
        else
        {
            int pw = (int)floor(fw * own / frameShape + 0.5);
            pic.left = frame.left + (fw - pw) / 2;
            pic.right = pic.left + pw;
        }
    }
    if (!EqualRect(&pic, &g_dest))
        InterlockedExchange(&g_repaint, 1);
    g_dest = pic;
    g_full = EqualRect(&pic, &g_client);
    if (g_child && !EqualRect(&pic, &g_childRect))
    {
        g_childRect = pic;
        wl_log("display: client %dx%d, picture %ldx%ld at %ld,%ld", w, h, pic.right - pic.left, pic.bottom - pic.top, pic.left, pic.top);
        SetWindowPos(g_child, NULL, pic.left, pic.top, pic.right - pic.left, pic.bottom - pic.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

static void apply_mode(void)
{
    if (!g_wnd)
        return;
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    DWORD ex = (DWORD)GetWindowLongW(g_wnd, GWL_EXSTYLE) & ~(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
    int mode = g_mode;
    int x, y, w, h;
    DWORD style;

    if (mode == WL_MODE_FULLSCREEN || mode == WL_MODE_FULLSCREEN_STRETCH)
    {
        style = WS_POPUP | WS_VISIBLE | (g_child ? WS_CLIPCHILDREN : 0);
        x = mi.rcMonitor.left;
        y = mi.rcMonitor.top;
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    else
    {
        style = mode == WL_MODE_WINDOW_STRETCH ? WS_OVERLAPPEDWINDOW | WS_VISIBLE : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        style |= g_child ? WS_CLIPCHILDREN : 0;
        RECT frame = {0, 0, 0, 0};
        AdjustWindowRectEx(&frame, style, FALSE, ex);
        int frameW = frame.right - frame.left, frameH = frame.bottom - frame.top;
        const RECT &work = mi.rcWork;

        // A stretched window keeps the size it was given; otherwise the client area takes the
        // picture's size at the aspect ratio (widened or heightened), shrunk to fit the screen
        RECT cur;
        GetClientRect(g_wnd, &cur);
        int cw, ch;
        if (mode == WL_MODE_WINDOW_STRETCH && g_appliedMode == WL_MODE_WINDOW_STRETCH && cur.right > 0)
        {
            cw = cur.right;
            ch = cur.bottom;
        }
        else
        {
            double a = frame_aspect();
            cw = g_bbW;
            ch = g_bbH;
            if ((double)cw / ch < a)
                cw = (int)floor(ch * a + 0.5);
            else
                ch = (int)floor(cw / a + 0.5);
            int maxW = work.right - work.left - frameW, maxH = work.bottom - work.top - frameH;
            if (cw > maxW)
            {
                ch = MulDiv(ch, maxW, cw);
                cw = maxW;
            }
            if (ch > maxH)
            {
                cw = MulDiv(cw, maxH, ch);
                ch = maxH;
            }
        }
        w = cw + frameW;
        h = ch + frameH;
        x = work.left + (work.right - work.left - w) / 2;
        y = work.top + (work.bottom - work.top - h) / 2;
    }

    wl_busy_enter();
    SetWindowLongW(g_wnd, GWL_STYLE, (LONG)style);
    SetWindowLongW(g_wnd, GWL_EXSTYLE, (LONG)ex);
    SetWindowPos(g_wnd, HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    wl_busy_leave();
    g_appliedMode = mode;
    layout();
    InterlockedExchange(&g_repaint, 1);
    wl_log("display: %s %s, window %dx%d at %d,%d, picture %ldx%ld at %ld,%ld%s", g_modeNames[mode], g_aspectNames[g_aspect], w, h, x, y,
           g_dest.right - g_dest.left, g_dest.bottom - g_dest.top, g_dest.left, g_dest.top,
           placed() || g_full ? "" : " (multisampled back buffer: the picture fills the window)");
}

static void paint_bars(void)
{
    HDC dc = GetDC(g_wnd);
    if (!dc)
        return;
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT r;
    SetRect(&r, 0, 0, g_client.right, g_dest.top);
    FillRect(dc, &r, black);
    SetRect(&r, 0, g_dest.bottom, g_client.right, g_client.bottom);
    FillRect(dc, &r, black);
    SetRect(&r, 0, g_dest.top, g_dest.left, g_dest.bottom);
    FillRect(dc, &r, black);
    SetRect(&r, g_dest.right, g_dest.top, g_client.right, g_dest.bottom);
    FillRect(dc, &r, black);
    ReleaseDC(g_wnd, dc);
}

// ------------------------------------------------------------------------------
// The window procedure: the F10 menu's input, Alt+Enter, repainting the bars
// ------------------------------------------------------------------------------

static LRESULT CALLBACK display_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT result;
    if (wl_menu_wndproc(h, msg, wp, lp, &result))
        return result;
    if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1 << 29)))
    {
        InterlockedExchange(&g_toggle, 1);
        return 0;
    }
    if (msg == WM_SYSCHAR && wp == VK_RETURN)
        return 0; // no beep for Alt+Enter
    if (msg == WM_PAINT || msg == WM_SIZE || msg == WM_ERASEBKGND)
        InterlockedExchange(&g_repaint, 1);
    return g_unicode ? CallWindowProcW(g_proc, h, msg, wp, lp) : CallWindowProcA(g_proc, h, msg, wp, lp);
}

// ------------------------------------------------------------------------------
// From the Direct3D 9 hooks
// ------------------------------------------------------------------------------

void wl_display_prepare(D3DPRESENT_PARAMETERS *pp)
{
    if (!g_wl.windowed || !pp || !pp->Windowed)
        return;
    wchar_t swap[16] = L"";
    GetEnvironmentVariableW(L"WINDOWSLOADER_SWAP", swap, 16);
    g_gdi = g_gdiSetting;
    g_copy = !g_gdi && pp->MultiSampleType == D3DMULTISAMPLE_NONE && _wcsicmp(swap, L"discard");
    if (g_copy)
    {
        pp->SwapEffect = D3DSWAPEFFECT_COPY;
        pp->BackBufferCount = 1;
    }
}

void wl_display_device(HWND wnd, const D3DPRESENT_PARAMETERS *pp)
{
    if (pp)
        wl_display_window(wnd, (int)pp->BackBufferWidth, (int)pp->BackBufferHeight);
}

// The child window the picture is shown in: the mouse goes through it to the game's window, and
// it is never erased (the swap chain draws all of it)
static LRESULT CALLBACK child_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(h, msg, wp, lp);
}

HWND wl_display_child(HWND parent)
{
    if (!g_wl.windowed || !parent)
        return parent;
    wchar_t swap[16] = L"";
    GetEnvironmentVariableW(L"WINDOWSLOADER_SWAP", swap, 16);
    if (!_wcsicmp(swap, L"discard"))
        return parent; // the game's own swap chain, as for Direct3D 9
    if (g_child)
        return GetParent(g_child) == parent ? g_child : parent;
    static ATOM cls;
    if (!cls)
    {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = child_proc;
        wc.hInstance = g_wlModule;
        wc.lpszClassName = L"windowsloader picture";
        cls = RegisterClassW(&wc);
    }
    RECT r;
    GetClientRect(parent, &r);
    wl_busy_enter();
    // Hidden when windowsloader copies the picture itself (GDI): the swap chain presents into it,
    // the picture is drawn into the game's window
    g_gdi = g_gdiSetting;
    g_child = CreateWindowExW(0, L"windowsloader picture", L"", WS_CHILD | (g_gdi ? 0 : WS_VISIBLE) | WS_CLIPSIBLINGS, 0, 0, r.right, r.bottom,
                              parent, NULL, g_wlModule, NULL);
    if (g_child)
        SetWindowLongW(parent, GWL_STYLE, GetWindowLongW(parent, GWL_STYLE) | WS_CLIPCHILDREN);
    wl_busy_leave();
    if (!g_child)
    {
        wl_log("display: no child window (error %lu): the picture fills the window", GetLastError());
        return parent;
    }
    g_childRect = r;
    wl_log("display: the swap chain shows the picture in window %p, a child of the game's %p placed where the picture goes", g_child, parent);
    return g_child;
}

void wl_display_window(HWND wnd, int width, int height)
{
    if (!g_wl.windowed || !wnd)
        return;
    g_wnd = wnd;
    g_bbW = width;
    g_bbH = height;
    if (width > 0 && height > 0)
        wl_log("display: the game's picture is %dx%d, its own shape %.3f (%s)", width, height, (double)width / height,
               fabs((double)width / height - 4.0 / 3.0) < 0.02 ? "4:3" : fabs((double)width / height - 16.0 / 9.0) < 0.02 ? "16:9" : "other");
    if (g_bbW <= 0 || g_bbH <= 0)
    {
        RECT r;
        GetClientRect(wnd, &r);
        g_bbW = r.right;
        g_bbH = r.bottom;
    }
    if (!g_proc)
    {
        g_unicode = IsWindowUnicode(wnd);
        g_proc = (WNDPROC)(g_unicode ? SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)display_wndproc)
                                     : SetWindowLongPtrA(wnd, GWLP_WNDPROC, (LONG_PTR)display_wndproc));
    }
    apply_mode();
}

static int focus_is_ours(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// The game's name between brackets and the frames per second in the window's title, once a second:
// "<the game's title> [segarally3] - 60 fps" (the brackets let BackForceFeeder find the game)
static void title_fps(HWND wnd)
{
    static wchar_t title[128];
    static int got;
    static unsigned frames;
    static ULONGLONG since;
    ULONGLONG now = GetTickCount64();
    frames++;
    if (!since)
        since = now;
    if (now - since < 1000)
        return;
    if (!got)
    {
        got = 1;
        GetWindowTextW(wnd, title, 128);
    }
    wchar_t text[200];
    int n = swprintf(text, 200, L"%s", title);
    if (strcmp(wl_game_name(), "unknown"))
        n += swprintf(text + n, 200 - n, L"%s[%S]", n ? L" " : L"", wl_game_name());
    swprintf(text + n, 200 - n, L"%s%.0f fps", n ? L" - " : L"", frames * 1000.0 / (double)(now - since));
    // The window's own thread may be another one: never wait long for it
    DWORD_PTR result;
    SendMessageTimeoutW(wnd, WM_SETTEXT, 0, (LPARAM)text, SMTO_ABORTIFHUNG, 50, &result);
    frames = 0;
    since = now;
}

void wl_display_title(HWND wnd)
{
    if (wnd)
        title_fps(wnd);
}

const RECT *wl_display_dest(void)
{
    static int f11Was, frames;
    if (!g_wnd)
        return NULL;
    title_fps(g_wnd);

    int f11 = focus_is_ours() && (GetAsyncKeyState(VK_F11) & 0x8000);
    if ((f11 && !f11Was) || InterlockedExchange(&g_toggle, 0))
    {
        static const int other[WL_MODE_COUNT] = {WL_MODE_FULLSCREEN, WL_MODE_FULLSCREEN_STRETCH, WL_MODE_WINDOW, WL_MODE_WINDOW_STRETCH};
        InterlockedExchange(&g_mode, other[g_mode]); // not saved: DISPLAY_MODE stays the one chosen
        InterlockedExchange(&g_apply, 1);
    }
    f11Was = f11;
    if (InterlockedExchange(&g_apply, 0))
        apply_mode();
    layout();

    // Bars only around a picture windowsloader places: one that fills the window has none to paint
    if (placed() && !g_full && (InterlockedExchange(&g_repaint, 0) || ++frames % 30 == 0))
        paint_bars();
    return g_copy && !g_full ? &g_dest : NULL;
}

int wl_display_gdi(void)
{
    return g_gdi && g_wnd;
}

void wl_display_gdi_draw(const void *pixels, int pitch, int width, int height, int bits, int green6)
{
    struct
    {
        BITMAPINFOHEADER h;
        DWORD masks[3];
    } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = pitch / (bits / 8); // the rows' length; the picture's width taken below
    bi.h.biHeight = -height;           // top-down
    bi.h.biPlanes = 1;
    bi.h.biBitCount = (WORD)bits;
    bi.h.biCompression = bits == 16 ? BI_BITFIELDS : BI_RGB;
    if (bits == 16 && green6)
        bi.masks[0] = 0xF800, bi.masks[1] = 0x07E0, bi.masks[2] = 0x001F;
    else if (bits == 16)
        bi.masks[0] = 0x7C00, bi.masks[1] = 0x03E0, bi.masks[2] = 0x001F;
    RECT r;
    wl_display_picture(&r);
    HDC dc = GetDC(g_wnd);
    if (!dc)
        return;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, width, height, pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_wnd, dc);
}

HWND wl_display_hwnd(void)
{
    return g_wnd;
}

void wl_display_picture(RECT *r)
{
    if (placed() && !g_full)
        *r = g_dest;
    else
        GetClientRect(g_wnd, r);
}

// ------------------------------------------------------------------------------
// What the window shows
// ------------------------------------------------------------------------------

int wl_display_screenshot(const char *path)
{
    if (!g_wnd)
        return 0;
    RECT c;
    GetClientRect(g_wnd, &c);
    POINT o = {0, 0};
    ClientToScreen(g_wnd, &o);
    int w = c.right, h = c.bottom;
    if (w <= 0 || h <= 0)
        return 0;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h; // bottom-up, as a BMP file
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    int ok = 0;
    if (bmp)
    {
        HGDIOBJ old = SelectObject(mem, bmp);
        // What the screen shows there; PrintWindow (the window's content even when covered) only
        // when that is all black: it misses the Direct3D picture of some windows
        int row = (w * 3 + 3) & ~3;
        ok = BitBlt(mem, 0, 0, w, h, screen, o.x, o.y, SRCCOPY);
        GdiFlush();
        int black = 1;
        for (size_t i = 0; ok && black && i < (size_t)row * h; i += 7)
            black = ((unsigned char *)bits)[i] == 0;
        if (!ok || black)
            ok = PrintWindow(g_wnd, mem, PW_CLIENTONLY | 0x2 /* PW_RENDERFULLCONTENT */) || ok;
        SelectObject(mem, old);
        GdiFlush();
        FILE *f = ok ? fopen(path, "wb") : NULL;
        if (f)
        {
            BITMAPFILEHEADER fh = {};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader;
            fh.bfSize = fh.bfOffBits + (DWORD)row * h;
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
            fwrite(bits, 1, (size_t)row * h, f);
            fclose(f);
        }
        else
            ok = 0;
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return ok;
}

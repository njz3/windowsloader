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

#include "agent.h"
#include "display.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"
#include "settings.h"
#include "wlshared.h"

static volatile LONG g_aspect = WL_ASPECT_ORIGINAL;
static volatile LONG g_mode = WL_MODE_WINDOW;
static volatile LONG g_modeChosen = WL_MODE_WINDOW; // the one saved (F11 does not change it)
static volatile LONG g_cropPosition;                // -100..100
static volatile LONG g_cropBarrel;
static volatile LONG g_barrelCenter = 60;           // % of the frame
static volatile LONG g_barrelRadius = 50;           // % of the frame
static int g_width, g_height;      // internal resolution, 0 = AUTO
static int g_nextW, g_nextH;       // the one to save
static volatile LONG g_apply;      // the window must take the mode again
// Windows changed from the thread that owns them: Present may run on another (Unreal Engine 4's
// render thread, Crazy Ride), and SetWindowPos or a style change from there waits for the owner's
// messages, while the owner may be waiting for the render thread. The mode is then applied by the
// window procedure (this message posted), the child placed asynchronously.
static UINT g_applyMsg;
static int owns(HWND wnd)
{
    return GetWindowThreadProcessId(wnd, NULL) == GetCurrentThreadId();
}
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
static RECT g_dest, g_client;      // what shows the picture in the client area, and that area
static RECT g_pic;                 // the whole picture's place (beyond g_dest when it is cropped)
static RECT g_src;                 // the back buffer's part shown (Direct3D 9's Present, cropped)
static int g_full = 1;             // the picture is the whole client area
static int g_glPicture;            // an OpenGL game whose picture windowsloader copies into its window (glhook.cpp)
static int g_hidden;               // a program of the cabinet's that shows nothing (wl_display_hidden)

static const char *g_aspectNames[WL_ASPECT_COUNT] = {"ORIGINAL", "4:3", "16:9", "4:3_LETTERBOX", "4:3_STRETCH", "16:9_STRETCH", "4:3_CROP", "16:9_CROP"};
static const char *g_modeNames[WL_MODE_COUNT] = {"WINDOW", "WINDOW_STRETCH", "FULLSCREEN", "FULLSCREEN_STRETCH", "BORDERLESS", "BORDERLESS_STRETCH"};

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
    "# Shape of the picture: ORIGINAL (the game's render resolution); 4:3 or 16:9 (that frame, the picture\r\n"
    "# whole inside at its own shape, black bars where the shapes differ; 4:3_LETTERBOX is 4:3); 4:3_STRETCH\r\n"
    "# or 16:9_STRETCH (the picture fills the frame, deformed); 4:3_CROP or 16:9_CROP (the picture fills the\r\n"
    "# frame, cut on the sides or above and below)\r\n"
    "ASPECT_RATIO = ORIGINAL\r\n"
    "\r\n"
    "# The part a cropped aspect ratio keeps: -100 (left or top) to 100 (right or bottom), 0 the middle\r\n"
    "CROP_POSITION = 0\r\n"
    "\r\n"
    "# Cropped aspect ratios: true bends the parts they would cut onto the frame's edges, as on a cylinder\r\n"
    "# seen from the front, so the whole picture stays visible (PRESENT = GDI only). BARREL_CENTER: the\r\n"
    "# middle shown undistorted, in % of the frame. BARREL_RADIUS: the cylinder's radius in % of the frame\r\n"
    "# (small bends hard at the edge, large compresses evenly)\r\n"
    "CROP_BARREL = false\r\n"
    "BARREL_CENTER = 60\r\n"
    "BARREL_RADIUS = 50\r\n"
    "\r\n"
    "# WINDOW: a window at the aspect ratio, square pixels. WINDOW_STRETCH: the picture fills the window,\r\n"
    "# which can be resized. FULLSCREEN: the whole screen, switched to its smallest mode holding the game's\r\n"
    "# picture, at the aspect ratio between black bars. FULLSCREEN_STRETCH: the same, the picture fills it.\r\n"
    "# BORDERLESS and BORDERLESS_STRETCH: the same in a window covering the screen at the desktop's\r\n"
    "# resolution (full screen window). F11 or Alt+Enter: window / full screen.\r\n"
    "DISPLAY_MODE = WINDOW\r\n"
    "\r\n"
    "# Direct3D 9 and 11 games in a window. GDI: windowsloader copies the picture into the window itself;\r\n"
    "# DIRECT: the game's own Present, faster, but some PCs show nothing of it (Windows 11 on a laptop).\r\n"
    "PRESENT = GDI\r\n"
    "\r\n"
    "[Paths]\r\n"
    "# The folder everything is written to (or --write-dir); the launcher's and the games' folders are then\r\n"
    "# only read. Its root holds windowsloader.ini and controls.ini (copied there the first time) and the log,\r\n"
    "# WRITE_DIR\\<game> what the game writes: its own files under their drive's letter, AppData in appdata, the\r\n"
    "# temporary folder in temp, ProgramData in programdata, Windows' in windows; for a write filter (UWF) on the\r\n"
    "# whole disk but this folder, or games on a share. Once it is set, the windowsloader.ini and controls.ini\r\n"
    "# read are the copies in that folder: change those, not the ones beside the launcher. Relative to the\r\n"
    "# launcher's folder, or a full path. Empty: everything is written in its usual place.\r\n"
    "# For Example: C:\\Racecab\\Saves\\windowsloader\r\n"
    "WRITE_DIR = \"\"\r\n"
    "\r\n"
    "[Outputs]\r\n"
    "# Set to true to send lamps and drive board data over the network, MAME style (BackForceFeeder, MAMEHooker...)\r\n"
    "OUTPUTS_NETWORK = false\r\n"
    "\r\n"
    "# TCP port the outputs server listens on\r\n"
    "OUTPUTS_NETWORK_PORT = 8000\r\n"
    "\r\n"
    "# UDP port on 127.0.0.1 that gets a start announcement (0 to disable)\r\n"
    "OUTPUTS_NETWORK_UDP_PORT = 8001\r\n"
    "\r\n"
    "[Launcher]\r\n"
    "# Folder the game picker lists when windowsloader starts without a game (empty: a games folder next to\r\n"
    "# the program, or one or two levels up)\r\n"
    "GAMES_DIR = \r\n"
    "\r\n"
    "# Set to false on a public cabinet: Tab no longer opens the settings over a running game (which pauses it)\r\n"
    "SETTINGS_MENU = true\r\n";

// The names without case, a space for the underscore ("4:3 crop", as linuxloader reads them)
static int name_index(const char *const *names, int count, const char *value, int def)
{
    char v[64];
    snprintf(v, sizeof v, "%s", value);
    for (char *c = v; *c; c++)
        if (*c == ' ')
            *c = '_';
    for (int i = 0; i < count; i++)
        if (!_stricmp(v, names[i]))
            return i;
    return def;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int placed(void);

// The game's own GetClientRect (its imports): its window at its picture's size once windowsloader
// places the picture. A game that follows its window's size (Battle Gear 4, DXUT's way: a Reset at
// the client area's size after each WM_SIZE) would else take each size windowsloader gives the
// window, and the window then the picture's new shape, growing at each turn
static BOOL(WINAPI *oGetClientRect)(HWND, LPRECT);
static BOOL WINAPI game_GetClientRect(HWND wnd, LPRECT r)
{
    if (wnd && wnd == g_wnd && r && g_bbW > 0 && g_bbH > 0 && placed())
    {
        SetRect(r, 0, 0, g_bbW, g_bbH);
        return TRUE;
    }
    return oGetClientRect(wnd, r);
}

void wl_display_load(void)
{
    if (g_wl.windowed && wl_patch_iat(GetModuleHandleW(NULL), "user32.dll", "GetClientRect", (void *)game_GetClientRect, (void **)&oGetClientRect))
        wl_log("display: the game's GetClientRect gives its window at its picture's size");
    const wchar_t *ini = wl_settings_file();
    if (wl_settings_common() && GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES)
    {
        FILE *f = _wfopen(ini, L"wb");
        if (f)
        {
            fputs(g_defaults, f);
            fclose(f);
        }
    }
    char v[64];
    wl_settings_get("Display", "ASPECT_RATIO", "ORIGINAL", v, sizeof v);
    g_aspect = name_index(g_aspectNames, WL_ASPECT_COUNT, v, WL_ASPECT_ORIGINAL);
    wl_settings_get("Display", "DISPLAY_MODE", "WINDOW", v, sizeof v);
    g_mode = g_modeChosen = name_index(g_modeNames, WL_MODE_COUNT, v, WL_MODE_WINDOW);
    g_cropPosition = clamp(wl_settings_get_int("Display", "CROP_POSITION", 0), -100, 100);
    g_cropBarrel = wl_settings_get_bool("Display", "CROP_BARREL", 0);
    g_barrelCenter = clamp(wl_settings_get_int("Display", "BARREL_CENTER", 60), 0, 100);
    g_barrelRadius = clamp(wl_settings_get_int("Display", "BARREL_RADIUS", 50), 1, 1000);
    wl_settings_get("Display", "PRESENT", "GDI", v, sizeof v);
    g_gdiSetting = _stricmp(v, "DIRECT") != 0;
    g_width = wl_settings_get_int("Display", "WIDTH", 0);
    g_height = wl_settings_get_int("Display", "HEIGHT", 0);
    if (g_width <= 0 || g_height <= 0)
        g_width = g_height = 0;
    g_nextW = g_width;
    g_nextH = g_height;
    char res[32] = "AUTO";
    if (g_width)
        snprintf(res, sizeof res, "%dx%d", g_width, g_height);
    wl_log("display: ASPECT_RATIO %s (CROP_POSITION %ld, CROP_BARREL %s, BARREL_CENTER %ld, BARREL_RADIUS %ld), DISPLAY_MODE %s, resolution %s, "
           "picture %s",
           g_aspectNames[g_aspect], g_cropPosition, g_cropBarrel ? "true" : "false", g_barrelCenter, g_barrelRadius, g_modeNames[g_mode], res,
           g_gdiSetting ? "copied by GDI (PRESENT = GDI)" : "presented by the game (PRESENT = DIRECT)");
}

int wl_display_save(const wchar_t *file)
{
    char n[16];
    int ok = wl_settings_write(file, "Display", "ASPECT_RATIO", g_aspectNames[g_aspect]);
    snprintf(n, sizeof n, "%ld", g_cropPosition);
    ok &= wl_settings_write(file, "Display", "CROP_POSITION", n);
    ok &= wl_settings_write(file, "Display", "CROP_BARREL", g_cropBarrel ? "true" : "false");
    snprintf(n, sizeof n, "%ld", g_barrelCenter);
    ok &= wl_settings_write(file, "Display", "BARREL_CENTER", n);
    snprintf(n, sizeof n, "%ld", g_barrelRadius);
    ok &= wl_settings_write(file, "Display", "BARREL_RADIUS", n);
    ok &= wl_settings_write(file, "Display", "DISPLAY_MODE", g_modeNames[g_modeChosen]);
    snprintf(n, sizeof n, "%d", g_nextW);
    ok &= wl_settings_write(file, "Display", "WIDTH", g_nextW > 0 ? n : "AUTO");
    snprintf(n, sizeof n, "%d", g_nextH);
    ok &= wl_settings_write(file, "Display", "HEIGHT", g_nextH > 0 ? n : "AUTO");
    char u[MAX_PATH * 2];
    wl_log("display: settings saved to %s%s", wl_u8(file, u, sizeof u), ok ? "" : " (failed)");
    return ok;
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
    InterlockedExchange(&g_modeChosen, mode);
    InterlockedExchange(&g_apply, 1);
}

void wl_display_crop(int *position, int *barrel, int *center, int *radius)
{
    *position = g_cropPosition;
    *barrel = g_cropBarrel;
    *center = g_barrelCenter;
    *radius = g_barrelRadius;
}

void wl_display_set_crop(int position, int barrel, int center, int radius)
{
    InterlockedExchange(&g_cropPosition, clamp(position, -100, 100));
    InterlockedExchange(&g_cropBarrel, barrel != 0);
    InterlockedExchange(&g_barrelCenter, clamp(center, 0, 100));
    InterlockedExchange(&g_barrelRadius, clamp(radius, 1, 1000));
    InterlockedExchange(&g_repaint, 1);
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

void wl_display_internal_next(int *width, int *height)
{
    *width = g_nextW;
    *height = g_nextH;
}

void wl_display_set_internal(int width, int height)
{
    // The game reads its resolution once, when it starts: g_width / g_height stay as they are
    g_nextW = width > 0 && height > 0 ? width : 0;
    g_nextH = width > 0 && height > 0 ? height : 0;
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
    int a = g_aspect;
    if (a == WL_ASPECT_4_3 || a == WL_ASPECT_4_3_LETTERBOX || a == WL_ASPECT_4_3_STRETCH || a == WL_ASPECT_4_3_CROP)
        return 4.0 / 3.0;
    if (a == WL_ASPECT_16_9 || a == WL_ASPECT_16_9_STRETCH || a == WL_ASPECT_16_9_CROP)
        return 16.0 / 9.0;
    return g_bbH > 0 ? (double)g_bbW / g_bbH : 16.0 / 9.0;
}

// How the picture takes the frame: whole inside at its shape (bars), filling it (deformed, or the
// game's own shape), cut, or cut with the cut parts bent onto the edges
enum
{
    VIEW_FIT,
    VIEW_FILL,
    VIEW_CROP,
    VIEW_BARREL
};

static int view_kind(void)
{
    int a = g_aspect;
    if (a == WL_ASPECT_4_3 || a == WL_ASPECT_16_9 || a == WL_ASPECT_4_3_LETTERBOX)
        return VIEW_FIT;
    if (a != WL_ASPECT_4_3_CROP && a != WL_ASPECT_16_9_CROP)
        return VIEW_FILL;
    if (g_gdi)
        return g_cropBarrel ? VIEW_BARREL : VIEW_CROP;
    // The game's own Present: Direct3D 9 shows a part of its back buffer (no bend); Direct3D 11's
    // fills the child window: the whole picture between bars. OpenGL's copy is cut to the frame (no
    // bend)
    return g_copy || g_glPicture ? VIEW_CROP : VIEW_FIT;
}

// The part of a w x h picture a crop keeps, CROP_POSITION's (the middle's when centred)
static RECT crop_source(int w, int h, int centred)
{
    RECT r = {0, 0, w, h};
    double own = h > 0 ? (double)w / h : 1, frameShape = frame_aspect();
    double kept = centred ? 0.5 : (g_cropPosition + 100) / 200.0; // 0 left or top, 1 right or bottom
    if (own > frameShape + 1e-6) // too wide: the sides go
    {
        int cw = (int)floor(h * frameShape + 0.5);
        r.left = (int)floor((w - cw) * kept + 0.5);
        r.right = r.left + cw;
    }
    else if (own < frameShape - 1e-6) // too tall: above and below go
    {
        int ch = (int)floor(w / frameShape + 0.5);
        r.top = (int)floor((h - ch) * kept + 0.5);
        r.bottom = r.top + ch;
    }
    return r;
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
    int stretch = g_mode == WL_MODE_WINDOW_STRETCH || g_mode == WL_MODE_FULLSCREEN_STRETCH || g_mode == WL_MODE_BORDERLESS_STRETCH;
    RECT frame = stretch ? g_client : fit(w, h, frame_aspect());
    RECT pic = frame, shown = frame;
    int kind = view_kind();
    double own = g_bbH > 0 ? (double)g_bbW / g_bbH : 1, frameShape = frame_aspect();
    int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
    if (kind == VIEW_FIT && g_bbH > 0)
    {
        // The picture whole inside the frame at the game's own shape (its render resolution's), its
        // bars as parts of the frame: a 4:3 game in a 16:9 frame gets bars left and right, a 16:9 one
        // in a 4:3 frame above and below. The bars follow the frame when it is stretched (a 4:3
        // screen getting a 16:9 signal).
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
        shown = pic;
    }
    else if ((kind == VIEW_CROP || kind == VIEW_BARREL) && g_bbH > 0)
    {
        // The frame filled; the whole picture at the crop's scale, centred on it, beyond it
        if (own > frameShape)
        {
            int pw = (int)floor(fw * own / frameShape + 0.5);
            pic.left = frame.left - (pw - fw) / 2;
            pic.right = pic.left + pw;
        }
        else
        {
            int ph = (int)floor(fh * frameShape / own + 0.5);
            pic.top = frame.top - (ph - fh) / 2;
            pic.bottom = pic.top + ph;
        }
    }
    g_src = crop_source(g_bbW, g_bbH, wl_menu_is_open()); // the menu whole: the middle while it is open
    if (!EqualRect(&shown, &g_dest))
        InterlockedExchange(&g_repaint, 1);
    g_dest = shown;
    g_pic = pic;
    g_full = EqualRect(&shown, &g_client);
    if (g_child && !EqualRect(&shown, &g_childRect))
    {
        g_childRect = shown;
        wl_log("display: client %dx%d, picture %ldx%ld at %ld,%ld", w, h, shown.right - shown.left, shown.bottom - shown.top, shown.left, shown.top);
        SetWindowPos(g_child, NULL, shown.left, shown.top, shown.right - shown.left, shown.bottom - shown.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | (owns(g_child) ? 0 : SWP_ASYNCWINDOWPOS));
    }
}

// FULLSCREEN: the screen the window is on switched to its smallest mode holding the game's picture
// (CDS_FULLSCREEN: a temporary mode, which Windows also takes back when the game ends), back to the
// desktop's in the other modes. A change the screen refuses is tried once: the full screen then
// stays at the desktop's resolution, as BORDERLESS.
static wchar_t g_screen[CCHDEVICENAME]; // the screen switched, "" when none
static int g_screenRefusedW, g_screenRefusedH;

static void screen_resolution(int full)
{
    if (!full)
    {
        if (!g_screen[0])
            return;
        wl_busy_enter();
        LONG r = ChangeDisplaySettingsExW(g_screen, NULL, NULL, 0, NULL);
        wl_busy_leave();
        wl_log("display: the screen back at the desktop's resolution (%ld)", r);
        g_screen[0] = 0;
        return;
    }
    if (g_screen[0] || g_bbW <= 0 || g_bbH <= 0 || (g_bbW == g_screenRefusedW && g_bbH == g_screenRefusedH))
        return;
    MONITORINFOEXW mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTOPRIMARY), &mi))
        return;
    // The screen's smallest mode holding the picture (its own size when the screen has it), as SDL
    // chooses for linuxloader
    DEVMODEW dm = {}, m = {};
    dm.dmSize = m.dmSize = sizeof dm;
    DWORD bestArea = 0;
    for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &m); i++)
        if (m.dmPelsWidth >= (DWORD)g_bbW && m.dmPelsHeight >= (DWORD)g_bbH && (!bestArea || m.dmPelsWidth * m.dmPelsHeight < bestArea))
        {
            bestArea = m.dmPelsWidth * m.dmPelsHeight;
            dm.dmPelsWidth = m.dmPelsWidth;
            dm.dmPelsHeight = m.dmPelsHeight;
        }
    if (!bestArea)
    {
        dm.dmPelsWidth = (DWORD)g_bbW;
        dm.dmPelsHeight = (DWORD)g_bbH;
    }
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    wl_busy_enter();
    LONG r = ChangeDisplaySettingsExW(mi.szDevice, &dm, NULL, CDS_FULLSCREEN, NULL);
    wl_busy_leave();
    if (r == DISP_CHANGE_SUCCESSFUL)
    {
        wcscpy_s(g_screen, CCHDEVICENAME, mi.szDevice);
        wl_log("display: the screen at %lux%lu for the full screen (the picture %dx%d)", dm.dmPelsWidth, dm.dmPelsHeight, g_bbW, g_bbH);
    }
    else
    {
        g_screenRefusedW = g_bbW;
        g_screenRefusedH = g_bbH;
        wl_log("display: the screen does not take %dx%d (%ld): full screen at the desktop's resolution", g_bbW, g_bbH, r);
    }
}

static void apply_mode(void)
{
    if (!g_wnd)
        return;
    int mode = g_mode;
    screen_resolution(mode == WL_MODE_FULLSCREEN || mode == WL_MODE_FULLSCREEN_STRETCH);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    DWORD ex = (DWORD)GetWindowLongW(g_wnd, GWL_EXSTYLE) & ~(WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
    int x, y, w, h;
    DWORD style;

    if (mode == WL_MODE_FULLSCREEN || mode == WL_MODE_FULLSCREEN_STRETCH || mode == WL_MODE_BORDERLESS || mode == WL_MODE_BORDERLESS_STRETCH)
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
           placed() || g_glPicture || g_full ? "" : " (multisampled back buffer: the picture fills the window)");
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
// The window procedure: the settings menu's input, Alt+Enter, repainting the bars
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
    if (msg == g_applyMsg && g_applyMsg)
    {
        apply_mode();
        return 0;
    }
    if (msg == WM_PAINT || msg == WM_SIZE || msg == WM_ERASEBKGND)
        InterlockedExchange(&g_repaint, 1);
    // The window's size is windowsloader's: a game that limits it (Frenzy Express: 640x480 at most,
    // its cabinet's screen) gets Windows' own limits back
    if (msg == WM_GETMINMAXINFO && lp)
    {
        LRESULT r = g_unicode ? CallWindowProcW(g_proc, h, msg, wp, lp) : CallWindowProcA(g_proc, h, msg, wp, lp);
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMaxTrackSize.x = GetSystemMetrics(SM_CXMAXTRACK);
        mm->ptMaxTrackSize.y = GetSystemMetrics(SM_CYMAXTRACK);
        mm->ptMaxSize.x = GetSystemMetrics(SM_CXMAXIMIZED);
        mm->ptMaxSize.y = GetSystemMetrics(SM_CYMAXIMIZED);
        return r;
    }
    // The game sees its window at its picture's size (as by GetClientRect above)
    if (msg == WM_SIZE && wp != SIZE_MINIMIZED && g_bbW > 0 && g_bbH > 0)
        lp = MAKELPARAM(g_bbW, g_bbH);
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
    if (!g_wl.windowed || !parent || g_hidden)
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

void wl_display_hidden(void)
{
    g_hidden = 1;
}

void wl_display_window(HWND wnd, int width, int height)
{
    if (!g_wl.windowed || !wnd)
        return;
    if (g_hidden)
    {
        ShowWindowAsync(wnd, SW_HIDE); // from the render thread: the window's own may be busy
        wl_log("display: window %p hidden (a program of the cabinet's that shows nothing)", wnd);
        return;
    }
    // Only a window of the game's own: never the desktop or another program's
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (wnd == GetDesktopWindow() || pid != GetCurrentProcessId())
    {
        wl_log("display: window %p is not the game's (pid %lu): left alone", wnd, pid);
        return;
    }
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
        g_applyMsg = RegisterWindowMessageW(L"windowsloader apply display mode");
        g_unicode = IsWindowUnicode(wnd);
        g_proc = (WNDPROC)(g_unicode ? SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)display_wndproc)
                                     : SetWindowLongPtrA(wnd, GWLP_WNDPROC, (LONG_PTR)display_wndproc));
    }
    apply_mode();
}

void wl_display_gl_window(HWND wnd, int width, int height)
{
    g_glPicture = 1;
    wl_display_window(wnd, width, height);
}

static int focus_is_ours(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// The game's name between brackets, the frames per second and the build in the window's title, once
// a second: "<the game's title> [segarally3] - 59.9 fps - windowsloader 0.1.0-20261025-njz3 FFB
// Edition" (the brackets let BackForceFeeder find the game). Timed by the performance counter (ours,
// not the game's that the pause stops): GetTickCount's 16 ms steps would make the tenth a guess
static void title_fps(HWND wnd)
{
    static wchar_t title[128];
    static int got;
    static unsigned frames;
    static LONGLONG since, freq;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
    frames++;
    if (!since)
        since = now.QuadPart;
    double seconds = (double)(now.QuadPart - since) / (double)freq;
    if (seconds < 1)
        return;
    if (!got)
    {
        got = 1;
        GetWindowTextW(wnd, title, 128);
    }
    wchar_t text[320]; // the title (128 at most) and the rest fit
    int n = swprintf(text, 320, L"%s", title);
    if (strcmp(wl_game_name(), "unknown"))
        n += swprintf(text + n, 320 - n, L"%s[%S]", n ? L" " : L"", wl_game_name());
    swprintf(text + n, 320 - n, L"%s%.1f fps - windowsloader %S %S", n ? L" - " : L"", frames / seconds, WL_BUILD_VERSION, WL_EDITION);
    // The window's own thread may be another one: never wait long for it
    DWORD_PTR result;
    SendMessageTimeoutW(wnd, WM_SETTEXT, 0, (LPARAM)text, SMTO_ABORTIFHUNG, 50, &result);
    frames = 0;
    since = now.QuadPart;
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
        // Window <-> the full screen chosen (BORDERLESS when the mode chosen is a window), stretched
        // or not as it was; not saved: DISPLAY_MODE stays the one chosen
        int mode = g_mode, chosen = g_modeChosen;
        int stretch = mode == WL_MODE_WINDOW_STRETCH || mode == WL_MODE_FULLSCREEN_STRETCH || mode == WL_MODE_BORDERLESS_STRETCH;
        int real = chosen == WL_MODE_FULLSCREEN || chosen == WL_MODE_FULLSCREEN_STRETCH;
        int next = mode == WL_MODE_WINDOW || mode == WL_MODE_WINDOW_STRETCH
                       ? (real ? (stretch ? WL_MODE_FULLSCREEN_STRETCH : WL_MODE_FULLSCREEN) : (stretch ? WL_MODE_BORDERLESS_STRETCH : WL_MODE_BORDERLESS))
                       : (stretch ? WL_MODE_WINDOW_STRETCH : WL_MODE_WINDOW);
        InterlockedExchange(&g_mode, next);
        InterlockedExchange(&g_apply, 1);
    }
    f11Was = f11;
    if (InterlockedExchange(&g_apply, 0))
    {
        if (owns(g_wnd) || !g_proc)
            apply_mode();
        else
            PostMessageW(g_wnd, g_applyMsg, 0, 0);
    }
    layout();

    // Bars only around a picture windowsloader places: one that fills the window has none to paint
    if (placed() && !g_full && (InterlockedExchange(&g_repaint, 0) || ++frames % 30 == 0))
        paint_bars();
    return g_copy && !g_full ? &g_dest : NULL;
}

const RECT *wl_display_source(void)
{
    return g_copy && view_kind() == VIEW_CROP ? &g_src : NULL;
}

int wl_display_gdi(void)
{
    return g_gdi && g_wnd;
}

// ------------------------------------------------------------------------------
// The picture drawn by GDI: whole, cut, or with the cut parts bent onto the edges
// ------------------------------------------------------------------------------

struct Dib
{
    BITMAPINFOHEADER h;
    DWORD masks[3];
};

// The rows top to bottom of src (picture pixels) into dst: from the first row shown, the DIB
// exactly those rows high (StretchDIBits' source origin is never in doubt)
static void draw_part(HDC dc, Dib *bi, const void *pixels, int pitch, const RECT &src, const RECT &dst)
{
    bi->h.biHeight = -(src.bottom - src.top);
    StretchDIBits(dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, src.left, 0, src.right - src.left, src.bottom - src.top,
                  (const unsigned char *)pixels + (size_t)src.top * pitch, (BITMAPINFO *)bi, DIB_RGB_COLORS, SRCCOPY);
}

// CROP_BARREL, taken from linuxloader (src/loader/graphics/blitStretching.c: barrelSetup, barrelArc,
// barrelSource; linuxloader's license):
// the cropped aspect ratios keep the frame's middle (BARREL_CENTER of it) as the crop shows it, and
// bend the parts the crop would cut onto the frame's edges, as on a cylinder seen from the front: a
// point at the arc s from the middle's edge shows at sin(s / R), R the cylinder's radius
// (BARREL_RADIUS), scaled so that the picture's edge lands on the frame's; a quarter turn at most.
// Along the cropped axis, in frames: the crop keeps a share k of the picture, which is 1 / k frames
// long; d, a point of the frame from 0 to 1, shows the picture at barrel_source(d)
struct Barrel
{
    double k;          // the share of the picture the crop keeps
    double edge;       // each edge's width, in frames
    double startLeft;  // where the middle starts in the picture, from 0 to 1 (left or top)
    double startRight; // where it ends
    double arcLeft;    // the picture beyond the middle on each side, in frames
    double arcRight;
    double angleLeft;  // the turn of the cylinder each side's picture takes, in radians
    double angleRight;
};

static Barrel barrel_setup(double k, double kept)
{
    Barrel b;
    double center = g_barrelCenter / 100.0, radius = g_barrelRadius / 100.0;
    double start = (1.0 - k) * kept; // the crop's window in the picture
    b.k = k;
    b.edge = (1.0 - center) / 2.0;
    b.startLeft = start + k * (1.0 - center) / 2.0;
    b.startRight = start + k * (1.0 + center) / 2.0;
    b.arcLeft = b.startLeft / k;
    b.arcRight = (1.0 - b.startRight) / k;
    // An edge with more picture than room bends; with less it only stretches
    b.angleLeft = b.arcLeft > b.edge ? fmin(b.arcLeft / radius, 3.14159265358979 / 2.0) : 0.0;
    b.angleRight = b.arcRight > b.edge ? fmin(b.arcRight / radius, 3.14159265358979 / 2.0) : 0.0;
    return b;
}

// x from the middle's edge outward, 0 to the edge's width: the arc of picture shown there
static double barrel_arc(double x, double edge, double arc, double angle)
{
    if (edge <= 0.0)
        return 0.0;
    if (angle < 1e-4)
        return arc * x / edge;
    return arc * asin(sin(angle) * x / edge) / angle;
}

static double barrel_source(const Barrel *b, double d)
{
    if (d < b->edge)
        return b->startLeft - b->k * barrel_arc(b->edge - d, b->edge, b->arcLeft, b->angleLeft);
    if (d > 1.0 - b->edge)
        return b->startRight + b->k * barrel_arc(d - (1.0 - b->edge), b->edge, b->arcRight, b->angleRight);
    return b->startLeft + b->k * (d - b->edge);
}

// The bent picture: along the cropped axis each of the frame's columns (or rows) takes the
// picture's column (row) barrel_source gives, into a picture the frame's size on that axis, which
// StretchDIBits then scales on the other one. 0 when there is nothing to bend.
static int draw_barrel(HDC dc, Dib *bi, const void *pixels, int pitch, int width, int height)
{
    static unsigned char *bent;
    static size_t bentSize;
    static int *map;
    static int mapSize;
    double own = (double)width / height, frameShape = frame_aspect();
    int dw = g_dest.right - g_dest.left, dh = g_dest.bottom - g_dest.top, bpp = bi->h.biBitCount / 8;
    if (fabs(own - frameShape) < 1e-6 || dw <= 0 || dh <= 0)
        return 0;
    int horizontal = own > frameShape;
    Barrel b = barrel_setup(horizontal ? frameShape / own : own / frameShape, (g_cropPosition + 100) / 200.0);
    int count = horizontal ? dw : dh, along = horizontal ? width : height;
    int outW = horizontal ? dw : width, outH = horizontal ? height : dh;
    size_t row = ((size_t)outW * bpp + 3) & ~(size_t)3, size = row * outH;
    if (count > mapSize)
    {
        free(map);
        map = (int *)malloc(sizeof(int) * count);
        mapSize = map ? count : 0;
    }
    if (size > bentSize)
    {
        free(bent);
        bent = (unsigned char *)malloc(size);
        bentSize = bent ? size : 0;
    }
    if (!map || !bent)
        return 0;
    for (int i = 0; i < count; i++)
    {
        int s = (int)floor(barrel_source(&b, (i + 0.5) / count) * along);
        map[i] = s < 0 ? 0 : s >= along ? along - 1 : s;
    }
    const unsigned char *in = (const unsigned char *)pixels;
    if (horizontal)
    {
        for (int y = 0; y < height; y++)
        {
            const unsigned char *src = in + (size_t)y * pitch;
            unsigned char *dst = bent + (size_t)y * row;
            if (bpp == 4)
                for (int x = 0; x < dw; x++)
                    ((DWORD *)dst)[x] = ((const DWORD *)src)[map[x]];
            else
                for (int x = 0; x < dw; x++)
                    ((WORD *)dst)[x] = ((const WORD *)src)[map[x]];
        }
    }
    else
        for (int y = 0; y < dh; y++)
            memcpy(bent + (size_t)y * row, in + (size_t)map[y] * pitch, (size_t)width * bpp);
    bi->h.biWidth = outW;
    bi->h.biHeight = -outH;
    StretchDIBits(dc, g_dest.left, g_dest.top, dw, dh, 0, 0, outW, outH, bent, (BITMAPINFO *)bi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}

// A game that does not wait for the screen is not slowed down by the copy: its picture is copied
// at most once per refresh of the screen (from 3/4 of its period, so that a game at the screen's
// pace keeps every frame), its other frames only presented to the hidden window. Wasteland Racers
// 2071 loads one texture per frame on its "Get Ready To Race" screen: with a copy each frame (its
// 2560x1440 multisampled back buffer read back and stretched by GDI, about 60 a second), its map
// took 141 s on the cabinets instead of 12 (938 frames a second with PRESENT = DIRECT)
int wl_display_gdi_due(int immediate)
{
    if (!immediate)
        return 1;
    static LONGLONG period, last;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!period)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        DEVMODEW dm;
        memset(&dm, 0, sizeof dm);
        dm.dmSize = sizeof dm;
        DWORD hz = EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1 ? dm.dmDisplayFrequency : 60;
        period = freq.QuadPart * 3 / 4 / hz;
        wl_log("display: the game does not wait for the screen: its picture copied about once per refresh (%lu Hz)", hz);
    }
    if (now.QuadPart - last < period)
        return 0;
    last = now.QuadPart;
    return 1;
}

void wl_display_gdi_draw(const void *pixels, int pitch, int width, int height, int bits, int green6)
{
    Dib bi;
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
    int kind = view_kind();
    if (kind == VIEW_FIT || kind == VIEW_FILL || width <= 0 || height <= 0)
    {
        StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, width, height, pixels, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(g_wnd, dc);
        return;
    }
    Dib whole = bi;
    if (kind != VIEW_BARREL || !draw_barrel(dc, &bi, pixels, pitch, width, height))
        draw_part(dc, &bi, pixels, pitch, crop_source(width, height, 0), g_dest);

    // The menu over a cut or bent picture: its part of the back buffer at the whole picture's
    // scale, centred on the frame (where the mouse is read), within the frame
    RECT m, all = {0, 0, width, height};
    if (wl_menu_rect(&m) && IntersectRect(&m, &m, &all))
    {
        int pw = r.right - r.left, ph = r.bottom - r.top;
        RECT to = {r.left + MulDiv(m.left, pw, width), r.top + MulDiv(m.top, ph, height), r.left + MulDiv(m.right, pw, width),
                   r.top + MulDiv(m.bottom, ph, height)};
        IntersectClipRect(dc, g_dest.left, g_dest.top, g_dest.right, g_dest.bottom);
        draw_part(dc, &whole, pixels, pitch, m, to);
        SelectClipRgn(dc, NULL);
    }
    ReleaseDC(g_wnd, dc);
}

HWND wl_display_hwnd(void)
{
    return g_wnd;
}

void wl_display_picture(RECT *r)
{
    if (placed() || g_glPicture)
        *r = g_pic;
    else
        GetClientRect(g_wnd, r);
}

int wl_display_frame(RECT *r)
{
    if (!g_wnd)
        return 0;
    *r = g_dest;
    return 1;
}

// ------------------------------------------------------------------------------
// What the window shows
// ------------------------------------------------------------------------------

int wl_display_screenshot(const char *path)
{
    if (!g_wnd)
        return 0;
    // An OpenGL game's window is read back by OpenGL (glhook.cpp): the screen's copy misses it on a
    // screen scaled for a game not aware of it
    if (g_glPicture)
        return wl_window_shot(path, 5000);
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
        wl_busy_enter(); // windowsloader's own file, where the agent asked: not the game's (no write folder)
        FILE *f = ok ? fopen(path, "wb") : NULL;
        wl_busy_leave();
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

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

// Windows of the game. In windowed mode the game must not take the desktop over: no display
// mode change, no always-on-top window, no cursor locked or moved.

#include <windows.h>
#include <stdio.h>

#include "hooks.h"
#include "log.h"

static void class_name(LPCSTR cls, char *buf, size_t size)
{
    if (IS_INTRESOURCE(cls))
        snprintf(buf, size, "#%u", (unsigned)(ULONG_PTR)cls);
    else
        snprintf(buf, size, "%s", cls);
}

static void class_nameW(LPCWSTR cls, char *buf, size_t size)
{
    if (IS_INTRESOURCE(cls))
        snprintf(buf, size, "#%u", (unsigned)(ULONG_PTR)cls);
    else
        wl_u8(cls, buf, size);
}

static DWORD fix_ex_style(DWORD ex, HWND parent)
{
    if (g_wl.windowed && !parent && (ex & WS_EX_TOPMOST))
        ex &= ~WS_EX_TOPMOST;
    return ex;
}

static void log_window(const char *cls, const char *title, DWORD ex, DWORD style, int x, int y, int w, int h, HWND parent, HWND r, const void *caller)
{
    char where[128];
    wl_log("CreateWindowEx class=\"%s\" title=\"%s\" style=0x%08lx ex=0x%08lx pos=%d,%d size=%dx%d parent=%p -> %p [%s]", cls, title, style, ex, x, y, w,
           h, parent, r, wl_where(caller, where, sizeof where));
}

static HWND(WINAPI *oCreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static HWND WINAPI hCreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst,
                                    LPVOID param)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oCreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    DWORD ex2 = fix_ex_style(ex, parent);
    wl_busy_enter();
    HWND r = oCreateWindowExA(ex2, cls, title, style, x, y, w, h, parent, menu, inst, param);
    wl_busy_leave();
    DWORD err = GetLastError();
    char c[128];
    class_name(cls, c, sizeof c);
    log_window(c, title ? title : "", ex, style, x, y, w, h, parent, r, WL_CALLER);
    if (ex2 != ex)
        wl_log("  always-on-top removed (windowed mode)");
    SetLastError(err);
    return r;
}

static HWND(WINAPI *oCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static HWND WINAPI hCreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst,
                                    LPVOID param)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oCreateWindowExW(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    DWORD ex2 = fix_ex_style(ex, parent);
    wl_busy_enter();
    HWND r = oCreateWindowExW(ex2, cls, title, style, x, y, w, h, parent, menu, inst, param);
    wl_busy_leave();
    DWORD err = GetLastError();
    char c[128], t[256];
    class_nameW(cls, c, sizeof c);
    log_window(c, title ? wl_u8(title, t, sizeof t) : "", ex, style, x, y, w, h, parent, r, WL_CALLER);
    if (ex2 != ex)
        wl_log("  always-on-top removed (windowed mode)");
    SetLastError(err);
    return r;
}

// ------------------------------------------------------------------------------
// Display mode
// ------------------------------------------------------------------------------

static void display_change(const char *what, DWORD width, DWORD height, DWORD bpp, DWORD hz, int restore, const void *caller)
{
    char where[128];
    if (restore)
        wl_log("%s(restore) %s [%s]", what, g_wl.windowed ? "ignored (windowed mode)" : "", wl_where(caller, where, sizeof where));
    else
        wl_log("%s %lux%lu %lu bpp %lu Hz %s [%s]", what, width, height, bpp, hz, g_wl.windowed ? "ignored (windowed mode)" : "",
               wl_where(caller, where, sizeof where));
}

static LONG(WINAPI *oChangeDisplaySettingsA)(DEVMODEA *, DWORD);
static LONG WINAPI hChangeDisplaySettingsA(DEVMODEA *mode, DWORD flags)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oChangeDisplaySettingsA(mode, flags);
    display_change("ChangeDisplaySettings", mode ? mode->dmPelsWidth : 0, mode ? mode->dmPelsHeight : 0, mode ? mode->dmBitsPerPel : 0,
                   mode ? mode->dmDisplayFrequency : 0, !mode, WL_CALLER);
    if (g_wl.windowed)
        return DISP_CHANGE_SUCCESSFUL;
    return oChangeDisplaySettingsA(mode, flags);
}

static LONG(WINAPI *oChangeDisplaySettingsW)(DEVMODEW *, DWORD);
static LONG WINAPI hChangeDisplaySettingsW(DEVMODEW *mode, DWORD flags)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oChangeDisplaySettingsW(mode, flags);
    display_change("ChangeDisplaySettings", mode ? mode->dmPelsWidth : 0, mode ? mode->dmPelsHeight : 0, mode ? mode->dmBitsPerPel : 0,
                   mode ? mode->dmDisplayFrequency : 0, !mode, WL_CALLER);
    if (g_wl.windowed)
        return DISP_CHANGE_SUCCESSFUL;
    return oChangeDisplaySettingsW(mode, flags);
}

static LONG(WINAPI *oChangeDisplaySettingsExA)(LPCSTR, DEVMODEA *, HWND, DWORD, LPVOID);
static LONG WINAPI hChangeDisplaySettingsExA(LPCSTR dev, DEVMODEA *mode, HWND wnd, DWORD flags, LPVOID param)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oChangeDisplaySettingsExA(dev, mode, wnd, flags, param);
    display_change("ChangeDisplaySettingsEx", mode ? mode->dmPelsWidth : 0, mode ? mode->dmPelsHeight : 0, mode ? mode->dmBitsPerPel : 0,
                   mode ? mode->dmDisplayFrequency : 0, !mode, WL_CALLER);
    if (g_wl.windowed)
        return DISP_CHANGE_SUCCESSFUL;
    return oChangeDisplaySettingsExA(dev, mode, wnd, flags, param);
}

static LONG(WINAPI *oChangeDisplaySettingsExW)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID);
static LONG WINAPI hChangeDisplaySettingsExW(LPCWSTR dev, DEVMODEW *mode, HWND wnd, DWORD flags, LPVOID param)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oChangeDisplaySettingsExW(dev, mode, wnd, flags, param);
    display_change("ChangeDisplaySettingsEx", mode ? mode->dmPelsWidth : 0, mode ? mode->dmPelsHeight : 0, mode ? mode->dmBitsPerPel : 0,
                   mode ? mode->dmDisplayFrequency : 0, !mode, WL_CALLER);
    if (g_wl.windowed)
        return DISP_CHANGE_SUCCESSFUL;
    return oChangeDisplaySettingsExW(dev, mode, wnd, flags, param);
}

// ------------------------------------------------------------------------------
// Cursor
// ------------------------------------------------------------------------------

static BOOL(WINAPI *oClipCursor)(const RECT *);
static BOOL WINAPI hClipCursor(const RECT *r)
{
    static volatile LONG logged;
    if (wl_busy() || !g_wl.windowed || wl_is_system_caller(WL_CALLER))
        return oClipCursor(r);
    if (r && InterlockedIncrement(&logged) <= 3)
        wl_log("ClipCursor(%ld,%ld-%ld,%ld) ignored (windowed mode)", r->left, r->top, r->right, r->bottom);
    return TRUE;
}

static BOOL(WINAPI *oSetCursorPos)(int, int);
static BOOL WINAPI hSetCursorPos(int x, int y)
{
    static volatile LONG logged;
    if (wl_busy() || !g_wl.windowed || wl_is_system_caller(WL_CALLER))
        return oSetCursorPos(x, y);
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("SetCursorPos(%d,%d) ignored (windowed mode)", x, y);
    return TRUE;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"user32.dll", CreateWindowExA),
    WL_HOOK(L"user32.dll", CreateWindowExW),
    WL_HOOK(L"user32.dll", ChangeDisplaySettingsA),
    WL_HOOK(L"user32.dll", ChangeDisplaySettingsW),
    WL_HOOK(L"user32.dll", ChangeDisplaySettingsExA),
    WL_HOOK(L"user32.dll", ChangeDisplaySettingsExW),
    WL_HOOK(L"user32.dll", ClipCursor),
    WL_HOOK(L"user32.dll", SetCursorPos),
};

void wl_window_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}

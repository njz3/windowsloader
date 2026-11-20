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

// The virtual screen (vscreen.h): the primary screen's size told to the game as W x H wherever it
// asks, its modes' lists holding that mode. A game is one screen: the primary one is the one
// changed, at 0,0. Asked by windowsloader itself or by Windows (wl_is_system_caller), or while
// windowsloader works (wl_busy), the real answers.

#include <windows.h>
#include <d3d9.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hooks.h"
#include "log.h"
#include "vscreen.h"

static int g_minW, g_minH;             // the game's minimum (wl_vscreen_minimum), 0: none known (AUTO then the real screen)
static volatile LONG g_w, g_h;          // the screen told, 0 when the real one
static volatile LONG g_modeW, g_modeH;  // the mode added to the modes' lists: the screen told, or AUTO's
                                        // minimum on a larger screen that lacks it (0: none)
static int g_realW, g_realH;
static wchar_t g_primary[CCHDEVICENAME]; // the primary screen's device name (\\.\DISPLAY1)

static int game_caller(const void *caller)
{
    return !wl_busy() && !wl_is_system_caller(caller);
}

// The game told the screen's size
static int game_asks(const void *caller)
{
    return g_w && game_caller(caller);
}

// The game given the modes' lists with ours
static int game_lists(const void *caller)
{
    return g_modeW && game_caller(caller);
}

void wl_vscreen_minimum(int width, int height)
{
    if (width > 0 && height > 0)
    {
        g_minW = width;
        g_minH = height;
    }
}

int wl_vscreen_size(int *width, int *height)
{
    if (width)
        *width = g_w;
    if (height)
        *height = g_h;
    return g_w != 0;
}

static int primary_device(const char *a, const wchar_t *w)
{
    if ((!a || !*a) && (!w || !*w))
        return 1;
    if (w)
        return !_wcsicmp(w, g_primary);
    wchar_t c[CCHDEVICENAME];
    MultiByteToWideChar(CP_ACP, 0, a, -1, c, CCHDEVICENAME);
    return !_wcsicmp(c, g_primary);
}

// ------------------------------------------------------------------------------
// Windows' answers
// ------------------------------------------------------------------------------

// GetSystemMetrics is system.cpp's hook (one hook per function: a second one on it is never made),
// which asks here
int wl_vscreen_metric(int index, const void *caller, int *value)
{
    if (!game_asks(caller))
        return 0;
    switch (index)
    {
    case SM_CXSCREEN:
    case SM_CXVIRTUALSCREEN:
    case SM_CXFULLSCREEN:
        *value = g_w;
        return 1;
    case SM_CYSCREEN:
    case SM_CYVIRTUALSCREEN:
    case SM_CYFULLSCREEN:
        *value = g_h;
        return 1;
    }
    return 0;
}

// The modes of the primary screen: the real ones, then ours when they lack it
static int g_modeCount = -1, g_modeThere;

static void count_modes(void)
{
    if (g_modeCount >= 0)
        return;
    DEVMODEW dm = {0};
    dm.dmSize = sizeof dm;
    int n = 0;
    while (EnumDisplaySettingsExW(NULL, n, &dm, 0)) // windowsloader's own call: the real list
    {
        if ((int)dm.dmPelsWidth == g_modeW && (int)dm.dmPelsHeight == g_modeH)
            g_modeThere = 1;
        n++;
    }
    g_modeCount = n;
}

// iMode: ENUM_CURRENT_SETTINGS / ENUM_REGISTRY_SETTINGS (the size changed), past the real list (ours)
template <typename DM> static int mode_answer(DWORD mode, DM *dm, BOOL (*real)(DWORD, DM *), BOOL *ok)
{
    if (mode == ENUM_CURRENT_SETTINGS || mode == ENUM_REGISTRY_SETTINGS)
    {
        if (!g_w)
            return 0; // the real screen's
        *ok = real(mode, dm);
        if (*ok)
        {
            dm->dmPelsWidth = (DWORD)g_w;
            dm->dmPelsHeight = (DWORD)g_h;
        }
        return 1;
    }
    count_modes();
    if ((int)mode < g_modeCount || g_modeThere || (int)mode > g_modeCount)
        return 0; // the real answer (FALSE past ours)
    *ok = real(ENUM_CURRENT_SETTINGS, dm);
    if (*ok)
    {
        dm->dmPelsWidth = (DWORD)g_modeW;
        dm->dmPelsHeight = (DWORD)g_modeH;
    }
    return 1;
}

static BOOL(WINAPI *oEnumDisplaySettingsExA)(LPCSTR, DWORD, DEVMODEA *, DWORD);
static BOOL(WINAPI *oEnumDisplaySettingsExW)(LPCWSTR, DWORD, DEVMODEW *, DWORD);
static BOOL(WINAPI *oEnumDisplaySettingsA)(LPCSTR, DWORD, DEVMODEA *);
static BOOL(WINAPI *oEnumDisplaySettingsW)(LPCWSTR, DWORD, DEVMODEW *);
static BOOL realA(DWORD mode, DEVMODEA *dm)
{
    return oEnumDisplaySettingsA(NULL, mode, dm);
}
static BOOL realW(DWORD mode, DEVMODEW *dm)
{
    return oEnumDisplaySettingsW(NULL, mode, dm);
}

static BOOL WINAPI hEnumDisplaySettingsExA(LPCSTR device, DWORD mode, DEVMODEA *dm, DWORD flags)
{
    BOOL ok;
    if (dm && game_lists(WL_CALLER) && primary_device(device, NULL) && mode_answer(mode, dm, realA, &ok))
        return ok;
    return oEnumDisplaySettingsExA(device, mode, dm, flags);
}

static BOOL WINAPI hEnumDisplaySettingsExW(LPCWSTR device, DWORD mode, DEVMODEW *dm, DWORD flags)
{
    BOOL ok;
    if (dm && game_lists(WL_CALLER) && primary_device(NULL, device) && mode_answer(mode, dm, realW, &ok))
        return ok;
    return oEnumDisplaySettingsExW(device, mode, dm, flags);
}

static BOOL WINAPI hEnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA *dm)
{
    BOOL ok;
    if (dm && game_lists(WL_CALLER) && primary_device(device, NULL) && mode_answer(mode, dm, realA, &ok))
        return ok;
    return oEnumDisplaySettingsA(device, mode, dm);
}

static BOOL WINAPI hEnumDisplaySettingsW(LPCWSTR device, DWORD mode, DEVMODEW *dm)
{
    BOOL ok;
    if (dm && game_lists(WL_CALLER) && primary_device(NULL, device) && mode_answer(mode, dm, realW, &ok))
        return ok;
    return oEnumDisplaySettingsW(device, mode, dm);
}

static void monitor_answer(MONITORINFO *mi)
{
    if (!(mi->dwFlags & MONITORINFOF_PRIMARY))
        return;
    SetRect(&mi->rcMonitor, 0, 0, g_w, g_h);
    mi->rcWork = mi->rcMonitor;
}

static BOOL(WINAPI *oGetMonitorInfoA)(HMONITOR, LPMONITORINFO);
static BOOL WINAPI hGetMonitorInfoA(HMONITOR m, LPMONITORINFO mi)
{
    BOOL ok = oGetMonitorInfoA(m, mi);
    if (ok && mi && game_asks(WL_CALLER))
        monitor_answer(mi);
    return ok;
}

static BOOL(WINAPI *oGetMonitorInfoW)(HMONITOR, LPMONITORINFO);
static BOOL WINAPI hGetMonitorInfoW(HMONITOR m, LPMONITORINFO mi)
{
    BOOL ok = oGetMonitorInfoW(m, mi);
    if (ok && mi && game_asks(WL_CALLER))
        monitor_answer(mi);
    return ok;
}

static int(WINAPI *oGetDeviceCaps)(HDC, int);
static int WINAPI hGetDeviceCaps(HDC dc, int index)
{
    if ((index == HORZRES || index == VERTRES || index == DESKTOPHORZRES || index == DESKTOPVERTRES) && game_asks(WL_CALLER) &&
        oGetDeviceCaps(dc, TECHNOLOGY) == DT_RASDISPLAY)
        return index == HORZRES || index == DESKTOPHORZRES ? g_w : g_h;
    return oGetDeviceCaps(dc, index);
}

static WlHook g_hooks[] = {
    WL_HOOK(L"user32.dll", EnumDisplaySettingsA),
    WL_HOOK(L"user32.dll", EnumDisplaySettingsW),
    WL_HOOK(L"user32.dll", EnumDisplaySettingsExA),
    WL_HOOK(L"user32.dll", EnumDisplaySettingsExW),
    WL_HOOK(L"user32.dll", GetMonitorInfoA),
    WL_HOOK(L"user32.dll", GetMonitorInfoW),
    WL_HOOK(L"gdi32.dll", GetDeviceCaps),
};

void wl_vscreen_settings(const char *value)
{
    static int done; // once: the settings may be read again (the menu's saving)
    if (done++ || !value || !*value)
        return;
    MONITORINFOEXW mi = {0};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi))
        return;
    g_realW = mi.rcMonitor.right - mi.rcMonitor.left;
    g_realH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    wcscpy_s(g_primary, CCHDEVICENAME, mi.szDevice);
    int w = 0, h = 0;
    if (!_stricmp(value, "OFF"))
        ;
    else if (sscanf_s(value, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
        ;
    else if (g_minW && (g_realW < g_minW || g_realH < g_minH)) // AUTO
        w = g_minW, h = g_minH;
    if (!w || (w == g_realW && h == g_realH))
    {
        // AUTO on a screen large enough: the game's size added to the modes' lists, for a game that
        // looks for it there (Sonic & Sega All-Stars Racing's 1280x720 at 60 Hz, missing from a
        // cabinet's virtual screen at 1920x1080), the screen's own size told
        if (!w && _stricmp(value, "OFF") && g_minW && !(g_minW == g_realW && g_minH == g_realH))
        {
            InterlockedExchange(&g_modeW, g_minW);
            InterlockedExchange(&g_modeH, g_minH);
            wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
            wl_log("vscreen: VIRTUAL_DISPLAY %s: the real screen, %dx%d; the game's %dx%d added to the modes' lists when they lack it", value,
                   g_realW, g_realH, g_minW, g_minH);
            return;
        }
        wl_log("vscreen: VIRTUAL_DISPLAY %s: the real screen, %dx%d (the game's minimum %dx%d)", value, g_realW, g_realH, g_minW, g_minH);
        return;
    }
    InterlockedExchange(&g_w, w);
    InterlockedExchange(&g_h, h);
    InterlockedExchange(&g_modeW, w);
    InterlockedExchange(&g_modeH, h);
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
    wl_log("vscreen: VIRTUAL_DISPLAY %s: the game is told a %dx%d screen (it is %dx%d; the game's minimum %dx%d); its picture put on the "
           "real one by [Display]",
           value, w, h, g_realW, g_realH, g_minW, g_minH);
}

// ------------------------------------------------------------------------------
// Direct3D 9: the adapter's modes and its mode
// ------------------------------------------------------------------------------

static void **vtable_of(void *com)
{
    return *(void ***)com;
}

static UINT(STDMETHODCALLTYPE *oGetAdapterModeCount)(IDirect3D9 *, UINT, D3DFORMAT);
static HRESULT(STDMETHODCALLTYPE *oEnumAdapterModes)(IDirect3D9 *, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE *);
static HRESULT(STDMETHODCALLTYPE *oGetAdapterDisplayMode)(IDirect3D9 *, UINT, D3DDISPLAYMODE *);

// Our modes for a format: our size at the screen's rate and at 60 Hz (Sonic & Sega All-Stars
// Racing looks for Settings.ini's SCREEN_WIDTH x SCREEN_HEIGHT at SCREEN_REFRESH, 60, among the
// X8R8G8B8 modes, +0x2ec4: "Invalid Game Resolution"), each one the list lacks; the real count in n
static int d3d9_ours(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format, UINT *n, UINT rates[2])
{
    *n = oGetAdapterModeCount(d3d, adapter, format);
    if (!*n)
        return 0;
    D3DDISPLAYMODE cur;
    UINT want[2] = {60, 0}, count = 0;
    if (SUCCEEDED(oGetAdapterDisplayMode(d3d, adapter, &cur)) && cur.RefreshRate && cur.RefreshRate != 60)
        want[1] = cur.RefreshRate;
    for (int k = 0; k < 2; k++)
    {
        if (!want[k])
            continue;
        int there = 0;
        for (UINT i = 0; i < *n && !there; i++)
        {
            D3DDISPLAYMODE m;
            there = SUCCEEDED(oEnumAdapterModes(d3d, adapter, format, i, &m)) && (int)m.Width == g_modeW && (int)m.Height == g_modeH &&
                    m.RefreshRate == want[k];
        }
        if (!there)
            rates[count++] = want[k];
    }
    return (int)count;
}

static UINT STDMETHODCALLTYPE hGetAdapterModeCount(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format)
{
    if (adapter != D3DADAPTER_DEFAULT || !game_lists(WL_CALLER))
        return oGetAdapterModeCount(d3d, adapter, format);
    UINT n, rates[2];
    int ours = d3d9_ours(d3d, adapter, format, &n, rates);
    return n + (UINT)ours;
}

static HRESULT STDMETHODCALLTYPE hEnumAdapterModes(IDirect3D9 *d3d, UINT adapter, D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode)
{
    if (adapter != D3DADAPTER_DEFAULT || !mode || !game_lists(WL_CALLER))
        return oEnumAdapterModes(d3d, adapter, format, index, mode);
    UINT n, rates[2];
    int ours = d3d9_ours(d3d, adapter, format, &n, rates);
    if (index < n || index >= n + (UINT)ours)
        return oEnumAdapterModes(d3d, adapter, format, index, mode);
    HRESULT hr = oEnumAdapterModes(d3d, adapter, format, 0, mode); // its format, our size and rate
    if (SUCCEEDED(hr))
    {
        mode->Width = (UINT)g_modeW;
        mode->Height = (UINT)g_modeH;
        mode->RefreshRate = rates[index - n];
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 2)
            wl_log("vscreen: Direct3D 9's modes: %dx%d at %u Hz added (format %d)", (int)g_modeW, (int)g_modeH, mode->RefreshRate, (int)format);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hGetAdapterDisplayMode(IDirect3D9 *d3d, UINT adapter, D3DDISPLAYMODE *mode)
{
    HRESULT hr = oGetAdapterDisplayMode(d3d, adapter, mode);
    if (SUCCEEDED(hr) && mode && adapter == D3DADAPTER_DEFAULT && game_asks(WL_CALLER))
    {
        mode->Width = (UINT)g_w;
        mode->Height = (UINT)g_h;
    }
    return hr;
}

void wl_vscreen_d3d9(void *direct3d9)
{
    static int done;
    if (!g_modeW || !direct3d9 || done++)
        return;
    void **vt = vtable_of(direct3d9);
    wl_hook_code(vt[6], (void *)hGetAdapterModeCount, (void **)&oGetAdapterModeCount, "IDirect3D9::GetAdapterModeCount (VIRTUAL_DISPLAY)");
    wl_hook_code(vt[7], (void *)hEnumAdapterModes, (void **)&oEnumAdapterModes, "IDirect3D9::EnumAdapterModes (VIRTUAL_DISPLAY)");
    wl_hook_code(vt[8], (void *)hGetAdapterDisplayMode, (void **)&oGetAdapterDisplayMode, "IDirect3D9::GetAdapterDisplayMode (VIRTUAL_DISPLAY)");
}

// ------------------------------------------------------------------------------
// DXGI: the output's place and its modes
// ------------------------------------------------------------------------------

static HRESULT(STDMETHODCALLTYPE *oOutputGetDesc)(IDXGIOutput *, DXGI_OUTPUT_DESC *);
static HRESULT(STDMETHODCALLTYPE *oGetDisplayModeList)(IDXGIOutput *, DXGI_FORMAT, UINT, UINT *, DXGI_MODE_DESC *);
static HRESULT(STDMETHODCALLTYPE *oFindClosestMatchingMode)(IDXGIOutput *, const DXGI_MODE_DESC *, DXGI_MODE_DESC *, IUnknown *);
static HRESULT(STDMETHODCALLTYPE *oGetDisplayModeList1)(IDXGIOutput1 *, DXGI_FORMAT, UINT, UINT *, DXGI_MODE_DESC1 *);

// The primary screen's output: the one at 0,0
static int primary_output(IDXGIOutput *out)
{
    DXGI_OUTPUT_DESC d;
    return SUCCEEDED(oOutputGetDesc(out, &d)) && d.DesktopCoordinates.left == 0 && d.DesktopCoordinates.top == 0;
}

static HRESULT STDMETHODCALLTYPE hOutputGetDesc(IDXGIOutput *out, DXGI_OUTPUT_DESC *d)
{
    HRESULT hr = oOutputGetDesc(out, d);
    if (SUCCEEDED(hr) && d && d->DesktopCoordinates.left == 0 && d->DesktopCoordinates.top == 0 && game_asks(WL_CALLER))
        SetRect(&d->DesktopCoordinates, 0, 0, g_w, g_h);
    return hr;
}

// A modes' list (DXGI_MODE_DESC or DXGI_MODE_DESC1, the size first in both) with ours added when
// it lacks it: modelled on the list's first mode, its rate and its format
template <typename D, typename Out, typename Fn> static HRESULT modes_with_ours(Out *out, DXGI_FORMAT format, UINT flags, UINT *num, D *desc, Fn real)
{
    UINT n = 0;
    HRESULT hr = real(out, format, flags, &n, NULL);
    if (FAILED(hr) || !n)
        return real(out, format, flags, num, desc);
    D *all = (D *)calloc(n + 1, sizeof(D));
    if (!all)
        return real(out, format, flags, num, desc);
    UINT got = n;
    hr = real(out, format, flags, &got, all);
    int there = 0;
    for (UINT i = 0; SUCCEEDED(hr) && i < got && !there; i++)
        there = (int)all[i].Width == g_modeW && (int)all[i].Height == g_modeH;
    if (FAILED(hr) || there)
    {
        free(all);
        return real(out, format, flags, num, desc);
    }
    all[got] = all[0];
    all[got].Width = (UINT)g_modeW;
    all[got].Height = (UINT)g_modeH;
    got++;
    if (!desc)
        *num = got;
    else if (*num < got)
    {
        memcpy(desc, all, *num * sizeof(D));
        hr = DXGI_ERROR_MORE_DATA;
    }
    else
    {
        memcpy(desc, all, got * sizeof(D));
        *num = got;
        hr = S_OK;
    }
    free(all);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hGetDisplayModeList(IDXGIOutput *out, DXGI_FORMAT format, UINT flags, UINT *num, DXGI_MODE_DESC *desc)
{
    if (!num || !game_lists(WL_CALLER) || !primary_output(out))
        return oGetDisplayModeList(out, format, flags, num, desc);
    return modes_with_ours(out, format, flags, num, desc, oGetDisplayModeList);
}

static HRESULT STDMETHODCALLTYPE hGetDisplayModeList1(IDXGIOutput1 *out, DXGI_FORMAT format, UINT flags, UINT *num, DXGI_MODE_DESC1 *desc)
{
    if (!num || !game_lists(WL_CALLER) || !primary_output(out))
        return oGetDisplayModeList1(out, format, flags, num, desc);
    return modes_with_ours(out, format, flags, num, desc, oGetDisplayModeList1);
}

static HRESULT STDMETHODCALLTYPE hFindClosestMatchingMode(IDXGIOutput *out, const DXGI_MODE_DESC *want, DXGI_MODE_DESC *closest, IUnknown *device)
{
    if (want && closest && (int)want->Width == g_modeW && (int)want->Height == g_modeH && game_lists(WL_CALLER) && primary_output(out))
    {
        *closest = *want;
        if (!closest->RefreshRate.Numerator || !closest->RefreshRate.Denominator)
            closest->RefreshRate = DXGI_RATIONAL{60, 1};
        if (closest->Format == DXGI_FORMAT_UNKNOWN)
            closest->Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        return S_OK;
    }
    return oFindClosestMatchingMode(out, want, closest, device);
}

void wl_vscreen_dxgi(void *dxgiFactory)
{
    static int done;
    if (!g_modeW || !dxgiFactory || done)
        return;
    IDXGIFactory *f = (IDXGIFactory *)dxgiFactory;
    IDXGIAdapter *ad = NULL;
    IDXGIOutput *out = NULL;
    wl_busy_enter();
    if (SUCCEEDED(f->EnumAdapters(0, &ad)) && ad && SUCCEEDED(ad->EnumOutputs(0, &out)) && out)
    {
        done = 1;
        void **vt = vtable_of(out);
        wl_hook_code(vt[7], (void *)hOutputGetDesc, (void **)&oOutputGetDesc, "IDXGIOutput::GetDesc (VIRTUAL_DISPLAY)");
        wl_hook_code(vt[8], (void *)hGetDisplayModeList, (void **)&oGetDisplayModeList, "IDXGIOutput::GetDisplayModeList (VIRTUAL_DISPLAY)");
        wl_hook_code(vt[9], (void *)hFindClosestMatchingMode, (void **)&oFindClosestMatchingMode,
                     "IDXGIOutput::FindClosestMatchingMode (VIRTUAL_DISPLAY)");
        IDXGIOutput1 *out1 = NULL;
        if (SUCCEEDED(out->QueryInterface(__uuidof(IDXGIOutput1), (void **)&out1)) && out1)
        {
            wl_hook_code(vtable_of(out1)[19], (void *)hGetDisplayModeList1, (void **)&oGetDisplayModeList1,
                         "IDXGIOutput1::GetDisplayModeList1 (VIRTUAL_DISPLAY)");
            out1->Release();
        }
        out->Release();
    }
    if (ad)
        ad->Release();
    wl_busy_leave();
}

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

// Direct3D 8 (Wasteland Racers 2071). By default played on Direct3D 9 by d3d8to9 (below), so that
// the device takes windowsloader's Direct3D 9 path. With WINDOWSLOADER_D3D8=native, Windows' own
// Direct3D 8: the device the game asks for logged and kept in a window, the window set for the
// display mode (display.cpp, the picture filling it), the frames counted (the agent's "ready",
// the frames per second in the title). The Windows SDK has no d3d8.h: the few types and vtable
// slots needed are declared here, from Direct3D 8's documented layout.

#include <windows.h>
#include <d3d9.h> // D3DFORMAT, D3DSWAPEFFECT, D3DMULTISAMPLE_TYPE, D3DDEVTYPE: the same values in Direct3D 8
#include <stdio.h>

#include "agent.h"
#include "display.h"
#include "hooks.h"
#include "log.h"

// Direct3D 8's D3DPRESENT_PARAMETERS (no MultiSampleQuality, no PresentationInterval of its own)
struct WlPresent8
{
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};

// vtable slots (d3d8.h declaration order)
enum
{
    SLOT_D3D8_GETADAPTERDISPLAYMODE = 8,
    SLOT_D3D8_CREATEDEVICE = 15,
    SLOT_DEVICE8_RESET = 14,
    SLOT_DEVICE8_PRESENT = 15,
};

static void **vtable_of(void *com)
{
    return *(void ***)com;
}

static void log_pp(const char *what, const WlPresent8 *pp)
{
    if (pp)
        wl_log("%s: %ux%u format=%d count=%u msaa=%d swap=%d window=%p windowed=%d depth=%d/%d flags=0x%lx refresh=%u interval=0x%x", what,
               pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount, pp->MultiSampleType, pp->SwapEffect,
               pp->hDeviceWindow, pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->Flags, pp->FullScreen_RefreshRateInHz,
               pp->FullScreen_PresentationInterval);
}

static void *g_d3d8; // the IDirect3D8, for its display mode

// Full screen -> window: Direct3D 8 wants the back buffer at the desktop's format there, no
// refresh rate and the default interval
static int make_windowed(WlPresent8 *pp)
{
    if (!g_wl.windowed || !pp || pp->Windowed)
        return 0;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->FullScreen_PresentationInterval = 0;
    D3DDISPLAYMODE mode;
    typedef HRESULT(STDMETHODCALLTYPE * GetModeFn)(void *, UINT, D3DDISPLAYMODE *);
    if (g_d3d8 && SUCCEEDED(((GetModeFn)vtable_of(g_d3d8)[SLOT_D3D8_GETADAPTERDISPLAYMODE])(g_d3d8, 0, &mode)))
        pp->BackBufferFormat = mode.Format;
    return 1;
}

static void after_device(const WlPresent8 *pp, HWND focus)
{
    if (!pp)
        return;
    wl_frame_size((int)pp->BackBufferWidth, (int)pp->BackBufferHeight);
    wl_display_window(pp->hDeviceWindow ? pp->hDeviceWindow : focus, (int)pp->BackBufferWidth, (int)pp->BackBufferHeight);
}

static HRESULT(STDMETHODCALLTYPE *oPresent8)(void *, const RECT *, const RECT *, HWND, const RGNDATA *);
static HRESULT STDMETHODCALLTYPE hPresent8(void *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    static unsigned frames;
    static HRESULT lastHr = S_OK;
    wl_display_dest(); // F11, Alt+Enter, the frames per second in the title
    HRESULT hr = oPresent8(dev, src, dst, wnd, dirty);
    wl_frame_presented();
    if (!frames++)
        wl_log("Direct3D 8 Present: first frame (hr=0x%08lx)", hr);
    if (hr != lastHr)
    {
        wl_log("Direct3D 8 Present -> 0x%08lx", hr);
        lastHr = hr;
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oReset8)(void *, WlPresent8 *);
static HRESULT STDMETHODCALLTYPE hReset8(void *dev, WlPresent8 *pp)
{
    log_pp("Direct3D 8 Reset", pp);
    if (make_windowed(pp))
        wl_log("  kept in a window");
    HRESULT hr = oReset8(dev, pp);
    if (SUCCEEDED(hr))
        after_device(pp, NULL);
    wl_log("Direct3D 8 Reset -> 0x%08lx", hr);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreateDevice8)(void *, UINT, D3DDEVTYPE, HWND, DWORD, WlPresent8 *, void **);
static HRESULT STDMETHODCALLTYPE hCreateDevice8(void *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, WlPresent8 *pp, void **out)
{
    static int hooked;
    char where[128];
    wl_log("Direct3D 8 CreateDevice adapter=%u type=%d focus=%p behavior=0x%lx [%s]", adapter, type, focus, behavior,
           wl_where(WL_CALLER, where, sizeof where));
    log_pp("  asked", pp);
    g_d3d8 = d3d;
    if (make_windowed(pp))
        log_pp("  kept in a window", pp);
    HRESULT hr = oCreateDevice8(d3d, adapter, type, focus, behavior, pp, out);
    wl_log("Direct3D 8 CreateDevice -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && out && *out)
    {
        if (!hooked)
        {
            hooked = 1;
            void **vt = vtable_of(*out);
            wl_hook_code(vt[SLOT_DEVICE8_PRESENT], (void *)hPresent8, (void **)&oPresent8, "IDirect3DDevice8::Present");
            wl_hook_code(vt[SLOT_DEVICE8_RESET], (void *)hReset8, (void **)&oReset8, "IDirect3DDevice8::Reset");
        }
        after_device(pp, focus);
    }
    return hr;
}

// d3d8to9 (src/d3d8to9, crosire's, BSD 2-clause): Direct3D 8 played on Direct3D 9
struct IDirect3D8;
int d3d8to9_create(IDirect3D9 *d3d, IDirect3D8 **out);

static void *(WINAPI *oDirect3DCreate8)(UINT);
static void *WINAPI hDirect3DCreate8(UINT sdk)
{
    static int hooked;
    char where[128];
    // By default the game's Direct3D 8 is played on Direct3D 9 (d3d8to9), whose device then takes
    // windowsloader's Direct3D 9 path: the copy by GDI, the display modes, the settings menu, the
    // agent's pictures. WINDOWSLOADER_D3D8=native keeps Windows' own Direct3D 8 (no picture on
    // this PC: a blt-model Present shows nothing here).
    wchar_t mode[16] = L"";
    GetEnvironmentVariableW(L"WINDOWSLOADER_D3D8", mode, 16);
#ifdef WL_D3D8TO9 // the 32-bit DLL: Direct3D 8 is 32-bit only
    if (_wcsicmp(mode, L"native"))
    {
        HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
        wl_hooks_install_pending(); // Direct3DCreate9's hook, for the device d3d8to9 makes
        typedef IDirect3D9 *(WINAPI * Create9Fn)(UINT);
        Create9Fn create9 = d3d9 ? (Create9Fn)GetProcAddress(d3d9, "Direct3DCreate9") : NULL;
        IDirect3D8 *d3d8 = NULL;
        int d3dx = d3d8to9_create(create9 ? create9(D3D_SDK_VERSION) : NULL, &d3d8);
        wl_log("Direct3DCreate8(%u) -> %p, Direct3D 8 played on Direct3D 9 (d3d8to9)%s [%s]", sdk, d3d8,
               d3dx ? "" : "; d3dx9_43.dll missing: the shaders that need it fail", wl_where(WL_CALLER, where, sizeof where));
        if (d3d8)
            return d3d8;
    }
#endif
    wl_busy_enter();
    void *d3d = oDirect3DCreate8(sdk);
    wl_busy_leave();
    wl_log("Direct3DCreate8(%u) -> %p, Windows' own Direct3D 8 [%s]", sdk, d3d, wl_where(WL_CALLER, where, sizeof where));
    if (d3d && !hooked)
    {
        hooked = 1;
        wl_hook_code(vtable_of(d3d)[SLOT_D3D8_CREATEDEVICE], (void *)hCreateDevice8, (void **)&oCreateDevice8, "IDirect3D8::CreateDevice");
    }
    return d3d;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"d3d8.dll", Direct3DCreate8),
};

void wl_d3d8_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}

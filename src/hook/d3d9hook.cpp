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

// Direct3D 9: logs the device the game asks for, keeps it in a window (windowed mode) and
// counts the frames. The COM methods are hooked through the vtables, which d3d9.dll shares
// between all its objects, so each method is hooked once.

#include <windows.h>
#include <d3d9.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "display.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"

// vtable slots (d3d9.h declaration order)
enum
{
    SLOT_D3D9_CREATEDEVICE = 16,
    SLOT_D3D9EX_CREATEDEVICEEX = 20,
    SLOT_DEVICE_RESET = 16,
    SLOT_DEVICE_PRESENT = 17,
};

static void **vtable_of(void *com)
{
    return *(void ***)com;
}

static void log_pp(const char *what, const D3DPRESENT_PARAMETERS *pp)
{
    if (!pp)
        return;
    wl_log("%s: %ux%u format=%d count=%u msaa=%d/%lu swap=%d window=%p windowed=%d depth=%d/%d flags=0x%lx refresh=%u interval=0x%x", what,
           pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount, pp->MultiSampleType, pp->MultiSampleQuality,
           pp->SwapEffect, pp->hDeviceWindow, pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->Flags,
           pp->FullScreen_RefreshRateInHz, pp->PresentationInterval);
}

// Full screen -> window, with the presentation settings a window accepts
// The game's presentation interval: its frames' pace once presented to the hidden window (GDI)
static UINT g_interval = D3DPRESENT_INTERVAL_ONE;

static int make_windowed(D3DPRESENT_PARAMETERS *pp)
{
    if (pp)
        g_interval = pp->PresentationInterval;
    if (!g_wl.windowed || !pp || pp->Windowed)
        return 0;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    if (pp->PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT && pp->PresentationInterval != D3DPRESENT_INTERVAL_ONE &&
        pp->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE)
        pp->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    return 1;
}

// ------------------------------------------------------------------------------
// Screenshots for the agent: the back buffer, read just before it is presented
// ------------------------------------------------------------------------------

static volatile LONG g_frames;
static volatile LONG g_shotPending;
static char g_shotPath[MAX_PATH];
static int g_shotOk;
static HANDLE g_shotDone;

// 32-bit BGRX (or RGBX) pixels to a 24-bit BMP (rows bottom-up, padded to 4 bytes)
int wl_write_bmp(const char *path, const unsigned char *pixels, int pitch, int w, int h, int rgba)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int row = (w * 3 + 3) & ~3;
    unsigned size = 54 + (unsigned)row * h;
    unsigned char hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54;
    hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    fwrite(hdr, 1, sizeof hdr, f);
    unsigned char *line = (unsigned char *)calloc(1, row);
    for (int y = h - 1; y >= 0 && line; y--)
    {
        const unsigned char *src = pixels + (size_t)y * pitch;
        for (int x = 0; x < w; x++)
        {
            line[x * 3] = src[x * 4 + (rgba ? 2 : 0)];
            line[x * 3 + 1] = src[x * 4 + 1];
            line[x * 3 + 2] = src[x * 4 + (rgba ? 0 : 2)];
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    return 1;
}

// 1 when the picture is written; else 0, and the step that failed in the log
static int save_back_buffer(IDirect3DDevice9 *dev, const char *path)
{
    IDirect3DSurface9 *bb = NULL, *resolved = NULL, *sys = NULL;
    HRESULT hr = dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if (FAILED(hr))
    {
        wl_log("shot: GetBackBuffer -> 0x%08lx", hr);
        return 0;
    }
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    int ok = 0;
    IDirect3DSurface9 *src = bb;
    const char *step = "format";
    hr = S_OK;
    // A multisampled back buffer is resolved into a plain render target first
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE && SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, NULL)) &&
        SUCCEEDED(dev->StretchRect(bb, NULL, resolved, NULL, D3DTEXF_NONE)))
        src = resolved;
    // 8 bits per channel, or A2R10G10B10 (Chase H.Q. 2) brought back to 8
    int tenBits = d.Format == D3DFMT_A2R10G10B10;
    if (d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8 || tenBits)
    {
        step = "CreateOffscreenPlainSurface";
        hr = dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, NULL);
        if (SUCCEEDED(hr))
        {
            step = "GetRenderTargetData";
            hr = dev->GetRenderTargetData(src, sys);
        }
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(hr))
        {
            step = "LockRect";
            hr = sys->LockRect(&lr, NULL, D3DLOCK_READONLY);
        }
        if (SUCCEEDED(hr))
        {
            step = "writing the file";
            unsigned char *pixels = (unsigned char *)lr.pBits;
            int pitch = lr.Pitch;
            unsigned char *converted = tenBits ? (unsigned char *)malloc((size_t)d.Width * d.Height * 4) : NULL;
            if (converted)
            {
                for (UINT y = 0; y < d.Height; y++)
                {
                    const DWORD *row = (const DWORD *)(pixels + (size_t)y * lr.Pitch);
                    unsigned char *out = converted + (size_t)y * d.Width * 4;
                    for (UINT x = 0; x < d.Width; x++)
                    {
                        DWORD v = row[x];
                        out[x * 4] = (unsigned char)(v >> 2), out[x * 4 + 1] = (unsigned char)(v >> 12), out[x * 4 + 2] = (unsigned char)(v >> 22);
                        out[x * 4 + 3] = 0;
                    }
                }
                pixels = converted;
                pitch = (int)d.Width * 4;
            }
            if (!tenBits || converted)
                ok = wl_write_bmp(path, pixels, pitch, (int)d.Width, (int)d.Height, 0);
            free(converted);
            sys->UnlockRect();
        }
    }
    if (!ok)
        wl_log("shot: %s failed (0x%08lx), back buffer %ux%u format %d msaa %d", step, hr, d.Width, d.Height, d.Format, d.MultiSampleType);
    if (sys)
        sys->Release();
    if (resolved)
        resolved->Release();
    bb->Release();
    return ok;
}

static volatile LONG g_width, g_height;

unsigned wl_d3d9_frames(void)
{
    return (unsigned)g_frames;
}

void wl_d3d9_size(int *w, int *h)
{
    *w = (int)g_width;
    *h = (int)g_height;
}

void wl_frame_presented(void)
{
    InterlockedIncrement(&g_frames);
}

void wl_frame_size(int w, int h)
{
    InterlockedExchange(&g_width, (LONG)w);
    InterlockedExchange(&g_height, (LONG)h);
}

static void remember_size(const D3DPRESENT_PARAMETERS *pp)
{
    if (!pp)
        return;
    InterlockedExchange(&g_width, (LONG)pp->BackBufferWidth);
    InterlockedExchange(&g_height, (LONG)pp->BackBufferHeight);
}

int wl_d3d9_shot(const char *path, DWORD timeoutMs)
{
    if (!g_shotDone)
        g_shotDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    strncpy(g_shotPath, path, sizeof g_shotPath - 1);
    g_shotOk = 0;
    ResetEvent(g_shotDone);
    InterlockedExchange(&g_shotPending, 1);
    if (WaitForSingleObject(g_shotDone, timeoutMs) != WAIT_OBJECT_0)
    {
        InterlockedExchange(&g_shotPending, 0);
        return 0;
    }
    return g_shotOk;
}

const char *wl_shot_pending(void)
{
    return g_shotPending ? g_shotPath : NULL;
}

void wl_shot_done(int ok)
{
    g_shotOk = ok;
    InterlockedExchange(&g_shotPending, 0);
    SetEvent(g_shotDone);
}

// ------------------------------------------------------------------------------
// Device methods
// ------------------------------------------------------------------------------

static HRESULT(STDMETHODCALLTYPE *oPresent)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
void wl_d3d9_take_pending_shot(IDirect3DDevice9 *dev)
{
    if (!g_shotPending)
        return;
    wl_shot_done(save_back_buffer(dev, g_shotPath));
}

// ------------------------------------------------------------------------------
// The picture copied into the window by GDI (display.h, PRESENT = GDI): the back buffer read
// back, drawn with StretchDIBits where the picture goes; the game's Present then goes to a hidden
// window, so the game's window never gets a Direct3D blt (which some PCs never show)
// ------------------------------------------------------------------------------

static IDirect3DDevice9 *g_gdiDev; // the device the surfaces below belong to
static IDirect3DSurface9 *g_gdiSys, *g_gdiResolved;
static D3DSURFACE_DESC g_gdiDesc;
static unsigned char *g_gdiPixels;
static HWND g_gdiHidden;

static void gdi_release(void)
{
    if (g_gdiSys)
        g_gdiSys->Release();
    if (g_gdiResolved)
        g_gdiResolved->Release();
    g_gdiSys = g_gdiResolved = NULL;
}

static HWND gdi_hidden_window(void)
{
    if (!g_gdiHidden)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"windowsloader present";
        RegisterClassW(&wc);
        g_gdiHidden = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, NULL, NULL, wc.hInstance, NULL);
    }
    return g_gdiHidden;
}

// 1 when the picture was drawn into the window
static int gdi_copy(IDirect3DDevice9 *dev)
{
    IDirect3DSurface9 *bb = NULL;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        return 0;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    int bits = d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A2R10G10B10 ? 32
               : d.Format == D3DFMT_R5G6B5 || d.Format == D3DFMT_X1R5G5B5                                   ? 16
                                                                                                             : 0;
    if (!bits)
    {
        bb->Release();
        return 0;
    }
    if (!g_gdiSys || dev != g_gdiDev || g_gdiDesc.Width != d.Width || g_gdiDesc.Height != d.Height || g_gdiDesc.Format != d.Format ||
        g_gdiDesc.MultiSampleType != d.MultiSampleType)
    {
        gdi_release();
        g_gdiDev = dev;
        g_gdiDesc = d;
        if (d.MultiSampleType != D3DMULTISAMPLE_NONE)
            dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &g_gdiResolved, NULL);
        dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &g_gdiSys, NULL);
        free(g_gdiPixels);
        g_gdiPixels = d.Format == D3DFMT_A2R10G10B10 ? (unsigned char *)malloc((size_t)d.Width * d.Height * 4) : NULL;
    }
    IDirect3DSurface9 *src = bb;
    if (g_gdiResolved && SUCCEEDED(dev->StretchRect(bb, NULL, g_gdiResolved, NULL, D3DTEXF_NONE)))
        src = g_gdiResolved;
    D3DLOCKED_RECT lr;
    int ok = g_gdiSys && SUCCEEDED(dev->GetRenderTargetData(src, g_gdiSys)) && SUCCEEDED(g_gdiSys->LockRect(&lr, NULL, D3DLOCK_READONLY));
    bb->Release();
    if (!ok)
        return 0;

    const void *pixels = lr.pBits;
    int pitch = lr.Pitch;
    if (d.Format == D3DFMT_A2R10G10B10 && g_gdiPixels)
    {
        for (UINT y = 0; y < d.Height; y++)
        {
            const DWORD *row = (const DWORD *)((const unsigned char *)lr.pBits + (size_t)y * lr.Pitch);
            DWORD *out = (DWORD *)(g_gdiPixels + (size_t)y * d.Width * 4);
            for (UINT x = 0; x < d.Width; x++)
                out[x] = (row[x] >> 2 & 0xFF) | (row[x] >> 12 & 0xFF) << 8 | (row[x] >> 22 & 0xFF) << 16;
        }
        pixels = g_gdiPixels;
        pitch = (int)d.Width * 4;
    }
    wl_display_gdi_draw(pixels, pitch, (int)d.Width, (int)d.Height, bits, d.Format == D3DFMT_R5G6B5);
    g_gdiSys->UnlockRect();
    return 1;
}

// The frame out: by GDI into the window and the Present to the hidden window, or the game's
// Present at the picture's place. The F10 menu presents through it too.
static HRESULT STDMETHODCALLTYPE present_out(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    if (wl_display_gdi())
    {
        gdi_copy(dev);
        HRESULT hr = oPresent(dev, NULL, NULL, gdi_hidden_window(), NULL);
        // The hidden window does not wait for the screen: one composition per interval asked
        int waits = g_interval == D3DPRESENT_INTERVAL_IMMEDIATE ? 0 : g_interval == D3DPRESENT_INTERVAL_TWO ? 2
                    : g_interval == D3DPRESENT_INTERVAL_THREE   ? 3 : g_interval == D3DPRESENT_INTERVAL_FOUR ? 4 : 1;
        for (int i = 0; i < waits; i++)
            DwmFlush();
        return hr == S_PRESENT_OCCLUDED ? D3D_OK : hr; // the hidden window: the game keeps drawing
    }
    return oPresent(dev, src, dst, wnd, dirty);
}

static HRESULT STDMETHODCALLTYPE hPresent(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    static unsigned frames, lastFrames;
    static ULONGLONG lastTick;
    wl_menu_present(dev, present_out); // F10: the menu runs here until it is closed
    wl_d3d9_take_pending_shot(dev);
    const RECT *place = wl_display_dest(); // the picture's place for the aspect ratio and the mode
    HRESULT hr = present_out(dev, src, dst ? dst : place, wnd, dirty);
    InterlockedIncrement(&g_frames);

    // A lost device (screen locked, remote desktop minimized) draws nothing until the game resets it
    static HRESULT lastHr = S_OK;
    if (hr != lastHr)
    {
        wl_log("Present -> 0x%08lx%s", hr, hr == D3DERR_DEVICELOST ? " (device lost)" : hr == S_OK ? " (drawing again)" : "");
        lastHr = hr;
    }

    ULONGLONG now = GetTickCount64();
    if (!frames++)
    {
        lastTick = now;
        wl_log("Present: first frame (hr=0x%08lx), source %s, destination %s, window %p", hr, src ? "given" : "all",
               dst ? "given" : place ? "placed" : "all", wnd);
    }
    else if (now - lastTick >= 10000)
    {
        wl_log("Present: %u frames, %.1f fps", frames, (frames - lastFrames) * 1000.0 / (double)(now - lastTick));
        lastFrames = frames;
        lastTick = now;
    }
    return hr;
}

// The window set for the display mode, the picture placed in it (display.cpp)
static void after_device(const D3DPRESENT_PARAMETERS *pp, HWND focus, int forced)
{
    remember_size(pp);
    if (pp)
        wl_display_device(pp->hDeviceWindow ? pp->hDeviceWindow : focus, pp);
}

static HRESULT(STDMETHODCALLTYPE *oReset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static HRESULT STDMETHODCALLTYPE hReset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    log_pp("Reset", pp);
    wl_menu_device_reset();
    gdi_release(); // its render target, in the default pool, would make the Reset fail
    int forced = make_windowed(pp);
    if (forced)
        wl_log("  kept in a window");
    wl_display_prepare(pp);
    HRESULT hr = oReset(dev, pp);
    if (SUCCEEDED(hr))
    {
        D3DDEVICE_CREATION_PARAMETERS cp = {};
        dev->GetCreationParameters(&cp);
        after_device(pp, cp.hFocusWindow, forced);
    }
    wl_log("Reset -> 0x%08lx", hr);
    return hr;
}

static void hook_device(IDirect3DDevice9 *dev)
{
    static int done;
    if (done || !dev)
        return;
    done = 1;
    void **vt = vtable_of(dev);
    wl_hook_code(vt[SLOT_DEVICE_PRESENT], (void *)hPresent, (void **)&oPresent, "IDirect3DDevice9::Present");
    wl_hook_code(vt[SLOT_DEVICE_RESET], (void *)hReset, (void **)&oReset, "IDirect3DDevice9::Reset");
}

// ------------------------------------------------------------------------------
// IDirect3D9 / IDirect3D9Ex
// ------------------------------------------------------------------------------

static HRESULT(STDMETHODCALLTYPE *oCreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT STDMETHODCALLTYPE hCreateDevice(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS *pp,
                                               IDirect3DDevice9 **out)
{
    char where[128];
    wl_log("CreateDevice adapter=%u type=%d focus=%p behavior=0x%lx [%s]", adapter, type, focus, behavior, wl_where(WL_CALLER, where, sizeof where));
    log_pp("  asked", pp);
    int forced = make_windowed(pp);
    if (forced)
        wl_log("  kept in a window");
    wl_display_prepare(pp);
    HRESULT hr = oCreateDevice(d3d, adapter, type, focus, behavior, pp, out);
    if (SUCCEEDED(hr))
        after_device(pp, focus, forced);
    wl_log("CreateDevice -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && out)
        hook_device(*out);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreateDeviceEx)(IDirect3D9Ex *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *,
                                                   IDirect3DDevice9Ex **);
static HRESULT STDMETHODCALLTYPE hCreateDeviceEx(IDirect3D9Ex *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS *pp,
                                                 D3DDISPLAYMODEEX *mode, IDirect3DDevice9Ex **out)
{
    char where[128];
    wl_log("CreateDeviceEx adapter=%u type=%d focus=%p behavior=0x%lx [%s]", adapter, type, focus, behavior, wl_where(WL_CALLER, where, sizeof where));
    log_pp("  asked", pp);
    int forced = make_windowed(pp);
    if (forced)
    {
        mode = NULL; // a windowed device takes no display mode
        wl_log("  kept in a window");
    }
    wl_display_prepare(pp);
    HRESULT hr = oCreateDeviceEx(d3d, adapter, type, focus, behavior, pp, mode, out);
    if (SUCCEEDED(hr))
        after_device(pp, focus, forced);
    wl_log("CreateDeviceEx -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && out)
        hook_device(*out);
    return hr;
}

static IDirect3D9 *(WINAPI *oDirect3DCreate9)(UINT);
static IDirect3D9 *WINAPI hDirect3DCreate9(UINT sdk)
{
    static int hooked;
    wl_busy_enter();
    IDirect3D9 *d3d = oDirect3DCreate9(sdk);
    wl_busy_leave();
    char where[128];
    wl_log("Direct3DCreate9(%u) -> %p [%s]", sdk, d3d, wl_where(WL_CALLER, where, sizeof where));
    if (d3d && !hooked)
    {
        hooked = 1;
        wl_hook_code(vtable_of(d3d)[SLOT_D3D9_CREATEDEVICE], (void *)hCreateDevice, (void **)&oCreateDevice, "IDirect3D9::CreateDevice");
    }
    return d3d;
}

static HRESULT(WINAPI *oDirect3DCreate9Ex)(UINT, IDirect3D9Ex **);
static HRESULT WINAPI hDirect3DCreate9Ex(UINT sdk, IDirect3D9Ex **out)
{
    static int hooked;
    wl_busy_enter();
    HRESULT hr = oDirect3DCreate9Ex(sdk, out);
    wl_busy_leave();
    char where[128];
    wl_log("Direct3DCreate9Ex(%u) -> 0x%08lx [%s]", sdk, hr, wl_where(WL_CALLER, where, sizeof where));
    if (SUCCEEDED(hr) && out && *out && !hooked)
    {
        hooked = 1;
        wl_hook_code(vtable_of(*out)[SLOT_D3D9EX_CREATEDEVICEEX], (void *)hCreateDeviceEx, (void **)&oCreateDeviceEx, "IDirect3D9Ex::CreateDeviceEx");
    }
    return hr;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"d3d9.dll", Direct3DCreate9),
    WL_HOOK(L"d3d9.dll", Direct3DCreate9Ex),
};

void wl_d3d9_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}

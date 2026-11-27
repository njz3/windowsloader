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
#include "framestats.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"
#include "vscreen.h"

// vtable slots (d3d9.h declaration order)
enum
{
    SLOT_D3D9_CREATEDEVICE = 16,
    SLOT_D3D9EX_CREATEDEVICEEX = 20,
    SLOT_DEVICE_RESET = 16,
    SLOT_DEVICE_CREATETEXTURE = 23,
    SLOT_DEVICE_CREATERENDERTARGET = 28,
    SLOT_DEVICE_CREATEDEPTHSTENCILSURFACE = 29,
    SLOT_DEVICE_PRESENT = 17,
    SLOT_DEVICE_SETTEXTURE = 65,
    SLOT_DEVICE_SETSCISSORRECT = 75,
    SLOT_DEVICE_DRAWPRIMITIVEUP = 83,
    SLOT_DEVICE_DRAWINDEXEDPRIMITIVEUP = 84,
    SLOT_DEVICE_SETVERTEXDECLARATION = 87,
    SLOT_DEVICE_SETFVF = 89,
    SLOT_DEVICE_CREATEPIXELSHADER = 106,
    SLOT_DEVICE9EX_PRESENTEX = 121,
    SLOT_DEVICE9EX_RESETEX = 132,
    SLOT_SWAPCHAIN_PRESENT = 3,
};

static void log_pp(const char *what, const D3DPRESENT_PARAMETERS *pp)
{
    if (!pp)
        return;
    wl_log("%s: %ux%u format=%d count=%u msaa=%d/%lu swap=%d window=%p windowed=%d depth=%d/%d flags=0x%lx refresh=%u interval=0x%x", what,
           pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount, pp->MultiSampleType, pp->MultiSampleQuality,
           pp->SwapEffect, pp->hDeviceWindow, pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->Flags,
           pp->FullScreen_RefreshRateInHz, pp->PresentationInterval);
}

// The game's presentation interval: its frames' pace once presented to the hidden window (GDI)
static UINT g_interval = D3DPRESENT_INTERVAL_ONE;

// A game told a smaller screen (VIRTUAL_DISPLAY) making or resetting its device larger, at its
// window's size or the real screen's: the screen told (its full-screen mode one of Direct3D's,
// vscreen.cpp). Crazy Speed's Ogre resets at its window's client area (its plug-in's own
// GetClientRect, past the game's), Dirty Drivin' asks the screen's mode (its -r 1360x768 aside):
// both drew their 1360x768 menus in a corner of 1920x1080. Only when larger both ways: a window
// narrower than the screen told is kept at its size, as a smaller one is (Crazy Speed on a 1280x960
// screen, reset at 1280x960: made 1360x768, its 3D view stopped 80 pixels before the right edge;
// at 640x480 kept, whole)
static void told_size(D3DPRESENT_PARAMETERS *pp)
{
    int vw, vh;
    if (pp && wl_vscreen_size(&vw, &vh) && (int)pp->BackBufferWidth > vw && (int)pp->BackBufferHeight > vh)
    {
        wl_log("  %ux%u, larger than the screen the game is told: %dx%d", pp->BackBufferWidth, pp->BackBufferHeight, vw, vh);
        pp->BackBufferWidth = (UINT)vw;
        pp->BackBufferHeight = (UINT)vh;
    }
    // A window's back buffer at its client area's size (0x0), which in a full-screen window is the
    // real screen's, not the one told: Dirty Drivin' reset so (BORDERLESS, 1920x1080 told 1360x768)
    // and its picture stood still on its loading text
    else if (pp && pp->Windowed && (!pp->BackBufferWidth || !pp->BackBufferHeight) && wl_vscreen_size(&vw, &vh))
    {
        wl_log("  %ux%u (its window's size): the screen the game is told, %dx%d", pp->BackBufferWidth, pp->BackBufferHeight, vw, vh);
        pp->BackBufferWidth = (UINT)vw;
        pp->BackBufferHeight = (UINT)vh;
    }
}

// Full screen -> window, with the presentation settings a window accepts (1 when made so); the
// interval kept, IMMEDIATE in a window with WINDOW_VSYNC false
static int make_windowed(D3DPRESENT_PARAMETERS *pp)
{
    if (pp)
        g_interval = pp->PresentationInterval;
    // [Display] WINDOW_VSYNC false: in a window, no wait for the screen (the compositor shows whole frames)
    if (pp && !wl_display_window_vsync() && (pp->Windowed || g_wl.windowed) && pp->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE)
    {
        wl_log("  interval 0x%x: IMMEDIATE in the window (WINDOW_VSYNC false)", pp->PresentationInterval);
        pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }
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
    wl_busy_enter(); // windowsloader's own file, where the agent asked: not the game's (no write folder)
    wl_files_allow_writes(1);
    FILE *f = fopen(path, "wb");
    wl_files_allow_writes(0);
    wl_busy_leave();
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
    // 8 bits per channel, or A2R10G10B10 (Chase H.Q. 2) and the 16-bit formats (Valve Limit R's
    // R5G6B5) brought to 8
    int tenBits = d.Format == D3DFMT_A2R10G10B10;
    int sixteen = d.Format == D3DFMT_R5G6B5 ? 6 : d.Format == D3DFMT_X1R5G5B5 || d.Format == D3DFMT_A1R5G5B5 ? 5 : 0;
    if (d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8 || tenBits || sixteen)
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
            unsigned char *converted = tenBits || sixteen ? (unsigned char *)malloc((size_t)d.Width * d.Height * 4) : NULL;
            if (converted && sixteen)
            {
                for (UINT y = 0; y < d.Height; y++)
                {
                    const WORD *row = (const WORD *)(pixels + (size_t)y * lr.Pitch);
                    unsigned char *out = converted + (size_t)y * d.Width * 4;
                    for (UINT x = 0; x < d.Width; x++)
                    {
                        unsigned v = row[x];
                        unsigned r = sixteen == 6 ? v >> 11 : v >> 10 & 0x1F, g = sixteen == 6 ? v >> 5 & 0x3F : v >> 5 & 0x1F, b = v & 0x1F;
                        out[x * 4] = (unsigned char)(b << 3 | b >> 2);
                        out[x * 4 + 1] = (unsigned char)(sixteen == 6 ? g << 2 | g >> 4 : g << 3 | g >> 2);
                        out[x * 4 + 2] = (unsigned char)(r << 3 | r >> 2);
                        out[x * 4 + 3] = 0;
                    }
                }
                pixels = converted;
                pitch = (int)d.Width * 4;
            }
            else if (converted)
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
            if ((!tenBits && !sixteen) || converted)
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

static volatile LONG g_shotWindow; // the picture asked is the whole window's (wl_window_shot)

int wl_window_shot(const char *path, DWORD timeoutMs)
{
    InterlockedExchange(&g_shotWindow, 1);
    int ok = wl_d3d9_shot(path, timeoutMs);
    InterlockedExchange(&g_shotWindow, 0);
    return ok;
}

int wl_shot_window(void)
{
    return g_shotWindow;
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
// Present at the picture's place. The settings menu presents through it too.
static HRESULT STDMETHODCALLTYPE present_out(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    if (wl_display_gdi())
    {
        if (wl_display_gdi_due(g_interval == D3DPRESENT_INTERVAL_IMMEDIATE)) // Wasteland Racers' (display.cpp)
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

// The same for IDirect3DDevice9Ex::PresentEx, its flags kept when the game's Present shows the picture
static HRESULT(STDMETHODCALLTYPE *oPresentEx)(IDirect3DDevice9Ex *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
static HRESULT present_out_ex(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty, DWORD flags)
{
    if (wl_display_gdi())
        return present_out(dev, src, dst, wnd, dirty);
    return oPresentEx((IDirect3DDevice9Ex *)dev, src, dst, wnd, dirty, flags);
}

// The game's device; the ones Windows makes for itself in the game's process (DirectShow's video
// renderer, quartz.dll) are left alone: same vtable, so their calls come here too. Only compared,
// never called through: the game may have released it (a device made later at the same address by
// Windows or as a helper clears it, hCreateDevice)
static IDirect3DDevice9 *g_gameDev;

// Stage 0's texture on the game's device (2D scaling: inset_texels). No reference of windowsloader's:
// the device holds one on the texture bound, and one of ours would keep the game's device alive
// after its last Release (DXUT's games stop on that). Forgotten at a Reset and when the game makes
// a device again
static IDirect3DBaseTexture9 *g_tex0;

static void keep_tex0(IDirect3DBaseTexture9 *tex)
{
    g_tex0 = tex;
}

static int made_by_windows(const void *caller)
{
    HMODULE mod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)caller, &mod);
    return mod != g_wlModule && wl_is_system_caller(caller);
}

static volatile LONGLONG g_presentBegan; // QueryPerformanceCounter as the game's last Present began

LONGLONG wl_d3d9_present_began(void)
{
    return g_presentBegan;
}

static volatile LONG g_presentThread; // the thread in game_present: its swap chain's Present is d3d9's own
static volatile LONG g_presentExThread; // the thread in PresentEx's original: a Present from there is d3d9's own

// The game's frame, from IDirect3DDevice9::Present (ex 0) or IDirect3DDevice9Ex::PresentEx (ex 1,
// its flags kept)
static HRESULT game_present(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty, int ex, DWORD flags)
{
    static unsigned frames, lastFrames;
    static ULONGLONG lastTick;
    struct PresentThread
    {
        PresentThread()
        {
            InterlockedExchange(&g_presentThread, (LONG)GetCurrentThreadId());
        }
        ~PresentThread()
        {
            InterlockedExchange(&g_presentThread, 0);
        }
    } inPresent;
    wl_menu_present(dev, present_out); // Tab: the menu runs here until it is closed
    wl_d3d9_take_pending_shot(dev);
    const RECT *place = wl_display_dest(); // the picture's place for the aspect ratio and the mode
    LARGE_INTEGER began;
    QueryPerformanceCounter(&began);
    g_presentBegan = began.QuadPart;
    wl_framestats_present_begin();
    HRESULT hr;
    if (ex)
    {
        InterlockedExchange(&g_presentExThread, (LONG)GetCurrentThreadId());
        hr = present_out_ex(dev, src || dst ? src : wl_display_source(), dst ? dst : place, wnd, dirty, flags);
        InterlockedExchange(&g_presentExThread, 0);
    }
    else
        hr = present_out(dev, src || dst ? src : wl_display_source(), dst ? dst : place, wnd, dirty);
    wl_framestats_present_end();
    // PresentEx with D3DPRESENT_DONOTWAIT, the GPU still busy: nothing presented, the game asks again
    if (hr == D3DERR_WASSTILLDRAWING)
        return hr;
    InterlockedIncrement(&g_frames);

    // A lost device (screen locked, remote desktop minimized) draws nothing until the game resets it
    static HRESULT lastHr = S_OK;
    if (hr != lastHr)
    {
        wl_log("Present -> 0x%08lx%s", hr,
               hr == D3DERR_DEVICELOST                           ? " (device lost)"
               : hr == S_PRESENT_OCCLUDED                         ? " (the window hidden: the game told S_OK)"
               : hr == S_PRESENT_MODE_CHANGED                     ? " (the desktop's mode changed: the game told S_OK)"
               : hr == S_OK                                       ? " (drawing again)"
                                                                  : "");
        lastHr = hr;
    }
    // The window hidden (another one over it, the cabinet's front end still in front as the game
    // starts) or the desktop's mode changed: success codes of Windows 8 and later's d3d9 that a
    // game of before takes for a failure (Battle Gear 4 Tuned and Pro wrote their settings and
    // quit, 3 s after the start); the frame is just not seen
    if (hr == S_PRESENT_OCCLUDED || hr == S_PRESENT_MODE_CHANGED)
        hr = S_OK;

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

static HRESULT STDMETHODCALLTYPE hPresent(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty)
{
    if (dev != g_gameDev || g_presentExThread == (LONG)GetCurrentThreadId())
        return oPresent(dev, src, dst, wnd, dirty);
    return game_present(dev, src, dst, wnd, dirty, 0, 0);
}

// A Direct3D 9Ex game's PresentEx goes the way of Present; d3d9's own call from the device's
// Present (or from the menu's) is left as it is
static HRESULT STDMETHODCALLTYPE hPresentEx(IDirect3DDevice9Ex *dev, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty, DWORD flags)
{
    if ((IDirect3DDevice9 *)dev != g_gameDev || g_presentThread == (LONG)GetCurrentThreadId())
        return oPresentEx(dev, src, dst, wnd, dirty, flags);
    static int told;
    if (!told++)
        wl_log("IDirect3DDevice9Ex::PresentEx: the game presents by it (flags 0x%lx)", flags);
    return game_present(dev, src, dst, wnd, dirty, 1, flags);
}

// A game presenting through its device's implicit swap chain (IDirect3DSwapChain9::Present: Storm
// Racer G) goes the way of the device's Present; d3d9's own call from the device's Present, and
// the other swap chains, are left as they are. The swap chain's device is asked (the one presenting,
// alive), compared with the game's
static HRESULT(STDMETHODCALLTYPE *oSwapChainPresent)(IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
static HRESULT STDMETHODCALLTYPE hSwapChainPresent(IDirect3DSwapChain9 *sc, const RECT *src, const RECT *dst, HWND wnd, const RGNDATA *dirty,
                                                   DWORD flags)
{
    if (g_presentThread == (LONG)GetCurrentThreadId() || !g_gameDev)
        return oSwapChainPresent(sc, src, dst, wnd, dirty, flags);
    IDirect3DDevice9 *dev = NULL;
    if (FAILED(sc->GetDevice(&dev)) || !dev)
        return oSwapChainPresent(sc, src, dst, wnd, dirty, flags);
    IDirect3DSwapChain9 *implicit = NULL;
    int ours = dev == g_gameDev && SUCCEEDED(dev->GetSwapChain(0, &implicit)) && implicit == sc;
    if (implicit)
        implicit->Release();
    dev->Release(); // the swap chain keeps it
    if (!ours)
        return oSwapChainPresent(sc, src, dst, wnd, dirty, flags);
    static int told;
    if (!told++)
        wl_log("IDirect3DSwapChain9::Present: the game presents through its device's swap chain");
    return game_present(dev, src, dst, wnd, dirty, 0, 0);
}

// The window set for the display mode, the picture placed in it (display.cpp)
static void after_device(const D3DPRESENT_PARAMETERS *pp, HWND focus, int forced)
{
    remember_size(pp);
    if (pp)
        wl_display_device(pp->hDeviceWindow ? pp->hDeviceWindow : focus, pp);
}

// A windowed device of 0x0 takes its window's client area: the one the game asked for when Windows
// made the window smaller (larger than the screen: Battle Gear 4's 800x600 shrunk to 800x231 on a
// 1920x240 screen, the game then crashed reading what it had not made for that size)
static void asked_size(D3DPRESENT_PARAMETERS *pp, HWND focus)
{
    if (!pp || !pp->Windowed || (pp->BackBufferWidth && pp->BackBufferHeight))
        return;
    HWND wnd = pp->hDeviceWindow ? pp->hDeviceWindow : focus;
    int w, h;
    RECT r;
    if (!wl_window_asked_client(wnd, &w, &h) || !GetClientRect(wnd, &r) || (r.right >= w && r.bottom >= h))
        return;
    wl_log("  back buffer at the window's asked client area, %dx%d (Windows made the window smaller: %ldx%ld)", w, h, r.right, r.bottom);
    if (!pp->BackBufferWidth)
        pp->BackBufferWidth = (UINT)w;
    if (!pp->BackBufferHeight)
        pp->BackBufferHeight = (UINT)h;
}

// A game whose Reset with its device's own settings, the device not lost, is not made (by its
// module): Road Fighters 3D's at the end of a game, after which its effects could
// not be made again (D3DXCreateEffect: E_OUTOFMEMORY) and it crashed (jgt.dll+0x1d3804)
static int g_keepSameReset;

void wl_d3d9_keep_same_reset(void)
{
    g_keepSameReset = 1;
}

static HRESULT(STDMETHODCALLTYPE *oReset)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static HRESULT(STDMETHODCALLTYPE *oResetEx)(IDirect3DDevice9Ex *, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *);
static volatile LONG g_resetThread; // the thread in Reset's or ResetEx's original: the other one from there is d3d9's own

// The game's device reset, by IDirect3DDevice9::Reset (ex 0) or IDirect3DDevice9Ex::ResetEx (ex 1,
// with its full-screen mode)
static HRESULT game_reset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp, int ex, D3DDISPLAYMODEEX *mode)
{
    log_pp(ex ? "ResetEx" : "Reset", pp);
    // Settings that can only fail (no swap effect), the back buffer's size unchanged: nothing to
    // reset, the device's settings given back and the Reset told done. The DirectX SDK framework's
    // Reset after its window was moved or sized (Battle Gear 4 Tuned, +0xe3f0: its copy of the
    // settings left empty, its device made from others, and its resources still there, so that
    // even the device's own settings fail): the game's "the device could not be reset", then a
    // crash. A cabinet in full screen never sees it
    if (pp && (pp->SwapEffect < D3DSWAPEFFECT_DISCARD || pp->SwapEffect > 5 /* D3DSWAPEFFECT_FLIPEX */))
    {
        IDirect3DSwapChain9 *sc = NULL;
        D3DPRESENT_PARAMETERS cur;
        int same = SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc && SUCCEEDED(sc->GetPresentParameters(&cur)) &&
                   (!pp->BackBufferWidth || pp->BackBufferWidth == cur.BackBufferWidth) &&
                   (!pp->BackBufferHeight || pp->BackBufferHeight == cur.BackBufferHeight);
        if (sc)
            sc->Release();
        if (same)
        {
            *pp = cur;
            wl_log("  no swap effect, the same size: not reset, the device's settings given back");
            return D3D_OK;
        }
    }
    if (g_keepSameReset && pp && dev->TestCooperativeLevel() == D3D_OK)
    {
        IDirect3DSwapChain9 *sc = NULL;
        D3DPRESENT_PARAMETERS cur;
        int same = SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc && SUCCEEDED(sc->GetPresentParameters(&cur)) &&
                   (!pp->BackBufferWidth || pp->BackBufferWidth == cur.BackBufferWidth) &&
                   (!pp->BackBufferHeight || pp->BackBufferHeight == cur.BackBufferHeight) &&
                   pp->BackBufferFormat == cur.BackBufferFormat && pp->MultiSampleType == cur.MultiSampleType &&
                   pp->Windowed == cur.Windowed && pp->EnableAutoDepthStencil == cur.EnableAutoDepthStencil &&
                   (!pp->EnableAutoDepthStencil || pp->AutoDepthStencilFormat == cur.AutoDepthStencilFormat);
        if (sc)
            sc->Release();
        if (same)
        {
            wl_log("  the device's own settings, the device not lost: not reset (its resources made again on the same device)");
            return D3D_OK;
        }
    }
    told_size(pp);
    wl_menu_device_reset();
    gdi_release(); // its render target, in the default pool, would make the Reset fail
    keep_tex0(NULL); // the same for the texture kept
    int forced = make_windowed(pp);
    {
        D3DDEVICE_CREATION_PARAMETERS cp = {};
        dev->GetCreationParameters(&cp);
        asked_size(pp, cp.hFocusWindow);
    }
    if (forced)
        wl_log("  kept in a window");
    wl_display_prepare(pp);
    InterlockedExchange(&g_resetThread, (LONG)GetCurrentThreadId());
    HRESULT hr = ex ? oResetEx((IDirect3DDevice9Ex *)dev, pp, pp && pp->Windowed ? NULL : mode) : oReset(dev, pp); // a window takes no mode
    InterlockedExchange(&g_resetThread, 0);
    if (SUCCEEDED(hr))
    {
        D3DDEVICE_CREATION_PARAMETERS cp = {};
        dev->GetCreationParameters(&cp);
        after_device(pp, cp.hFocusWindow, forced);
    }
    wl_log("%s -> 0x%08lx", ex ? "ResetEx" : "Reset", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hReset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    if (dev != g_gameDev || g_resetThread == (LONG)GetCurrentThreadId())
        return oReset(dev, pp);
    return game_reset(dev, pp, 0, NULL);
}

static HRESULT STDMETHODCALLTYPE hResetEx(IDirect3DDevice9Ex *dev, D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *mode)
{
    if ((IDirect3DDevice9 *)dev != g_gameDev || g_resetThread == (LONG)GetCurrentThreadId())
        return oResetEx(dev, pp, mode);
    return game_reset(dev, pp, 1, mode);
}

// ------------------------------------------------------------------------------
// 2D placed for a smaller screen (wl_d3d9_scale_2d): the pretransformed vertices (XYZRHW, drawn
// from memory) and the scissor rectangles within that screen are scaled to the back buffer. What
// lies beyond it was placed for the back buffer already and is left as it is.
// ------------------------------------------------------------------------------

static int g_2dW, g_2dH;
static DWORD g_fvf;

void wl_d3d9_scale_2d(int w, int h)
{
    g_2dW = w;
    g_2dH = h;
}

static int scaling_2d(float *sx, float *sy)
{
    LONG bw = g_width, bh = g_height;
    if (g_2dW <= 0 || g_2dH <= 0 || (bw == g_2dW && bh == g_2dH) || bw <= 0 || bh <= 0)
        return 0;
    *sx = (float)bw / (float)g_2dW;
    *sy = (float)bh / (float)g_2dH;
    return 1;
}

static UINT vertices_of(D3DPRIMITIVETYPE type, UINT prims)
{
    switch (type)
    {
    case D3DPT_POINTLIST:
        return prims;
    case D3DPT_LINELIST:
        return prims * 2;
    case D3DPT_LINESTRIP:
        return prims + 1;
    case D3DPT_TRIANGLELIST:
        return prims * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return prims + 2;
    default:
        return 0;
    }
}

// A sprite drawn larger than its texels samples half a texel beyond its edges, where its texture
// wraps or its neighbour in the atlas starts: lines between the tiles. Its texture coordinates are
// drawn in by half a texel, per quad (6 vertices of a list, or a whole strip or fan).
static void inset_texels(unsigned char *verts, UINT count, UINT stride, D3DPRIMITIVETYPE type)
{
    if (!g_tex0 || g_tex0->GetType() != D3DRTYPE_TEXTURE || (g_fvf & D3DFVF_TEXCOUNT_MASK) == 0 || (g_fvf & 0x30000) != 0)
        return;
    D3DSURFACE_DESC d;
    if (FAILED(((IDirect3DTexture9 *)g_tex0)->GetLevelDesc(0, &d)) || !d.Width || !d.Height)
        return;
    UINT uv = 16 + ((g_fvf & D3DFVF_PSIZE) ? 4 : 0) + ((g_fvf & D3DFVF_DIFFUSE) ? 4 : 0) + ((g_fvf & D3DFVF_SPECULAR) ? 4 : 0);
    if (uv + 8 > stride)
        return;
    const float hu = 0.5f / (float)d.Width, hv = 0.5f / (float)d.Height;
    UINT group = type == D3DPT_TRIANGLELIST ? (count % 6 == 0 ? 6 : 3) : type == D3DPT_FORCE_DWORD && count % 4 == 0 ? 4 : count;
    for (UINT first = 0; first + group <= count; first += group)
    {
        float u0 = 1e30f, u1 = -1e30f, v0 = 1e30f, v1 = -1e30f;
        for (UINT i = first; i < first + group; i++)
        {
            const float *t = (const float *)(verts + (size_t)i * stride + uv);
            u0 = t[0] < u0 ? t[0] : u0, u1 = t[0] > u1 ? t[0] : u1;
            v0 = t[1] < v0 ? t[1] : v0, v1 = t[1] > v1 ? t[1] : v1;
        }
        for (UINT i = first; i < first + group; i++)
        {
            float *t = (float *)(verts + (size_t)i * stride + uv);
            if (u1 - u0 > 4 * hu)
                t[0] = t[0] < u0 + hu ? u0 + hu : t[0] > u1 - hu ? u1 - hu : t[0];
            if (v1 - v0 > 4 * hv)
                t[1] = t[1] < v0 + hv ? v0 + hv : t[1] > v1 - hv ? v1 - hv : t[1];
        }
    }
}

// A copy of the vertices with x and y scaled; NULL when they are left as they are
static const void *scaled_2d(const void *verts, UINT count, UINT stride, D3DPRIMITIVETYPE type)
{
    static unsigned char *copy;
    static size_t size;
    float sx, sy;
    if (!verts || !count || stride < 16 || (g_fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW || !scaling_2d(&sx, &sy))
        return NULL;
    float minX = 1e30f, minY = 1e30f;
    for (UINT i = 0; i < count; i++)
    {
        const float *p = (const float *)((const unsigned char *)verts + (size_t)i * stride);
        minX = p[0] < minX ? p[0] : minX;
        minY = p[1] < minY ? p[1] : minY;
    }
    if (minX >= (float)g_2dW || minY >= (float)g_2dH)
        return NULL;
    size_t need = (size_t)count * stride;
    if (need > size)
    {
        unsigned char *bigger = (unsigned char *)realloc(copy, need);
        if (!bigger)
            return NULL;
        copy = bigger;
        size = need;
    }
    memcpy(copy, verts, need);
    for (UINT i = 0; i < count; i++)
    {
        float *p = (float *)(copy + (size_t)i * stride);
        p[0] = (p[0] + 0.5f) * sx - 0.5f;
        p[1] = (p[1] + 0.5f) * sy - 0.5f;
    }
    inset_texels(copy, count, stride, type);
    return copy;
}

static HRESULT(STDMETHODCALLTYPE *oSetFVF)(IDirect3DDevice9 *, DWORD);
static HRESULT STDMETHODCALLTYPE hSetFVF(IDirect3DDevice9 *dev, DWORD fvf)
{
    g_fvf = fvf;
    return oSetFVF(dev, fvf);
}

static HRESULT(STDMETHODCALLTYPE *oSetTexture)(IDirect3DDevice9 *, DWORD, IDirect3DBaseTexture9 *);
static HRESULT STDMETHODCALLTYPE hSetTexture(IDirect3DDevice9 *dev, DWORD stage, IDirect3DBaseTexture9 *tex)
{
    if (stage == 0 && dev == g_gameDev)
        keep_tex0(tex);
    return oSetTexture(dev, stage, tex);
}

static HRESULT(STDMETHODCALLTYPE *oSetVertexDeclaration)(IDirect3DDevice9 *, IDirect3DVertexDeclaration9 *);
static HRESULT STDMETHODCALLTYPE hSetVertexDeclaration(IDirect3DDevice9 *dev, IDirect3DVertexDeclaration9 *decl)
{
    g_fvf = 0; // a declaration in place of the FVF
    return oSetVertexDeclaration(dev, decl);
}

static HRESULT(STDMETHODCALLTYPE *oDrawPrimitiveUP)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
static HRESULT STDMETHODCALLTYPE hDrawPrimitiveUP(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE type, UINT prims, const void *verts, UINT stride)
{
    const void *scaled = scaled_2d(verts, vertices_of(type, prims), stride, type);
    return oDrawPrimitiveUP(dev, type, prims, scaled ? scaled : verts, stride);
}

static HRESULT(STDMETHODCALLTYPE *oDrawIndexedPrimitiveUP)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT,
                                                           const void *, UINT);
static HRESULT STDMETHODCALLTYPE hDrawIndexedPrimitiveUP(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE type, UINT minIndex, UINT count, UINT prims,
                                                         const void *indices, D3DFORMAT format, const void *verts, UINT stride)
{
    const void *scaled = scaled_2d(verts, minIndex + count, stride, D3DPT_FORCE_DWORD); // indexed: one group
    return oDrawIndexedPrimitiveUP(dev, type, minIndex, count, prims, indices, format, scaled ? scaled : verts, stride);
}

static HRESULT(STDMETHODCALLTYPE *oSetScissorRect)(IDirect3DDevice9 *, const RECT *);
static HRESULT STDMETHODCALLTYPE hSetScissorRect(IDirect3DDevice9 *dev, const RECT *r)
{
    float sx, sy;
    if (!r || r->right > g_2dW || r->bottom > g_2dH || !scaling_2d(&sx, &sy))
        return oSetScissorRect(dev, r);
    RECT s = {(LONG)(r->left * sx), (LONG)(r->top * sy), (LONG)(r->right * sx + 0.5f), (LONG)(r->bottom * sy + 0.5f)};
    return oSetScissorRect(dev, &s);
}

// ------------------------------------------------------------------------------
// A game's pixel shader replaced (wl_d3d9_replace_pixel_shader): the one whose instructions are
// those given (its comments, the constant table, left out of the comparison) made from the
// assembly given instead, by the D3DX the game loaded (D3DXAssembleShader)
// ------------------------------------------------------------------------------

static const DWORD *g_psTokens;
static int g_psCount;
static const char *g_psSource, *g_psWhy;

void wl_d3d9_replace_pixel_shader(const DWORD *tokens, int count, const char *source, const char *why)
{
    g_psTokens = tokens;
    g_psCount = count;
    g_psSource = source;
    g_psWhy = why;
}

// The shader's instructions equal the given ones, its comments skipped; else the first that
// differs, in *differs (its place among the instructions), -1 when they are all the same
static int same_shader(const DWORD *code, int *differs)
{
    int n = 0, i = 0;
    *differs = -1;
    for (; i < 4096 && n < g_psCount; i++)
    {
        DWORD t = code[i];
        if (i > 0 && (t & 0xFFFF) == 0xFFFE)
        {
            i += (int)((t >> 16) & 0x7FFF);
            continue;
        }
        if (t != g_psTokens[n++])
        {
            *differs = n - 1;
            return 0;
        }
        if (t == 0x0000FFFF)
            break;
    }
    return n == g_psCount;
}

typedef HRESULT(WINAPI *AssembleFn)(const char *, UINT, const void *, void *, DWORD, IUnknown **, IUnknown **);

// The bytes of an ID3DXBuffer (QueryInterface, AddRef, Release, GetBufferPointer, GetBufferSize)
static const void *buffer_bytes(IUnknown *b, size_t *size)
{
    void **vt = vtable_of(b);
    *size = ((SIZE_T(WINAPI *)(IUnknown *))vt[4])(b);
    return ((void *(WINAPI *)(IUnknown *))vt[3])(b);
}

static HRESULT(STDMETHODCALLTYPE *oCreatePixelShader)(IDirect3DDevice9 *, const DWORD *, IDirect3DPixelShader9 **);
static HRESULT STDMETHODCALLTYPE hCreatePixelShader(IDirect3DDevice9 *dev, const DWORD *code, IDirect3DPixelShader9 **out)
{
    int differs;
    if (!code || !same_shader(code, &differs))
    {
        static int told;
        if (code && told++ < 8)
            wl_log("d3d9: pixel shader 0x%08lx not the one to replace (%s): instruction %d differs", code[0], g_psWhy, differs);
        return oCreatePixelShader(dev, code, out);
    }
    static const wchar_t *dlls[] = {L"d3dx9_40.dll", L"d3dx9_43.dll", L"d3dx9_42.dll", L"d3dx9_41.dll"};
    AssembleFn assemble = NULL;
    for (int i = 0; i < 4 && !assemble; i++)
    {
        HMODULE m = GetModuleHandleW(dlls[i]);
        assemble = m ? (AssembleFn)GetProcAddress(m, "D3DXAssembleShader") : NULL;
    }
    IUnknown *shader = NULL, *errors = NULL;
    HRESULT hr = assemble ? assemble(g_psSource, (UINT)strlen(g_psSource), NULL, NULL, 0, &shader, &errors) : E_NOTIMPL;
    if (FAILED(hr) || !shader)
    {
        size_t n = 0;
        const char *e = errors ? (const char *)buffer_bytes(errors, &n) : "no D3DX";
        wl_log("d3d9: pixel shader (%s) not replaced: 0x%08lx %.*s", g_psWhy, hr, (int)(n ? n : strlen(e)), e);
        if (errors)
            errors->Release();
        return oCreatePixelShader(dev, code, out);
    }
    // The game's comments (its constant table, which D3DX's effects read back from the shader to
    // set its constants) put back after the version token, the assembler's own left out
    size_t n;
    const DWORD *made = (const DWORD *)buffer_bytes(shader, &n);
    size_t madeCount = n / 4, commentEnd = 1, madeStart = 1;
    while (commentEnd < 4096 && (code[commentEnd] & 0xFFFF) == 0xFFFE)
        commentEnd += 1 + ((code[commentEnd] >> 16) & 0x7FFF);
    while (madeStart < madeCount && (made[madeStart] & 0xFFFF) == 0xFFFE)
        madeStart += 1 + ((made[madeStart] >> 16) & 0x7FFF);
    DWORD *joined = (DWORD *)malloc((commentEnd + madeCount) * sizeof(DWORD));
    if (joined)
    {
        memcpy(joined, made, sizeof(DWORD));
        memcpy(joined + 1, code + 1, (commentEnd - 1) * sizeof(DWORD));
        memcpy(joined + commentEnd, made + madeStart, (madeCount - madeStart) * sizeof(DWORD));
    }
    hr = oCreatePixelShader(dev, joined ? joined : made, out);
    free(joined);
    shader->Release();
    if (errors)
        errors->Release();
    wl_log("d3d9: pixel shader replaced (%s) -> 0x%08lx", g_psWhy, hr);
    return SUCCEEDED(hr) ? hr : oCreatePixelShader(dev, code, out);
}

// A resource the game cannot make, logged (its first ones): a game that goes on without it reads
// a null pointer later (Road Fighters 3D after its end of game's Reset)
static int g_createFails;
static void create_failed(const char *what, UINT w, UINT h, int format, DWORD extra, HRESULT hr, void *caller)
{
    char where[128];
    if (g_createFails++ < 16)
        wl_log("%s %ux%u format=%d (0x%lx) -> 0x%08lx [%s]", what, w, h, format, extra, hr, wl_where(caller, where, sizeof where));
}
// A game whose Bink videos decode into its own textures (BinkRegisterFrameBuffers): Bink builds
// each frame from the previous one, read back from them. Made in video memory and dynamic (L8, one
// per plane), their locks gave back zeros on today's drivers: the blocks a frame left as they were
// came out green (Cars' Disney and Pixar logos; its device a Direct3D 9Ex one, where
// managed textures do not exist). Each such texture gets a copy in memory (its private data,
// released with it): the game's locks are given that copy, which keeps the last frame, and the
// unlock sends it to the texture (wl_d3d9_video_textures_managed)
static int g_videoManaged;

void wl_d3d9_video_textures_managed(void)
{
    g_videoManaged = 1;
}

static const GUID WL_SHADOW_GUID = {0x7c1d3a52, 0x5b0e, 0x4d8a, {0x9a, 0x61, 0x2f, 0x43, 0x8e, 0x15, 0xc7, 0x09}};

struct Shadow : IUnknown
{
    LONG refs;
    UINT w, h;
    int locked;
    unsigned char *data;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
    {
        if (id == IID_IUnknown)
        {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override
    {
        return (ULONG)InterlockedIncrement(&refs);
    }
    ULONG STDMETHODCALLTYPE Release(void) override
    {
        LONG n = InterlockedDecrement(&refs);
        if (!n)
        {
            free(data);
            delete this;
        }
        return (ULONG)n;
    }
};

static Shadow *shadow_of(IDirect3DTexture9 *t)
{
    IUnknown *u = NULL;
    DWORD size = sizeof u;
    if (FAILED(t->GetPrivateData(WL_SHADOW_GUID, &u, &size)) || !u)
        return NULL;
    u->Release(); // the texture keeps its own reference
    return (Shadow *)u;
}

static HRESULT(STDMETHODCALLTYPE *oTexLockRect)(IDirect3DTexture9 *, UINT, D3DLOCKED_RECT *, const RECT *, DWORD);
static HRESULT(STDMETHODCALLTYPE *oTexUnlockRect)(IDirect3DTexture9 *, UINT);

static HRESULT STDMETHODCALLTYPE hTexLockRect(IDirect3DTexture9 *t, UINT level, D3DLOCKED_RECT *lr, const RECT *r, DWORD flags)
{
    Shadow *sh = level == 0 && lr ? shadow_of(t) : NULL;
    if (!sh)
        return oTexLockRect(t, level, lr, r, flags);
    sh->locked = 1;
    lr->Pitch = (INT)sh->w;
    lr->pBits = sh->data + (r ? (size_t)r->top * sh->w + r->left : 0);
    return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE hTexUnlockRect(IDirect3DTexture9 *t, UINT level)
{
    Shadow *sh = level == 0 ? shadow_of(t) : NULL;
    if (!sh)
        return oTexUnlockRect(t, level);
    if (!sh->locked)
        return D3DERR_INVALIDCALL;
    sh->locked = 0;
    D3DLOCKED_RECT lr;
    HRESULT hr = oTexLockRect(t, 0, &lr, NULL, D3DLOCK_DISCARD);
    if (FAILED(hr))
        return D3D_OK; // the copy kept: sent at the next unlock
    for (UINT y = 0; y < sh->h; y++)
        memcpy((unsigned char *)lr.pBits + (size_t)y * lr.Pitch, sh->data + (size_t)y * sh->w, sh->w);
    return oTexUnlockRect(t, 0);
}

static void video_shadow(IDirect3DTexture9 *t, UINT w, UINT h)
{
    Shadow *sh = new Shadow();
    sh->refs = 1;
    sh->w = w;
    sh->h = h;
    sh->locked = 0;
    sh->data = (unsigned char *)calloc((size_t)w * h, 1);
    IUnknown *u = sh;
    if (!sh->data || FAILED(t->SetPrivateData(WL_SHADOW_GUID, u, sizeof u, D3DSPD_IUNKNOWN)))
    {
        sh->Release();
        return;
    }
    sh->Release(); // the texture holds it now
    static int hooked, told;
    if (!hooked++)
    {
        void **vt = *(void ***)t;
        wl_hook_code(vt[19], (void *)hTexLockRect, (void **)&oTexLockRect, "IDirect3DTexture9::LockRect (video planes)");
        wl_hook_code(vt[20], (void *)hTexUnlockRect, (void **)&oTexUnlockRect, "IDirect3DTexture9::UnlockRect (video planes)");
    }
    if (told++ < 4)
        wl_log("CreateTexture %ux%u L8 dynamic: a copy in memory for its locks (its video's planes, read back by Bink)", w, h);
}

static HRESULT(STDMETHODCALLTYPE *oCreateTexture)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9 **, HANDLE *);
static HRESULT STDMETHODCALLTYPE hCreateTexture(IDirect3DDevice9 *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                IDirect3DTexture9 **out, HANDLE *shared)
{
    HRESULT hr = oCreateTexture(dev, w, h, levels, usage, format, pool, out, shared);
    if (FAILED(hr))
        create_failed("CreateTexture", w, h, (int)format, usage | (DWORD)pool << 24, hr, WL_CALLER);
    else if (g_videoManaged && format == D3DFMT_L8 && (usage & D3DUSAGE_DYNAMIC) && pool == D3DPOOL_DEFAULT && levels == 1 && out && *out)
        video_shadow(*out, w, h);
    return hr;
}
static HRESULT(STDMETHODCALLTYPE *oCreateRenderTarget)(IDirect3DDevice9 *, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **,
                                                       HANDLE *);
static HRESULT STDMETHODCALLTYPE hCreateRenderTarget(IDirect3DDevice9 *dev, UINT w, UINT h, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms, DWORD quality,
                                                     BOOL lockable, IDirect3DSurface9 **out, HANDLE *shared)
{
    HRESULT hr = oCreateRenderTarget(dev, w, h, format, ms, quality, lockable, out, shared);
    if (FAILED(hr))
        create_failed("CreateRenderTarget", w, h, (int)format, (DWORD)ms, hr, WL_CALLER);
    return hr;
}
static HRESULT(STDMETHODCALLTYPE *oCreateDepthStencilSurface)(IDirect3DDevice9 *, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                                              IDirect3DSurface9 **, HANDLE *);
static HRESULT STDMETHODCALLTYPE hCreateDepthStencilSurface(IDirect3DDevice9 *dev, UINT w, UINT h, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms,
                                                            DWORD quality, BOOL discard, IDirect3DSurface9 **out, HANDLE *shared)
{
    HRESULT hr = oCreateDepthStencilSurface(dev, w, h, format, ms, quality, discard, out, shared);
    if (FAILED(hr))
        create_failed("CreateDepthStencilSurface", w, h, (int)format, (DWORD)ms, hr, WL_CALLER);
    return hr;
}

static void hook_device(IDirect3DDevice9 *dev)
{
    static int done, doneEx;
    if (!dev)
        return;
    // A Direct3D 9Ex device: its PresentEx and ResetEx too (the first such device's vtable, maybe
    // after a plain device's)
    IDirect3DDevice9Ex *ex = NULL;
    if (!doneEx && SUCCEEDED(dev->QueryInterface(__uuidof(IDirect3DDevice9Ex), (void **)&ex)) && ex)
    {
        doneEx = 1;
        void **vtx = vtable_of(ex);
        wl_hook_code(vtx[SLOT_DEVICE9EX_PRESENTEX], (void *)hPresentEx, (void **)&oPresentEx, "IDirect3DDevice9Ex::PresentEx");
        wl_hook_code(vtx[SLOT_DEVICE9EX_RESETEX], (void *)hResetEx, (void **)&oResetEx, "IDirect3DDevice9Ex::ResetEx");
        ex->Release();
    }
    if (done)
        return;
    done = 1;
    void **vt = vtable_of(dev);
    if (g_psTokens)
        wl_hook_code(vt[SLOT_DEVICE_CREATEPIXELSHADER], (void *)hCreatePixelShader, (void **)&oCreatePixelShader, "IDirect3DDevice9::CreatePixelShader");
    wl_hook_code(vt[SLOT_DEVICE_PRESENT], (void *)hPresent, (void **)&oPresent, "IDirect3DDevice9::Present");
    wl_hook_code(vt[SLOT_DEVICE_RESET], (void *)hReset, (void **)&oReset, "IDirect3DDevice9::Reset");
    wl_hook_code(vt[SLOT_DEVICE_CREATETEXTURE], (void *)hCreateTexture, (void **)&oCreateTexture, "IDirect3DDevice9::CreateTexture");
    wl_hook_code(vt[SLOT_DEVICE_CREATERENDERTARGET], (void *)hCreateRenderTarget, (void **)&oCreateRenderTarget, "IDirect3DDevice9::CreateRenderTarget");
    wl_hook_code(vt[SLOT_DEVICE_CREATEDEPTHSTENCILSURFACE], (void *)hCreateDepthStencilSurface, (void **)&oCreateDepthStencilSurface,
                 "IDirect3DDevice9::CreateDepthStencilSurface");
    if (g_2dW > 0)
    {
        wl_hook_code(vt[SLOT_DEVICE_SETFVF], (void *)hSetFVF, (void **)&oSetFVF, "IDirect3DDevice9::SetFVF");
        wl_hook_code(vt[SLOT_DEVICE_SETTEXTURE], (void *)hSetTexture, (void **)&oSetTexture, "IDirect3DDevice9::SetTexture");
        wl_hook_code(vt[SLOT_DEVICE_SETVERTEXDECLARATION], (void *)hSetVertexDeclaration, (void **)&oSetVertexDeclaration,
                     "IDirect3DDevice9::SetVertexDeclaration");
        wl_hook_code(vt[SLOT_DEVICE_DRAWPRIMITIVEUP], (void *)hDrawPrimitiveUP, (void **)&oDrawPrimitiveUP, "IDirect3DDevice9::DrawPrimitiveUP");
        wl_hook_code(vt[SLOT_DEVICE_DRAWINDEXEDPRIMITIVEUP], (void *)hDrawIndexedPrimitiveUP, (void **)&oDrawIndexedPrimitiveUP,
                     "IDirect3DDevice9::DrawIndexedPrimitiveUP");
        wl_hook_code(vt[SLOT_DEVICE_SETSCISSORRECT], (void *)hSetScissorRect, (void **)&oSetScissorRect, "IDirect3DDevice9::SetScissorRect");
    }
    IDirect3DSwapChain9 *sc = NULL;
    if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc)
    {
        wl_hook_code(vtable_of(sc)[SLOT_SWAPCHAIN_PRESENT], (void *)hSwapChainPresent, (void **)&oSwapChainPresent, "IDirect3DSwapChain9::Present");
        sc->Release();
    }
}

// ------------------------------------------------------------------------------
// IDirect3D9 / IDirect3D9Ex
// ------------------------------------------------------------------------------

// A game drawing on one thread and loading its textures on another, its device not made
// D3DCREATE_MULTITHREADED: Direct3D then serialises their calls (wl_d3d9_multithreaded). Storm
// Rider's loader (d3dx9's CreateTexture) and its frames met in the graphics driver, which waited
// for good: about one game in three stopped on the bike's loading
static int g_multithreaded;

void wl_d3d9_multithreaded(void)
{
    g_multithreaded = 1;
}

static DWORD behavior_flags(DWORD behavior)
{
    if (g_multithreaded && !(behavior & D3DCREATE_MULTITHREADED))
    {
        wl_log("  behavior 0x%lx: D3DCREATE_MULTITHREADED added (its textures loaded on another thread)", behavior);
        behavior |= D3DCREATE_MULTITHREADED;
    }
    return behavior;
}

// A device not the game's made at the address of the game's: the game released that one, forgotten
static void not_the_game(HRESULT hr, IDirect3DDevice9 *dev)
{
    if (SUCCEEDED(hr) && dev && dev == g_gameDev)
    {
        wl_log("  at the address of the game's device, which it released: that one forgotten");
        g_gameDev = NULL;
    }
}

static HRESULT(STDMETHODCALLTYPE *oCreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT STDMETHODCALLTYPE hCreateDevice(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS *pp,
                                               IDirect3DDevice9 **out)
{
    char where[128];
    wl_log("CreateDevice adapter=%u type=%d focus=%p behavior=0x%lx [%s]", adapter, type, focus, behavior, wl_where(WL_CALLER, where, sizeof where));
    if (made_by_windows(WL_CALLER))
    {
        HRESULT hr = oCreateDevice(d3d, adapter, type, focus, behavior, pp, out);
        wl_log("CreateDevice -> 0x%08lx, Windows' own device (a video's): left alone", hr);
        not_the_game(hr, out ? (IDirect3DDevice9 *)*out : NULL);
        return hr;
    }
    behavior = behavior_flags(behavior);
    log_pp("  asked", pp);
    if (wl_display_dxgi_shown())
    {
        // A helper device of a game that shows its picture by DXGI (Mario Kart DX's Direct3D 9Ex at
        // its window's size): its picture's size and place left as the swap chain set them
        int forced = make_windowed(pp);
        HRESULT hr = oCreateDevice(d3d, adapter, type, focus, behavior, pp, out);
        wl_log("CreateDevice -> 0x%08lx, a helper of the game's (its picture is the DXGI swap chain's): left alone%s", hr, forced ? ", kept in a window" : "");
        not_the_game(hr, out ? (IDirect3DDevice9 *)*out : NULL);
        return hr;
    }
    D3DADAPTER_IDENTIFIER9 id;
    if (SUCCEEDED(d3d->GetAdapterIdentifier(adapter, 0, &id)))
        wl_log("  on %s", id.Description);
    told_size(pp);
    int forced = make_windowed(pp);
    if (forced)
        wl_log("  kept in a window");
    asked_size(pp, focus);
    wl_display_prepare(pp);
    keep_tex0(NULL); // the previous device's texture let go
    HRESULT hr = oCreateDevice(d3d, adapter, type, focus, behavior, pp, out);
    if (SUCCEEDED(hr) && out)
        g_gameDev = *out;
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
    if (made_by_windows(WL_CALLER))
    {
        HRESULT hr = oCreateDeviceEx(d3d, adapter, type, focus, behavior, pp, mode, out);
        wl_log("CreateDeviceEx -> 0x%08lx, Windows' own device (a video's): left alone", hr);
        not_the_game(hr, out ? (IDirect3DDevice9 *)*out : NULL);
        return hr;
    }
    behavior = behavior_flags(behavior);
    log_pp("  asked", pp);
    if (wl_display_dxgi_shown())
    {
        // A helper device of a game that shows its picture by DXGI (Mario Kart DX's Direct3D 9Ex at
        // its window's size): its picture's size and place left as the swap chain set them
        int forced = make_windowed(pp);
        if (forced)
            mode = NULL;
        HRESULT hr = oCreateDeviceEx(d3d, adapter, type, focus, behavior, pp, mode, out);
        wl_log("CreateDeviceEx -> 0x%08lx, a helper of the game's (its picture is the DXGI swap chain's): left alone%s", hr, forced ? ", kept in a window" : "");
        not_the_game(hr, out ? (IDirect3DDevice9 *)*out : NULL);
        return hr;
    }
    D3DADAPTER_IDENTIFIER9 id;
    if (SUCCEEDED(d3d->GetAdapterIdentifier(adapter, 0, &id)))
        wl_log("  on %s", id.Description);
    int forced = make_windowed(pp);
    if (forced)
    {
        mode = NULL; // a windowed device takes no display mode
        wl_log("  kept in a window");
    }
    asked_size(pp, focus);
    wl_display_prepare(pp);
    keep_tex0(NULL);
    HRESULT hr = oCreateDeviceEx(d3d, adapter, type, focus, behavior, pp, mode, out);
    if (SUCCEEDED(hr) && out)
        g_gameDev = *out;
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
    wl_vscreen_d3d9(d3d); // its modes, when the game is told another screen or looks for its sizes
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

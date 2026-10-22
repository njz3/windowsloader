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

// Direct3D 11 and DXGI: logs the device and the swap chain the game asks for, keeps the swap
// chain in a window (windowed mode: no exclusive full screen, the display mode stays, DXGI's own
// Alt+Enter off) and counts the frames. COM methods hooked through the vtables, once each. A game
// may create its swap chain from the factory of its device's adapter (Daytona Championship USA)
// rather than from CreateDXGIFactory: that factory is hooked as soon as the device exists.

#include <windows.h>
#include <stdlib.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <stdio.h>
#include <string.h>

#include "agent.h"
#include "display.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"

enum
{
    SLOT_FACTORY_MAKEWINDOWASSOCIATION = 8,
    SLOT_FACTORY_CREATESWAPCHAIN = 10,
    SLOT_FACTORY2_CREATESWAPCHAINFORHWND = 15,
    SLOT_SWAPCHAIN_PRESENT = 8,
    SLOT_SWAPCHAIN_SETFULLSCREENSTATE = 10,
    SLOT_ADAPTER_ENUMOUTPUTS = 7,
    SLOT_DEVICE_CREATEBUFFER = 3,
    SLOT_DEVICE_CREATETEXTURE2D = 5,
};

// What the game's resources take, by usage (default, immutable, dynamic, staging): the dynamic and
// staging ones live in the game's own address space
static volatile LONGLONG g_resBytes[4];
static volatile LONG g_resCount[4];

static void count_resource(D3D11_USAGE usage, unsigned long long bytes)
{
    int u = usage >= 0 && usage < 4 ? (int)usage : 0;
    InterlockedExchangeAdd64(&g_resBytes[u], (LONGLONG)bytes);
    InterlockedIncrement(&g_resCount[u]);
}

static unsigned format_bits(DXGI_FORMAT f)
{
    if (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC1_UNORM_SRGB)
        return 4;
    if (f >= DXGI_FORMAT_BC4_TYPELESS && f <= DXGI_FORMAT_BC4_SNORM)
        return 4;
    if ((f >= DXGI_FORMAT_BC2_TYPELESS && f <= DXGI_FORMAT_BC3_UNORM_SRGB) || (f >= DXGI_FORMAT_BC5_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) ||
        (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB))
        return 8;
    if (f >= DXGI_FORMAT_R32G32B32A32_TYPELESS && f <= DXGI_FORMAT_R32G32B32A32_SINT)
        return 128;
    if (f >= DXGI_FORMAT_R16G16B16A16_TYPELESS && f <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
        return 64;
    if (f >= DXGI_FORMAT_R8G8_TYPELESS && f <= DXGI_FORMAT_R16_SINT)
        return 16;
    if (f >= DXGI_FORMAT_R8_TYPELESS && f <= DXGI_FORMAT_A8_UNORM)
        return 8;
    return 32;
}

static HRESULT(STDMETHODCALLTYPE *oCreateTexture2D)(ID3D11Device *, const D3D11_TEXTURE2D_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **);
static HRESULT STDMETHODCALLTYPE hCreateTexture2D(ID3D11Device *dev, const D3D11_TEXTURE2D_DESC *d, const D3D11_SUBRESOURCE_DATA *init, ID3D11Texture2D **out)
{
    HRESULT hr = oCreateTexture2D(dev, d, init, out);
    if (SUCCEEDED(hr) && d && !wl_busy())
    {
        unsigned long long px = 0, w = d->Width, h = d->Height;
        for (UINT m = 0; m < (d->MipLevels ? d->MipLevels : 1); m++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1)
            px += w * h;
        count_resource(d->Usage, px * d->ArraySize * (d->SampleDesc.Count ? d->SampleDesc.Count : 1) * format_bits(d->Format) / 8);
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreateBuffer)(ID3D11Device *, const D3D11_BUFFER_DESC *, const D3D11_SUBRESOURCE_DATA *, ID3D11Buffer **);
static HRESULT STDMETHODCALLTYPE hCreateBuffer(ID3D11Device *dev, const D3D11_BUFFER_DESC *d, const D3D11_SUBRESOURCE_DATA *init, ID3D11Buffer **out)
{
    HRESULT hr = oCreateBuffer(dev, d, init, out);
    if (SUCCEEDED(hr) && d && !wl_busy())
        count_resource(d->Usage, d->ByteWidth);
    return hr;
}

static void hook_device(ID3D11Device *dev)
{
    static int done;
    if (done || !dev)
        return;
    done = 1;
    void **vt = *(void ***)dev;
    wl_hook_code(vt[SLOT_DEVICE_CREATETEXTURE2D], (void *)hCreateTexture2D, (void **)&oCreateTexture2D, "ID3D11Device::CreateTexture2D");
    wl_hook_code(vt[SLOT_DEVICE_CREATEBUFFER], (void *)hCreateBuffer, (void **)&oCreateBuffer, "ID3D11Device::CreateBuffer");
}

static void **vtable_of(void *com)
{
    return *(void ***)com;
}

static void log_desc(const char *what, const DXGI_SWAP_CHAIN_DESC *d)
{
    if (!d)
        return;
    wl_log("%s: %ux%u format=%d refresh=%u/%u buffers=%u msaa=%u/%u window=%p windowed=%d swap=%d flags=0x%x", what, d->BufferDesc.Width,
           d->BufferDesc.Height, d->BufferDesc.Format, d->BufferDesc.RefreshRate.Numerator, d->BufferDesc.RefreshRate.Denominator, d->BufferCount,
           d->SampleDesc.Count, d->SampleDesc.Quality, d->OutputWindow, d->Windowed, d->SwapEffect, d->Flags);
}

// ------------------------------------------------------------------------------
// IDXGISwapChain
// ------------------------------------------------------------------------------

// The picture the agent asked for: the back buffer about to be presented, through a staging copy
void wl_d3d11_take_pending_shot(IDXGISwapChain *sc)
{
    const char *path = wl_shot_pending();
    if (!path)
        return;
    ID3D11Texture2D *bb = NULL, *resolved = NULL, *staging = NULL;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    int ok = 0;
    const char *step = "GetBuffer";
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb);
    D3D11_TEXTURE2D_DESC d;
    memset(&d, 0, sizeof d);
    if (SUCCEEDED(hr))
    {
        bb->GetDesc(&d);
        bb->GetDevice(&dev);
        dev->GetImmediateContext(&ctx);
        int rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        int bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
        int ten = d.Format == DXGI_FORMAT_R10G10B10A2_UNORM; // DiRT Showdown's: 10 bits a channel, made 8 for the file
        ID3D11Texture2D *src = bb;
        D3D11_TEXTURE2D_DESC s = d;
        s.MipLevels = 1;
        s.ArraySize = 1;
        s.SampleDesc.Count = 1;
        s.SampleDesc.Quality = 0;
        s.MiscFlags = 0;
        step = "format";
        hr = E_FAIL;
        if (rgba || bgra || ten)
        {
            // A multisampled back buffer is resolved first
            if (d.SampleDesc.Count > 1)
            {
                s.Usage = D3D11_USAGE_DEFAULT;
                s.BindFlags = 0;
                s.CPUAccessFlags = 0;
                if (SUCCEEDED(dev->CreateTexture2D(&s, NULL, &resolved)))
                {
                    ctx->ResolveSubresource(resolved, 0, bb, 0, d.Format);
                    src = resolved;
                }
            }
            s.Usage = D3D11_USAGE_STAGING;
            s.BindFlags = 0;
            s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            step = "CreateTexture2D";
            hr = dev->CreateTexture2D(&s, NULL, &staging);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(hr))
            {
                ctx->CopyResource(staging, src);
                step = "Map";
                hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
            }
            if (SUCCEEDED(hr))
            {
                step = "writing the file";
                if (ten)
                {
                    unsigned char *pixels = (unsigned char *)malloc((size_t)d.Width * d.Height * 4);
                    if (pixels)
                    {
                        for (UINT y = 0; y < d.Height; y++)
                        {
                            const UINT *row = (const UINT *)((const unsigned char *)m.pData + (size_t)y * m.RowPitch);
                            unsigned char *out = pixels + (size_t)y * d.Width * 4;
                            for (UINT x = 0; x < d.Width; x++)
                            {
                                UINT v = row[x];
                                out[x * 4] = (unsigned char)((v & 0x3FF) >> 2);
                                out[x * 4 + 1] = (unsigned char)(((v >> 10) & 0x3FF) >> 2);
                                out[x * 4 + 2] = (unsigned char)(((v >> 20) & 0x3FF) >> 2);
                                out[x * 4 + 3] = 255;
                            }
                        }
                        ok = wl_write_bmp(path, pixels, (int)d.Width * 4, (int)d.Width, (int)d.Height, 1);
                        free(pixels);
                    }
                }
                else
                    ok = wl_write_bmp(path, (const unsigned char *)m.pData, (int)m.RowPitch, (int)d.Width, (int)d.Height, rgba);
                ctx->Unmap(staging, 0);
            }
        }
    }
    if (!ok)
        wl_log("shot: %s failed (0x%08lx), back buffer %ux%u format %d msaa %u", step, hr, d.Width, d.Height, d.Format, d.SampleDesc.Count);
    if (staging)
        staging->Release();
    if (resolved)
        resolved->Release();
    if (ctx)
        ctx->Release();
    if (dev)
        dev->Release();
    if (bb)
        bb->Release();
    wl_shot_done(ok);
}

// ------------------------------------------------------------------------------
// The picture copied into the window by GDI (display.h, PRESENT = GDI): the back buffer read back
// through a staging texture, drawn into the game's window; the swap chain presents into its hidden
// child window
// ------------------------------------------------------------------------------

static ID3D11Device *g_gdiDev;
static ID3D11Texture2D *g_gdiStaging, *g_gdiResolved;
static D3D11_TEXTURE2D_DESC g_gdiDesc;
static unsigned char *g_gdiPixels;

static void gdi_release(void)
{
    if (g_gdiStaging)
        g_gdiStaging->Release();
    if (g_gdiResolved)
        g_gdiResolved->Release();
    g_gdiStaging = g_gdiResolved = NULL;
    free(g_gdiPixels);
    g_gdiPixels = NULL;
}

static void gdi_copy(IDXGISwapChain *sc)
{
    ID3D11Texture2D *bb = NULL;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)))
        return;
    D3D11_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    int rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    int bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
    int ten = d.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    bb->GetDevice(&dev);
    dev->GetImmediateContext(&ctx);
    if ((rgba || bgra || ten) && (!g_gdiStaging || dev != g_gdiDev || d.Width != g_gdiDesc.Width || d.Height != g_gdiDesc.Height ||
                                  d.Format != g_gdiDesc.Format || d.SampleDesc.Count != g_gdiDesc.SampleDesc.Count))
    {
        gdi_release();
        g_gdiDev = dev;
        g_gdiDesc = d;
        D3D11_TEXTURE2D_DESC s = d;
        s.MipLevels = 1;
        s.ArraySize = 1;
        s.SampleDesc.Count = 1;
        s.SampleDesc.Quality = 0;
        s.MiscFlags = 0;
        s.BindFlags = 0;
        if (d.SampleDesc.Count > 1)
        {
            s.Usage = D3D11_USAGE_DEFAULT;
            s.CPUAccessFlags = 0;
            dev->CreateTexture2D(&s, NULL, &g_gdiResolved);
        }
        s.Usage = D3D11_USAGE_STAGING;
        s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        dev->CreateTexture2D(&s, NULL, &g_gdiStaging);
        if (!bgra)
            g_gdiPixels = (unsigned char *)malloc((size_t)d.Width * d.Height * 4);
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if ((rgba || bgra || ten) && g_gdiStaging)
    {
        ID3D11Texture2D *src = bb;
        if (g_gdiResolved)
        {
            ctx->ResolveSubresource(g_gdiResolved, 0, bb, 0, d.Format);
            src = g_gdiResolved;
        }
        ctx->CopyResource(g_gdiStaging, src);
        if (SUCCEEDED(ctx->Map(g_gdiStaging, 0, D3D11_MAP_READ, 0, &m)))
        {
            const void *pixels = m.pData;
            int pitch = (int)m.RowPitch;
            if (!bgra && g_gdiPixels)
            {
                // Made BGRX: R and B swapped, or 10 bits a channel brought to 8 (DiRT Showdown)
                for (UINT y = 0; y < d.Height; y++)
                {
                    const UINT *row = (const UINT *)((const unsigned char *)m.pData + (size_t)y * m.RowPitch);
                    UINT *out = (UINT *)(g_gdiPixels + (size_t)y * d.Width * 4);
                    for (UINT x = 0; x < d.Width; x++)
                    {
                        UINT v = row[x];
                        out[x] = ten ? ((v >> 22 & 0xFF) | (v >> 12 & 0xFF) << 8 | (v >> 2 & 0xFF) << 16)
                                     : ((v >> 16 & 0xFF) | (v & 0xFF00) | (v & 0xFF) << 16);
                    }
                }
                pixels = g_gdiPixels;
                pitch = (int)d.Width * 4;
            }
            wl_display_gdi_draw(pixels, pitch, (int)d.Width, (int)d.Height, 32, 0);
            ctx->Unmap(g_gdiStaging, 0);
        }
    }
    ctx->Release();
    dev->Release();
    bb->Release();
}

// The frame out: by GDI into the window, the swap chain presenting into its hidden child at no
// pace of its own (one composition of the screen per interval asked), or the game's Present.
// The F10 menu presents through it too.
static HRESULT(STDMETHODCALLTYPE *oPresent)(IDXGISwapChain *, UINT, UINT);
static HRESULT STDMETHODCALLTYPE present_out(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    if (!wl_display_gdi() || (flags & DXGI_PRESENT_TEST))
        return oPresent(sc, sync, flags);
    gdi_copy(sc);
    HRESULT hr = oPresent(sc, 0, flags);
    for (UINT i = 0; i < sync; i++)
        DwmFlush();
    return hr == DXGI_STATUS_OCCLUDED ? S_OK : hr; // its child is hidden: the game keeps drawing
}

static HRESULT STDMETHODCALLTYPE hPresent(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    static unsigned frames, lastFrames;
    static ULONGLONG lastTick;
    wl_menu_present11(sc, present_out); // F10: the menu runs here until it is closed
    wl_d3d11_take_pending_shot(sc);
    wl_display_dest(); // F11, Alt+Enter, the mode chosen in the menu
    HRESULT hr = present_out(sc, sync, flags);
    wl_frame_presented();

    // Occluded (the window hidden or the screen locked): DXGI draws nothing until it is shown again
    static HRESULT lastHr = S_OK;
    if (hr != lastHr && frames)
    {
        wl_log("DXGI Present -> 0x%08lx%s", hr, hr == DXGI_STATUS_OCCLUDED ? " (occluded)" : hr == S_OK ? " (drawing again)" : "");
        lastHr = hr;
    }
    ULONGLONG now = GetTickCount64();
    if (!frames++)
    {
        lastTick = now;
        DXGI_SWAP_CHAIN_DESC d;
        if (SUCCEEDED(sc->GetDesc(&d)))
            wl_frame_size((int)d.BufferDesc.Width, (int)d.BufferDesc.Height);
        wl_log("DXGI Present: first frame (hr=0x%08lx)", hr);
    }
    else if (now - lastTick >= 10000)
    {
        // A 32-bit game short of address space fails its allocations: what holds it, by kind
        unsigned long long kinds[3] = {0, 0, 0}, largestFree = 0;
        MEMORY_BASIC_INFORMATION mbi;
        for (const char *p = NULL; VirtualQuery(p, &mbi, sizeof mbi) == sizeof mbi; p = (const char *)mbi.BaseAddress + mbi.RegionSize)
        {
            if (mbi.State == MEM_FREE && mbi.RegionSize > largestFree)
                largestFree = mbi.RegionSize;
            else if (mbi.State != MEM_FREE)
                kinds[mbi.Type == MEM_IMAGE ? 0 : mbi.Type == MEM_MAPPED ? 1 : 2] += mbi.RegionSize;
            if ((const char *)mbi.BaseAddress + mbi.RegionSize < (const char *)mbi.BaseAddress)
                break;
        }
        wl_log("DXGI Present: %u frames, %.1f fps; address space: images %llu MB, mapped %llu MB, private %llu MB, largest free %llu MB",
               frames, (frames - lastFrames) * 1000.0 / (double)(now - lastTick), kinds[0] >> 20, kinds[1] >> 20, kinds[2] >> 20, largestFree >> 20);
        wl_log("  resources: default %ld (%lld MB), immutable %ld (%lld MB), dynamic %ld (%lld MB), staging %ld (%lld MB)", g_resCount[0],
               g_resBytes[0] >> 20, g_resCount[1], g_resBytes[1] >> 20, g_resCount[2], g_resBytes[2] >> 20, g_resCount[3], g_resBytes[3] >> 20);
        lastFrames = frames;
        lastTick = now;
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oSetFullscreenState)(IDXGISwapChain *, BOOL, IDXGIOutput *);
static HRESULT STDMETHODCALLTYPE hSetFullscreenState(IDXGISwapChain *sc, BOOL full, IDXGIOutput *out)
{
    if (full && g_wl.windowed)
    {
        wl_log("DXGI SetFullscreenState(TRUE) refused (windowed mode)");
        return S_OK;
    }
    return oSetFullscreenState(sc, full, out);
}

static void hook_swapchain(IDXGISwapChain *sc)
{
    static int done;
    if (done || !sc)
        return;
    done = 1;
    void **vt = vtable_of(sc);
    wl_hook_code(vt[SLOT_SWAPCHAIN_PRESENT], (void *)hPresent, (void **)&oPresent, "IDXGISwapChain::Present");
    wl_hook_code(vt[SLOT_SWAPCHAIN_SETFULLSCREENSTATE], (void *)hSetFullscreenState, (void **)&oSetFullscreenState, "IDXGISwapChain::SetFullscreenState");
}

// A full-screen swap chain asked for: a windowed one instead
static const DXGI_SWAP_CHAIN_DESC *windowed_desc(const DXGI_SWAP_CHAIN_DESC *d, DXGI_SWAP_CHAIN_DESC *copy)
{
    if (!d || d->Windowed || !g_wl.windowed)
        return d;
    *copy = *d;
    copy->Windowed = TRUE;
    copy->BufferDesc.RefreshRate.Numerator = 0;
    copy->BufferDesc.RefreshRate.Denominator = 0;
    wl_log("  kept in a window");
    return copy;
}

// ------------------------------------------------------------------------------
// IDXGIFactory
// ------------------------------------------------------------------------------

// In a window, DXGI's own Alt+Enter would take the exclusive full screen
static HRESULT(STDMETHODCALLTYPE *oMakeWindowAssociation)(IDXGIFactory *, HWND, UINT);
static HRESULT STDMETHODCALLTYPE hMakeWindowAssociation(IDXGIFactory *f, HWND wnd, UINT flags)
{
    if (!wl_busy() && g_wl.windowed && !(flags & DXGI_MWA_NO_ALT_ENTER))
    {
        wl_log("IDXGIFactory::MakeWindowAssociation(%p, 0x%x): Alt+Enter left to windowsloader", wnd, flags);
        flags |= DXGI_MWA_NO_ALT_ENTER;
    }
    return oMakeWindowAssociation(f, wnd, flags);
}

static HRESULT(STDMETHODCALLTYPE *oCreateSwapChain)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
static HRESULT STDMETHODCALLTYPE hCreateSwapChain(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d, IDXGISwapChain **out)
{
    if (wl_busy())
        return oCreateSwapChain(f, dev, d, out);
    char where[128];
    wl_log("IDXGIFactory::CreateSwapChain [%s]", wl_where(WL_CALLER, where, sizeof where));
    log_desc("  asked", d);
    DXGI_SWAP_CHAIN_DESC copy, placed;
    const DXGI_SWAP_CHAIN_DESC *use = windowed_desc(d, &copy);
    if (use && g_wl.windowed)
    {
        placed = *use;
        placed.OutputWindow = wl_display_child(use->OutputWindow);
        use = &placed;
    }
    wl_busy_enter();
    HRESULT hr = oCreateSwapChain(f, dev, (DXGI_SWAP_CHAIN_DESC *)use, out);
    wl_busy_leave();
    wl_log("CreateSwapChain -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && out)
    {
        hook_swapchain(*out);
        if (d && g_wl.windowed)
        {
            f->MakeWindowAssociation(d->OutputWindow, DXGI_MWA_NO_ALT_ENTER);
            wl_display_window(d->OutputWindow, (int)d->BufferDesc.Width, (int)d->BufferDesc.Height);
        }
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreateSwapChainForHwnd)(IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
static HRESULT STDMETHODCALLTYPE hCreateSwapChainForHwnd(IDXGIFactory2 *f, IUnknown *dev, HWND wnd, const DXGI_SWAP_CHAIN_DESC1 *d,
                                                         const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *out, IDXGISwapChain1 **sc)
{
    if (wl_busy())
        return oCreateSwapChainForHwnd(f, dev, wnd, d, fs, out, sc);
    char where[128];
    wl_log("IDXGIFactory2::CreateSwapChainForHwnd window=%p [%s]", wnd, wl_where(WL_CALLER, where, sizeof where));
    if (d)
        wl_log("  asked: %ux%u format=%d buffers=%u msaa=%u/%u swap=%d scaling=%d flags=0x%x windowed=%d", d->Width, d->Height, d->Format,
               d->BufferCount, d->SampleDesc.Count, d->SampleDesc.Quality, d->SwapEffect, d->Scaling, d->Flags, fs ? fs->Windowed : 1);
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC copy;
    if (fs && !fs->Windowed && g_wl.windowed)
    {
        copy = *fs;
        copy.Windowed = TRUE;
        fs = &copy;
        wl_log("  kept in a window");
    }
    HWND target = g_wl.windowed ? wl_display_child(wnd) : wnd;
    wl_busy_enter();
    HRESULT hr = oCreateSwapChainForHwnd(f, dev, target, d, fs, out, sc);
    wl_busy_leave();
    wl_log("CreateSwapChainForHwnd -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && sc)
    {
        hook_swapchain(*sc);
        if (g_wl.windowed)
        {
            f->MakeWindowAssociation(wnd, DXGI_MWA_NO_ALT_ENTER);
            if (d)
                wl_display_window(wnd, (int)d->Width, (int)d->Height);
        }
    }
    return hr;
}

static void hook_factory(IDXGIFactory *f)
{
    static int done, done2;
    if (!f)
        return;
    if (!done)
    {
        done = 1;
        void **vt = vtable_of(f);
        wl_hook_code(vt[SLOT_FACTORY_CREATESWAPCHAIN], (void *)hCreateSwapChain, (void **)&oCreateSwapChain, "IDXGIFactory::CreateSwapChain");
        wl_hook_code(vt[SLOT_FACTORY_MAKEWINDOWASSOCIATION], (void *)hMakeWindowAssociation, (void **)&oMakeWindowAssociation,
                     "IDXGIFactory::MakeWindowAssociation");
    }
    IDXGIFactory2 *f2 = NULL;
    if (!done2 && SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory2), (void **)&f2)) && f2)
    {
        done2 = 1;
        wl_hook_code(vtable_of(f2)[SLOT_FACTORY2_CREATESWAPCHAINFORHWND], (void *)hCreateSwapChainForHwnd, (void **)&oCreateSwapChainForHwnd,
                     "IDXGIFactory2::CreateSwapChainForHwnd");
        f2->Release();
    }
}

// The factory of the adapter the device was created on, the one a game reaches through
// IDXGIDevice::GetAdapter and IDXGIAdapter::GetParent
static void hook_factory_of(ID3D11Device *dev)
{
    hook_device(dev);
    IDXGIDevice *dx = NULL;
    IDXGIAdapter *ad = NULL;
    IDXGIFactory *f = NULL;
    wl_busy_enter();
    if (dev && SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dx)) && SUCCEEDED(dx->GetAdapter(&ad)) &&
        SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory), (void **)&f)))
        hook_factory(f);
    if (f)
        f->Release();
    if (ad)
        ad->Release();
    if (dx)
        dx->Release();
    wl_busy_leave();
}

static HRESULT(WINAPI *oCreateDXGIFactory)(REFIID, void **);
static HRESULT WINAPI hCreateDXGIFactory(REFIID iid, void **out)
{
    wl_busy_enter();
    HRESULT hr = oCreateDXGIFactory(iid, out);
    wl_busy_leave();
    if (SUCCEEDED(hr) && out && !wl_is_system_caller(WL_CALLER))
        hook_factory((IDXGIFactory *)*out);
    return hr;
}

static HRESULT(WINAPI *oCreateDXGIFactory1)(REFIID, void **);
static HRESULT WINAPI hCreateDXGIFactory1(REFIID iid, void **out)
{
    wl_busy_enter();
    HRESULT hr = oCreateDXGIFactory1(iid, out);
    wl_busy_leave();
    if (SUCCEEDED(hr) && out && !wl_is_system_caller(WL_CALLER))
        hook_factory((IDXGIFactory *)*out);
    return hr;
}

// ------------------------------------------------------------------------------
// d3d11.dll
// ------------------------------------------------------------------------------

// On a PC with two GPUs (a laptop's integrated and dedicated ones), Direct3D's default adapter is
// the integrated one, whose textures live in system memory mapped into the process: a 32-bit game
// with 2.4 GB of textures runs out of address space there (Daytona Championship USA at its car
// select). An arcade PC has one dedicated GPU: the game gets the adapter with the most dedicated
// video memory when it asks for the default one. WINDOWSLOADER_GPU=default keeps Windows' choice.
// The adapter showing the screen, for a dedicated GPU without outputs of its own (a laptop's,
// whose screen is the integrated GPU's): the game enumerates its adapter's outputs and takes the
// first one without checking (Daytona Championship USA)
static IDXGIAdapter *g_displayAdapter;

static HRESULT(STDMETHODCALLTYPE *oEnumOutputs)(IDXGIAdapter *, UINT, IDXGIOutput **);
static HRESULT STDMETHODCALLTYPE hEnumOutputs(IDXGIAdapter *ad, UINT index, IDXGIOutput **out)
{
    HRESULT hr = oEnumOutputs(ad, index, out);
    if (hr != DXGI_ERROR_NOT_FOUND || !g_displayAdapter || ad == g_displayAdapter || wl_busy() || wl_is_system_caller(WL_CALLER))
        return hr;
    IDXGIOutput *own = NULL;
    if (index && SUCCEEDED(oEnumOutputs(ad, 0, &own)))
    {
        own->Release(); // it has outputs of its own: the list simply ends here
        return hr;
    }
    hr = oEnumOutputs(g_displayAdapter, index, out);
    static int logged;
    if (!logged++)
        wl_log(SUCCEEDED(hr) ? "IDXGIAdapter::EnumOutputs: the adapter has no screen, the game gets the screens of the one showing them"
               : index ? "IDXGIAdapter::EnumOutputs: no more screens"
                       : "IDXGIAdapter::EnumOutputs: no screen on any adapter (screen off, or the session shown nowhere): the game "
                         "takes the first one without checking and stops");
    return hr;
}

static IDXGIAdapter *best_adapter(void)
{
    wchar_t v[16] = L"";
    GetEnvironmentVariableW(L"WINDOWSLOADER_GPU", v, 16);
    if (!_wcsicmp(v, L"default"))
        return NULL;
    IDXGIFactory1 *f = NULL;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f)))
        return NULL;
    IDXGIAdapter1 *best = NULL, *ad = NULL;
    SIZE_T bestMemory = 0;
    UINT bestIndex = 0;
    for (UINT i = 0; f->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 d;
        if (SUCCEEDED(ad->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > bestMemory)
        {
            if (best)
                best->Release();
            best = ad;
            bestMemory = d.DedicatedVideoMemory;
            bestIndex = i;
        }
        else
            ad->Release();
    }
    // The first adapter is already the default one
    if (best && !bestIndex)
    {
        best->Release();
        best = NULL;
    }
    if (best)
    {
        // The screens stay the default adapter's
        IDXGIAdapter1 *first = NULL;
        if (!g_displayAdapter && SUCCEEDED(f->EnumAdapters1(0, &first)))
            g_displayAdapter = first;
        if (!oEnumOutputs)
            wl_hook_code(vtable_of(best)[SLOT_ADAPTER_ENUMOUTPUTS], (void *)hEnumOutputs, (void **)&oEnumOutputs, "IDXGIAdapter::EnumOutputs");
        DXGI_ADAPTER_DESC1 d;
        best->GetDesc1(&d);
        char name[128];
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, NULL, NULL);
        wl_log("  on %s (%llu MB of video memory) rather than Windows' default adapter", name, (unsigned long long)d.DedicatedVideoMemory >> 20);
    }
    f->Release();
    return best;
}

static HRESULT(WINAPI *oD3D11CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                       const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *,
                                                       ID3D11DeviceContext **);
static HRESULT WINAPI hD3D11CreateDeviceAndSwapChain(IDXGIAdapter *ad, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags, const D3D_FEATURE_LEVEL *levels,
                                                     UINT nlevels, UINT sdk, const DXGI_SWAP_CHAIN_DESC *d, IDXGISwapChain **sc, ID3D11Device **dev,
                                                     D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **ctx)
{
    // D3D11CreateDevice is this call without a swap chain
    if (wl_busy())
        return oD3D11CreateDeviceAndSwapChain(ad, type, sw, flags, levels, nlevels, sdk, d, sc, dev, level, ctx);
    char where[128];
    wl_log("D3D11CreateDeviceAndSwapChain type=%d flags=0x%x [%s]", type, flags, wl_where(WL_CALLER, where, sizeof where));
    log_desc("  asked", d);
    DXGI_SWAP_CHAIN_DESC copy, placed;
    const DXGI_SWAP_CHAIN_DESC *use = windowed_desc(d, &copy);
    if (use && sc && g_wl.windowed)
    {
        placed = *use;
        placed.OutputWindow = wl_display_child(use->OutputWindow);
        use = &placed;
    }
    IDXGIAdapter *best = !ad && type == D3D_DRIVER_TYPE_HARDWARE && !wl_is_system_caller(WL_CALLER) ? best_adapter() : NULL;
    wl_busy_enter();
    HRESULT hr = oD3D11CreateDeviceAndSwapChain(best ? best : ad, best ? D3D_DRIVER_TYPE_UNKNOWN : type, sw, flags, levels, nlevels, sdk,
                                                use, sc, dev, level, ctx);
    wl_busy_leave();
    if (best)
        best->Release();
    wl_log("D3D11CreateDeviceAndSwapChain -> 0x%08lx, feature level 0x%x", hr, level ? *level : 0);
    if (SUCCEEDED(hr) && sc)
        hook_swapchain(*sc);
    if (SUCCEEDED(hr) && sc && *sc && d && g_wl.windowed)
        wl_display_window(d->OutputWindow, (int)d->BufferDesc.Width, (int)d->BufferDesc.Height);
    if (SUCCEEDED(hr) && dev)
        hook_factory_of(*dev);
    return hr;
}

static HRESULT(WINAPI *oD3D11CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                           D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
static HRESULT WINAPI hD3D11CreateDevice(IDXGIAdapter *ad, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags, const D3D_FEATURE_LEVEL *levels, UINT nlevels,
                                         UINT sdk, ID3D11Device **dev, D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **ctx)
{
    int nested = wl_busy();
    int game = !nested && !wl_is_system_caller(WL_CALLER);
    IDXGIAdapter *best = game && !ad && type == D3D_DRIVER_TYPE_HARDWARE ? best_adapter() : NULL;
    wl_busy_enter();
    HRESULT hr = oD3D11CreateDevice(best ? best : ad, best ? D3D_DRIVER_TYPE_UNKNOWN : type, sw, flags, levels, nlevels, sdk, dev, level, ctx);
    wl_busy_leave();
    if (best)
        best->Release();
    if (game)
    {
        char where[128];
        wl_log("D3D11CreateDevice type=%d flags=0x%x -> 0x%08lx, feature level 0x%x [%s]", type, flags, hr, level ? *level : 0,
               wl_where(WL_CALLER, where, sizeof where));
        if (SUCCEEDED(hr) && dev)
            hook_factory_of(*dev);
    }
    return hr;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"d3d11.dll", D3D11CreateDeviceAndSwapChain),
    WL_HOOK(L"d3d11.dll", D3D11CreateDevice),
    WL_HOOK(L"dxgi.dll", CreateDXGIFactory),
    WL_HOOK(L"dxgi.dll", CreateDXGIFactory1),
};

void wl_d3d11_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}

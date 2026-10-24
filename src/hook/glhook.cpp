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

// OpenGL games (Initial D 6 on a Sega RingEdge): each SwapBuffers counts a frame, takes the picture
// the agent asked for from the back buffer, and puts the frames per second in the window's title.
// The window stays as the game made it (no display modes yet, no settings menu).
// opengl32.dll is the game's: its functions are looked up once it is loaded.

#include <windows.h>
#include <stdlib.h>

#include "agent.h"
#include "display.h"
#include "hooks.h"
#include "log.h"

#define GL_BACK 0x0405
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

typedef void(WINAPI *GlReadBuffer)(unsigned mode);
typedef void(WINAPI *GlReadPixels)(int x, int y, int w, int h, unsigned format, unsigned type, void *pixels);
typedef void(WINAPI *GlPixelStorei)(unsigned name, int value);
typedef const char *(WINAPI *GlGetString)(unsigned name);

static GlReadBuffer pglReadBuffer;
static GlReadPixels pglReadPixels;
static GlPixelStorei pglPixelStorei;

static int gl_functions(void)
{
    if (pglReadPixels)
        return 1;
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl)
        return 0;
    pglReadBuffer = (GlReadBuffer)GetProcAddress(gl, "glReadBuffer");
    pglPixelStorei = (GlPixelStorei)GetProcAddress(gl, "glPixelStorei");
    pglReadPixels = (GlReadPixels)GetProcAddress(gl, "glReadPixels");
    return pglReadBuffer && pglPixelStorei && pglReadPixels;
}

static void take_pending_shot(int w, int h)
{
    const char *path = wl_shot_pending();
    if (!path)
        return;
    int ok = 0;
    unsigned char *pixels = (unsigned char *)malloc((size_t)w * h * 4);
    if (pixels && gl_functions())
    {
        pglReadBuffer(GL_BACK);
        pglPixelStorei(GL_PACK_ALIGNMENT, 4);
        pglReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        // OpenGL's rows go from the bottom up
        ok = wl_write_bmp(path, pixels + (size_t)(h - 1) * w * 4, -w * 4, w, h, 0);
    }
    free(pixels);
    wl_shot_done(ok);
}

static BOOL(WINAPI *oSwapBuffers)(HDC);
static BOOL WINAPI hSwapBuffers(HDC dc)
{
    static unsigned frames;
    static ULONGLONG lastTick;
    static unsigned lastFrames;
    if (wl_busy())
        return oSwapBuffers(dc);
    HWND wnd = WindowFromDC(dc);
    RECT r = {0, 0, 0, 0};
    if (wnd)
        GetClientRect(wnd, &r);
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w > 0 && h > 0)
        take_pending_shot(w, h);
    wl_display_title(wnd);
    BOOL ok = oSwapBuffers(dc);
    wl_frame_presented();

    ULONGLONG now = GetTickCount64();
    if (!frames++)
    {
        lastTick = now;
        wl_frame_size(w, h);
        wl_log("SwapBuffers: first frame, window %p, %dx%d", wnd, w, h);
        HMODULE gl = GetModuleHandleW(L"opengl32.dll");
        GlGetString getString = gl ? (GlGetString)GetProcAddress(gl, "glGetString") : NULL;
        if (getString)
        {
            const char *vendor = getString(GL_VENDOR), *renderer = getString(GL_RENDERER), *version = getString(GL_VERSION);
            wl_log("OpenGL: %s, %s, %s", vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
        }
    }
    else if (now - lastTick >= 10000)
    {
        wl_log("SwapBuffers: %u frames, %.1f fps", frames, (frames - lastFrames) * 1000.0 / (double)(now - lastTick));
        lastFrames = frames;
        lastTick = now;
    }
    return ok;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"gdi32.dll", SwapBuffers),
};

void wl_gl_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}

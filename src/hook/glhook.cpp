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

// OpenGL games (Initial D 6 on a Sega RingEdge): each SwapBuffers opens the settings menu on Tab
// (menu.cpp), counts a frame, takes the picture the agent asked for from the back buffer, and puts
// the frames per second in the window's title. The window stays as the game made it (no display
// modes), unless the game's module gave its picture's size (wl_gl_picture): its window is then set
// for the display mode, and its viewports moved to where the picture goes.
// opengl32.dll is the game's: its functions are looked up once it is loaded (the menu's are
// linked delay-loaded, so that only an OpenGL game loads it).

#include <windows.h>
#include <stdlib.h>

#include "agent.h"
#include "display.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"

#define GL_BACK 0x0405
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_VIEWPORT 0x0BA2

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

void wl_gl_take_pending_shot(int w, int h)
{
    take_pending_shot(w, h);
}

// ------------------------------------------------------------------------------
// A picture of a fixed size (wl_gl_picture): the game sets its viewports in its picture (640x480),
// they are moved and scaled to the picture's place in the window (OpenGL counts rows from the
// bottom). Its glClear, which no viewport limits, keeps the bars around black.
// ------------------------------------------------------------------------------

static int g_picW, g_picH;   // 0: the window left as the game made it
static HWND g_picWnd;
static RECT g_picRect;       // the picture's place in the client area
static int g_clientH;
static int g_view[4], g_viewSet; // the game's last viewport, in its picture

static void(WINAPI *oglViewport)(int, int, int, int);

static void viewport_to_window(const int *v)
{
    int pw = g_picRect.right - g_picRect.left, ph = g_picRect.bottom - g_picRect.top;
    int bottom = g_clientH - g_picRect.bottom;
    int x0 = g_picRect.left + MulDiv(v[0], pw, g_picW), x1 = g_picRect.left + MulDiv(v[0] + v[2], pw, g_picW);
    int y0 = bottom + MulDiv(v[1], ph, g_picH), y1 = bottom + MulDiv(v[1] + v[3], ph, g_picH);
    oglViewport(x0, y0, x1 - x0, y1 - y0);
}

static int moved(void)
{
    return !wl_busy() && g_picWnd && g_picRect.right > g_picRect.left;
}

static void WINAPI hglViewport(int x, int y, int w, int h)
{
    g_view[0] = x;
    g_view[1] = y;
    g_view[2] = w;
    g_view[3] = h;
    g_viewSet = 1;
    if (moved())
        viewport_to_window(g_view);
    else
        oglViewport(x, y, w, h);
}

// The viewport read back is the game's own: a game that sets it again from what it reads (Frenzy
// Express's attract does, each frame) would else shrink the picture at each turn
static void(WINAPI *oglGetIntegerv)(unsigned, int *);
static void WINAPI hglGetIntegerv(unsigned name, int *v)
{
    if (name == GL_VIEWPORT && v && g_viewSet && moved())
        memcpy(v, g_view, sizeof g_view);
    else
        oglGetIntegerv(name, v);
}

static void(WINAPI *oglGetFloatv)(unsigned, float *);
static void WINAPI hglGetFloatv(unsigned name, float *v)
{
    if (name == GL_VIEWPORT && v && g_viewSet && moved())
        for (int i = 0; i < 4; i++)
            v[i] = (float)g_view[i];
    else
        oglGetFloatv(name, v);
}

// At each SwapBuffers, before it: the window set for the mode at the first one; F11, Alt+Enter and
// the title (wl_display_dest); the game's viewport moved again when the picture's place changed
static void place_picture(HWND wnd)
{
    if (!g_picWnd)
    {
        g_picWnd = wnd;
        wl_display_gl_window(wnd, g_picW, g_picH);
    }
    wl_display_dest();
    RECT r, c;
    wl_display_picture(&r);
    GetClientRect(wnd, &c);
    if (EqualRect(&r, &g_picRect) && c.bottom == g_clientH)
        return;
    g_picRect = r;
    g_clientH = c.bottom;
    if (g_viewSet && oglViewport)
        viewport_to_window(g_view);
}

static WlHook g_viewportHooks[] = {
    {L"opengl32.dll", "glViewport", (void *)hglViewport, (void **)&oglViewport, 0},
    {L"opengl32.dll", "glGetIntegerv", (void *)hglGetIntegerv, (void **)&oglGetIntegerv, 0},
    {L"opengl32.dll", "glGetFloatv", (void *)hglGetFloatv, (void **)&oglGetFloatv, 0},
};

void wl_gl_picture(int width, int height)
{
    g_picW = width;
    g_picH = height;
    wl_hooks_add(g_viewportHooks, sizeof g_viewportHooks / sizeof g_viewportHooks[0]);
    wl_log("OpenGL: the game's picture is %dx%d, its window placed by windowsloader", width, height);
}

static BOOL(WINAPI *oSwapBuffers)(HDC);
static BOOL WINAPI hSwapBuffers(HDC dc)
{
    static unsigned frames;
    static ULONGLONG lastTick;
    static unsigned lastFrames;
    if (wl_busy())
        return oSwapBuffers(dc);
    wl_menu_swap_gl(dc, oSwapBuffers); // Tab: the settings menu, the game held until it closes
    HWND wnd = WindowFromDC(dc);
    if (g_picW && wnd)
        place_picture(wnd);
    else
        wl_display_title(wnd);
    RECT r = {0, 0, 0, 0};
    if (wnd)
        GetClientRect(wnd, &r);
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w > 0 && h > 0)
        take_pending_shot(w, h);
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

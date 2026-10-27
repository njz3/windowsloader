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

// OpenGL games (Initial D 6 to 8, Frenzy Express): each SwapBuffers opens the settings menu on Tab
// (menu.cpp), counts a frame, takes the picture the agent asked for, and puts the frames per second
// in the window's title.
//
// The game's window is windowsloader's, as a Direct3D game's ([Display]: mode, aspect ratio, crop,
// F11): the game draws in a framebuffer object of windowsloader's, the size its window's client
// area had at its first SwapBuffers, in place of the window's own framebuffer (0). At each
// SwapBuffers that framebuffer is copied (glBlitFramebuffer, scaled) where the picture goes in the
// window, black around it. The game does not see the change:
// - its glBindFramebuffer(..., 0) binds windowsloader's (the functions it gets by wglGetProcAddress);
// - glGetIntegerv gives it 0 for that framebuffer, GL_BACK for its colour buffer;
// - its glDrawBuffer / glReadBuffer / glDrawBuffers of the window's buffers (GL_BACK...) are the
//   framebuffer's colour buffer;
// - another surface made current with its context (a pbuffer) gets its own framebuffer back.
// The framebuffer has the window's format (colour with or without alpha, depth, stencil, samples:
// a multisampled one is resolved into a second before the copy). Without framebuffer objects (an
// old driver) the window stays as the game made it.
// The picture is the whole framebuffer, or the part its module gives (wl_gl_picture: Frenzy
// Express's 640x480, drawn in the corner of a window the size of the screen).
// opengl32.dll is the game's: windowsloader's own calls are linked delay-loaded, so that only an
// OpenGL game loads it, and made between wl_busy_enter and wl_busy_leave (past these hooks).

#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "display.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"

#define GL_BGRA_ 0x80E1
#define GL_FRAMEBUFFER_ 0x8D40
#define GL_READ_FRAMEBUFFER_ 0x8CA8
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING_ 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING_ 0x8CAA
#define GL_RENDERBUFFER_ 0x8D41
#define GL_RENDERBUFFER_BINDING_ 0x8CA7
#define GL_COLOR_ATTACHMENT0_ 0x8CE0
#define GL_DEPTH_ATTACHMENT_ 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT_ 0x821A
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5
#define GL_RGB8_ 0x8051
#define GL_RGBA8_ 0x8058
#define GL_DEPTH_COMPONENT24_ 0x81A6
#define GL_DEPTH24_STENCIL8_ 0x88F0
#define GL_SAMPLES_ 0x80A9
#define GL_FRAMEBUFFER_SRGB_ 0x8DB9
#define GL_DRAW_BUFFER0_ 0x8825

typedef void(APIENTRY *PfnGen)(GLsizei, GLuint *);
typedef void(APIENTRY *PfnDelete)(GLsizei, const GLuint *);
typedef void(APIENTRY *PfnBind)(GLenum, GLuint);
typedef void(APIENTRY *PfnStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY *PfnStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY *PfnAttachRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum(APIENTRY *PfnCheck)(GLenum);
typedef void(APIENTRY *PfnBlit)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void(APIENTRY *PfnDrawBuffers)(GLsizei, const GLenum *);

static PROC(WINAPI *owglGetProcAddress)(LPCSTR);
static void(WINAPI *oglGetIntegerv)(GLenum, GLint *);

// The driver's framebuffer functions, for windowsloader's own framebuffer (OpenGL 3.0's, or
// EXT_framebuffer_object's and EXT_framebuffer_blit's)
static struct
{
    int looked, ok;
    PfnGen genFramebuffers, genRenderbuffers;
    PfnDelete deleteFramebuffers, deleteRenderbuffers;
    PfnBind bindFramebuffer, bindRenderbuffer;
    PfnStorage renderbufferStorage;
    PfnStorageMultisample renderbufferStorageMultisample;
    PfnAttachRenderbuffer framebufferRenderbuffer;
    PfnCheck checkFramebufferStatus;
    PfnBlit blitFramebuffer;
} g_fb;

static PROC driver_proc(const char *core, const char *ext)
{
    PROC p = owglGetProcAddress ? owglGetProcAddress(core) : NULL;
    if ((!p || (INT_PTR)p <= 3 || p == (PROC)-1) && ext)
        p = owglGetProcAddress ? owglGetProcAddress(ext) : NULL;
    return p && (INT_PTR)p > 3 && p != (PROC)-1 ? p : NULL;
}

static int framebuffer_functions(void)
{
    if (g_fb.looked)
        return g_fb.ok;
    g_fb.looked = 1;
    g_fb.genFramebuffers = (PfnGen)driver_proc("glGenFramebuffers", "glGenFramebuffersEXT");
    g_fb.genRenderbuffers = (PfnGen)driver_proc("glGenRenderbuffers", "glGenRenderbuffersEXT");
    g_fb.deleteFramebuffers = (PfnDelete)driver_proc("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    g_fb.deleteRenderbuffers = (PfnDelete)driver_proc("glDeleteRenderbuffers", "glDeleteRenderbuffersEXT");
    g_fb.bindFramebuffer = (PfnBind)driver_proc("glBindFramebuffer", "glBindFramebufferEXT");
    g_fb.bindRenderbuffer = (PfnBind)driver_proc("glBindRenderbuffer", "glBindRenderbufferEXT");
    g_fb.renderbufferStorage = (PfnStorage)driver_proc("glRenderbufferStorage", "glRenderbufferStorageEXT");
    g_fb.renderbufferStorageMultisample =
        (PfnStorageMultisample)driver_proc("glRenderbufferStorageMultisample", "glRenderbufferStorageMultisampleEXT");
    g_fb.framebufferRenderbuffer = (PfnAttachRenderbuffer)driver_proc("glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT");
    g_fb.checkFramebufferStatus = (PfnCheck)driver_proc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
    g_fb.blitFramebuffer = (PfnBlit)driver_proc("glBlitFramebuffer", "glBlitFramebufferEXT");
    g_fb.ok = g_fb.genFramebuffers && g_fb.genRenderbuffers && g_fb.deleteFramebuffers && g_fb.deleteRenderbuffers &&
              g_fb.bindFramebuffer && g_fb.bindRenderbuffer && g_fb.renderbufferStorage && g_fb.framebufferRenderbuffer &&
              g_fb.checkFramebufferStatus && g_fb.blitFramebuffer;
    if (!g_fb.ok)
        wl_log("OpenGL: no framebuffer objects in the driver: the game's window stays as it made it");
    return g_fb.ok;
}

// ------------------------------------------------------------------------------
// The game's framebuffer, one for each context drawing in a window (framebuffer objects are not
// shared between contexts)
// ------------------------------------------------------------------------------

struct GlTarget
{
    HGLRC rc;
    HWND wnd;
    int w, h;          // the framebuffer's size: the window's client area at the first SwapBuffers
    int picW, picH;    // the picture, in its lower left corner
    int samples;
    GLuint fbo, color, depth;     // the game's
    GLuint resolve, resolveColor; // multisampled: the single-sample copy the picture is taken from
};

static GlTarget g_targets[4];
static SRWLOCK g_targetLock = SRWLOCK_INIT;
static __declspec(thread) GlTarget *t_target; // the current context's, while it draws in its window
static int g_picW, g_picH;                    // wl_gl_picture's, 0 for the whole framebuffer
static int g_failed;                          // a framebuffer refused once: no other tried
static HWND g_placedWnd;

static GlTarget *find_target(HGLRC rc)
{
    GlTarget *t = NULL;
    AcquireSRWLockShared(&g_targetLock);
    for (int i = 0; i < (int)(sizeof g_targets / sizeof g_targets[0]) && !t; i++)
        if (g_targets[i].rc == rc)
            t = &g_targets[i];
    ReleaseSRWLockShared(&g_targetLock);
    return t;
}

static GLint bound(GLenum binding)
{
    GLint v = 0;
    if (oglGetIntegerv)
        oglGetIntegerv(binding, &v);
    else
        glGetIntegerv(binding, &v); // not hooked: the driver's own
    return v;
}

static GLuint read_source(const GlTarget *t)
{
    return t->resolve ? t->resolve : t->fbo;
}

static void free_objects(GlTarget *t)
{
    if (t->fbo)
        g_fb.deleteFramebuffers(1, &t->fbo);
    if (t->resolve)
        g_fb.deleteFramebuffers(1, &t->resolve);
    GLuint rbs[3] = {t->color, t->depth, t->resolveColor};
    for (int i = 0; i < 3; i++)
        if (rbs[i])
            g_fb.deleteRenderbuffers(1, &rbs[i]);
}

// A renderbuffer of the format, multisampled when samples > 1
static GLuint renderbuffer(GLenum format, int samples, int w, int h)
{
    GLuint rb = 0;
    g_fb.genRenderbuffers(1, &rb);
    g_fb.bindRenderbuffer(GL_RENDERBUFFER_, rb);
    if (samples > 1 && g_fb.renderbufferStorageMultisample)
        g_fb.renderbufferStorageMultisample(GL_RENDERBUFFER_, samples, format, w, h);
    else
        g_fb.renderbufferStorage(GL_RENDERBUFFER_, format, w, h);
    return rb;
}

// The framebuffer for the current context, at the window's format; bound in place of 0 (under
// wl_busy_enter)
static GlTarget *make_target(HGLRC rc, HWND wnd, int w, int h)
{
    GLint drawWas = bound(GL_DRAW_FRAMEBUFFER_BINDING_), readWas = bound(GL_READ_FRAMEBUFFER_BINDING_);
    GLint rbWas = bound(GL_RENDERBUFFER_BINDING_);
    g_fb.bindFramebuffer(GL_FRAMEBUFFER_, 0);
    GLint samples = 0, alpha = 0, depth = 0, stencil = 0;
    oglGetIntegerv(GL_SAMPLES_, &samples);
    oglGetIntegerv(GL_ALPHA_BITS, &alpha);
    oglGetIntegerv(GL_DEPTH_BITS, &depth);
    oglGetIntegerv(GL_STENCIL_BITS, &stencil);
    if (!g_fb.renderbufferStorageMultisample)
        samples = 0;

    GlTarget t;
    memset(&t, 0, sizeof t);
    t.rc = rc;
    t.wnd = wnd;
    t.w = w;
    t.h = h;
    t.picW = g_picW > 0 && g_picW <= w ? g_picW : w;
    t.picH = g_picH > 0 && g_picH <= h ? g_picH : h;
    t.samples = samples;
    GLenum colorFormat = alpha ? GL_RGBA8_ : GL_RGB8_; // no alpha: the game's blending reads 1 there, as in its window
    t.color = renderbuffer(colorFormat, samples, w, h);
    if (depth || stencil)
        t.depth = renderbuffer(stencil ? GL_DEPTH24_STENCIL8_ : GL_DEPTH_COMPONENT24_, samples, w, h);
    g_fb.genFramebuffers(1, &t.fbo);
    g_fb.bindFramebuffer(GL_FRAMEBUFFER_, t.fbo);
    g_fb.framebufferRenderbuffer(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, t.color);
    if (t.depth)
        g_fb.framebufferRenderbuffer(GL_FRAMEBUFFER_, stencil ? GL_DEPTH_STENCIL_ATTACHMENT_ : GL_DEPTH_ATTACHMENT_, GL_RENDERBUFFER_, t.depth);
    GLenum status = g_fb.checkFramebufferStatus(GL_FRAMEBUFFER_);
    if (status == GL_FRAMEBUFFER_COMPLETE_ && samples > 1)
    {
        t.resolveColor = renderbuffer(colorFormat, 0, w, h);
        g_fb.genFramebuffers(1, &t.resolve);
        g_fb.bindFramebuffer(GL_FRAMEBUFFER_, t.resolve);
        g_fb.framebufferRenderbuffer(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, t.resolveColor);
        status = g_fb.checkFramebufferStatus(GL_FRAMEBUFFER_);
    }
    g_fb.bindRenderbuffer(GL_RENDERBUFFER_, (GLuint)rbWas);

    GlTarget *slot = NULL;
    if (status == GL_FRAMEBUFFER_COMPLETE_)
    {
        AcquireSRWLockExclusive(&g_targetLock);
        for (int i = 0; i < (int)(sizeof g_targets / sizeof g_targets[0]) && !slot; i++)
            if (!g_targets[i].rc)
            {
                slot = &g_targets[i];
                *slot = t;
            }
        ReleaseSRWLockExclusive(&g_targetLock);
    }
    if (!slot)
    {
        g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, (GLuint)drawWas);
        g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, (GLuint)readWas);
        free_objects(&t);
        g_failed = 1;
        wl_log("OpenGL: windowsloader's framebuffer refused (status 0x%x): the game's window stays as it made it", status);
        return NULL;
    }
    // The game's own framebuffer left bound when it has one
    g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, drawWas ? (GLuint)drawWas : slot->fbo);
    g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, readWas ? (GLuint)readWas : slot->fbo);
    wl_log("OpenGL: the game draws in windowsloader's framebuffer, %dx%d (its window's), colour %s, depth %d, stencil %d, %d samples; "
           "its picture %dx%d shown in the window as [Display] says",
           w, h, alpha ? "RGBA8" : "RGB8", depth, stencil, samples, slot->picW, slot->picH);
    return slot;
}

// At SwapBuffers: the current context's framebuffer, made at its first one in a window
static GlTarget *swap_target(HWND wnd)
{
    HGLRC rc = wglGetCurrentContext();
    if (!rc || !wnd)
        return NULL;
    GlTarget *t = find_target(rc);
    if (t)
        return t_target = (t->wnd == wnd ? t : NULL);
    if (g_failed || !framebuffer_functions())
        return NULL;
    RECT r;
    if (!GetClientRect(wnd, &r) || r.right <= 0 || r.bottom <= 0)
        return NULL;
    wl_busy_enter();
    t = make_target(rc, wnd, r.right, r.bottom);
    wl_busy_leave();
    return t_target = t;
}

// The game's picture where it goes in the window, black around it (under wl_busy_enter)
static void present(GlTarget *t)
{
    RECT client, pic, frame;
    if (!GetClientRect(t->wnd, &client) || client.right <= 0 || client.bottom <= 0)
        return;
    wl_display_picture(&pic);
    if (!wl_display_frame(&frame))
        frame = client;
    if (pic.right <= pic.left || pic.bottom <= pic.top)
        pic = client;
    int H = client.bottom;
    GLint drawWas = bound(GL_DRAW_FRAMEBUFFER_BINDING_), readWas = bound(GL_READ_FRAMEBUFFER_BINDING_);
    glPushAttrib(GL_ENABLE_BIT | GL_SCISSOR_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB_);
    if (t->resolve)
    {
        g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, t->fbo);
        g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, t->resolve);
        g_fb.blitFramebuffer(0, 0, t->w, t->h, 0, 0, t->w, t->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, read_source(t));
    g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
    glDrawBuffer(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // The frame: a cropped picture is cut to it
    glEnable(GL_SCISSOR_TEST);
    glScissor(frame.left, H - frame.bottom, frame.right - frame.left, frame.bottom - frame.top);
    int same = pic.right - pic.left == t->picW && pic.bottom - pic.top == t->picH;
    g_fb.blitFramebuffer(0, 0, t->picW, t->picH, pic.left, H - pic.bottom, pic.right, H - pic.top, GL_COLOR_BUFFER_BIT,
                         same ? GL_NEAREST : GL_LINEAR);
    g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, (GLuint)drawWas);
    g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, (GLuint)readWas);
    glPopAttrib();
}

int wl_gl_redirected(void)
{
    return t_target != NULL;
}

int wl_gl_present(void)
{
    GlTarget *t = t_target;
    if (!t)
        return 0;
    wl_busy_enter();
    present(t);
    wl_busy_leave();
    return 1;
}

// ------------------------------------------------------------------------------
// The agent's pictures
// ------------------------------------------------------------------------------

// From the framebuffer bound for reading (0: the window's back buffer), its lower left w x h
static void take_pending_shot(GLuint fb, int w, int h)
{
    const char *path = wl_shot_pending();
    if (!path)
        return;
    int ok = 0;
    unsigned char *pixels = (unsigned char *)malloc((size_t)w * h * 4);
    if (pixels)
    {
        wl_busy_enter();
        GLint readWas = fb || g_fb.ok ? bound(GL_READ_FRAMEBUFFER_BINDING_) : 0;
        if (g_fb.ok)
            g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, fb);
        glPushAttrib(GL_PIXEL_MODE_BIT);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glReadBuffer(fb ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glReadPixels(0, 0, w, h, GL_BGRA_, GL_UNSIGNED_BYTE, pixels);
        glPopClientAttrib();
        glPopAttrib();
        if (g_fb.ok)
            g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, (GLuint)readWas);
        wl_busy_leave();
        // OpenGL's rows go from the bottom up
        ok = wl_write_bmp(path, pixels + (size_t)(h - 1) * w * 4, -w * 4, w, h, 0);
    }
    free(pixels);
    wl_shot_done(ok);
}

void wl_gl_take_pending_shot(int w, int h)
{
    take_pending_shot(0, w, h);
}

// ------------------------------------------------------------------------------
// What the game sees: 0 for windowsloader's framebuffer, the window's buffers for its colour buffer
// ------------------------------------------------------------------------------

static int window_buffer(GLenum mode)
{
    return mode == GL_FRONT_LEFT || mode == GL_FRONT_RIGHT || mode == GL_BACK_LEFT || mode == GL_BACK_RIGHT || mode == GL_FRONT ||
           mode == GL_BACK || mode == GL_LEFT || mode == GL_RIGHT || mode == GL_FRONT_AND_BACK;
}

// The game's target when it may be redirected now, else NULL
static GlTarget *game_target(void)
{
    GlTarget *t = t_target;
    return t && !wl_busy() ? t : NULL;
}

static PfnBind g_gameBind, g_gameBindExt;

static void bind_framebuffer(PfnBind real, GLenum target, GLuint fb)
{
    GlTarget *t = game_target();
    if (!fb && t)
        fb = t->fbo;
    real(target, fb);
}

static void APIENTRY game_BindFramebuffer(GLenum target, GLuint fb)
{
    bind_framebuffer(g_gameBind, target, fb);
}

static void APIENTRY game_BindFramebufferEXT(GLenum target, GLuint fb)
{
    bind_framebuffer(g_gameBindExt, target, fb);
}

static PfnDrawBuffers g_gameDrawBuffers, g_gameDrawBuffersArb;

static void draw_buffers(PfnDrawBuffers real, GLsizei n, const GLenum *bufs)
{
    GlTarget *t = game_target();
    GLenum mine[8];
    if (t && bufs && n > 0 && n <= 8 && (GLuint)bound(GL_DRAW_FRAMEBUFFER_BINDING_) == t->fbo)
    {
        for (int i = 0; i < n; i++)
            mine[i] = window_buffer(bufs[i]) ? (i ? GL_NONE : GL_COLOR_ATTACHMENT0_) : bufs[i];
        bufs = mine;
    }
    real(n, bufs);
}

static void APIENTRY game_DrawBuffers(GLsizei n, const GLenum *bufs)
{
    draw_buffers(g_gameDrawBuffers, n, bufs);
}

static void APIENTRY game_DrawBuffersARB(GLsizei n, const GLenum *bufs)
{
    draw_buffers(g_gameDrawBuffersArb, n, bufs);
}

static const struct
{
    const char *name;
    void **real;
    void *wrapper;
} g_wrapped[] = {
    {"glBindFramebuffer", (void **)&g_gameBind, (void *)game_BindFramebuffer},
    {"glBindFramebufferEXT", (void **)&g_gameBindExt, (void *)game_BindFramebufferEXT},
    {"glDrawBuffers", (void **)&g_gameDrawBuffers, (void *)game_DrawBuffers},
    {"glDrawBuffersARB", (void **)&g_gameDrawBuffersArb, (void *)game_DrawBuffersARB},
};

static PROC WINAPI hwglGetProcAddress(LPCSTR name)
{
    PROC p = owglGetProcAddress(name);
    if (!p || (INT_PTR)p <= 3 || p == (PROC)-1 || !name || ((UINT_PTR)name >> 16) == 0 || wl_busy())
        return p;
    for (int i = 0; i < (int)(sizeof g_wrapped / sizeof g_wrapped[0]); i++)
        if (!strcmp(name, g_wrapped[i].name))
        {
            *g_wrapped[i].real = (void *)p;
            return (PROC)g_wrapped[i].wrapper;
        }
    return p;
}

static void WINAPI hglGetIntegerv(GLenum name, GLint *v)
{
    oglGetIntegerv(name, v);
    GlTarget *t = game_target();
    if (!t || !v)
        return;
    if ((name == GL_DRAW_FRAMEBUFFER_BINDING_ || name == GL_READ_FRAMEBUFFER_BINDING_) && (GLuint)*v == t->fbo)
        *v = 0;
    else if ((name == GL_DRAW_BUFFER || name == GL_DRAW_BUFFER0_) && *v == GL_COLOR_ATTACHMENT0_ &&
             (GLuint)bound(GL_DRAW_FRAMEBUFFER_BINDING_) == t->fbo)
        *v = GL_BACK;
    else if (name == GL_READ_BUFFER && *v == GL_COLOR_ATTACHMENT0_ && (GLuint)bound(GL_READ_FRAMEBUFFER_BINDING_) == t->fbo)
        *v = GL_BACK;
}

static void(WINAPI *oglDrawBuffer)(GLenum);
static void WINAPI hglDrawBuffer(GLenum mode)
{
    GlTarget *t = game_target();
    if (t && window_buffer(mode) && (GLuint)bound(GL_DRAW_FRAMEBUFFER_BINDING_) == t->fbo)
        mode = GL_COLOR_ATTACHMENT0_;
    oglDrawBuffer(mode);
}

static void(WINAPI *oglReadBuffer)(GLenum);
static void WINAPI hglReadBuffer(GLenum mode)
{
    GlTarget *t = game_target();
    if (t && window_buffer(mode) && (GLuint)bound(GL_READ_FRAMEBUFFER_BINDING_) == t->fbo)
        mode = GL_COLOR_ATTACHMENT0_;
    oglReadBuffer(mode);
}

// A context made current: its framebuffer in place of 0 when it draws in its window, the surface's
// own when it draws elsewhere (a pbuffer)
static BOOL(WINAPI *owglMakeCurrent)(HDC, HGLRC);
static BOOL WINAPI hwglMakeCurrent(HDC dc, HGLRC rc)
{
    BOOL ok = owglMakeCurrent(dc, rc);
    if (!ok || wl_busy())
        return ok;
    GlTarget *t = rc ? find_target(rc) : NULL;
    if (t)
    {
        int window = dc && WindowFromDC(dc) == t->wnd;
        wl_busy_enter();
        GLuint draw = (GLuint)bound(GL_DRAW_FRAMEBUFFER_BINDING_), read = (GLuint)bound(GL_READ_FRAMEBUFFER_BINDING_);
        if (window && !draw)
            g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, t->fbo);
        if (window && !read)
            g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, t->fbo);
        if (!window && draw == t->fbo)
            g_fb.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
        if (!window && read == t->fbo)
            g_fb.bindFramebuffer(GL_READ_FRAMEBUFFER_, 0);
        wl_busy_leave();
        if (!window)
            t = NULL;
    }
    t_target = t;
    return ok;
}

// A context deleted: its framebuffer goes with it
static BOOL(WINAPI *owglDeleteContext)(HGLRC);
static BOOL WINAPI hwglDeleteContext(HGLRC rc)
{
    GlTarget *t = rc ? find_target(rc) : NULL;
    if (t)
    {
        if (wglGetCurrentContext() == rc)
        {
            wl_busy_enter();
            free_objects(t);
            wl_busy_leave();
        }
        if (t_target == t)
            t_target = NULL;
        AcquireSRWLockExclusive(&g_targetLock);
        memset(t, 0, sizeof *t);
        ReleaseSRWLockExclusive(&g_targetLock);
    }
    return owglDeleteContext(rc);
}

void wl_gl_picture(int width, int height)
{
    g_picW = width;
    g_picH = height;
    wl_log("OpenGL: the game's picture is %dx%d, in the lower left corner of its window", width, height);
}

// ------------------------------------------------------------------------------
// SwapBuffers
// ------------------------------------------------------------------------------

static BOOL(WINAPI *oSwapBuffers)(HDC);
static BOOL WINAPI hSwapBuffers(HDC dc)
{
    static unsigned frames;
    static ULONGLONG lastTick;
    static unsigned lastFrames;
    if (wl_busy())
        return oSwapBuffers(dc);
    HWND wnd = WindowFromDC(dc);
    GlTarget *t = swap_target(wnd);
    wl_menu_swap_gl(dc, oSwapBuffers); // Tab: the settings menu, the game held until it closes
    int w = 0, h = 0;
    if (t)
    {
        // The window set for the mode at the first frame, then placed (F11, Alt+Enter, the title)
        if (!g_placedWnd)
        {
            g_placedWnd = wnd;
            wl_display_gl_window(wnd, t->picW, t->picH);
        }
        wl_display_dest();
        wl_busy_enter();
        present(t);
        wl_busy_leave();
        w = t->picW;
        h = t->picH;
        RECT c;
        if (!wl_shot_window())
            take_pending_shot(read_source(t), w, h); // the game's picture, as the other renderers'
        else if (GetClientRect(wnd, &c) && c.right > 0 && c.bottom > 0)
            take_pending_shot(0, c.right, c.bottom); // the agent's "screen": the window, bars included
    }
    else
    {
        wl_display_title(wnd);
        RECT r = {0, 0, 0, 0};
        if (wnd)
            GetClientRect(wnd, &r);
        w = r.right - r.left;
        h = r.bottom - r.top;
        if (w > 0 && h > 0)
            take_pending_shot(0, w, h);
    }
    BOOL ok = oSwapBuffers(dc);
    wl_frame_presented();

    ULONGLONG now = GetTickCount64();
    if (!frames++)
    {
        lastTick = now;
        wl_frame_size(w, h);
        wl_log("SwapBuffers: first frame, window %p, %dx%d", wnd, w, h);
        wl_busy_enter();
        const char *vendor = (const char *)glGetString(GL_VENDOR), *renderer = (const char *)glGetString(GL_RENDERER),
                   *version = (const char *)glGetString(GL_VERSION);
        wl_busy_leave();
        wl_log("OpenGL: %s, %s, %s", vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
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
    {L"opengl32.dll", "wglGetProcAddress", (void *)hwglGetProcAddress, (void **)&owglGetProcAddress, 0},
    {L"opengl32.dll", "wglMakeCurrent", (void *)hwglMakeCurrent, (void **)&owglMakeCurrent, 0},
    {L"opengl32.dll", "wglDeleteContext", (void *)hwglDeleteContext, (void **)&owglDeleteContext, 0},
    {L"opengl32.dll", "glGetIntegerv", (void *)hglGetIntegerv, (void **)&oglGetIntegerv, 0},
    {L"opengl32.dll", "glDrawBuffer", (void *)hglDrawBuffer, (void **)&oglDrawBuffer, 0},
    {L"opengl32.dll", "glReadBuffer", (void *)hglReadBuffer, (void **)&oglReadBuffer, 0},
};

void wl_gl_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}

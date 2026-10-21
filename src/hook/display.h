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

#pragma once

#include <windows.h>
#include <d3d9.h>

// How the game's picture is shown, with linuxloader's [Display] settings (windowsloader.ini):
//   ASPECT_RATIO = ORIGINAL | 4:3 | 16:9 | 4:3_LETTERBOX
//       the shape of the picture: the game's own, forced to 4:3 or 16:9, or a 4:3 frame holding the
//       whole picture at its own shape (black bars above and below a 16:9 one)
//   DISPLAY_MODE = WINDOW | WINDOW_STRETCH | FULLSCREEN | FULLSCREEN_STRETCH
//       a window at the aspect ratio (square pixels), a window the picture fills, the whole screen
//       with the picture at the aspect ratio between black bars, or the whole screen filled
//   WIDTH / HEIGHT = AUTO | pixels
//       the game's render resolution, when its module can set it (taken at the next start)
// The game is kept in a window whatever it asks (a borderless one covering the screen in the full
// screen modes); its back buffer is presented into the right part of that window.

enum WlAspect
{
    WL_ASPECT_ORIGINAL,
    WL_ASPECT_4_3,
    WL_ASPECT_16_9,
    WL_ASPECT_4_3_LETTERBOX,
    WL_ASPECT_COUNT
};

enum WlDisplayMode
{
    WL_MODE_WINDOW,
    WL_MODE_WINDOW_STRETCH,
    WL_MODE_FULLSCREEN,
    WL_MODE_FULLSCREEN_STRETCH,
    WL_MODE_COUNT
};

void wl_display_load(const wchar_t *iniPath);

// ASPECT_RATIO / DISPLAY_MODE names of windowsloader.ini, -1 when unknown
int wl_display_aspect_by_name(const char *name);
int wl_display_mode_by_name(const char *name);
const char *wl_display_aspect_name(int aspect);
const char *wl_display_mode_name(int mode);

int wl_display_aspect(void);
int wl_display_mode(void);
void wl_display_set(int aspect, int mode); // applied now and saved

// WIDTH x HEIGHT, 0 x 0 for AUTO; saved, used at the next start
void wl_display_internal(int *width, int *height);
void wl_display_set_internal(int width, int height);
void wl_display_set_internal_supported(int supported); // by the game's module
int wl_display_internal_supported(void);

// From the Direct3D 9 hooks
void wl_display_prepare(D3DPRESENT_PARAMETERS *pp);          // before CreateDevice / Reset
void wl_display_device(HWND wnd, const D3DPRESENT_PARAMETERS *pp); // after: the window set for the mode
const RECT *wl_display_dest(void);                           // each Present: where the picture goes, NULL for the whole window

// From the other renderers (Direct3D 11): the window set for the mode once the swap chain exists.
// Their Present fills the whole window, so the aspect ratio holds in the window modes only, the
// window's shape being the picture's. wl_display_dest is still called each Present (F11, Alt+Enter).
void wl_display_window(HWND wnd, int width, int height);

// The part of the window's client area showing the picture (for the menu's mouse)
void wl_display_picture(RECT *r);

// What the window shows, as a 24-bit BMP (the agent's "screen")
int wl_display_screenshot(const char *path);

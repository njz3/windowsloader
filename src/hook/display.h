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

// How the game's picture is shown, with linuxloader's [Display] settings (windowsloader.ini, or
// the game's own file, settings.h):
//   ASPECT_RATIO = ORIGINAL | 4:3 | 16:9 | 4:3_LETTERBOX | 4:3_STRETCH | 16:9_STRETCH | 4:3_CROP | 16:9_CROP
//       the shape of the frame the picture is shown in: the game's own (that of its render
//       resolution), or 4:3 or 16:9. 4:3 and 16:9: the picture whole inside at its own shape, black
//       bars where the shapes differ (a 4:3 game in 16:9: bars left and right; a 16:9 one in 4:3:
//       above and below); 4:3_LETTERBOX is 4:3. _STRETCH: the picture fills the frame, deformed.
//       _CROP: the picture fills the frame at its own shape, the part that overflows cut
//   CROP_POSITION = -100..100: the part the crop keeps, -100 left or top, 0 the middle
//   CROP_BARREL = true | false: the parts the crop would cut bent onto the frame's edges, as on a
//       cylinder seen from the front (the whole picture visible); BARREL_CENTER = 0..100 the middle
//       shown undistorted, in % of the frame (60); BARREL_RADIUS the cylinder's radius in % of the
//       frame (50): small bends hard at the edge, large compresses evenly. Only when windowsloader
//       copies the picture (PRESENT = GDI); with PRESENT = DIRECT, Direct3D 9 crops without the
//       bend and Direct3D 11 shows the whole picture between bars
//   DISPLAY_MODE = WINDOW | WINDOW_STRETCH | FULLSCREEN | FULLSCREEN_STRETCH | BORDERLESS |
//       BORDERLESS_STRETCH: a window at the aspect ratio (square pixels), a window the frame fills,
//       the whole screen with the frame at the aspect ratio between black bars or the whole screen
//       filled, the screen switched to its smallest mode holding the game's picture (FULLSCREEN) or left at the
//       desktop's (BORDERLESS, a full screen window)
//   WIDTH / HEIGHT = AUTO | pixels
//       the game's render resolution, when its module can set it (taken at the next start)
// The game is kept in a window whatever it asks (a borderless one covering the screen in the full
// screen modes); its back buffer is presented into the right part of that window. The menu's
// changes apply at the next frame; they are written by wl_display_save (the menu's buttons).

enum WlAspect
{
    WL_ASPECT_ORIGINAL,
    WL_ASPECT_4_3,
    WL_ASPECT_16_9,
    WL_ASPECT_4_3_LETTERBOX,
    WL_ASPECT_4_3_STRETCH,
    WL_ASPECT_16_9_STRETCH,
    WL_ASPECT_4_3_CROP,
    WL_ASPECT_16_9_CROP,
    WL_ASPECT_COUNT
};

enum WlDisplayMode
{
    WL_MODE_WINDOW,
    WL_MODE_WINDOW_STRETCH,
    WL_MODE_FULLSCREEN,
    WL_MODE_FULLSCREEN_STRETCH,
    WL_MODE_BORDERLESS,
    WL_MODE_BORDERLESS_STRETCH,
    WL_MODE_COUNT
};

// The settings read (wl_settings_init first); the defaults written when windowsloader.ini is missing
void wl_display_load(void);
// The settings written to that file (the menu's "Sauvegarder"): 1 when done
int wl_display_save(const wchar_t *file);

// ASPECT_RATIO / DISPLAY_MODE names of windowsloader.ini, -1 when unknown
int wl_display_aspect_by_name(const char *name);
int wl_display_mode_by_name(const char *name);
const char *wl_display_aspect_name(int aspect);
const char *wl_display_mode_name(int mode);

int wl_display_aspect(void);
int wl_display_mode(void);
void wl_display_set(int aspect, int mode); // applied at the next frame, not saved

// CROP_POSITION, CROP_BARREL, BARREL_CENTER, BARREL_RADIUS: read at each frame
void wl_display_crop(int *position, int *barrel, int *center, int *radius);
void wl_display_set_crop(int position, int barrel, int center, int radius);

// WIDTH x HEIGHT, 0 x 0 for AUTO: the one the game started with, and the one to save (used at the
// next start)
void wl_display_internal(int *width, int *height);
void wl_display_internal_next(int *width, int *height);
void wl_display_set_internal(int width, int height);
void wl_display_set_internal_supported(int supported); // by the game's module
int wl_display_internal_supported(void);

// From the Direct3D 9 hooks
void wl_display_prepare(D3DPRESENT_PARAMETERS *pp);          // before CreateDevice / Reset
void wl_display_device(HWND wnd, const D3DPRESENT_PARAMETERS *pp); // after: the window set for the mode
const RECT *wl_display_dest(void);                           // each Present: where the picture goes, NULL for the whole window
const RECT *wl_display_source(void);                         // and the part of the back buffer shown (a crop), NULL for all

// From the other renderers (Direct3D 11), whose Present fills its whole window: the swap chain is
// given wl_display_child's window instead of the game's, a child placed where the picture goes,
// with the black bars painted around it; then the game's window set for the mode once the swap
// chain exists. wl_display_dest is still called each Present (placing, F11, Alt+Enter).
HWND wl_display_child(HWND parent);
void wl_display_window(HWND wnd, int width, int height);

// OpenGL games (their window as they made it): the frames per second and the game's name in the
// window's title, at each SwapBuffers
void wl_display_title(HWND wnd);

// The part of the window's client area showing the picture (for the menu's mouse); a cropped
// picture's goes beyond the frame (the whole picture at the crop's scale, centred on the frame: the
// menu is shown that way over a cropped or bent picture)
void wl_display_picture(RECT *r);

// The picture shown by GDI ([Display] PRESENT = GDI, the default): windowsloader copies it into the
// window (at wl_display_picture); the game's Present goes to a hidden window (Direct3D 9) or a
// hidden child (Direct3D 11). A Direct3D blt into a window shows nothing on some PCs (this
// laptop's Windows 11).
int wl_display_gdi(void);
// The picture (top-down rows: 32-bit BGRX, or 16-bit 565 when green6, else 555) drawn where it goes
void wl_display_gdi_draw(const void *pixels, int pitch, int width, int height, int bits, int green6);
HWND wl_display_hwnd(void);

// What the window shows, as a 24-bit BMP (the agent's "screen")
int wl_display_screenshot(const char *path);

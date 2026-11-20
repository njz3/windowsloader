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

// The virtual screen ([Game.<name>] VIRTUAL_DISPLAY = AUTO | OFF | <width>x<height>): a game that
// refuses a screen smaller than it needs (a cabinet's CRT at 512x384 or 1024x384: "The current
// resolution is too low to run this game") is told the screen is that size, everywhere it asks
// (GetSystemMetrics, EnumDisplaySettings(Ex), GetMonitorInfo, GetDeviceCaps, Direct3D 9's adapter
// modes, DXGI's output and its modes); it draws at that size in its window, and windowsloader puts
// its picture on the real screen as for any game ([Display] DISPLAY_MODE, ASPECT_RATIO). AUTO (the
// default): the game's own minimum (wl_vscreen_minimum), only when the real screen is smaller in a
// direction (on a larger one, only that size added to the modes' lists that lack it: SSASR looks
// for its 1280x720 there); a size: always that one, larger than the real screen too (a game drawing at the
// screen's size then draws more pixels, its picture reduced on the real screen); OFF: never. In the
// option list of Crazy Ride, SWDC and Sonic & Sega All-Stars Racing (with their minimum); any other
// game reads it from its section when it is written there by hand (AUTO then the real screen).
// Only the game's calls are told (windowsloader's own and Windows' get the real screen).

// By the game's module, when it is found: the smallest screen its game accepts
void wl_vscreen_minimum(int width, int height);

// From game.cpp once the options are read: the option's value ("" when the game has none)
void wl_vscreen_settings(const char *value);

// 1 and the size when the game is told another screen
int wl_vscreen_size(int *width, int *height);

// From system.cpp's GetSystemMetrics hook: 1 and the value told to that caller (the screen's size)
int wl_vscreen_metric(int index, const void *caller, int *value);

// From system.cpp's SystemParametersInfo hook: SPI_GETWORKAREA's rectangle, the virtual screen's
void wl_vscreen_work_area(const void *caller, RECT *r);

// A module that hooks EnumDisplaySettingsA itself (Codemasters'): told while it finds its game, then
// its hook asks here first (1: answered, its result in ok), real its original
void wl_vscreen_hooked_elsewhere_a(void);
int wl_vscreen_display_settings_a(LPCSTR device, DWORD mode, DEVMODEA *dm, const void *caller, BOOL(WINAPI *real)(LPCSTR, DWORD, DEVMODEA *),
                                  BOOL *ok);

// From the Direct3D hooks: an IDirect3D9 and a DXGI factory the game made (their modes and
// outputs then answer the virtual screen)
void wl_vscreen_d3d9(void *direct3d9);
void wl_vscreen_dxgi(void *dxgiFactory);

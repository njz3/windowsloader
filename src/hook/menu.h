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

// The F10 settings menu, drawn over the game with Dear ImGui. While it is open the game is
// paused (pause.h): the render thread stays in the menu, drawing the game's last frame under it.

typedef HRESULT(STDMETHODCALLTYPE *WlPresentFn)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);

// From the Present hook, before the game's frame is presented: opens the menu on F10 (or on the
// agent's request) and returns once it is closed
void wl_menu_present(IDirect3DDevice9 *dev, WlPresentFn present);

// The same from a Direct3D 11 swap chain's Present hook
struct IDXGISwapChain;
typedef HRESULT(STDMETHODCALLTYPE *WlPresent11Fn)(IDXGISwapChain *, UINT, UINT);
void wl_menu_present11(IDXGISwapChain *sc, WlPresent11Fn present);

// Before IDirect3DDevice9::Reset: the menu's device objects are released
void wl_menu_device_reset(void);

// From the game window's procedure (display.cpp): 1 when the menu took the message
int wl_menu_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result);

// The agent's "menu on|off"
void wl_menu_request(int open);
void wl_menu_request_tab(int tab); // "menu tab N": 0 Jeu, 1 Contrôles, 2 Affichage, 3 Retour de force, 4 À propos
int wl_menu_is_open(void);

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

// The game paused under the F10 menu: its clocks stand still (QueryPerformanceCounter,
// GetTickCount and timeGetTime of the game's exe, replaced in its import table) and its sound
// is muted (its audio session). Its render thread itself is held by the menu.

void wl_pause_init(void); // patches the game's imports, from DllMain
void wl_pause_begin(void);
void wl_pause_end(void);

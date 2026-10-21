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

// Game modules: each one recognizes its game and sets up the hardware it emulates for it.
// Returns 1 when the game is its own.

int wl_game_segarally3(const wchar_t *exePath);
int wl_game_daytona(const wchar_t *exePath, const char **title); // both editions

// The game recognized by wl_game_init, "unknown" when none: only the observer then runs
const char *wl_game_name(void);
const char *wl_game_title(void); // "Sega Rally 3", for the menu
void wl_game_init(void);

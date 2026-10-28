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

// The games windowsloader plays, for the picker: told apart the way their modules recognize them
// (src/hook/games/*.cpp), by the program's name, files beside it and, for a few (Battle Gear 4's
// two, Initial D Zero's versions), the program's time stamp. To keep in step with the modules.

enum
{
    WL_GAME_PLAYS,  // played, checked in agent mode
    WL_GAME_PARTLY, // played, with something left (see BILAN.md)
    WL_GAME_WORK    // being worked on: does not play yet
};

struct WlKnownGame
{
    const char *title;
    const char *name;          // its short name: the DLL module's ([Game.<name>], WRITE_DIR\<name>)
    const char *version;       // "" when the copies do not tell it
    const wchar_t *exes[3];    // the program's names
    DWORD crc32[3];            // each one's CRC-32 in the cabinets' copies (zip's), 0 when not known: shown
                               // by --list-games only, never checked (another program is played the same)
    const wchar_t *markers[3]; // files that must be there, from the program's folder
    DWORD stamp;               // the program's PE time stamp, 0 for any
    const wchar_t *args;       // the arguments the cabinet gives it, NULL for none
    const char *platform;
    const char *year;
    int state;
    const char *note; // what windowsloader plays for it
};

extern const WlKnownGame g_wlKnownGames[];
extern const int g_wlKnownGameCount;

// Whether name is one of the programs of the table (no file looked at)
int wl_known_program_name(const wchar_t *name);
// The game this program is, NULL when none
const WlKnownGame *wl_known_game(const wchar_t *exePath);

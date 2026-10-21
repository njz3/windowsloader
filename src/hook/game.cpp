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

#include <windows.h>

#include "game.h"
#include "log.h"

static const char *g_name = "unknown";
static const char *g_title = "jeu inconnu";

const char *wl_game_name(void)
{
    return g_name;
}

const char *wl_game_title(void)
{
    return g_title;
}

void wl_game_init(void)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (wl_game_segarally3(exe))
    {
        g_name = "segarally3";
        g_title = "Sega Rally 3";
    }
    else
        wl_log("game not recognized: observing only");
}
